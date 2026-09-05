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
    // pre: both are fixed-width RFC 3339 UTC strings, so a lexicographic compare orders them.
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

    // post: the MINIMUM over the inputs' declared caps, because a merge is never finer than its
    // coarsest member; an input that declares nothing is SKIPPED, never folded in as a zero.
    // note: min is symmetric, which is what makes the commutativity MUST hold on the cap fields.
    // refs: DN-56.D2, DN-56.D6
    [[nodiscard]] std::optional<std::size_t> min_declared_cap(std::optional<std::size_t> lhs,
                                                              std::optional<std::size_t> rhs)
    {
        if (!lhs)
            return rhs;
        if (!rhs)
            return lhs;
        return std::min(*lhs, *rhs);
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
        if (lhs.host_count && rhs.host_count)
            out.host_count = *lhs.host_count + *rhs.host_count;
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
        // refs: SRC-D-TIR-5
        for (const auto& entry : doc.stats.top_k)
        {
            counts[entry.template_id] += entry.count;
            if (entry.dominant_level && !levels.contains(entry.template_id))
                levels.emplace(entry.template_id, entry.dominant_level);
        }
    }

    // invariant: top_k and reservoir are disjoint within one document, so folding both never
    // double-counts an input; across inputs a template gets its full merged count.
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

    // post: value_counts union with sums on matching keys, truncated to the cap by count with a key
    // tie-break; total is the sum and entropy is recomputed over the merged counts.
    // note: cardinality takes the max (no registers on the wire); a one-sided slot is carried.
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
                                              return lhs.first < rhs.first;
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

    struct ComposeState
    {
        std::unordered_map<TemplateId, std::uint64_t> counts;
        std::unordered_map<TemplateId, std::optional<EventLevel>> levels;
        std::vector<std::pair<TemplateId, std::uint64_t>> ordered;
        std::unordered_set<TemplateId> reserved;
    };

    // post: both inputs' top_k and reservoir folded in, ordered count desc then id asc.
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

    // post: the highest-count templates, with per-param histogram compose-carry.
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
            // note: histograms absent on both sides yield none, matching a cap that was a no-op.
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

    // invariant: carries the max structural-surprise and novelty seen in either reservoir.
    struct ComposeSalienceInfo
    {
        std::uint32_t structural_surprise{0};
        std::uint32_t novelty{0};
        StructuralRole role{StructuralRole::None};
        std::string where_leaf;
    };

    struct ComposeReservoirCandidate
    {
        TemplateId template_id;
        std::uint32_t salience;
        std::uint32_t structural_surprise;
        std::uint32_t novelty;
        StructuralRole role;
        // note: the axis is the composed document's own verdict, since rarity is scale-relative.
        std::optional<RetentionAxis> retention_axis;
        std::string where_leaf;
    };

    // post: the templates salient in either input's reservoir that did not rise into the composed
    // top_k, ranked by salience RE-DERIVED over the merged count and composed line total.
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
                    // note: the WHERE leaf comes from the first input entry with a cube coord.
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
                continue;
            const auto cit{counts.find(tid)};
            const std::uint64_t cnt{cit != counts.end() ? cit->second : 0};
            const auto lit{levels.find(tid)};
            const std::optional<EventLevel> lvl{lit != levels.end() ? lit->second : std::nullopt};
            // assert: the cue tier is moot on an empty template, so the echoed gate is a no-op and
            // a composed input carries no per-line provenance anyway.
            // note: salience reads severity, not evidence quality, so provenance is not an input.
            // refs: SRC-D-TIR-5, SRC-D-PROV-1
            const auto sal{
                salience_score(lvl ? std::optional<LogLevel>{lvl->value()} : std::nullopt,
                               info.role, std::string_view{},
                               /*echoed_source=*/false, cnt, out.window.lines_observed,
                               info.structural_surprise, info.novelty)};
            if (sal.score > 0U)
                res_cands.push_back(
                    ComposeReservoirCandidate{.template_id = tid,
                                              .salience = sal.score,
                                              .structural_surprise = info.structural_surprise,
                                              .novelty = info.novelty,
                                              .role = info.role,
                                              .retention_axis = sal.axis,
                                              .where_leaf = info.where_leaf});
        }
        return res_cands;
    }

    // post: the rare-salient templates ride through composition instead of dropping into the tail,
    // in salience order with a template_id tie-break, bounded by the cap C declares.
    // note: the composed reservoir is NON-ASSOCIATIVE by ruling: the key moves with scope.
    // refs: DN-56.D2, DN-56.D3
    void rederive_reservoir(MetaLogDocument& out, ComposeState& state, const MetaLogDocument& lhs,
                            const MetaLogDocument& rhs)
    {
        const auto& counts = state.counts;
        const auto& levels = state.levels;
        auto& reserved = state.reserved;
        const auto total_lines = static_cast<double>(out.window.lines_observed);

        // note: a composed document emits a cube only when both inputs had one.
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
        out.stats.reservoir_size =
            min_declared_cap(lhs.stats.reservoir_size, rhs.stats.reservoir_size);
        // note: absent on both sides means C declares no bound and admits every positive candidate.
        const std::size_t admission_bound{out.stats.reservoir_size.value_or(res_cands.size())};
        out.stats.reservoir.reserve(std::min(admission_bound, res_cands.size()));
        for (const auto& cand : res_cands)
        {
            if (out.stats.reservoir.size() >= admission_bound)
                break;
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
            entry.retention_axis = cand.retention_axis;
            if (inputs_have_cube)
                entry.cube_coord = cube::cube_location(
                    entry.dominant_level ? std::optional<LogLevel>{entry.dominant_level->value()}
                                         : std::nullopt,
                    cand.where_leaf);
            out.stats.reservoir.push_back(std::move(entry));
            reserved.insert(cand.template_id);
        }
    }

    // post: tail aggregates and tail_summary recomputed from the merged tail, with each input's own
    // tail mass collapsed into a residual bucket.
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
                continue;
            const auto count = ordered[i].second;
            tail_count += count;
            // note: NOLINT: a defensive clamp over the tail, not a min/max of two operands.
            // NOLINTNEXTLINE(readability-use-std-min-max)
            if (count > tail_max)
                tail_max = count;
            tail_counts.push_back(count);
        }
        out.stats.tail_count =
            // note: reservoir-promoted templates are excluded, so the tail never double-counts.
            tail_count + lhs.stats.tail_count + rhs.stats.tail_count;
        out.stats.tail_unique = static_cast<std::uint64_t>(tail_counts.size());

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

    // post: entropy is recomputed from the merged counts -- it is not additive, so there is nothing
    // to carry or average from the inputs.
    // assert: each input's own tail_count enters as ONE residual bucket, so the denominator stays
    // lines_observed and no mass is dropped or attributed to a template.
    // refs: DN-56.D7
    void recompute_composed_entropy(MetaLogDocument& out, const ComposeState& state,
                                    const MetaLogDocument& lhs, const MetaLogDocument& rhs)
    {
        if (out.window.lines_observed == 0)
            return;
        std::vector<std::uint64_t> counts;
        counts.reserve(state.ordered.size() + 1);
        for (const auto& entry : state.ordered)
            counts.push_back(entry.second);
        if (const std::uint64_t residual{lhs.stats.tail_count + rhs.stats.tail_count}; residual > 0)
            counts.push_back(residual);
        out.stats.entropy_bits = shannon_entropy_bits(counts, out.window.lines_observed);
    }

    // post: a composed document always carries provenance; the inputs' own entries extend it.
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

    // post: the SET of the inputs' raw-leaf coordinates, never a coarse single bound; nullopt when
    // neither input carried one.
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
        // invariant: a COMPOSED coordinate has children and no source_ref or bounds, and consumers
        // discriminate on the presence of children.
        ReDerivationCoordinate composed;
        composed.children = std::move(child_coords);
        return composed;
    }

    // post: top_ngrams merged by summing counts on identical sequences; branching, dominant_path
    // and graph_edge_count are deliberately NOT re-derived.
    // note: structural signals are diffed at raw scales; salient structure rides the reservoir.
    std::optional<BehaviorBlock> merge_behavior(const MetaLogDocument& lhs,
                                                const MetaLogDocument& rhs)
    {
        if (!lhs.behavior && !rhs.behavior)
            return std::nullopt;
        BehaviorBlock behavior;
        behavior.ngram_size = lhs.behavior ? lhs.behavior->ngram_size : rhs.behavior->ngram_size;
        // note: the cap comes from the one side that has a behavior block when only one does.
        // refs: DN-56.D2
        behavior.top_ngrams_size =
            (lhs.behavior && rhs.behavior)
                ? std::min(lhs.behavior->top_ngrams_size, rhs.behavior->top_ngrams_size)
                : (lhs.behavior ? lhs.behavior->top_ngrams_size : rhs.behavior->top_ngrams_size);
        // invariant: the sum is OMITTED at zero -- an absent key AFFIRMS nothing was dropped, so a
        // written 0 and a silent omission on inputs that did drop are both wrong.
        if (const std::uint64_t dropped{
                (lhs.behavior ? lhs.behavior->dropped_ngram_observations.value_or(0) : 0) +
                (rhs.behavior ? rhs.behavior->dropped_ngram_observations.value_or(0) : 0)};
            dropped > 0)
            behavior.dropped_ngram_observations = dropped;
        // note: one accumulator keyed on the scalar id replaces three sequence-keyed maps.
        // refs: SRC-D-TIR-4, ADR-16.D1
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

    // post: the orientation is derived from the two documents' OWN window envelopes, so an argument
    // order that disagrees with time order cannot change the result.
    // invariant: the churn product is not commutative, so a wrong-order fold is deterministic and
    // WRONG -- the one failure shape a determinism gate cannot see.
    // refs: DN-50.D4
    [[nodiscard]] bool lhs_is_earlier(const MetaLogDocument& lhs, const MetaLogDocument& rhs)
    {
        if (lhs.window.start_iso.empty() || rhs.window.start_iso.empty())
            return true;
        if (lhs.window.start_iso != rhs.window.start_iso)
            return lhs.window.start_iso < rhs.window.start_iso;
        return true;
    }

    // post: one monoid product per template of the composed retained set, plus the root roll-up.
    // note: the scratch indices are sized by the inputs' already-declared salience memory.
    // refs: DN-50.D4
    void fold_presence_churn(MetaLogDocument& out, const MetaLogDocument& lhs,
                             const MetaLogDocument& rhs)
    {
        const bool in_order{lhs_is_earlier(lhs, rhs)};
        const MetaLogDocument& earlier{in_order ? lhs : rhs};
        const MetaLogDocument& later{in_order ? rhs : lhs};

        const auto span_of{[](const MetaLogDocument& doc) -> std::uint32_t
                           { return doc.presence_churn ? doc.presence_churn->span_windows : 0U; }};
        const std::uint32_t earlier_span{span_of(earlier)};
        const std::uint32_t later_span{span_of(later)};
        if (earlier_span == 0 && later_span == 0)
            return;

        const auto index_of{[](const MetaLogDocument& doc)
                            {
                                std::unordered_map<TemplateId, PresenceChurn> index;
                                index.reserve(doc.stats.top_k.size() + doc.stats.reservoir.size());
                                for (const auto& entry : doc.stats.top_k)
                                    index.emplace(entry.template_id, entry.presence_churn);
                                for (const auto& entry : doc.stats.reservoir)
                                    index.emplace(entry.template_id, entry.presence_churn);
                                return index;
                            }};
        const auto earlier_index{index_of(earlier)};
        const auto later_index{index_of(later)};
        // note: exhaustive retention makes an absence definite; truncation makes it unknowable.
        // refs: DN-50.D4
        const PresenceChurn earlier_absent{presence_churn_of_unretained_range(
            earlier_span, retention_is_exhaustive(earlier.stats))};
        const PresenceChurn later_absent{
            presence_churn_of_unretained_range(later_span, retention_is_exhaustive(later.stats))};

        const auto element_of{[](const std::unordered_map<TemplateId, PresenceChurn>& index,
                                 const PresenceChurn& absent, const TemplateId& tid)
                              {
                                  const auto found{index.find(tid)};
                                  return found != index.end() ? found->second : absent;
                              }};

        PresenceChurnSummary summary{.span_windows = earlier_span + later_span,
                                     .templates_with_churn = 0,
                                     .total_transitions = 0,
                                     .total_indeterminate = 0};
        const auto fold_row{[&](const TemplateId& tid)
                            {
                                const PresenceChurn folded{compose_presence_churn(
                                    element_of(earlier_index, earlier_absent, tid),
                                    element_of(later_index, later_absent, tid))};
                                summary.total_transitions += folded.transitions;
                                summary.total_indeterminate += folded.indeterminate;
                                if (folded.transitions > 0)
                                    ++summary.templates_with_churn;
                                return folded;
                            }};
        for (TopKEntry& entry : out.stats.top_k)
            entry.presence_churn = fold_row(entry.template_id);
        for (ReservoirEntry& entry : out.stats.reservoir)
            entry.presence_churn = fold_row(entry.template_id);
        out.presence_churn = summary;
    }

} // namespace

MetaLogDocument compose(const MetaLogDocument& lhs, const MetaLogDocument& rhs)
{
    // assert: the comparability gate fires BEFORE any merging, so an incompatible pair fails loudly
    // rather than producing a hybrid document.
    check_processing_identifier_gate(lhs.canonicalization_version, rhs.canonicalization_version,
                                     "canonicalization_version", "compose");
    check_processing_identifier_gate(lhs.retention_profile, rhs.retention_profile,
                                     "retention_profile", "compose");
    // note: composing across different ruleset identities merges vocabularies -- refuse.
    // refs: SRC-II-7, ADR-17.D8
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
    // note: carried only when both inputs supplied it; omitting is the honest output.
    // refs: SRC-II-7
    out.ruleset = (lhs.ruleset && rhs.ruleset) ? lhs.ruleset : std::nullopt;
    // invariant: the transport declaration is carried only when both inputs declared the SAME
    // stack, and it is NOT a compose gate and must not become one.
    // note: an empty names[] here would claim the merged runs declared NOTHING -- a wrong claim.
    // refs: ADR-23.D4
    out.transport = (lhs.transport && rhs.transport && *lhs.transport == *rhs.transport)
                        ? lhs.transport
                        : std::nullopt;
    out.window.start_iso = iso_min(lhs.window.start_iso, rhs.window.start_iso);
    out.window.end_iso = iso_max(lhs.window.end_iso, rhs.window.end_iso);
    out.window.lines_observed = lhs.window.lines_observed + rhs.window.lines_observed;
    // note: duration sums when the windows are disjoint and takes the max when they overlap.
    out.window.duration_seconds =
        std::max(lhs.window.duration_seconds, rhs.window.duration_seconds);
    if (lhs.window.start_iso != rhs.window.start_iso || lhs.window.end_iso != rhs.window.end_iso)
        out.window.duration_seconds = lhs.window.duration_seconds + rhs.window.duration_seconds;
    out.source = common_source(lhs.source, rhs.source);

    ComposeState state;
    aggregate_and_order(state, lhs, rhs);
    // invariant: the top_k cap is declared here because both composed builders cut at it.
    // refs: DN-56.D2
    out.stats.top_k_size = std::min(lhs.stats.top_k_size, rhs.stats.top_k_size);
    out.stats.unique_templates = state.ordered.size();

    build_composed_top_k(out, state, lhs, rhs);
    rederive_reservoir(out, state, lhs, rhs);
    build_composed_tail(out, state, lhs, rhs);
    recompute_composed_entropy(out, state, lhs, rhs);
    // assert: the churn fold runs after the composed retained set exists.
    fold_presence_churn(out, lhs, rhs);

    // refs: SRC-D-TIR-5
    out.provenance = merge_provenance(lhs, rhs);
    if (auto coord{merge_coordinate(lhs, rhs)})
        out.coordinate = std::move(coord);
    if (auto behavior{merge_behavior(lhs, rhs)})
        out.behavior = std::move(behavior);
    if (lhs.has_cube && rhs.has_cube)
    {
        out.cube = cube::compose_cubes(lhs.cube, rhs.cube);
        out.has_cube = true;
    }

    return out;
}

} // namespace insight::metalog
