// MetaLog composition (SPEC §12): merge two documents into one bounded
// fingerprint covering both windows. Lossy where either input had a non-empty
// tail; required fields (lines_observed, unique_templates union, time envelope,
// re-derived reservoir) are preserved. Single responsibility — the compose
// semantics only.

#include "insight/metalog/metalog_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "insight/metalog/detail/comparability.hpp"
#include "insight/metalog/detail/salience.hpp"
#include "insight/metalog/detail/statistics.hpp"

namespace insight::metalog
{
using detail::carry_processing_identifier;
using detail::check_processing_identifier_gate;
using detail::dominant_level_of;
using detail::dominant_role_of;
using detail::salience_score;
using detail::shannon_entropy_bits;

// ── compose (SPEC §12) ─────────────────────────────────────────

namespace
{
// Compare ISO 8601 lexicographically — valid for fixed-format
// RFC 3339 UTC strings as we emit (always Z, fixed widths).
[[nodiscard]] const std::string& iso_min(const std::string& a, const std::string& b)
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    return a < b ? a : b;
}
[[nodiscard]] const std::string& iso_max(const std::string& a, const std::string& b)
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    return a > b ? a : b;
}

SourceBlock common_source(const SourceBlock& a, const SourceBlock& b)
{
    if (a == b)
        return a;
    SourceBlock out;
    if (a.fleet == b.fleet)
        out.fleet = a.fleet;
    if (a.service == b.service)
        out.service = a.service;
    if (a.host == b.host)
        out.host = a.host;
    // host_count: sum if both present; otherwise leave unset.
    if (a.host_count && b.host_count)
        out.host_count = *a.host_count + *b.host_count;
    // tags: keep entries present and equal in both.
    for (const auto& [k, v] : a.tags)
    {
        auto it{b.tags.find(k)};
        if (it != b.tags.end() && it->second == v)
            out.tags.emplace(k, v);
    }
    return out;
}

void aggregate_top_k(std::unordered_map<std::string, std::uint64_t>& counts,
                     std::unordered_map<std::string, std::string>& templates,
                     std::unordered_map<std::string, std::optional<LogLevel>>& levels,
                     const MetaLogDocument& doc)
{
    for (const auto& e : doc.stats.top_k)
    {
        counts[e.template_id] += e.count;
        if (!e.template_str.empty() && !templates.contains(e.template_id))
            templates.emplace(e.template_id, e.template_str);
        if (e.dominant_level && !levels.contains(e.template_id))
            levels.emplace(e.template_id, e.dominant_level);
    }
    for (const auto& [tid, tstr] : doc.templates)
        if (!templates.contains(tid))
            templates.emplace(tid, tstr);
}

// Fold a document's RESERVOIR mass into the same maps (F8). A template is disjoint
// across top_k/reservoir within one document, so this never double-counts an input;
// across inputs a template that is top_k in one and reservoir in the other gets its
// full merged count. Lets the composed top_k ranking and the re-derived reservoir
// see the rare-salient templates' counts, which `aggregate_top_k` alone misses.
void aggregate_reservoir(std::unordered_map<std::string, std::uint64_t>& counts,
                         std::unordered_map<std::string, std::string>& templates,
                         std::unordered_map<std::string, std::optional<LogLevel>>& levels,
                         const MetaLogDocument& doc)
{
    for (const auto& e : doc.stats.reservoir)
    {
        counts[e.template_id] += e.count;
        if (!e.template_str.empty() && !templates.contains(e.template_id))
            templates.emplace(e.template_id, e.template_str);
        if (e.dominant_level && !levels.contains(e.template_id))
            levels.emplace(e.template_id, e.dominant_level);
    }
}

// §3.5 / §12.1 compose-carry of param_histograms. For each (template_id,
// param_index) in both inputs: union value_counts (sum on matching keys),
// truncate to the cap (top-N by count, deterministic tie-break by key),
// total = a.total + b.total, recompute entropy_bits over the merged counts,
// approximate_cardinality = max (HLL sketch is not in the doc — we don't have
// the registers to union; max is the spec's conservative fallback). For a slot
// present in only ONE input we carry it unchanged (§12.1: MAY carry or omit).
[[nodiscard]] std::vector<FieldHistogram>
merge_field_histograms(const std::vector<FieldHistogram>& lhs,
                       const std::vector<FieldHistogram>& rhs, std::size_t cap)
{
    std::unordered_map<std::uint32_t, const FieldHistogram*> lhs_index;
    lhs_index.reserve(lhs.size());
    for (const auto& fh : lhs)
        lhs_index.emplace(fh.param_index, &fh);

    std::vector<FieldHistogram> out;
    out.reserve(lhs.size() + rhs.size());
    std::unordered_set<std::uint32_t> seen;
    seen.reserve(rhs.size());

    for (const auto& b : rhs)
    {
        seen.insert(b.param_index);
        const auto found{lhs_index.find(b.param_index)};
        if (found == lhs_index.end())
        {
            out.push_back(b);
            continue;
        }
        const FieldHistogram& a{*found->second};
        FieldHistogram merged;
        merged.param_index = b.param_index;
        merged.total = a.total + b.total;
        std::unordered_map<std::string, std::uint64_t> values;
        values.reserve(a.value_counts.size() + b.value_counts.size());
        for (const auto& [v, c] : a.value_counts)
            values[v] += c;
        for (const auto& [v, c] : b.value_counts)
            values[v] += c;
        if (cap > 0 && values.size() > cap)
        {
            std::vector<std::pair<std::string, std::uint64_t>> sorted(values.begin(), values.end());
            std::ranges::partial_sort(sorted, sorted.begin() + static_cast<std::ptrdiff_t>(cap),
                                      [](const auto& x, const auto& y)
                                      {
                                          if (x.second != y.second)
                                              return x.second > y.second;
                                          return x.first < y.first; // deterministic tie-break
                                      });
            values.clear();
            values.reserve(cap);
            for (std::size_t i{0}; i < cap; ++i)
                values.emplace(std::move(sorted[i].first), sorted[i].second);
        }
        std::vector<std::uint64_t> counts;
        counts.reserve(values.size());
        for (const auto& [v, c] : values)
            counts.push_back(c);
        merged.value_counts = std::move(values);
        merged.entropy_bits = shannon_entropy_bits(counts, merged.total);
        merged.approximate_cardinality =
            std::max(a.approximate_cardinality, b.approximate_cardinality);
        out.push_back(std::move(merged));
    }
    for (const auto& a : lhs)
        if (!seen.contains(a.param_index))
            out.push_back(a);
    std::ranges::sort(out,
                      [](const auto& x, const auto& y) { return x.param_index < y.param_index; });
    return out;
}

// Shared scratch for compose(): the per-template aggregation across both inputs
// (merged counts, inline template strings, dominant levels), the count-sorted
// view, and the set of templates promoted into the re-derived reservoir.
struct ComposeState
{
    std::unordered_map<std::string, std::uint64_t> counts;
    std::unordered_map<std::string, std::string> templates;
    std::unordered_map<std::string, std::optional<LogLevel>> levels;
    std::vector<std::pair<std::string, std::uint64_t>> ordered;
    std::unordered_set<std::string> reserved;
};

// Fold both inputs' top_k + reservoir into the aggregation maps, then build the
// count-desc / id-asc ordering the composed top_k and tail draw from.
void aggregate_and_order(ComposeState& state, const MetaLogDocument& lhs,
                         const MetaLogDocument& rhs)
{
    aggregate_top_k(state.counts, state.templates, state.levels, lhs);
    aggregate_top_k(state.counts, state.templates, state.levels, rhs);
    aggregate_reservoir(state.counts, state.templates, state.levels, lhs);
    aggregate_reservoir(state.counts, state.templates, state.levels, rhs);

    state.ordered.assign(state.counts.begin(), state.counts.end());
    std::ranges::sort(state.ordered,
                      [](const auto& a, const auto& b)
                      {
                          if (a.second != b.second)
                              return a.second > b.second;
                          return a.first < b.first;
                      });
}

// Composed top_k: the highest-count templates, with §3.5/§12.1 param-histogram
// compose-carry (merge per-param across inputs; one-sided carried).
void build_composed_top_k(MetaLogDocument& out, const ComposeState& state,
                          const MetaLogDocument& lhs, const MetaLogDocument& rhs)
{
    const auto& templates = state.templates;
    const auto& levels = state.levels;
    const auto& ordered = state.ordered;
    const auto build_topk_index{[](const MetaLogDocument& doc)
                                {
                                    std::unordered_map<std::string, const TopKEntry*> index;
                                    index.reserve(doc.stats.top_k.size());
                                    for (const auto& entry : doc.stats.top_k)
                                        index.emplace(entry.template_id, &entry);
                                    return index;
                                }};
    const auto lhs_topk{build_topk_index(lhs)};
    const auto rhs_topk{build_topk_index(rhs)};
    static constexpr std::size_t kComposeHistogramCap{MetaLogConfig::kDefaultMaxHistogramValues};

    const auto k{std::min(out.stats.top_k_size, ordered.size())};
    out.stats.top_k.reserve(k);
    const auto total_lines = static_cast<double>(out.window.lines_observed);
    for (std::size_t i = 0; i < k; ++i)
    {
        TopKEntry e;
        e.template_id = ordered[i].first;
        if (auto t{templates.find(e.template_id)}; t != templates.end())
            e.template_str = t->second; // preserved when at least one input had it inline
        e.count = ordered[i].second;
        e.frequency = total_lines > 0.0 ? static_cast<double>(e.count) / total_lines : 0.0;
        if (auto l{levels.find(e.template_id)}; l != levels.end())
            e.dominant_level = l->second;
        // §3.5 / §12.1 compose-visible param_histograms. Look up the matching
        // entries in each input and merge per-param; one-sided is carried; entirely
        // absent → no histograms (consistent with the cap being a no-op when input
        // producers didn't emit any). Closes the F8/F2-value compose gap.
        const auto lhs_entry_it{lhs_topk.find(e.template_id)};
        const auto rhs_entry_it{rhs_topk.find(e.template_id)};
        const std::vector<FieldHistogram> empty{};
        const auto& lhs_hists{
            lhs_entry_it != lhs_topk.end() ? lhs_entry_it->second->field_histograms : empty};
        const auto& rhs_hists{
            rhs_entry_it != rhs_topk.end() ? rhs_entry_it->second->field_histograms : empty};
        if (!lhs_hists.empty() || !rhs_hists.empty())
            e.field_histograms = merge_field_histograms(lhs_hists, rhs_hists, kComposeHistogramCap);
        out.stats.top_k.push_back(std::move(e));
    }
}

// Salience reservoir re-derivation (F8; SPEC §3.7.3 / §12.1). Carry the
// rare-salient templates through composition instead of dropping them into the
// tail (the multi-scale gap: composed/pyramid baselines were blind to a lone
// fatal / off-path branch). Candidates are templates that were salient in EITHER
// input's reservoir and did not rise into the composed top_k by frequency.
// structural_surprise/novelty carry through as the max across inputs; salience is
// RE-DERIVED over the merged count + composed line total (rarity shifts on merge),
// so the ranking reflects the composed window, not either input's. Bounded by the
// inputs' (already diversity-capped) reservoirs.
void rederive_reservoir(MetaLogDocument& out, ComposeState& state, const MetaLogDocument& lhs,
                        const MetaLogDocument& rhs)
{
    const auto& counts = state.counts;
    const auto& levels = state.levels;
    const auto& templates = state.templates;
    auto& reserved = state.reserved;
    const auto total_lines = static_cast<double>(out.window.lines_observed);
    {
        struct SalienceInfo
        {
            std::uint32_t structural_surprise{0};
            std::uint32_t novelty{0};
            StructuralRole role{StructuralRole::None};
        };
        std::unordered_map<std::string, SalienceInfo> sal_info;
        const auto absorb_reservoir{[&](const MetaLogDocument& doc)
                                    {
                                        for (const auto& e : doc.stats.reservoir)
                                        {
                                            auto& info{sal_info[e.template_id]};
                                            info.structural_surprise = std::max(
                                                info.structural_surprise, e.structural_surprise);
                                            info.novelty = std::max(info.novelty, e.novelty);
                                            if (info.role == StructuralRole::None)
                                                info.role = e.structural_role;
                                        }
                                    }};
        absorb_reservoir(lhs);
        absorb_reservoir(rhs);

        std::unordered_set<std::string> topk_ids;
        topk_ids.reserve(out.stats.top_k.size());
        for (const auto& e : out.stats.top_k)
            topk_ids.insert(e.template_id);

        struct ResCand
        {
            std::string template_id;
            std::uint32_t salience;
            std::uint32_t structural_surprise;
            std::uint32_t novelty;
        };
        std::vector<ResCand> res_cands;
        for (const auto& [tid, info] : sal_info)
        {
            if (topk_ids.contains(tid))
                continue; // rose into top_k by merged frequency — not a reservoir entry
            const auto cit{counts.find(tid)};
            const std::uint64_t cnt{cit != counts.end() ? cit->second : 0};
            const auto lit{levels.find(tid)};
            const std::optional<LogLevel> lvl{lit != levels.end() ? lit->second : std::nullopt};
            const auto tit{templates.find(tid)};
            const std::string_view tstr{tit != templates.end() ? std::string_view{tit->second}
                                                               : std::string_view{}};
            const auto sal{salience_score(lvl, info.role, tstr, cnt, out.window.lines_observed,
                                          info.structural_surprise, info.novelty)};
            if (sal > 0U)
                res_cands.push_back(ResCand{.template_id = tid,
                                            .salience = sal,
                                            .structural_surprise = info.structural_surprise,
                                            .novelty = info.novelty});
        }
        std::ranges::sort(res_cands,
                          [](const ResCand& a, const ResCand& b)
                          {
                              if (a.salience != b.salience)
                                  return a.salience > b.salience;
                              return a.template_id < b.template_id;
                          });
        out.stats.reservoir.reserve(res_cands.size());
        for (const auto& cand : res_cands)
        {
            ReservoirEntry entry;
            entry.template_id = cand.template_id;
            if (auto t{templates.find(cand.template_id)}; t != templates.end())
                entry.template_str = t->second;
            const auto cit{counts.find(cand.template_id)};
            entry.count = cit != counts.end() ? cit->second : 0;
            entry.frequency =
                total_lines > 0.0 ? static_cast<double>(entry.count) / total_lines : 0.0;
            if (auto l{levels.find(cand.template_id)}; l != levels.end())
                entry.dominant_level = l->second;
            entry.structural_role = sal_info[cand.template_id].role;
            entry.structural_surprise = cand.structural_surprise;
            entry.novelty = cand.novelty;
            entry.salience = cand.salience;
            out.stats.reservoir.push_back(std::move(entry));
            reserved.insert(cand.template_id);
        }
    }
}

// Composed tail aggregates + §3.6/§12.3 tail_summary, recomputed from the merged
// tail with the inputs' own tail masses collapsed into a residual bucket.
void build_composed_tail(MetaLogDocument& out, const ComposeState& state,
                         const MetaLogDocument& lhs, const MetaLogDocument& rhs)
{
    const auto& ordered = state.ordered;
    const auto& reserved = state.reserved;
    const auto k{std::min(out.stats.top_k_size, ordered.size())};
    std::uint64_t tail_count = 0;
    std::uint64_t tail_max = 0;
    std::vector<std::uint64_t> tail_counts;
    if (ordered.size() > k)
        tail_counts.reserve(ordered.size() - k);
    for (std::size_t i = k; i < ordered.size(); ++i)
    {
        if (reserved.contains(ordered[i].first))
            continue; // promoted to the reservoir — excluded from tail aggregates (SPEC §3.7.3)
        const auto c = ordered[i].second;
        tail_count += c;
        if (c > tail_max)
            tail_max = c;
        tail_counts.push_back(c);
    }
    out.stats.tail_count =
        tail_count + lhs.stats.tail_count + rhs.stats.tail_count; // approximate (SPEC §12.3)
    // Excludes templates promoted into the reservoir (not double-counted in the tail).
    out.stats.tail_unique = static_cast<std::uint64_t>(tail_counts.size());

    // SPEC §3.6 + §12.3: tail_summary is recomputed from the merged
    // tail directly (not aggregated from inputs) so it stays exact for
    // the templates we actually have counts for. Inputs' own tail
    // masses (lhs.stats.tail_count / rhs.stats.tail_count) are
    // collapsed into a residual bucket so the entropy reflects "how
    // concentrated is the visible tail vs the lumped residuals".
    if (out.stats.tail_unique > 0 && out.window.lines_observed > 0)
    {
        TailSummary ts;
        ts.tail_template_count = out.stats.tail_unique;
        std::vector<std::uint64_t> entropy_counts = tail_counts;
        const std::uint64_t residual = lhs.stats.tail_count + rhs.stats.tail_count;
        if (residual > 0)
            entropy_counts.push_back(residual);
        const std::uint64_t denom = tail_count + residual;
        ts.tail_entropy_bits = shannon_entropy_bits(entropy_counts, denom);
        ts.tail_max_rate =
            static_cast<double>(tail_max) / static_cast<double>(out.window.lines_observed);
        out.stats.tail_summary = ts;
    }
}

// Provenance: a composed document always carries it (SPEC §12.4). Extend with the
// inputs' own provenance when present; otherwise record both inputs directly.
std::vector<ProvenanceEntry> merge_provenance(const MetaLogDocument& lhs,
                                              const MetaLogDocument& rhs)
{
    std::vector<ProvenanceEntry> prov;
    if (lhs.provenance)
        prov = *lhs.provenance;
    if (rhs.provenance)
        prov.insert(prov.end(), rhs.provenance->begin(), rhs.provenance->end());
    if (prov.empty())
    {
        prov.push_back({lhs.window.start_iso, lhs.window.end_iso, lhs.source,
                        lhs.window.lines_observed, std::nullopt, lhs.coordinate});
        prov.push_back({rhs.window.start_iso, rhs.window.end_iso, rhs.source,
                        rhs.window.lines_observed, std::nullopt, rhs.coordinate});
    }
    return prov;
}

// §15.5 composed re-derivation coordinate: the SET of the inputs' raw-leaf
// coordinates (a composed input contributes its own children), never a coarse
// single bound. Returns nullopt when neither input carried a coordinate.
std::optional<ReDerivationCoordinate> merge_coordinate(const MetaLogDocument& lhs,
                                                       const MetaLogDocument& rhs)
{
    std::vector<ReDerivationCoordinate> child_coords;
    const auto collect_leaves{[&child_coords](const MetaLogDocument& doc)
                              {
                                  if (!doc.coordinate)
                                      return;
                                  if (doc.coordinate->children)
                                      child_coords.insert(child_coords.end(),
                                                          doc.coordinate->children->begin(),
                                                          doc.coordinate->children->end());
                                  else
                                      child_coords.push_back(*doc.coordinate);
                              }};
    collect_leaves(lhs);
    collect_leaves(rhs);
    if (child_coords.empty())
        return std::nullopt;
    // §15.2 COMPOSED coordinate: children present, source_ref + bounds ABSENT
    // (no sentinel — §15.2 explicitly forbids them). Consumers discriminate by
    // the presence of `children`.
    ReDerivationCoordinate composed;
    composed.children = std::move(child_coords);
    return composed;
}

// Behavior: best-effort merge of top_ngrams by summing counts on identical
// sequences. Branching/dominant_path/graph_edge_count are intentionally NOT
// re-derived (structural signals are diffed at RAW pyramid scales, never against
// composed baselines); the rare-salient STRUCTURE that must survive rides the
// reservoir (structural_surprise per entry). Returns nullopt when neither input
// had a behavior block.
std::optional<BehaviorBlock> merge_behavior(const MetaLogDocument& lhs, const MetaLogDocument& rhs)
{
    if (!lhs.behavior && !rhs.behavior)
        return std::nullopt;
    BehaviorBlock bh;
    bh.ngram_size = lhs.behavior ? lhs.behavior->ngram_size : rhs.behavior->ngram_size;
    bh.top_ngrams_size =
        lhs.behavior ? lhs.behavior->top_ngrams_size : rhs.behavior->top_ngrams_size;
    std::map<std::vector<std::string>, std::uint64_t> seq_counts;
    std::map<std::vector<std::string>, double> seq_prob_sum;
    std::map<std::vector<std::string>, std::uint64_t> seq_prob_n;
    auto absorb = [&](const std::optional<BehaviorBlock>& b)
    {
        if (!b)
            return;
        for (const auto& e : b->top_ngrams)
        {
            seq_counts[e.sequence] += e.count;
            seq_prob_sum[e.sequence] += e.probability * static_cast<double>(e.count);
            seq_prob_n[e.sequence] += e.count;
        }
    };
    absorb(lhs.behavior);
    absorb(rhs.behavior);
    std::vector<NGramEntry> entries;
    entries.reserve(seq_counts.size());
    for (auto& [seq, c] : seq_counts)
    {
        NGramEntry e;
        e.sequence = seq;
        e.count = c;
        const auto n{seq_prob_n[seq]};
        e.probability = n > 0 ? seq_prob_sum[seq] / static_cast<double>(n) : 0.0;
        entries.push_back(std::move(e));
    }
    std::ranges::sort(entries,
                      [](const NGramEntry& a, const NGramEntry& b)
                      {
                          if (a.count != b.count)
                              return a.count > b.count;
                          return a.sequence < b.sequence;
                      });
    if (entries.size() > bh.top_ngrams_size)
        entries.resize(bh.top_ngrams_size);
    bh.top_ngrams = std::move(entries);
    return bh;
}

} // namespace

MetaLogDocument compose(const MetaLogDocument& lhs, const MetaLogDocument& rhs)
{
    // §2.4 gate fires BEFORE any merging — incompatible inputs MUST fail loudly,
    // not produce a hybrid document the consumer can't reason about.
    check_processing_identifier_gate(lhs.canonicalization_version, rhs.canonicalization_version,
                                     "canonicalization_version", "compose");
    check_processing_identifier_gate(lhs.retention_profile, rhs.retention_profile,
                                     "retention_profile", "compose");

    MetaLogDocument out;
    out.metalog_version = lhs.metalog_version;
    out.producer = lhs.producer;
    out.canonicalization_version =
        carry_processing_identifier(lhs.canonicalization_version, rhs.canonicalization_version);
    out.retention_profile =
        carry_processing_identifier(lhs.retention_profile, rhs.retention_profile);
    out.window.start_iso = iso_min(lhs.window.start_iso, rhs.window.start_iso);
    out.window.end_iso = iso_max(lhs.window.end_iso, rhs.window.end_iso);
    out.window.lines_observed = lhs.window.lines_observed + rhs.window.lines_observed;
    // duration_seconds: real wall-time across the merged envelope. Approximate by
    // sum-of-durations when windows are disjoint, max-of-durations when they overlap.
    out.window.duration_seconds =
        std::max(lhs.window.duration_seconds, rhs.window.duration_seconds);
    if (lhs.window.start_iso != rhs.window.start_iso || lhs.window.end_iso != rhs.window.end_iso)
        out.window.duration_seconds = lhs.window.duration_seconds + rhs.window.duration_seconds;
    out.source = common_source(lhs.source, rhs.source);

    ComposeState state;
    aggregate_and_order(state, lhs, rhs);
    out.stats.top_k_size = lhs.stats.top_k_size;
    out.stats.unique_templates = state.ordered.size();

    build_composed_top_k(out, state, lhs, rhs);
    rederive_reservoir(out, state, lhs, rhs);
    build_composed_tail(out, state, lhs, rhs);

    // Templates dedup map: union (matches SPEC §12).
    for (auto& [tid, tstr] : state.templates)
        out.templates.emplace(tid, std::move(tstr));

    // Stability dropped per SPEC §12.1.
    out.provenance = merge_provenance(lhs, rhs);
    if (auto coord{merge_coordinate(lhs, rhs)})
        out.coordinate = std::move(coord);
    if (auto behavior{merge_behavior(lhs, rhs)})
        out.behavior = std::move(behavior);

    return out;
}

} // namespace insight::metalog
