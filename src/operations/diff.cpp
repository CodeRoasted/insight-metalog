module;

module insight.metalog;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats;
import insight.metalog.detail.operations;
import insight.metalog.detail.cube;

// MetaLog pairwise diff (SPEC §13): the stateless delta between two documents
// (previous -> current). Template/branching/n-gram/field-histogram/tail deltas +
// the KL/JS divergence summary. Single responsibility — diff semantics only.

namespace insight::metalog
{

// ── diff (SPEC §13) ────────────────────────────────────────────

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

    // template_deltas: union of template_ids, each row's prev/cur count + delta +
    // frequencies; also collects new/vanished. Sorted by |delta| desc, id asc.
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

    // branching_delta: join branching entropies on template_id. Missing branching
    // rows mean "not comparable" (e.g. composed docs), not zero entropy.
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
        // SRC-D-TIR-4(2): scalar-NgramId point-lookup maps, value carrying the sequence for
        // output. new_ngrams / vanished_ngrams are iterated into output, so — unlike
        // rate_changed (sorted at the end) — they are explicitly sorted here to stay
        // determinism-stable across stdlibs (ADR-16: never emit unordered iteration order).
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

    // service_edge_delta (O4b, SRC-D-OTEL-21): the service-topology delta — its OWN pass. Defined
    // ONLY when BOTH documents carried a service_edges block (both had trace substrate); absent on
    // either ⇒ leave it unset (edge verdicts are *unknown*, never "all emerged" — SRC-D-OTEL-20).
    // Semantics-free set/integer arithmetic (metalog stays polarity-blind; the degraded reading +
    // fold are eidos). Both blocks are canonical-sorted, and std::map iteration is ordered →
    // emerged/vanished/weight_changed are canonical without a re-sort. Emitted only when non-empty
    // (the ngram_delta discipline — an empty delta and an absent block both mean "no edge change"
    // to the consumer).
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
        for (const auto& [key, weight] :
             cur_w) // emerged: in current, absent in previous (θ_was=0, θ_now=1)
            if (!prev_w.contains(key))
                delta.emerged.push_back(
                    {.caller = key.first, .callee = key.second, .weight = weight});
        for (const auto& [key, weight] : prev_w) // vanished: in previous, absent in current
            if (!cur_w.contains(key))
                delta.vanished.push_back(
                    {.caller = key.first, .callee = key.second, .weight = weight});
        for (const auto& [key, cur_weight] :
             cur_w) // weight-changed: present both sides, weight moved
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
                // Per-side sample size backing the JS estimate (confidence basis).
                fhd.previous_sample_count = prev_fh->total;
                fhd.current_sample_count = curr_fh.total;
                // Cardinality tracking: propagate HLL estimates when both sides have them.
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

    // The ordinal histogram for `field_name` within a top_k entry, or nullptr.
    [[nodiscard]] const OrdinalHistogram* find_ordinal_histogram(const TopKEntry& entry,
                                                                 std::string_view field_name)
    {
        for (const auto& hist : entry.ordinal_histograms)
            if (hist.field_name == field_name)
                return &hist;
        return nullptr;
    }

    // ordinal_histogram_deltas (§4A.4 SRC-D-W1-1/SRC-D-W1-4 — the W1 channel): per-(template_id,
    // ordinal field)
    // pairing of the two windows' binned ordinal histograms. Only for (template_id, field_name)
    // present in BOTH top_k lists with ordinal_histograms. Carries both sides' raw counts + totals
    // + schedule_ids — eidos gates on the schedule_ids matching (the SRC-D-W1-4 comparability gate)
    // then computes the exact-integer Wasserstein-1 distance. Deterministic order (template_id,
    // field_name).
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

    // ── latency_shift differential axis (cube_differential_axes.md §4) ──
    // The Attribution Cube's first differential dimension: a per-component latency shift the
    // emerging border reads at diff time. Metalog owns the ladder + the W1 distance, so the whole
    // computation lives here; the cube (detail.cube) only consumes the finished map.

    // A component's summed latency distribution for this document: every top_k entry's
    // DurationLog2Ns ordinal histogram, attributed to the entry's dominant_component (the
    // per-component MECH granularity — §7.3; the finer per-(template,component) is the alternative
    // not built). All duration fields share the frozen 48-bin ladder, so their counts are
    // directly summable. Deterministic: integer sums over the ordered top_k, order-independent.
    struct ComponentOrdinal
    {
        std::vector<std::uint64_t> counts;
        std::uint64_t total{0};

        // Thin-sample admissibility floor (§6.1.1). ordinal_w1's octave thresholds are
        // scale-relative (W1 = numerator/(Na·Nb)), so they apply NO absolute-sample gate — a
        // 1-event-vs-1-event pairing manufactures latency_shift=HIGH from a single differing bin. A
        // shift verdict is trustworthy only when BOTH sides carry at least this many paired events.
        // This is an ABSOLUTE count (not a ratio like kCoverFloor): the ratio-normalization is
        // exactly what lets tiny samples read HIGH, so what is missing is a floor on N, not another
        // ratio.
        //
        // CORPUS-PICKED (pre-registered, then FROZEN — studies/003): a resampling scan draws
        // matched NULL pairs (both sides from the SAME representative log2-duration shape — no real
        // shift) at a grid of sample sizes and measures the false-ACTIONABLE rate (MED ≥2 octaves /
        // HIGH ≥5 — the bands a consumer acts on; the LOW ½-octave band is sampling-noise-dominated
        // and accepted as noise-adjacent). The binding shape is a bimodal cache (hit/miss): its
        // null false-actionable rate is 5.4% at N=16, 1.8% at N=24, 0.6% at N=32. 32 is chosen over
        // the razor-thin 24 for a ~3× margin under the 2% target (a frozen guard at 1.8% would
        // flake across seeds), and a real +3-octave (8×) latency_multiplier regression still
        // emerges >97% of the time at N=32 (the positive control). Frozen here; the scan + both
        // guards are test_shift_sample_floor.cpp.
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
                continue; // no WHERE label → no per-component shift attribution
            const std::string& component{*entry.dominant_component}; // engaged: guarded just above
            for (const auto& hist : entry.ordinal_histograms)
            {
                if (hist.schedule_id != duration_schedule)
                    continue; // only the latency/duration ladder feeds latency_shift (size/bytes is
                              // not it)
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

    // The per-component latency drift map the cube's diff-only latency_shift axis reads. For each
    // component present in BOTH windows with a comparable duration distribution, the exact W1 shift
    // bucket AND direction — BIDIRECTIONAL and polarity-MUTE (cube_differential_axes.md §7.4): a
    // component that moved in EITHER direction (up = higher/slower, down = lower/faster) is a shift
    // cell. metalog does NOT judge good/bad — the reading layer (eidos classify) maps the sign to
    // regression/recovery. Only within-noise (shift None) and the rare exactly-balanced case
    // (direction None) are dropped (no directional shift); those are the SHIFT_NONE baseline. Used
    // for point lookup only (never iterated into content) → the unordered_map is not a determinism
    // surface; the (component → drift) SET is a deterministic function of the inputs. The sign is
    // oriented previous→current (the MetaLogDiff previous/current stamp): up = current shifted
    // higher than previous, so diff(A,B) and diff(B,A) carry opposite signs — a consumer reads the
    // order off the serialized previous/current, not a call parameter.
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
                continue; // absent in baseline → no comparable distribution (no A-side to diff
                          // against)
            // Thin-sample floor (§6.1.1): a shift verdict needs enough paired events to be
            // trustworthy. Below the floor the axis is INADMISSIBLE for this window — we skip the
            // component, leaving it on the SHIFT_NONE ≡ kStar mute baseline. On this
            // emergent-at-diff axis "cannot carry the axis" and "no cell emerges" coincide by
            // construction (project-to-*), so the skip IS the projection — never a manufactured
            // SHIFT_NONE cell. This is the ONLY shift border-emitter, so single-definition holds
            // without a shared predicate.
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

    // ── reservoir delta (§5.3) ─────────────────────────────────────
    // The ERROR/FATAL failure frontier is `is_failure_level`, EXPORTED from metalog.api.cppm —
    // this TU held the second of two copies of it (DN-64.D3 row 6) and no longer spells its own.

    // One side's salience-memory record for a template: everything the frontier compare and the
    // crossing row need, so a consumer never re-reads the documents to render one.
    struct SalienceMemoryEntry
    {
        std::optional<EventLevel> level;
        std::uint64_t count{0};
        double frequency{0.0};
    };

    // A document's salience memory = the template_ids of top_k ∪ reservoir with the
    // dominant_level, count and share each carries. Disjoint by construction (a reservoir template
    // did not make top_k), so no key collision. POINT-LOOKUP map only (membership + frontier level
    // compare); never iterated into output, so the unordered_map is not a determinism surface
    // (ADR-31.D8), exactly like component_latency_shifts above.
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
                                   .retention_axis = entry.retention_axis};
    }

    // The §5.3 reservoir delta: new/vanished rare-salient templates over the two documents'
    // salience memory + ERROR/FATAL failure-frontier crossings. Additive on the derived diff
    // (no version bump). Every list is emitted sorted by template_id — the ONLY output order,
    // so the unordered_map membership lookups never leak (ADR-31.D8).
    void diff_reservoir_delta(MetaLogDiff& out, const MetaLogDocument& previous,
                              const MetaLogDocument& current)
    {
        const std::unordered_map<TemplateId, SalienceMemoryEntry> prev_memory{
            salience_memory(previous)};
        const std::unordered_map<TemplateId, SalienceMemoryEntry> cur_memory{
            salience_memory(current)};
        ReservoirDelta& delta{out.reservoir_delta};

        // new_salient: current.reservoir entries absent from previous.(top_k ∪ reservoir).
        for (const auto& entry : current.stats.reservoir)
            if (!prev_memory.contains(entry.template_id))
                delta.new_salient.push_back(reservoir_snapshot(entry));

        // vanished_salient: previous.reservoir entries absent from current.(top_k ∪ reservoir).
        for (const auto& entry : previous.stats.reservoir)
            if (!cur_memory.contains(entry.template_id))
                delta.vanished_salient.push_back(reservoir_snapshot(entry));

        // frontier_crossings: templates in BOTH memories whose dominant_level crosses the
        // failure frontier (a change in failure-membership). Direction is oriented
        // previous→current: Up = crossed INTO failure, Down = crossed OUT. Both sides' counts and
        // shares ride along, from the memory record each side owns.
        for (const auto& [template_id, cur_side] : cur_memory)
        {
            const auto prev_it{prev_memory.find(template_id)};
            if (prev_it == prev_memory.end())
                continue; // not on both sides → not a crossing (it is a new/vanished member)
            const SalienceMemoryEntry& prev_side{prev_it->second};
            if (is_failure_level(prev_side.level) == is_failure_level(cur_side.level))
                continue; // failure-membership unchanged → no crossing
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
    // §2.4 comparability gate (§13): a diff across mismatched processing contracts
    // is not meaningful — the documents fingerprint different rules. MUST fail.
    check_processing_identifier_gate(previous.canonicalization_version,
                                     current.canonicalization_version, "canonicalization_version",
                                     "diff");
    check_processing_identifier_gate(previous.retention_profile, current.retention_profile,
                                     "retention_profile", "diff");
    // SRC-II-7 (ADR-17): two documents are comparable iff their composed-ruleset identity
    // matches. Both stamped + different ⇒ the docs fingerprint DIFFERENT vocabularies ⇒ REFUSE
    // (this is the "no raw inputs to re-segment" branch — a stored MetaLog cannot be re-tokenized;
    // the Sift raw path re-tokenizes both sides under the live composition, so their identities
    // match here). One side absent (a legacy producer) ⇒ proceed, absence-tolerant — never a silent
    // equal across a KNOWN mismatch.
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
    diff_ordinal_histogram_deltas(out, previous, current); // W1 (§4A.4 SRC-D-W1-1/SRC-D-W1-4)
    diff_tail_delta(out, previous, current);
    diff_reservoir_delta(out, previous, current); // §5.3 chronic-vs-new streaming seam
    diff_service_edge_delta(out, previous,
                            current); // O4b (SRC-D-OTEL-21): distilled service topology
    // SPEC §13.6 cube_diff — the emerging border. The ONE gate is "both documents carried a cube",
    // on the explicit presence flags. There is no axes-equality gate, here or in cube_diff_of
    // (DN-42.D17 §4): the §2.4 gate above freezes the axis SET per canonicalization_version, but
    // NOT the per-window collapse STAMPS (band_floor / floor_depth, §16.10) — two windows of the
    // same contract routinely differ there, and §16.10 mandates diffing that pair at the minimal
    // common collapse rather than refusing it. So the presence check below is the whole gate:
    // cube_diff_of is total over the pair it is handed.
    if (previous.has_cube && current.has_cube)
    {
        // The diff-only latency_shift differential axis (§4): a per-component SIGNED latency shift
        // (up or down, polarity-mute) the emerging border reads. Computed from the two documents'
        // ordinal histograms (metalog owns the ladder + W1); empty when neither carries comparable
        // duration data → the plain 3-D border.
        const std::unordered_map<std::string, OrdinalDrift> latency_shifts{
            component_latency_shifts(previous, current)};
        out.cube_diff = cube::cube_diff_of(previous.cube, current.cube, latency_shifts);
        out.has_cube_diff = true;
    }

    return out;
}

} // namespace insight::metalog
