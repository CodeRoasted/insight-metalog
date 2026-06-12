module;

module insight.metalog;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail;

// MetaLog pairwise diff (SPEC §13): the stateless delta between two documents
// (previous -> current). Template/branching/n-gram/field-histogram/tail deltas +
// the KL/JS divergence summary. Single responsibility — diff semantics only.




namespace insight::metalog
{

// ── diff (SPEC §13) ────────────────────────────────────────────

namespace
{
[[nodiscard]] std::unordered_map<std::string, std::uint64_t> counts_of(const MetaLogDocument& doc)
{
    std::unordered_map<std::string, std::uint64_t> out;
    out.reserve(doc.stats.top_k.size());
    for (const auto& entry : doc.stats.top_k)
        out.emplace(entry.template_id, entry.count);
    return out;
}

[[nodiscard]] std::unordered_map<std::string, double> freqs_of(const MetaLogDocument& doc)
{
    std::unordered_map<std::string, double> out;
    out.reserve(doc.stats.top_k.size());
    for (const auto& entry : doc.stats.top_k)
        out.emplace(entry.template_id, entry.frequency);
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
    for (const auto& [key, _sink] : prev_counts)
        all_ids.insert(key);
    for (const auto& [key, _sink] : cur_counts)
        all_ids.insert(key);
    out.template_deltas.reserve(all_ids.size());
    for (const auto& template_id : all_ids)
    {
        auto p_it{prev_counts.find(template_id)};
        auto c_it{cur_counts.find(template_id)};
        const std::uint64_t prev_count = p_it == prev_counts.end() ? 0 : p_it->second;
        const std::uint64_t cur_count = c_it == cur_counts.end() ? 0 : c_it->second;
        TemplateDelta template_delta;
        template_delta.template_id = template_id;
        template_delta.previous_count = prev_count;
        template_delta.current_count = cur_count;
        template_delta.delta =
            static_cast<std::int64_t>(cur_count) - static_cast<std::int64_t>(prev_count);
        if (auto prev_freq{prev_freqs.find(template_id)}; prev_freq != prev_freqs.end())
            template_delta.previous_frequency = prev_freq->second;
        if (auto cur_freq{cur_freqs.find(template_id)}; cur_freq != cur_freqs.end())
            template_delta.current_frequency = cur_freq->second;
        if (prev_count == 0 && cur_count > 0)
            out.new_templates.push_back(template_id);
        else if (prev_count > 0 && cur_count == 0)
            out.vanished_templates.push_back(template_id);
        out.template_deltas.push_back(std::move(template_delta));
    }
    std::ranges::sort(out.template_deltas,
                      [](const TemplateDelta& lhs, const TemplateDelta& rhs)
                      {
                          if (std::abs(lhs.delta) != std::abs(rhs.delta))
                              return std::abs(lhs.delta) > std::abs(rhs.delta);
                          return lhs.template_id < rhs.template_id;
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
        for (const auto& branch : *previous.behavior->branching)
            prev_h[branch.template_id] = branch.entropy_bits;
        std::unordered_map<std::string, double> cur_h;
        for (const auto& branch : *current.behavior->branching)
            cur_h[branch.template_id] = branch.entropy_bits;
        std::unordered_set<std::string> ids;
        for (const auto& [key, _sink] : prev_h)
            ids.insert(key);
        for (const auto& [key, _sink] : cur_h)
            ids.insert(key);
        out.branching_delta.reserve(ids.size());
        for (const auto& template_id : ids)
        {
            BranchingDelta branch_delta;
            branch_delta.template_id = template_id;
            branch_delta.previous_entropy_bits =
                prev_h.contains(template_id) ? prev_h[template_id] : 0.0;
            branch_delta.current_entropy_bits =
                cur_h.contains(template_id) ? cur_h[template_id] : 0.0;
            branch_delta.delta_bits =
                branch_delta.current_entropy_bits - branch_delta.previous_entropy_bits;
            out.branching_delta.push_back(std::move(branch_delta));
        }
        std::ranges::sort(out.branching_delta,
                          [](const BranchingDelta& lhs, const BranchingDelta& rhs)
                          {
                              if (std::abs(lhs.delta_bits) != std::abs(rhs.delta_bits))
                                  return std::abs(lhs.delta_bits) > std::abs(rhs.delta_bits);
                              return lhs.template_id < rhs.template_id;
                          });
    }
}

// ngram_delta: new/vanished sequences and rate-changed common ones.
void diff_ngram_delta(MetaLogDiff& out, const MetaLogDocument& previous,
                      const MetaLogDocument& current)
{
    if (!previous.behavior || !current.behavior)
        return;
    NGramDelta ngram_delta;
    ngram_delta.ngram_size = current.behavior->ngram_size;
    std::map<std::vector<std::string>, double> prev_p;
    for (const auto& entry : previous.behavior->top_ngrams)
        prev_p[entry.sequence] = entry.probability;
    std::map<std::vector<std::string>, double> cur_p;
    for (const auto& entry : current.behavior->top_ngrams)
        cur_p[entry.sequence] = entry.probability;
    for (const auto& [seq, _sink] : cur_p)
        if (!prev_p.contains(seq))
            ngram_delta.new_ngrams.push_back(seq);
    for (const auto& [seq, _sink] : prev_p)
        if (!cur_p.contains(seq))
            ngram_delta.vanished_ngrams.push_back(seq);
    for (const auto& [seq, cur_prob] : cur_p)
    {
        auto pp_it{prev_p.find(seq)};
        if (pp_it == prev_p.end())
            continue;
        const double delta = cur_prob - pp_it->second;
        if (std::abs(delta) > 0.0)
            ngram_delta.rate_changed.push_back({seq, pp_it->second, cur_prob, delta});
    }
    std::ranges::sort(ngram_delta.rate_changed,
                      [](const NGramRateChange& lhs, const NGramRateChange& rhs)
                      {
                          if (std::abs(lhs.delta) != std::abs(rhs.delta))
                              return std::abs(lhs.delta) > std::abs(rhs.delta);
                          return lhs.sequence < rhs.sequence;
                      });
    if (!ngram_delta.new_ngrams.empty() || !ngram_delta.vanished_ngrams.empty() ||
        !ngram_delta.rate_changed.empty())
        out.ngram_delta = std::move(ngram_delta);
}

// The histogram for a given wildcard slot within a top_k entry, or nullptr.
[[nodiscard]] const FieldHistogram* find_param_histogram(const TopKEntry& entry,
                                                         std::uint32_t param_index)
{
    for (const auto& param_hist : entry.field_histograms)
        if (param_hist.param_index == param_index)
            return &param_hist;
    return nullptr;
}

// field_histogram_deltas: per-(template_id, param_index) JS divergence between the
// two value_counts distributions. Only for template_ids present in both top_k
// lists with field_histograms (max_param_histograms > 0). Sorted by JS desc.
void diff_field_histogram_deltas(MetaLogDiff& out, const MetaLogDocument& previous,
                                 const MetaLogDocument& current)
{
    // Build lookup: template_id -> TopKEntry* for previous doc.
    std::unordered_map<std::string, const TopKEntry*> prev_tke;
    for (const auto& entry : previous.stats.top_k)
        if (!entry.field_histograms.empty())
            prev_tke[entry.template_id] = &entry;

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
            const FieldHistogram* prev_fh{find_param_histogram(prev_entry, curr_fh.param_index)};
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
                      [](const FieldHistogramDelta& lhs, const FieldHistogramDelta& rhs)
                      {
                          if (lhs.js_divergence != rhs.js_divergence)
                              return lhs.js_divergence > rhs.js_divergence;
                          if (lhs.template_id != rhs.template_id)
                              return lhs.template_id < rhs.template_id;
                          return lhs.param_index < rhs.param_index;
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
        TailDelta tail;
        tail.previous_tail_template_count = prev_tail.tail_template_count;
        tail.current_tail_template_count = cur_tail.tail_template_count;
        tail.tail_template_count_delta = static_cast<std::int64_t>(cur_tail.tail_template_count) -
                                         static_cast<std::int64_t>(prev_tail.tail_template_count);
        tail.previous_tail_entropy_bits = prev_tail.tail_entropy_bits;
        tail.current_tail_entropy_bits = cur_tail.tail_entropy_bits;
        tail.tail_entropy_bits_delta = cur_tail.tail_entropy_bits - prev_tail.tail_entropy_bits;
        tail.previous_tail_max_rate = prev_tail.tail_max_rate;
        tail.current_tail_max_rate = cur_tail.tail_max_rate;
        tail.tail_max_rate_delta = cur_tail.tail_max_rate - prev_tail.tail_max_rate;
        out.tail_delta = tail;
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

    const auto [kl_value, js_value] = divergences(cur_counts, current.window.lines_observed,
                                                  prev_counts, previous.window.lines_observed);
    if (current.window.lines_observed > 0 && previous.window.lines_observed > 0)
    {
        out.kl_divergence = kl_value;
        out.js_divergence = js_value;
        out.stability_score = std::clamp(1.0 - js_value, 0.0, 1.0);
    }

    diff_template_deltas(out, prev_counts, cur_counts, prev_freqs, cur_freqs);
    diff_branching_delta(out, previous, current);
    diff_ngram_delta(out, previous, current);
    diff_field_histogram_deltas(out, previous, current);
    diff_tail_delta(out, previous, current);

    return out;
}

} // namespace insight::metalog
