module;

module insight.metalog;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats;
import insight.metalog.detail.operations;
import insight.metalog.detail.cube;

// MetaLog composition (SPEC §12): merge two documents into one bounded
// fingerprint covering both windows. Lossy where either input had a non-empty
// tail; required fields (lines_observed, unique_templates union, time envelope,
// re-derived reservoir) are preserved. Single responsibility — the compose
// semantics only.

namespace insight::metalog
{

// ── compose (SPEC §12) ─────────────────────────────────────────

namespace
{
    // Compare ISO 8601 lexicographically — valid for fixed-format
    // RFC 3339 UTC strings as we emit (always Z, fixed widths).
    [[nodiscard]] std::string_view iso_min(std::string_view lhs, std::string_view rhs)
    {
        if (lhs.empty())
            return rhs;
        if (rhs.empty())
            return lhs;
        return lhs < rhs ? lhs : rhs;
    }
    [[nodiscard]] std::string_view iso_max(std::string_view lhs, std::string_view rhs)
    {
        if (lhs.empty())
            return rhs;
        if (rhs.empty())
            return lhs;
        return lhs > rhs ? lhs : rhs;
    }

    SourceBlock common_source(const SourceBlock& lhs, const SourceBlock& rhs)
    {
        if (lhs == rhs)
            return lhs;
        SourceBlock out;
        if (lhs.fleet == rhs.fleet)
            out.fleet = lhs.fleet;
        if (lhs.service == rhs.service)
            out.service = lhs.service;
        if (lhs.host == rhs.host)
            out.host = lhs.host;
        // host_count: sum if both present; otherwise leave unset.
        if (lhs.host_count && rhs.host_count)
            out.host_count = *lhs.host_count + *rhs.host_count;
        // tags: keep entries present and equal in both.
        for (const auto& [key, value] : lhs.tags)
        {
            auto iter{rhs.tags.find(key)};
            if (iter != rhs.tags.end() && iter->second == value)
                out.tags.emplace(key, value);
        }
        return out;
    }

    void aggregate_top_k(std::unordered_map<TemplateId, std::uint64_t>& counts,
                         std::unordered_map<TemplateId, std::optional<EventLevel>>& levels,
                         const MetaLogDocument& doc)
    {
        // Counts + levels are the decision signal a composed document carries. The display
        // template_str is gone (SRC-D-TIR-5 field-drop): it lived only for serialise/explain,
        // resolved by id from the engine registry — never read off a composed (diff-only) document.
        for (const auto& entry : doc.stats.top_k)
        {
            counts[entry.template_id] += entry.count;
            if (entry.dominant_level && !levels.contains(entry.template_id))
                levels.emplace(entry.template_id, entry.dominant_level);
        }
    }

    // Fold a document's RESERVOIR mass into the same maps. A template is disjoint
    // across top_k/reservoir within one document, so this never double-counts an input;
    // across inputs a template that is top_k in one and reservoir in the other gets its
    // full merged count. Lets the composed top_k ranking and the re-derived reservoir
    // see the rare-salient templates' counts, which `aggregate_top_k` alone misses.
    void aggregate_reservoir(std::unordered_map<TemplateId, std::uint64_t>& counts,
                             std::unordered_map<TemplateId, std::optional<EventLevel>>& levels,
                             const MetaLogDocument& doc)
    {
        for (const auto& entry : doc.stats.reservoir)
        {
            counts[entry.template_id] += entry.count;
            if (entry.dominant_level && !levels.contains(entry.template_id))
                levels.emplace(entry.template_id, entry.dominant_level);
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
        for (const auto& hist : lhs)
            lhs_index.emplace(hist.param_index, &hist);

        std::vector<FieldHistogram> out;
        out.reserve(lhs.size() + rhs.size());
        std::unordered_set<std::uint32_t> seen;
        seen.reserve(rhs.size());

        for (const auto& rhs_hist : rhs)
        {
            seen.insert(rhs_hist.param_index);
            const auto found{lhs_index.find(rhs_hist.param_index)};
            if (found == lhs_index.end())
            {
                out.push_back(rhs_hist);
                continue;
            }
            const FieldHistogram& lhs_hist{*found->second};
            FieldHistogram merged;
            merged.param_index = rhs_hist.param_index;
            merged.total = lhs_hist.total + rhs_hist.total;
            std::unordered_map<std::string, std::uint64_t> values;
            values.reserve(lhs_hist.value_counts.size() + rhs_hist.value_counts.size());
            for (const auto& [value, count] : lhs_hist.value_counts)
                values[value] += count;
            for (const auto& [value, count] : rhs_hist.value_counts)
                values[value] += count;
            if (cap > 0 && values.size() > cap)
            {
                std::vector<std::pair<std::string, std::uint64_t>> sorted(values.begin(),
                                                                          values.end());
                std::ranges::partial_sort(sorted, sorted.begin() + static_cast<std::ptrdiff_t>(cap),
                                          [](const auto& lhs, const auto& rhs)
                                          {
                                              if (lhs.second != rhs.second)
                                                  return lhs.second > rhs.second;
                                              return lhs.first <
                                                     rhs.first; // deterministic tie-break
                                          });
                values.clear();
                values.reserve(cap);
                for (std::size_t i{0}; i < cap; ++i)
                    values.emplace(std::move(sorted[i].first), sorted[i].second);
            }
            std::vector<std::uint64_t> counts;
            counts.reserve(values.size());
            for (const auto& [value, count] : values)
                counts.push_back(count);
            merged.value_counts = std::move(values);
            merged.entropy_bits = shannon_entropy_bits(counts, merged.total);
            merged.approximate_cardinality =
                std::max(lhs_hist.approximate_cardinality, rhs_hist.approximate_cardinality);
            out.push_back(std::move(merged));
        }
        for (const auto& lhs_hist : lhs)
            if (!seen.contains(lhs_hist.param_index))
                out.push_back(lhs_hist);
        std::ranges::sort(out, [](const auto& lhs, const auto& rhs)
                          { return lhs.param_index < rhs.param_index; });
        return out;
    }

    // Shared scratch for compose(): the per-template aggregation across both inputs
    // (merged counts, inline template strings, dominant levels), the count-sorted
    // view, and the set of templates promoted into the re-derived reservoir.
    struct ComposeState
    {
        std::unordered_map<TemplateId, std::uint64_t> counts;
        std::unordered_map<TemplateId, std::optional<EventLevel>> levels;
        std::vector<std::pair<TemplateId, std::uint64_t>> ordered;
        std::unordered_set<TemplateId> reserved;
    };

    // Fold both inputs' top_k + reservoir into the aggregation maps, then build the
    // count-desc / id-asc ordering the composed top_k and tail draw from.
    void aggregate_and_order(ComposeState& state, const MetaLogDocument& lhs,
                             const MetaLogDocument& rhs)
    {
        aggregate_top_k(state.counts, state.levels, lhs);
        aggregate_top_k(state.counts, state.levels, rhs);
        aggregate_reservoir(state.counts, state.levels, lhs);
        aggregate_reservoir(state.counts, state.levels, rhs);

        state.ordered.assign(state.counts.begin(), state.counts.end());
        std::ranges::sort(state.ordered,
                          [](const auto& lhs, const auto& rhs)
                          {
                              if (lhs.second != rhs.second)
                                  return lhs.second > rhs.second;
                              return lhs.first < rhs.first;
                          });
    }

    // Composed top_k: the highest-count templates, with §3.5/§12.1 param-histogram
    // compose-carry (merge per-param across inputs; one-sided carried).
    void build_composed_top_k(MetaLogDocument& out, const ComposeState& state,
                              const MetaLogDocument& lhs, const MetaLogDocument& rhs)
    {
        const auto& levels = state.levels;
        const auto& ordered = state.ordered;
        const auto build_topk_index{[](const MetaLogDocument& doc)
                                    {
                                        std::unordered_map<TemplateId, const TopKEntry*> index;
                                        index.reserve(doc.stats.top_k.size());
                                        for (const auto& entry : doc.stats.top_k)
                                            index.emplace(entry.template_id, &entry);
                                        return index;
                                    }};
        const auto lhs_topk{build_topk_index(lhs)};
        const auto rhs_topk{build_topk_index(rhs)};
        static constexpr std::size_t kComposeHistogramCap{
            MetaLogConfig::kDefaultMaxHistogramValues};

        const auto top_k_cut{std::min(out.stats.top_k_size, ordered.size())};
        out.stats.top_k.reserve(top_k_cut);
        const auto total_lines = static_cast<double>(out.window.lines_observed);
        for (std::size_t i = 0; i < top_k_cut; ++i)
        {
            TopKEntry entry;
            entry.template_id = ordered[i].first;
            entry.count = ordered[i].second;
            entry.frequency =
                total_lines > 0.0 ? static_cast<double>(entry.count) / total_lines : 0.0;
            if (auto level_it{levels.find(entry.template_id)}; level_it != levels.end())
                entry.dominant_level = level_it->second;
            // §3.5 / §12.1 compose-visible param_histograms. Look up the matching
            // entries in each input and merge per-param; one-sided is carried; entirely
            // absent → no histograms (consistent with the cap being a no-op when input
            // producers didn't emit any). Closes the value-histogram compose gap.
            const auto lhs_entry_it{lhs_topk.find(entry.template_id)};
            const auto rhs_entry_it{rhs_topk.find(entry.template_id)};
            const std::vector<FieldHistogram> empty{};
            const auto& lhs_hists{
                lhs_entry_it != lhs_topk.end() ? lhs_entry_it->second->field_histograms : empty};
            const auto& rhs_hists{
                rhs_entry_it != rhs_topk.end() ? rhs_entry_it->second->field_histograms : empty};
            if (!lhs_hists.empty() || !rhs_hists.empty())
                entry.field_histograms =
                    merge_field_histograms(lhs_hists, rhs_hists, kComposeHistogramCap);
            out.stats.top_k.push_back(std::move(entry));
        }
    }

    // Per-template salience inputs carried across compose: the max structural-surprise
    // and novelty seen in either input's reservoir, plus the first structural role.
    struct ComposeSalienceInfo
    {
        std::uint32_t structural_surprise{0};
        std::uint32_t novelty{0};
        StructuralRole role{StructuralRole::None};
        std::string where_leaf; // §16.6 cube_coord WHERE leaf carried from an input entry
    };

    // A below-composed-top_k template ranked for the re-derived reservoir.
    struct ComposeReservoirCandidate
    {
        TemplateId template_id;
        std::uint32_t salience;
        std::uint32_t structural_surprise;
        std::uint32_t novelty;
        StructuralRole role;
        std::string where_leaf; // §16.6 cube_coord WHERE leaf
    };

    // Rank the templates salient in EITHER input's reservoir that did NOT rise into the
    // composed top_k: fold both reservoirs into per-template salience inputs, then
    // RE-DERIVE salience over the merged count + composed line total (rarity shifts on
    // merge), so the ranking reflects the composed window, not either input's.
    std::vector<ComposeReservoirCandidate>
    collect_compose_reservoir_candidates(const MetaLogDocument& out, const ComposeState& state,
                                         const MetaLogDocument& lhs, const MetaLogDocument& rhs)
    {
        const auto& counts = state.counts;
        const auto& levels = state.levels;

        std::unordered_map<TemplateId, ComposeSalienceInfo> sal_info;
        const auto absorb_reservoir{
            [&](const MetaLogDocument& doc)
            {
                for (const auto& entry : doc.stats.reservoir)
                {
                    auto& info{sal_info[entry.template_id]};
                    info.structural_surprise =
                        std::max(info.structural_surprise, entry.structural_surprise);
                    info.novelty = std::max(info.novelty, entry.novelty);
                    if (info.role == StructuralRole::None)
                        info.role = entry.structural_role;
                    // §16.6: carry the WHERE leaf from the first input
                    // entry that has a cube_coord (LOCATION only).
                    if (info.where_leaf.empty() && entry.cube_coord && entry.cube_coord->where &&
                        !entry.cube_coord->where->empty())
                        info.where_leaf = entry.cube_coord->where->back();
                }
            }};
        absorb_reservoir(lhs);
        absorb_reservoir(rhs);

        std::unordered_set<TemplateId> topk_ids;
        topk_ids.reserve(out.stats.top_k.size());
        for (const auto& entry : out.stats.top_k)
            topk_ids.insert(entry.template_id);

        std::vector<ComposeReservoirCandidate> res_cands;
        for (const auto& [tid, info] : sal_info)
        {
            if (topk_ids.contains(tid))
                continue; // rose into top_k by merged frequency — not a reservoir entry
            const auto cit{counts.find(tid)};
            const std::uint64_t cnt{cit != counts.end() ? cit->second : 0};
            const auto lit{levels.find(tid)};
            const std::optional<EventLevel> lvl{lit != levels.end() ? lit->second : std::nullopt};
            // template_str is gone from composed documents (SRC-D-TIR-5). looks_like_failure's
            // lexicon cue is redundant here: a reservoir candidate is folded from inputs that were
            // ALREADY admitted by salience (carrying level + structural_surprise/novelty, the
            // dominant severity axes); canon also lifts declared failure markers to
            // LogLevel::Error, captured by `lvl`. The composed re-derivation re-ranks on those
            // carried signals, not on re-parsing the string. The failure-cue tier is moot on an
            // empty tmpl, so the SRC-D-PROV-1 echoed_source gate is a no-op here → pass false (the
            // composed input carries no per-line provenance).
            // salience reads SEVERITY, not evidence quality — provenance is deliberately not a
            // salience input (a declared Error and an inferred one are equally worth retaining).
            const auto sal{
                salience_score(lvl ? std::optional<LogLevel>{lvl->value()} : std::nullopt,
                               info.role, std::string_view{},
                               /*echoed_source=*/false, cnt, out.window.lines_observed,
                               info.structural_surprise, info.novelty)};
            if (sal > 0U)
                res_cands.push_back(
                    ComposeReservoirCandidate{.template_id = tid,
                                              .salience = sal,
                                              .structural_surprise = info.structural_surprise,
                                              .novelty = info.novelty,
                                              .role = info.role,
                                              .where_leaf = info.where_leaf});
        }
        return res_cands;
    }

    // Salience reservoir re-derivation (SPEC §3.7.3 / §12.1). Carry the rare-salient
    // templates through composition instead of dropping them into the tail (the
    // multi-scale gap: composed/pyramid baselines were blind to a lone fatal / off-path
    // branch). Bounded by the inputs' (already diversity-capped) reservoirs; admitted in
    // salience order with a deterministic template_id tie-break.
    void rederive_reservoir(MetaLogDocument& out, ComposeState& state, const MetaLogDocument& lhs,
                            const MetaLogDocument& rhs)
    {
        const auto& counts = state.counts;
        const auto& levels = state.levels;
        auto& reserved = state.reserved;
        const auto total_lines = static_cast<double>(out.window.lines_observed);

        // §16.6: a composed document emits a cube only when BOTH inputs had one; the
        // re-derived reservoir entries carry a cube_coord only in that case.
        const bool inputs_have_cube{lhs.has_cube && rhs.has_cube};
        auto res_cands = collect_compose_reservoir_candidates(out, state, lhs, rhs);
        std::ranges::sort(
            res_cands,
            [](const ComposeReservoirCandidate& lhs, const ComposeReservoirCandidate& rhs)
            {
                if (lhs.salience != rhs.salience)
                    return lhs.salience > rhs.salience;
                return lhs.template_id < rhs.template_id;
            });
        out.stats.reservoir.reserve(res_cands.size());
        for (const auto& cand : res_cands)
        {
            ReservoirEntry entry;
            entry.template_id = cand.template_id;
            const auto cit{counts.find(cand.template_id)};
            entry.count = cit != counts.end() ? cit->second : 0;
            entry.frequency =
                total_lines > 0.0 ? static_cast<double>(entry.count) / total_lines : 0.0;
            if (auto level_it{levels.find(cand.template_id)}; level_it != levels.end())
                entry.dominant_level = level_it->second;
            entry.structural_role = cand.role;
            entry.structural_surprise = cand.structural_surprise;
            entry.novelty = cand.novelty;
            entry.salience = cand.salience;
            if (inputs_have_cube)
                entry.cube_coord = cube::cube_location(
                    entry.dominant_level ? std::optional<LogLevel>{entry.dominant_level->value()}
                                         : std::nullopt,
                    cand.where_leaf);
            out.stats.reservoir.push_back(std::move(entry));
            reserved.insert(cand.template_id);
        }
    }

    // Composed tail aggregates + §3.6/§12.3 tail_summary, recomputed from the merged
    // tail with the inputs' own tail masses collapsed into a residual bucket.
    void build_composed_tail(MetaLogDocument& out, const ComposeState& state,
                             const MetaLogDocument& lhs, const MetaLogDocument& rhs)
    {
        const auto& ordered = state.ordered;
        const auto& reserved = state.reserved;
        const auto top_k_cut{std::min(out.stats.top_k_size, ordered.size())};
        std::uint64_t tail_count = 0;
        std::uint64_t tail_max = 0;
        std::vector<std::uint64_t> tail_counts;
        if (ordered.size() > top_k_cut)
            tail_counts.reserve(ordered.size() - top_k_cut);
        for (std::size_t i = top_k_cut; i < ordered.size(); ++i)
        {
            if (reserved.contains(ordered[i].first))
                continue; // promoted to the reservoir — excluded from tail aggregates (SPEC §3.7.3)
            const auto count = ordered[i].second;
            tail_count += count;
            // NOLINTNEXTLINE (readability-use-std-min-max) defensive clamp (hot path)
            if (count > tail_max)
                tail_max = count;
            tail_counts.push_back(count);
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
            TailSummary summary;
            summary.tail_template_count = out.stats.tail_unique;
            std::vector<std::uint64_t> entropy_counts = tail_counts;
            const std::uint64_t residual = lhs.stats.tail_count + rhs.stats.tail_count;
            if (residual > 0)
                entropy_counts.push_back(residual);
            const std::uint64_t denom = tail_count + residual;
            summary.tail_entropy_bits = shannon_entropy_bits(entropy_counts, denom);
            summary.tail_max_rate =
                static_cast<double>(tail_max) / static_cast<double>(out.window.lines_observed);
            out.stats.tail_summary = summary;
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
    std::optional<BehaviorBlock> merge_behavior(const MetaLogDocument& lhs,
                                                const MetaLogDocument& rhs)
    {
        if (!lhs.behavior && !rhs.behavior)
            return std::nullopt;
        BehaviorBlock behavior;
        behavior.ngram_size = lhs.behavior ? lhs.behavior->ngram_size : rhs.behavior->ngram_size;
        behavior.top_ngrams_size =
            lhs.behavior ? lhs.behavior->top_ngrams_size : rhs.behavior->top_ngrams_size;
        // SRC-D-TIR-4(2): one n-gram accumulator keyed on the scalar NgramId, carrying the
        // sequence for output — replaces the three vector<TemplateId>-keyed maps. One O(L)
        // id-compute + one fixed-width map op per entry instead of three sequence
        // hashes+compares. The output `entries` is re-sorted below (count desc, sequence
        // asc), so the map iteration order is not a determinism surface (ADR-16).
        struct NgramAccum
        {
            std::vector<TemplateId> sequence;
            std::uint64_t count{0};
            double prob_sum{0.0};
            std::uint64_t prob_n{0};
        };
        std::unordered_map<NgramId, NgramAccum> acc;
        auto absorb = [&](const std::optional<BehaviorBlock>& block)
        {
            if (!block)
                return;
            for (const auto& entry : block->top_ngrams)
            {
                auto [iter, inserted]{acc.try_emplace(insight::ngram_id_of(entry.sequence))};
                if (inserted)
                    iter->second.sequence = entry.sequence;
                iter->second.count += entry.count;
                iter->second.prob_sum += entry.probability * static_cast<double>(entry.count);
                iter->second.prob_n += entry.count;
            }
        };
        absorb(lhs.behavior);
        absorb(rhs.behavior);
        std::vector<NGramEntry> entries;
        entries.reserve(acc.size());
        for (auto& [tid, accum] : acc)
        {
            NGramEntry entry;
            entry.sequence = std::move(accum.sequence);
            entry.count = accum.count;
            entry.probability =
                accum.prob_n > 0 ? accum.prob_sum / static_cast<double>(accum.prob_n) : 0.0;
            entries.push_back(std::move(entry));
        }
        std::ranges::sort(entries,
                          [](const NGramEntry& lhs, const NGramEntry& rhs)
                          {
                              if (lhs.count != rhs.count)
                                  return lhs.count > rhs.count;
                              return lhs.sequence < rhs.sequence;
                          });
        if (entries.size() > behavior.top_ngrams_size)
            entries.resize(behavior.top_ngrams_size);
        behavior.top_ngrams = std::move(entries);
        return behavior;
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
    // SRC-II-7 (ADR-17): composing across DIFFERENT composed-ruleset identities merges documents
    // that fingerprint different vocabularies — refuse. Absence-tolerant (a legacy input proceeds).
    check_processing_identifier_gate(
        lhs.ruleset ? std::optional<std::string>{lhs.ruleset->semantic_identity} : std::nullopt,
        rhs.ruleset ? std::optional<std::string>{rhs.ruleset->semantic_identity} : std::nullopt,
        "semantic_identity", "compose");

    MetaLogDocument out;
    out.metalog_version = lhs.metalog_version;
    out.producer = lhs.producer;
    out.canonicalization_version =
        carry_processing_identifier(lhs.canonicalization_version, rhs.canonicalization_version);
    out.retention_profile =
        carry_processing_identifier(lhs.retention_profile, rhs.retention_profile);
    // SRC-II-7: carry the composed-ruleset identity only when BOTH inputs supplied it (matched —
    // gated above); when one omits, omitting from the output is honest (the merge covers an input
    // under an unstated ruleset). Mirrors carry_processing_identifier for the
    // optional<RulesetIdentity> field.
    out.ruleset = (lhs.ruleset && rhs.ruleset) ? lhs.ruleset : std::nullopt;
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

    // Display template strings are no longer carried on a composed document (SRC-D-TIR-5): they
    // resolve by id from the engine registry at serialise.

    // Stability dropped per SPEC §12.1.
    out.provenance = merge_provenance(lhs, rhs);
    if (auto coord{merge_coordinate(lhs, rhs)})
        out.coordinate = std::move(coord);
    if (auto behavior{merge_behavior(lhs, rhs)})
        out.behavior = std::move(behavior);
    // SPEC §16.7 / §12.1: the cube is RE-CLOSED, not merged cell-by-cell (the
    // distributive counts add but the closure/border do not). Omitted when either input
    // omits a cube. Gate on the explicit presence flags; compose_cubes returns optional<CubeBlock>
    // (it omits the re-closure when the axes mismatch), a local in metalog's TU — unaffected by the
    // MSVC consumer-synthesis bug that drove the cube member to bool+value.
    if (lhs.has_cube && rhs.has_cube)
    {
        if (auto composed{cube::compose_cubes(lhs.cube, rhs.cube)})
        {
            out.cube = std::move(*composed);
            out.has_cube = true;
        }
    }

    return out;
}

} // namespace insight::metalog
