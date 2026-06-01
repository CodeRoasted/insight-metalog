// MetaLog pairwise diff (SPEC §13): the stateless delta between two documents
// (previous -> current). Template/branching/n-gram/field-histogram/tail deltas +
// the KL/JS divergence summary. Single responsibility — diff semantics only.

#include "insight/metalog/metalog_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "insight/metalog/detail/comparability.hpp"
#include "insight/metalog/detail/statistics.hpp"

namespace insight::metalog
{
using detail::check_processing_identifier_gate;
using detail::divergences;
using detail::histogram_js;

// ── diff (SPEC §13) ────────────────────────────────────────────

namespace
{
[[nodiscard]] std::unordered_map<std::string, std::uint64_t> counts_of(const MetaLogDocument& d)
{
    std::unordered_map<std::string, std::uint64_t> out;
    out.reserve(d.stats.top_k.size());
    for (const auto& e : d.stats.top_k)
        out.emplace(e.template_id, e.count);
    return out;
}

[[nodiscard]] std::unordered_map<std::string, double> freqs_of(const MetaLogDocument& d)
{
    std::unordered_map<std::string, double> out;
    out.reserve(d.stats.top_k.size());
    for (const auto& e : d.stats.top_k)
        out.emplace(e.template_id, e.frequency);
    return out;
}

// template_deltas: union of template_ids, each row's prev/cur count + delta +
// frequencies; also collects new/vanished. Sorted by |delta| desc, id asc.
void diff_template_deltas(MetaLogDiff& out,
                          const std::unordered_map<std::string, std::uint64_t>& prev_counts,
                          const std::unordered_map<std::string, std::uint64_t>& cur_counts,
                          const std::unordered_map<std::string, double>& prev_freqs,
                          const std::unordered_map<std::string, double>& cur_freqs)
{
    std::unordered_set<std::string> all_ids;
    all_ids.reserve(prev_counts.size() + cur_counts.size());
    for (const auto& [k, _] : prev_counts)
        all_ids.insert(k);
    for (const auto& [k, _] : cur_counts)
        all_ids.insert(k);
    out.template_deltas.reserve(all_ids.size());
    for (const auto& id : all_ids)
    {
        auto p_it{prev_counts.find(id)};
        auto c_it{cur_counts.find(id)};
        const std::uint64_t pc = p_it == prev_counts.end() ? 0 : p_it->second;
        const std::uint64_t cc = c_it == cur_counts.end() ? 0 : c_it->second;
        TemplateDelta td;
        td.template_id = id;
        td.previous_count = pc;
        td.current_count = cc;
        td.delta = static_cast<std::int64_t>(cc) - static_cast<std::int64_t>(pc);
        if (auto pf{prev_freqs.find(id)}; pf != prev_freqs.end())
            td.previous_frequency = pf->second;
        if (auto cf{cur_freqs.find(id)}; cf != cur_freqs.end())
            td.current_frequency = cf->second;
        if (pc == 0 && cc > 0)
            out.new_templates.push_back(id);
        else if (pc > 0 && cc == 0)
            out.vanished_templates.push_back(id);
        out.template_deltas.push_back(std::move(td));
    }
    std::ranges::sort(out.template_deltas,
                      [](const TemplateDelta& a, const TemplateDelta& b)
                      {
                          if (std::abs(a.delta) != std::abs(b.delta))
                              return std::abs(a.delta) > std::abs(b.delta);
                          return a.template_id < b.template_id;
                      });
    std::ranges::sort(out.new_templates);
    std::ranges::sort(out.vanished_templates);
}

// branching_delta: join branching entropies on template_id. Missing branching
// rows mean "not comparable" (e.g. composed docs), not zero entropy.
void diff_branching_delta(MetaLogDiff& out, const MetaLogDocument& previous,
                          const MetaLogDocument& current)
{
    if (previous.behavior && current.behavior && previous.behavior->branching &&
        !previous.behavior->branching->empty() && current.behavior->branching &&
        !current.behavior->branching->empty())
    {
        std::unordered_map<std::string, double> prev_h;
        for (const auto& b : *previous.behavior->branching)
            prev_h[b.template_id] = b.entropy_bits;
        std::unordered_map<std::string, double> cur_h;
        for (const auto& b : *current.behavior->branching)
            cur_h[b.template_id] = b.entropy_bits;
        std::unordered_set<std::string> ids;
        for (const auto& [k, _] : prev_h)
            ids.insert(k);
        for (const auto& [k, _] : cur_h)
            ids.insert(k);
        out.branching_delta.reserve(ids.size());
        for (const auto& id : ids)
        {
            BranchingDelta bd;
            bd.template_id = id;
            bd.previous_entropy_bits = prev_h.contains(id) ? prev_h[id] : 0.0;
            bd.current_entropy_bits = cur_h.contains(id) ? cur_h[id] : 0.0;
            bd.delta_bits = bd.current_entropy_bits - bd.previous_entropy_bits;
            out.branching_delta.push_back(std::move(bd));
        }
        std::ranges::sort(out.branching_delta,
                          [](const BranchingDelta& a, const BranchingDelta& b)
                          {
                              if (std::abs(a.delta_bits) != std::abs(b.delta_bits))
                                  return std::abs(a.delta_bits) > std::abs(b.delta_bits);
                              return a.template_id < b.template_id;
                          });
    }
}

// ngram_delta: new/vanished sequences and rate-changed common ones.
void diff_ngram_delta(MetaLogDiff& out, const MetaLogDocument& previous,
                      const MetaLogDocument& current)
{
    if (previous.behavior && current.behavior)
    {
        NGramDelta nd;
        nd.ngram_size = current.behavior->ngram_size;
        std::map<std::vector<std::string>, double> prev_p;
        for (const auto& e : previous.behavior->top_ngrams)
            prev_p[e.sequence] = e.probability;
        std::map<std::vector<std::string>, double> cur_p;
        for (const auto& e : current.behavior->top_ngrams)
            cur_p[e.sequence] = e.probability;
        for (const auto& [seq, _] : cur_p)
            if (!prev_p.contains(seq))
                nd.new_ngrams.push_back(seq);
        for (const auto& [seq, _] : prev_p)
            if (!cur_p.contains(seq))
                nd.vanished_ngrams.push_back(seq);
        for (const auto& [seq, cp] : cur_p)
        {
            auto pp_it{prev_p.find(seq)};
            if (pp_it == prev_p.end())
                continue;
            const double delta = cp - pp_it->second;
            if (std::abs(delta) > 0.0)
                nd.rate_changed.push_back({seq, pp_it->second, cp, delta});
        }
        std::ranges::sort(nd.rate_changed,
                          [](const NGramRateChange& a, const NGramRateChange& b)
                          {
                              if (std::abs(a.delta) != std::abs(b.delta))
                                  return std::abs(a.delta) > std::abs(b.delta);
                              return a.sequence < b.sequence;
                          });
        if (!nd.new_ngrams.empty() || !nd.vanished_ngrams.empty() || !nd.rate_changed.empty())
            out.ngram_delta = std::move(nd);
    }
}

// field_histogram_deltas: per-(template_id, param_index) JS divergence between the
// two value_counts distributions. Only for template_ids present in both top_k
// lists with field_histograms (max_param_histograms > 0). Sorted by JS desc.
void diff_field_histogram_deltas(MetaLogDiff& out, const MetaLogDocument& previous,
                                 const MetaLogDocument& current)
{
    // Build lookup: template_id -> TopKEntry* for previous doc.
    std::unordered_map<std::string, const TopKEntry*> prev_tke;
    for (const auto& e : previous.stats.top_k)
        if (!e.field_histograms.empty())
            prev_tke[e.template_id] = &e;

    for (const auto& curr_entry : current.stats.top_k)
    {
        if (curr_entry.field_histograms.empty())
            continue;
        auto prev_it = prev_tke.find(curr_entry.template_id);
        if (prev_it == prev_tke.end())
            continue;
        const auto& prev_entry = *prev_it->second;

        for (const auto& curr_fh : curr_entry.field_histograms)
        {
            // Find the matching previous histogram for this param_index.
            const FieldHistogram* prev_fh = nullptr;
            for (const auto& ph : prev_entry.field_histograms)
            {
                if (ph.param_index == curr_fh.param_index)
                {
                    prev_fh = &ph;
                    break;
                }
            }
            if (prev_fh == nullptr)
                continue;

            FieldHistogramDelta fhd;
            fhd.template_id = curr_entry.template_id;
            fhd.param_index = curr_fh.param_index;
            fhd.previous_entropy_bits = prev_fh->entropy_bits;
            fhd.current_entropy_bits = curr_fh.entropy_bits;
            fhd.js_divergence = histogram_js(prev_fh->value_counts, prev_fh->total,
                                             curr_fh.value_counts, curr_fh.total);
            // Per-side sample size backing the JS estimate (confidence basis).
            fhd.previous_sample_count = prev_fh->total;
            fhd.current_sample_count = curr_fh.total;
            // Cardinality tracking: propagate HLL estimates when both sides have them.
            fhd.previous_cardinality = prev_fh->approximate_cardinality;
            fhd.current_cardinality = curr_fh.approximate_cardinality;
            if (fhd.previous_cardinality > 0 || fhd.current_cardinality > 0)
                fhd.cardinality_delta = static_cast<std::int64_t>(fhd.current_cardinality) -
                                        static_cast<std::int64_t>(fhd.previous_cardinality);
            out.field_histogram_deltas.push_back(std::move(fhd));
        }
    }

    std::ranges::sort(out.field_histogram_deltas,
                      [](const FieldHistogramDelta& a, const FieldHistogramDelta& b)
                      {
                          if (a.js_divergence != b.js_divergence)
                              return a.js_divergence > b.js_divergence;
                          if (a.template_id != b.template_id)
                              return a.template_id < b.template_id;
                          return a.param_index < b.param_index;
                      });
}

// tail_delta: pairwise change in long-tail shape. Only when BOTH documents carry
// a tail_summary (a one-sided tail is appearance/vanishing, expressed by the
// template-level signals). Stateless before/after/delta; consumers decide
// significance (the "louder AND more concentrated" rule is theirs).
void diff_tail_delta(MetaLogDiff& out, const MetaLogDocument& previous,
                     const MetaLogDocument& current)
{
    if (previous.stats.tail_summary && current.stats.tail_summary)
    {
        const auto& prev_tail = *previous.stats.tail_summary;
        const auto& cur_tail = *current.stats.tail_summary;
        TailDelta td;
        td.previous_tail_template_count = prev_tail.tail_template_count;
        td.current_tail_template_count = cur_tail.tail_template_count;
        td.tail_template_count_delta = static_cast<std::int64_t>(cur_tail.tail_template_count) -
                                       static_cast<std::int64_t>(prev_tail.tail_template_count);
        td.previous_tail_entropy_bits = prev_tail.tail_entropy_bits;
        td.current_tail_entropy_bits = cur_tail.tail_entropy_bits;
        td.tail_entropy_bits_delta = cur_tail.tail_entropy_bits - prev_tail.tail_entropy_bits;
        td.previous_tail_max_rate = prev_tail.tail_max_rate;
        td.current_tail_max_rate = cur_tail.tail_max_rate;
        td.tail_max_rate_delta = cur_tail.tail_max_rate - prev_tail.tail_max_rate;
        out.tail_delta = td;
    }
}

} // namespace

MetaLogDiff diff(const MetaLogDocument& previous, const MetaLogDocument& current)
{
    // §2.4 comparability gate (§13): a diff across mismatched processing contracts
    // is not meaningful — the documents fingerprint different rules. MUST fail.
    check_processing_identifier_gate(previous.canonicalization_version,
                                     current.canonicalization_version, "canonicalization_version",
                                     "diff");
    check_processing_identifier_gate(previous.retention_profile, current.retention_profile,
                                     "retention_profile", "diff");

    MetaLogDiff out;
    out.previous.window_start_iso = previous.window.start_iso;
    out.previous.window_end_iso = previous.window.end_iso;
    out.current.window_start_iso = current.window.start_iso;
    out.current.window_end_iso = current.window.end_iso;

    const auto prev_counts = counts_of(previous);
    const auto cur_counts = counts_of(current);
    const auto prev_freqs = freqs_of(previous);
    const auto cur_freqs = freqs_of(current);

    const auto [kl, js] = divergences(cur_counts, current.window.lines_observed, prev_counts,
                                      previous.window.lines_observed);
    if (current.window.lines_observed > 0 && previous.window.lines_observed > 0)
    {
        out.kl_divergence = kl;
        out.js_divergence = js;
        out.stability_score = std::clamp(1.0 - js, 0.0, 1.0);
    }

    diff_template_deltas(out, prev_counts, cur_counts, prev_freqs, cur_freqs);
    diff_branching_delta(out, previous, current);
    diff_ngram_delta(out, previous, current);
    diff_field_histogram_deltas(out, previous, current);
    diff_tail_delta(out, previous, current);

    return out;
}

} // namespace insight::metalog
