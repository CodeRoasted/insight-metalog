module;

module insight.metalog;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats;
import insight.metalog.detail.operations;
import insight.metalog.detail.cube;

namespace insight::metalog
{

namespace
{
    [[nodiscard]] std::unordered_map<TemplateId, std::uint64_t>
    counts_of(const MetaLogDocument& doc)
    {
        std::unordered_map<TemplateId, std::uint64_t> out;
        out.reserve(doc.stats.top_k.size());
        for (const auto& entry : doc.stats.top_k)
            out.emplace(entry.template_id, entry.count);
        return out;
    }

    [[nodiscard]] std::unordered_map<TemplateId, double> freqs_of(const MetaLogDocument& doc)
    {
        std::unordered_map<TemplateId, double> out;
        out.reserve(doc.stats.top_k.size());
        for (const auto& entry : doc.stats.top_k)
            out.emplace(entry.template_id, entry.frequency);
        return out;
    }

    // post: a row per template in either window with both counts, the delta and both frequencies,
    // sorted by absolute delta desc then id asc.
    void diff_template_deltas(MetaLogDiff& out,
                              const std::unordered_map<TemplateId, std::uint64_t>& prev_counts,
                              const std::unordered_map<TemplateId, std::uint64_t>& cur_counts,
                              const std::unordered_map<TemplateId, double>& prev_freqs,
                              const std::unordered_map<TemplateId, double>& cur_freqs)
    {
        std::unordered_set<TemplateId> all_ids;
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
            out.template_deltas.push_back(template_delta);
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

    // note: a missing branching row means not comparable, never zero entropy.
    void diff_branching_delta(MetaLogDiff& out, const MetaLogDocument& previous,
                              const MetaLogDocument& current)
    {
        if (previous.behavior && current.behavior && previous.behavior->branching &&
            !previous.behavior->branching->empty() && current.behavior->branching &&
            !current.behavior->branching->empty())
        {
            std::unordered_map<TemplateId, double> prev_h;
            for (const auto& branch : *previous.behavior->branching)
                prev_h[branch.template_id] = branch.entropy_bits;
            std::unordered_map<TemplateId, double> cur_h;
            for (const auto& branch : *current.behavior->branching)
                cur_h[branch.template_id] = branch.entropy_bits;
            std::unordered_set<TemplateId> ids;
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
                out.branching_delta.push_back(branch_delta);
            }
            std::ranges::sort(out.branching_delta,
                              [](const BranchingDelta& lhs, const BranchingDelta& rhs)
                              {
                                  if (std::abs(lhs.delta_bits) != std::abs(rhs.delta_bits))
                                      return std::abs(lhs.delta_bits) > std::abs(rhs.delta_bits);
                                  return lhs.template_id < rhs.template_id;
                              });
            // invariant: emitted only when some entropy moved -- the declared vacuity is the EMPTY
            // array, so emitting an unmoved union would publish a false witness.
            // note: rows that did not move survive beside those that did; the array is the finding.
            if (std::ranges::none_of(out.branching_delta, [](const BranchingDelta& row)
                                     { return row.delta_bits != 0.0; }))
                out.branching_delta.clear();
        }
    }

    // post: new and vanished sequences plus the rate-changed common ones.
    void diff_ngram_delta(MetaLogDiff& out, const MetaLogDocument& previous,
                          const MetaLogDocument& current)
    {
        if (!previous.behavior || !current.behavior)
            return;
        NGramDelta ngram_delta;
        ngram_delta.ngram_size = current.behavior->ngram_size;
        // note: new and vanished are sorted explicitly because they are iterated into the output.
        // refs: SRC-D-TIR-4, ADR-16.D1
        struct NgramProb
        {
            std::vector<TemplateId> sequence;
            double probability{0.0};
        };
        std::unordered_map<NgramId, NgramProb> prev_p;
        for (const auto& entry : previous.behavior->top_ngrams)
            prev_p[insight::ngram_id_of(entry.sequence)] = {.sequence = entry.sequence,
                                                            .probability = entry.probability};
        std::unordered_map<NgramId, NgramProb> cur_p;
        for (const auto& entry : current.behavior->top_ngrams)
            cur_p[insight::ngram_id_of(entry.sequence)] = {.sequence = entry.sequence,
                                                           .probability = entry.probability};
        for (const auto& [ngram_id, cur] : cur_p)
            if (!prev_p.contains(ngram_id))
                ngram_delta.new_ngrams.push_back(cur.sequence);
        for (const auto& [ngram_id, prev] : prev_p)
            if (!cur_p.contains(ngram_id))
                ngram_delta.vanished_ngrams.push_back(prev.sequence);
        std::ranges::sort(ngram_delta.new_ngrams);
        std::ranges::sort(ngram_delta.vanished_ngrams);
        for (const auto& [ngram_id, cur] : cur_p)
        {
            auto pp_it{prev_p.find(ngram_id)};
            if (pp_it == prev_p.end())
                continue;
            const double delta = cur.probability - pp_it->second.probability;
            if (std::abs(delta) > 0.0)
                ngram_delta.rate_changed.push_back(
                    {cur.sequence, pp_it->second.probability, cur.probability, delta});
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

    // post: defined ONLY when both documents carried a service_edges block; absent on either side
    // leaves it unset, so edge verdicts are unknown rather than all-emerged.
    // note: both blocks are canonically sorted, so the three lists need no re-sort.
    // refs: SRC-D-OTEL-21, SRC-D-OTEL-20
    void diff_service_edge_delta(MetaLogDiff& out, const MetaLogDocument& previous,
                                 const MetaLogDocument& current)
    {
        if (!previous.service_edges || !current.service_edges)
            return;
        std::map<std::pair<std::string, std::string>, std::uint64_t> prev_w;
        for (const ServiceEdge& edge : previous.service_edges->edges)
            prev_w[{edge.caller, edge.callee}] = edge.weight;
        std::map<std::pair<std::string, std::string>, std::uint64_t> cur_w;
        for (const ServiceEdge& edge : current.service_edges->edges)
            cur_w[{edge.caller, edge.callee}] = edge.weight;

        ServiceEdgeDelta delta;
        for (const auto& [key, weight] : cur_w)
            if (!prev_w.contains(key))
                delta.emerged.push_back(
                    {.caller = key.first, .callee = key.second, .weight = weight});
        for (const auto& [key, weight] : prev_w)
            if (!cur_w.contains(key))
                delta.vanished.push_back(
                    {.caller = key.first, .callee = key.second, .weight = weight});
        for (const auto& [key, cur_weight] : cur_w)
        {
            const auto prev_it{prev_w.find(key)};
            if (prev_it == prev_w.end() || prev_it->second == cur_weight)
                continue;
            delta.weight_changed.push_back({.caller = key.first,
                                            .callee = key.second,
                                            .previous_weight = prev_it->second,
                                            .current_weight = cur_weight,
                                            .delta = static_cast<std::int64_t>(cur_weight) -
                                                     static_cast<std::int64_t>(prev_it->second)});
        }
        if (!delta.emerged.empty() || !delta.vanished.empty() || !delta.weight_changed.empty())
            out.service_edge_delta = std::move(delta);
    }

    // post: the histogram for that wildcard slot, or nullptr.
    [[nodiscard]] const FieldHistogram* find_param_histogram(const TopKEntry& entry,
                                                             std::uint32_t param_index)
    {
        for (const auto& param_hist : entry.field_histograms)
            if (param_hist.param_index == param_index)
                return &param_hist;
        return nullptr;
    }

    // post: a JS divergence per (template_id, param_index) present in BOTH top_k lists with field
    // histograms, sorted by JS desc.
    void diff_field_histogram_deltas(MetaLogDiff& out, const MetaLogDocument& previous,
                                     const MetaLogDocument& current)
    {
        std::unordered_map<TemplateId, const TopKEntry*> prev_tke;
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
                const FieldHistogram* prev_fh{
                    find_param_histogram(prev_entry, curr_fh.param_index)};
                if (prev_fh == nullptr)
                    continue;

                FieldHistogramDelta fhd;
                fhd.template_id = curr_entry.template_id;
                fhd.param_index = curr_fh.param_index;
                fhd.previous_entropy_bits = prev_fh->entropy_bits;
                fhd.current_entropy_bits = curr_fh.entropy_bits;
                fhd.js_divergence = histogram_js(prev_fh->value_counts, prev_fh->total,
                                                 curr_fh.value_counts, curr_fh.total);
                // note: the two sample counts are the confidence basis behind the JS estimate.
                fhd.previous_sample_count = prev_fh->total;
                fhd.current_sample_count = curr_fh.total;
                fhd.previous_cardinality = prev_fh->approximate_cardinality;
                fhd.current_cardinality = curr_fh.approximate_cardinality;
                if (fhd.previous_cardinality > 0 || fhd.current_cardinality > 0)
                    fhd.cardinality_delta = static_cast<std::int64_t>(fhd.current_cardinality) -
                                            static_cast<std::int64_t>(fhd.previous_cardinality);
                out.field_histogram_deltas.push_back(fhd);
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

    // post: the ordinal histogram for that field, or nullptr.
    [[nodiscard]] const OrdinalHistogram* find_ordinal_histogram(const TopKEntry& entry,
                                                                 std::string_view field_name)
    {
        for (const auto& hist : entry.ordinal_histograms)
            if (hist.field_name == field_name)
                return &hist;
        return nullptr;
    }

    // post: a row per (template_id, field_name) in BOTH top_k lists, carrying both sides' counts,
    // totals and schedule_ids, in deterministic order.
    // note: the consumer gates on the schedule_ids matching, then computes the W1 distance.
    // refs: SRC-D-W1-1, SRC-D-W1-4
    void diff_ordinal_histogram_deltas(MetaLogDiff& out, const MetaLogDocument& previous,
                                       const MetaLogDocument& current)
    {
        std::unordered_map<TemplateId, const TopKEntry*> prev_tke;
        for (const auto& entry : previous.stats.top_k)
            if (!entry.ordinal_histograms.empty())
                prev_tke[entry.template_id] = &entry;

        for (const auto& curr_entry : current.stats.top_k)
        {
            if (curr_entry.ordinal_histograms.empty())
                continue;
            auto prev_it = prev_tke.find(curr_entry.template_id);
            if (prev_it == prev_tke.end())
                continue;
            const auto& prev_entry = *prev_it->second;

            for (const auto& curr_oh : curr_entry.ordinal_histograms)
            {
                const OrdinalHistogram* prev_oh{
                    find_ordinal_histogram(prev_entry, curr_oh.field_name)};
                if (prev_oh == nullptr)
                    continue;
                OrdinalHistogramDelta ohd;
                ohd.template_id = curr_entry.template_id;
                ohd.field_name = curr_oh.field_name;
                ohd.previous_schedule_id = prev_oh->schedule_id;
                ohd.current_schedule_id = curr_oh.schedule_id;
                ohd.previous_counts = prev_oh->counts;
                ohd.current_counts = curr_oh.counts;
                ohd.previous_total = prev_oh->total;
                ohd.current_total = curr_oh.total;
                out.ordinal_histogram_deltas.push_back(std::move(ohd));
            }
        }

        std::ranges::sort(out.ordinal_histogram_deltas,
                          [](const OrdinalHistogramDelta& lhs, const OrdinalHistogramDelta& rhs)
                          {
                              if (lhs.template_id != rhs.template_id)
                                  return lhs.template_id < rhs.template_id;
                              return lhs.field_name < rhs.field_name;
                          });
    }

    // post: emitted only when BOTH documents carry a tail_summary; a one-sided tail is carried by
    // the template-level signals instead.
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
            tail.tail_template_count_delta =
                static_cast<std::int64_t>(cur_tail.tail_template_count) -
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

    // note: metalog owns the ladder and the W1 distance; the cube consumes the finished map.
    // invariant: every duration field shares the frozen ladder, so the counts are directly summable
    // and the sum is an integer over the ordered top_k.
    struct ComponentOrdinal
    {
        std::vector<std::uint64_t> counts;
        std::uint64_t total{0};

        // invariant: an ABSOLUTE paired-event floor, not a ratio -- the W1 thresholds are
        // scale-relative, which is exactly what lets a tiny sample read HIGH.
        // note: corpus-picked then FROZEN; the scan and both guards are in the sample-floor test.
        // refs: STU-3.A1
        static constexpr std::uint64_t kShiftSampleFloor{32};
    };

    [[nodiscard]] std::unordered_map<std::string, ComponentOrdinal>
    aggregate_duration_by_component(const MetaLogDocument& doc)
    {
        const std::string_view duration_schedule{
            ordinal_schedule_id(OrdinalSchedule::DurationLog2Ns)};
        std::unordered_map<std::string, ComponentOrdinal> by_component;
        for (const auto& entry : doc.stats.top_k)
        {
            if (!entry.dominant_component || entry.dominant_component->empty())
                continue;
            const std::string& component{*entry.dominant_component};
            for (const auto& hist : entry.ordinal_histograms)
            {
                if (hist.schedule_id != duration_schedule)
                    continue;
                ComponentOrdinal& agg{by_component[component]};
                if (agg.counts.size() < hist.counts.size())
                    agg.counts.resize(hist.counts.size(), 0);
                for (std::size_t i{0}; i < hist.counts.size(); ++i)
                    agg.counts[i] += hist.counts[i];
                agg.total += hist.total;
            }
        }
        return by_component;
    }

    // post: for each component in BOTH windows with a comparable duration distribution, the exact
    // W1 shift bucket and its direction, oriented previous to current.
    // assert: the map is point-lookup only and never iterated into content, so it is not a
    // determinism surface.
    // note: metalog does not judge good or bad; the reading layer maps the sign.
    [[nodiscard]] std::unordered_map<std::string, OrdinalDrift>
    component_latency_shifts(const MetaLogDocument& previous, const MetaLogDocument& current)
    {
        const std::unordered_map<std::string, ComponentOrdinal> prev_by_component{
            aggregate_duration_by_component(previous)};
        const std::unordered_map<std::string, ComponentOrdinal> cur_by_component{
            aggregate_duration_by_component(current)};
        std::unordered_map<std::string, OrdinalDrift> shifts;
        for (const auto& [component, cur_agg] : cur_by_component)
        {
            const auto prev_it{prev_by_component.find(component)};
            if (prev_it == prev_by_component.end())
                // assert: below the floor the axis is INADMISSIBLE, so the component stays on the
                // mute baseline -- the skip IS the projection, never a made-up cell.
                continue;
            if (std::min(prev_it->second.total, cur_agg.total) <
                ComponentOrdinal::kShiftSampleFloor)
                continue;
            const OrdinalDrift drift{ordinal_w1(prev_it->second.counts, cur_agg.counts,
                                                prev_it->second.total, cur_agg.total)};
            if (drift.shift != OrdinalShift::None && drift.direction != OrdinalDriftDirection::None)
                shifts.emplace(component, drift);
        }
        return shifts;
    }

    // note: one side's record carries everything the frontier compare and the row need.
    // refs: DN-64.D3
    struct SalienceMemoryEntry
    {
        std::optional<EventLevel> level;
        std::uint64_t count{0};
        double frequency{0.0};
    };

    // post: the template_ids of top_k union reservoir with each one's dominant level, count and
    // share; the two sets are disjoint by construction.
    // note: point-lookup only, so the map is not a determinism surface.
    // refs: ADR-31.D8
    [[nodiscard]] std::unordered_map<TemplateId, SalienceMemoryEntry>
    salience_memory(const MetaLogDocument& doc)
    {
        std::unordered_map<TemplateId, SalienceMemoryEntry> memory;
        memory.reserve(doc.stats.top_k.size() + doc.stats.reservoir.size());
        const auto add{[&memory](const auto& entries)
                       {
                           for (const auto& entry : entries)
                               memory.emplace(entry.template_id,
                                              SalienceMemoryEntry{.level = entry.dominant_level,
                                                                  .count = entry.count,
                                                                  .frequency = entry.frequency});
                       }};
        add(doc.stats.top_k);
        add(doc.stats.reservoir);
        return memory;
    }

    [[nodiscard]] ReservoirDeltaEntry reservoir_snapshot(const ReservoirEntry& entry)
    {
        return ReservoirDeltaEntry{.template_id = entry.template_id,
                                   .dominant_level = entry.dominant_level,
                                   .structural_role = entry.structural_role,
                                   .salience = entry.salience,
                                   .count = entry.count,
                                   .frequency = entry.frequency,
                                   .retention_axis = entry.retention_axis};
    }

    // post: new and vanished rare-salient templates plus failure-frontier crossings, every list
    // emitted sorted by template_id.
    void diff_reservoir_delta(MetaLogDiff& out, const MetaLogDocument& previous,
                              const MetaLogDocument& current)
    {
        const std::unordered_map<TemplateId, SalienceMemoryEntry> prev_memory{
            salience_memory(previous)};
        const std::unordered_map<TemplateId, SalienceMemoryEntry> cur_memory{
            salience_memory(current)};
        ReservoirDelta& delta{out.reservoir_delta};

        for (const auto& entry : current.stats.reservoir)
            if (!prev_memory.contains(entry.template_id))
                delta.new_salient.push_back(reservoir_snapshot(entry));

        for (const auto& entry : previous.stats.reservoir)
            if (!cur_memory.contains(entry.template_id))
                delta.vanished_salient.push_back(reservoir_snapshot(entry));

        // note: direction is oriented previous to current -- Up crossed INTO failure, Down out.
        for (const auto& [template_id, cur_side] : cur_memory)
        {
            const auto prev_it{prev_memory.find(template_id)};
            if (prev_it == prev_memory.end())
                continue;
            const SalienceMemoryEntry& prev_side{prev_it->second};
            if (is_failure_level(prev_side.level) == is_failure_level(cur_side.level))
                continue;
            delta.frontier_crossings.push_back(FrontierCrossing{
                .template_id = template_id,
                .direction = is_failure_level(cur_side.level) ? FrontierDirection::Up
                                                              : FrontierDirection::Down,
                .previous_level = prev_side.level,
                .current_level = cur_side.level,
                .previous_count = prev_side.count,
                .current_count = cur_side.count,
                .previous_frequency = prev_side.frequency,
                .current_frequency = cur_side.frequency});
        }

        const auto by_entry_id{[](const ReservoirDeltaEntry& lhs, const ReservoirDeltaEntry& rhs)
                               { return lhs.template_id < rhs.template_id; }};
        const auto by_crossing_id{[](const FrontierCrossing& lhs, const FrontierCrossing& rhs)
                                  { return lhs.template_id < rhs.template_id; }};
        std::ranges::sort(delta.new_salient, by_entry_id);
        std::ranges::sort(delta.vanished_salient, by_entry_id);
        std::ranges::sort(delta.frontier_crossings, by_crossing_id);
    }

} // namespace

MetaLogDiff diff(const MetaLogDocument& previous, const MetaLogDocument& current)
{
    // assert: a diff across mismatched processing contracts is not meaningful and MUST fail.
    check_processing_identifier_gate(previous.canonicalization_version,
                                     current.canonicalization_version, "canonicalization_version",
                                     "diff");
    check_processing_identifier_gate(previous.retention_profile, current.retention_profile,
                                     "retention_profile", "diff");
    // note: one side absent is a legacy producer and proceeds; both stamped and different refuses.
    // refs: SRC-II-7, ADR-17.D8
    check_processing_identifier_gate(
        previous.ruleset ? std::optional<std::string>{previous.ruleset->semantic_identity}
                         : std::nullopt,
        current.ruleset ? std::optional<std::string>{current.ruleset->semantic_identity}
                        : std::nullopt,
        "semantic_identity", "diff");

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
    diff_ordinal_histogram_deltas(out, previous, current);
    diff_tail_delta(out, previous, current);
    diff_reservoir_delta(out, previous, current);
    diff_service_edge_delta(out, previous, current);
    // assert: the ONE gate is that both carried a cube; there is no axes-equality gate, since the
    // contract freezes the axis SET, not the collapse stamps.
    // refs: DN-42.D17
    if (previous.has_cube && current.has_cube)
    {
        // note: the shift map is empty when neither document carries comparable duration data.
        const std::unordered_map<std::string, OrdinalDrift> latency_shifts{
            component_latency_shifts(previous, current)};
        out.cube_diff = cube::cube_diff_of(previous.cube, current.cube, latency_shifts);
        out.has_cube_diff = true;
    }

    return out;
}

// post: true iff the diff computed field-histogram rows that never reach the wire.
// note: its declared vacuity is an empty array, so ANY row is a finding.
[[nodiscard]] bool withholds_field_histogram_deltas(const MetaLogDiff& diff) noexcept
{
    return !diff.field_histogram_deltas.empty();
}

// note: the contract and the two deltas that deliberately do not decide are on the header.
ComparisonOutcome comparison_outcome_of(const MetaLogDiff& diff) noexcept
{
    // note: the two divergences are vacuous at zero -- identical distributions.
    if (diff.kl_divergence && *diff.kl_divergence != 0.0)
        return ComparisonOutcome::Changed;
    if (diff.js_divergence && *diff.js_divergence != 0.0)
        return ComparisonOutcome::Changed;
    // assert: stability_score's no-change value is ONE, not zero, so a greater-than-zero rule would
    // witness on perfect stability.
    if (diff.stability_score && *diff.stability_score != 1.0)
        return ComparisonOutcome::Changed;
    // note: template_deltas is declared PER ROW, so a non-zero delta is the finding, not a row.
    if (std::ranges::any_of(diff.template_deltas,
                            [](const TemplateDelta& row) { return row.delta != 0; }))
        return ComparisonOutcome::Changed;
    // note: these three declare an empty array, so the array itself is the finding.
    if (!diff.new_templates.empty() || !diff.vanished_templates.empty() ||
        !diff.branching_delta.empty())
        return ComparisonOutcome::Changed;
    // note: ngram_delta is tested on content, never on the optional's engagement.
    if (diff.ngram_delta &&
        (!diff.ngram_delta->new_ngrams.empty() || !diff.ngram_delta->vanished_ngrams.empty() ||
         !diff.ngram_delta->rate_changed.empty()))
        return ComparisonOutcome::Changed;
    // note: the three delta members are the finding; the six coordinates are not.
    if (diff.tail_delta && (diff.tail_delta->tail_template_count_delta != 0 ||
                            diff.tail_delta->tail_entropy_bits_delta != 0.0 ||
                            diff.tail_delta->tail_max_rate_delta != 0.0))
        return ComparisonOutcome::Changed;
    // note: axes is a required descriptor and never a finding -- the border is.
    if (diff.has_cube_diff)
    {
        const CubeDiffBlock& cube{diff.cube_diff};
        const bool emerged{cube.has_emerging &&
                           (!cube.emerging.lower.empty() || !cube.emerging.upper.empty())};
        const bool vanished{cube.has_vanishing &&
                            (!cube.vanishing.lower.empty() || !cube.vanishing.upper.empty())};
        if (emerged || vanished)
            return ComparisonOutcome::Changed;
    }
    // note: vacuous when no salient template appeared, vanished or crossed the frontier.
    if (!diff.reservoir_delta.empty())
        return ComparisonOutcome::Changed;
    // assert: the withheld-signals witness is tested LAST -- it witnesses a finding the document
    // does not carry, so every property that carries its own has already returned.
    if (withholds_field_histogram_deltas(diff))
        return ComparisonOutcome::Changed;

    return ComparisonOutcome::Unchanged;
}

// note: the contract -- what may be named, and why this is derived -- is on the declaration.
std::vector<std::string> withheld_signals_of(const MetaLogDiff& diff)
{
    std::vector<std::string> names;
    if (withholds_field_histogram_deltas(diff))
        names.emplace_back("field_histogram_deltas");
    // assert: sorted explicitly so the MUST is held by this line, not by whoever adds a second
    // clause remembering the alphabet.
    std::ranges::sort(names);
    return names;
}

} // namespace insight::metalog
