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
    // pre: both are fixed-width RFC 3339 UTC strings, so a lexicographic compare orders them;
    // compose() checks that form with stamped_envelope() before either is called.
    [[nodiscard]] std::string_view iso_min(std::string_view lhs, std::string_view rhs)
    {
        return lhs < rhs ? lhs : rhs;
    }
    [[nodiscard]] std::string_view iso_max(std::string_view lhs, std::string_view rhs)
    {
        return lhs > rhs ? lhs : rhs;
    }

    // post: the MINIMUM over the inputs' declared caps, because a merge is never finer than its
    // coarsest member; an input that declares nothing is SKIPPED, never folded in as a zero.
    // note: min is symmetric, which is what makes the commutativity MUST hold on the cap fields.
    // refs: ADR-25.D5
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

    // invariant: one input's own view of one template -- the count it contributed and the labels it
    // carried; top_k and reservoir are disjoint within a document, so one entry feeds each view.
    struct InputTemplate
    {
        std::uint64_t count{0};
        std::optional<EventLevel> level;
        std::optional<std::string> component;
        StructuralRole role{StructuralRole::None};
        std::optional<std::vector<std::string>> where;
    };

    using InputTemplateIndex = std::unordered_map<TemplateId, InputTemplate>;

    // refs: F-SRC-insight-metalog:metalog.api.cppm:TemplateRegistry
    [[nodiscard]] InputTemplateIndex index_input_templates(const MetaLogDocument& doc)
    {
        InputTemplateIndex index;
        index.reserve(doc.stats.top_k.size() + doc.stats.reservoir.size());
        for (const auto& entry : doc.stats.top_k)
        {
            InputTemplate& view{index[entry.template_id]};
            view.count += entry.count;
            view.level = entry.dominant_level;
            view.component = entry.dominant_component;
        }
        for (const auto& entry : doc.stats.reservoir)
        {
            InputTemplate& view{index[entry.template_id]};
            view.count += entry.count;
            view.level = entry.dominant_level;
            view.component = entry.dominant_component;
            view.role = entry.structural_role;
            if (entry.cube_coord && entry.cube_coord->where && !entry.cube_coord->where->empty())
                view.where = entry.cube_coord->where;
        }
        return index;
    }

    // post: the label of the input that contributed the higher count, the smaller under `less` at
    // equal counts, the present one when only one input carries it, nullptr when neither does.
    // invariant: no branch reads which operand a label came from, so compose(A, B) and
    // compose(B, A) resolve every label alike (SPEC 12.1, 12.2).
    template <class Label, class Less>
    [[nodiscard]] const Label* resolve_label(const Label* lhs, std::uint64_t lhs_count,
                                             const Label* rhs, std::uint64_t rhs_count, Less less)
    {
        if (lhs == nullptr)
            return rhs;
        if (rhs == nullptr)
            return lhs;
        if (lhs_count != rhs_count)
            return lhs_count > rhs_count ? lhs : rhs;
        return less(*rhs, *lhs) ? rhs : lhs;
    }

    // invariant: the per-template labels of the composed document, each resolved by SPEC 12.1's
    // count-then-lexicographic rule over the two inputs' values.
    struct ResolvedLabels
    {
        std::optional<EventLevel> level;
        std::optional<std::string> component;
        StructuralRole role{StructuralRole::None};
        std::optional<std::vector<std::string>> where;
    };

    // post: a level the wire carries is ordered by its spelling, and at an equal spelling a
    // declared level precedes an inferred one, so the domain-only provenance half is symmetric too.
    [[nodiscard]] bool level_less(const EventLevel& lhs, const EventLevel& rhs)
    {
        const std::string lhs_text{level_to_spec_string(lhs.value())};
        const std::string rhs_text{level_to_spec_string(rhs.value())};
        if (lhs_text != rhs_text)
            return lhs_text < rhs_text;
        return lhs.is_declared() && !rhs.is_declared();
    }

    // post: the composed level -- a level the wire carries wins over one it omits (Unknown), and an
    // engaged Unknown survives only when neither input carries a wire level.
    // refs: ADR-20.D19
    [[nodiscard]] std::optional<EventLevel> resolve_level(const InputTemplate& lhs,
                                                          const InputTemplate& rhs)
    {
        const auto carried{[](const InputTemplate& view) -> const EventLevel*
                           {
                               return view.level && view.level->value() != LogLevel::Unknown
                                          ? &*view.level
                                          : nullptr;
                           }};
        if (const EventLevel* chosen{
                resolve_label(carried(lhs), lhs.count, carried(rhs), rhs.count, level_less)})
            return *chosen;
        if (lhs.level || rhs.level)
            return EventLevel::inferred(LogLevel::Unknown);
        return std::nullopt;
    }

    [[nodiscard]] ResolvedLabels resolve_labels(const InputTemplate& lhs, const InputTemplate& rhs)
    {
        ResolvedLabels out;
        out.level = resolve_level(lhs, rhs);
        if (const std::string* component{
                resolve_label(lhs.component ? &*lhs.component : nullptr, lhs.count,
                              rhs.component ? &*rhs.component : nullptr, rhs.count, std::less<>{})})
            out.component = *component;
        const auto role_of{[](const InputTemplate& view) -> const StructuralRole*
                           { return view.role != StructuralRole::None ? &view.role : nullptr; }};
        if (const StructuralRole *role{
                resolve_label(role_of(lhs), lhs.count, role_of(rhs), rhs.count,
                              [](StructuralRole left, StructuralRole right)
                              { return insight::to_string(left) < insight::to_string(right); })})
            out.role = *role;
        // note: std::vector's operator< is the element-by-element order, a prefix first.
        if (const std::vector<std::string>* where{
                resolve_label(lhs.where ? &*lhs.where : nullptr, lhs.count,
                              rhs.where ? &*rhs.where : nullptr, rhs.count, std::less<>{})})
            out.where = *where;
        return out;
    }

    // post: the slot holds at most `cap` values, the highest counts kept with a key tie-break, and
    // its entropy is recomputed over what it keeps; total and cardinality are untouched.
    // note: a slot already within the cap is left as it is, entropy included.
    // refs: DN-148.D3
    void readmit_slot(FieldHistogram& slot, std::size_t cap)
    {
        if (slot.value_counts.size() <= cap)
            return;
        std::vector<std::pair<std::string, std::uint64_t>> sorted(slot.value_counts.begin(),
                                                                  slot.value_counts.end());
        std::ranges::partial_sort(sorted, sorted.begin() + static_cast<std::ptrdiff_t>(cap),
                                  [](const auto& lhs, const auto& rhs)
                                  {
                                      if (lhs.second != rhs.second)
                                          return lhs.second > rhs.second;
                                      return lhs.first < rhs.first;
                                  });
        slot.value_counts.clear();
        slot.value_counts.reserve(cap);
        std::vector<std::uint64_t> counts;
        counts.reserve(cap);
        for (std::size_t i{0}; i < cap; ++i)
        {
            counts.push_back(sorted[i].second);
            slot.value_counts.emplace(std::move(sorted[i].first), sorted[i].second);
        }
        slot.entropy_bits = shannon_entropy_bits(counts, slot.total);
    }

    // post: value_counts union with sums on matching keys, total the sum and entropy recomputed
    // over the merge; every slot, a one-sided one included, re-admitted under `value_cap`.
    // post: a slot whose param_index is at or above an engaged `slot_cap` is dropped from both.
    // note: cardinality takes the max (no registers on the wire).
    // refs: DN-148.D5
    [[nodiscard]] std::vector<FieldHistogram>
    merge_field_histograms(const std::vector<FieldHistogram>& lhs,
                           const std::vector<FieldHistogram>& rhs,
                           std::optional<std::size_t> slot_cap, std::size_t value_cap)
    {
        const auto admitted{[slot_cap](const FieldHistogram& hist)
                            { return !slot_cap || hist.param_index < *slot_cap; }};
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
            if (!admitted(rhs_hist))
                continue;
            seen.insert(rhs_hist.param_index);
            const auto found{lhs_index.find(rhs_hist.param_index)};
            if (found == lhs_index.end())
            {
                out.push_back(rhs_hist);
                readmit_slot(out.back(), value_cap);
                continue;
            }
            const FieldHistogram& lhs_hist{*found->second};
            FieldHistogram merged;
            merged.param_index = rhs_hist.param_index;
            merged.total = lhs_hist.total + rhs_hist.total;
            merged.value_counts.reserve(lhs_hist.value_counts.size() +
                                        rhs_hist.value_counts.size());
            for (const auto& [value, count] : lhs_hist.value_counts)
                merged.value_counts[value] += count;
            for (const auto& [value, count] : rhs_hist.value_counts)
                merged.value_counts[value] += count;
            std::vector<std::uint64_t> counts;
            counts.reserve(merged.value_counts.size());
            for (const auto& [value, count] : merged.value_counts)
                counts.push_back(count);
            merged.entropy_bits = shannon_entropy_bits(counts, merged.total);
            merged.approximate_cardinality =
                std::max(lhs_hist.approximate_cardinality, rhs_hist.approximate_cardinality);
            readmit_slot(merged, value_cap);
            out.push_back(std::move(merged));
        }
        for (const auto& lhs_hist : lhs)
            if (admitted(lhs_hist) && !seen.contains(lhs_hist.param_index))
            {
                out.push_back(lhs_hist);
                readmit_slot(out.back(), value_cap);
            }
        std::ranges::sort(out, [](const auto& lhs, const auto& rhs)
                          { return lhs.param_index < rhs.param_index; });
        return out;
    }

    struct ComposeState
    {
        std::unordered_map<TemplateId, std::uint64_t> counts;
        std::unordered_map<TemplateId, ResolvedLabels> labels;
        std::vector<std::pair<TemplateId, std::uint64_t>> ordered;
        std::unordered_set<TemplateId> reserved;
    };

    // post: every template of either input with its merged count and resolved labels, ordered
    // count desc then id asc -- the order that also decides the last top_k slot.
    void aggregate_and_order(ComposeState& state, const MetaLogDocument& lhs,
                             const MetaLogDocument& rhs)
    {
        const InputTemplateIndex lhs_index{index_input_templates(lhs)};
        const InputTemplateIndex rhs_index{index_input_templates(rhs)};
        const InputTemplate not_carried{};
        const auto fold{[&](const TemplateId& template_id)
                        {
                            if (state.counts.contains(template_id))
                                return;
                            const auto lhs_it{lhs_index.find(template_id)};
                            const auto rhs_it{rhs_index.find(template_id)};
                            const InputTemplate& lhs_view{lhs_it != lhs_index.end() ? lhs_it->second
                                                                                    : not_carried};
                            const InputTemplate& rhs_view{rhs_it != rhs_index.end() ? rhs_it->second
                                                                                    : not_carried};
                            state.counts.emplace(template_id, lhs_view.count + rhs_view.count);
                            state.labels.emplace(template_id, resolve_labels(lhs_view, rhs_view));
                        }};
        for (const auto& [template_id, view] : lhs_index)
            fold(template_id);
        for (const auto& [template_id, view] : rhs_index)
            fold(template_id);

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
        const std::size_t value_cap{
            out.stats.value_counts_size.value_or(MetaLogConfig::kDefaultMaxHistogramValues)};

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
            if (const auto labels_it{state.labels.find(entry.template_id)};
                labels_it != state.labels.end())
            {
                entry.dominant_level = labels_it->second.level;
                entry.dominant_component = labels_it->second.component;
            }
            // note: histograms absent on both sides yield none, matching a cap that was a no-op.
            const auto lhs_entry_it{lhs_topk.find(entry.template_id)};
            const auto rhs_entry_it{rhs_topk.find(entry.template_id)};
            const std::vector<FieldHistogram> empty{};
            const auto& lhs_hists{
                lhs_entry_it != lhs_topk.end() ? lhs_entry_it->second->field_histograms : empty};
            const auto& rhs_hists{
                rhs_entry_it != rhs_topk.end() ? rhs_entry_it->second->field_histograms : empty};
            if (!lhs_hists.empty() || !rhs_hists.empty())
                entry.field_histograms = merge_field_histograms(
                    lhs_hists, rhs_hists, out.stats.param_histograms_size, value_cap);
            out.stats.top_k.push_back(std::move(entry));
        }
    }

    // invariant: carries the max structural-surprise and novelty seen in either reservoir.
    struct ComposeSalienceInfo
    {
        std::uint32_t structural_surprise{0};
        std::uint32_t novelty{0};
    };

    struct ComposeReservoirCandidate
    {
        TemplateId template_id;
        std::uint32_t salience;
        std::uint32_t structural_surprise;
        std::uint32_t novelty;
        // note: the axis is the composed document's own verdict, since rarity is scale-relative.
        std::optional<RetentionAxis> retention_axis;
    };

    // post: the templates salient in either input's reservoir that did not rise into the composed
    // top_k, ranked by salience RE-DERIVED over the merged count and composed line total.
    std::vector<ComposeReservoirCandidate>
    collect_compose_reservoir_candidates(const MetaLogDocument& out, const ComposeState& state,
                                         const MetaLogDocument& lhs, const MetaLogDocument& rhs)
    {
        const auto& counts = state.counts;

        std::unordered_map<TemplateId, ComposeSalienceInfo> sal_info;
        const auto absorb_reservoir{[&](const MetaLogDocument& doc)
                                    {
                                        for (const auto& entry : doc.stats.reservoir)
                                        {
                                            auto& info{sal_info[entry.template_id]};
                                            info.structural_surprise =
                                                std::max(info.structural_surprise,
                                                         entry.structural_surprise);
                                            info.novelty = std::max(info.novelty, entry.novelty);
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
            const ResolvedLabels& labels{state.labels.at(tid)};
            const std::optional<EventLevel>& lvl{labels.level};
            // assert: the cue tier is moot on an empty template, so the echoed gate is a no-op and
            // a composed input carries no per-line provenance anyway.
            // note: salience reads severity, not evidence quality, so provenance is not an input.
            // refs: F-SRC-insight-metalog:metalog.api.cppm:TemplateRegistry, ADR-20.D5
            const auto sal{
                salience_score(lvl ? std::optional<LogLevel>{lvl->value()} : std::nullopt,
                               labels.role, std::string_view{},
                               /*echoed_source=*/false, cnt, out.window.lines_observed,
                               info.structural_surprise, info.novelty)};
            if (sal.score > 0U)
                res_cands.push_back(
                    ComposeReservoirCandidate{.template_id = tid,
                                              .salience = sal.score,
                                              .structural_surprise = info.structural_surprise,
                                              .novelty = info.novelty,
                                              .retention_axis = sal.axis});
        }
        return res_cands;
    }

    // post: the rare-salient templates ride through composition instead of dropping into the tail,
    // in salience order with a template_id tie-break, bounded by the cap C declares.
    // note: the composed reservoir is NON-ASSOCIATIVE by ruling: the key moves with scope.
    // refs: ADR-25.D5
    void rederive_reservoir(MetaLogDocument& out, ComposeState& state, const MetaLogDocument& lhs,
                            const MetaLogDocument& rhs)
    {
        const auto& counts = state.counts;
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
            const ResolvedLabels& labels{state.labels.at(cand.template_id)};
            entry.dominant_level = labels.level;
            entry.dominant_component = labels.component;
            entry.structural_role = labels.role;
            entry.structural_surprise = cand.structural_surprise;
            entry.novelty = cand.novelty;
            entry.salience = cand.salience;
            entry.retention_axis = cand.retention_axis;
            if (inputs_have_cube)
                entry.cube_coord = cube::cube_location(
                    entry.dominant_level ? std::optional<LogLevel>{entry.dominant_level->value()}
                                         : std::nullopt,
                    labels.where ? std::string_view{labels.where->back()} : std::string_view{});
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
    // refs: ADR-25.D5
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

    // post: `text` as an RFC 8785 string: quote and backslash escaped, a control byte as its short
    // escape or as \u00xx in lower-case hex, every other byte verbatim.
    void append_jcs_string(std::string& out, std::string_view text)
    {
        static constexpr std::string_view kHexDigits{"0123456789abcdef"};
        static constexpr unsigned kFirstPrintable{0x20};
        static constexpr unsigned kNibbleBits{4};
        static constexpr unsigned kNibbleMask{0x0F};
        out.push_back('"');
        for (const char glyph : text)
        {
            const auto byte{static_cast<unsigned char>(glyph)};
            switch (glyph)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (byte < kFirstPrintable)
                {
                    out += "\\u00";
                    out.push_back(kHexDigits[(byte >> kNibbleBits) & kNibbleMask]);
                    out.push_back(kHexDigits[byte & kNibbleMask]);
                }
                else
                    out.push_back(glyph);
            }
        }
        out.push_back('"');
    }

    // invariant: Ecma is RFC 8785's own number form; Exact writes every integer's exact decimal,
    // the refinement that orders two values Ecma rounds to one double.
    enum class NumberForm : std::uint8_t
    {
        Ecma,
        Exact,
    };

    // post: `value` as RFC 8785 writes a number -- the ECMAScript form of the IEEE 754 double it
    // maps to: the exact decimal up to 2^53, the shortest round-trip digits zero-padded above it.
    // note: every uint64 lies below 1e21, where that form never switches to an exponent.
    // assert: the int-to-double rounding and to_chars' shortest form are both exactly specified,
    // so the digits are the same on every leg; nothing here feeds a count back.
    void append_jcs_number(std::string& out, std::uint64_t value, NumberForm form)
    {
        static constexpr std::uint64_t kExactDoubleLimit{std::uint64_t{1}
                                                         << std::numeric_limits<double>::digits};
        static constexpr std::string_view kPositiveExponent{"e+"};
        static constexpr std::size_t kScientificWidth{32};
        if (form == NumberForm::Exact || value <= kExactDoubleLimit)
        {
            out += std::to_string(value);
            return;
        }
        std::array<char, kScientificWidth> buffer{};
        const auto written{std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                         static_cast<double>(value),
                                         std::chars_format::scientific)};
        const std::string_view scientific{buffer.data(), written.ptr};
        const std::size_t exponent_at{scientific.find(kPositiveExponent)};
        std::string digits;
        for (const char glyph : scientific.substr(0, exponent_at))
            if (glyph != '.')
                digits.push_back(glyph);
        std::size_t exponent{0};
        const std::string_view exponent_text{
            scientific.substr(exponent_at + kPositiveExponent.size())};
        std::from_chars(exponent_text.data(), exponent_text.data() + exponent_text.size(),
                        exponent);
        digits.resize(exponent + 1, '0');
        out += digits;
    }

    // post: a UTF-8 byte remapped so that byte order equals RFC 8785's UTF-16 code-unit order: a
    // four-byte lead (a surrogate pair) sorts after U+D7FF's lead and before U+E000's.
    // note: a continuation byte never moves, so two strings part at aligned positions.
    [[nodiscard]] constexpr unsigned utf16_order_of(unsigned char byte) noexcept
    {
        static constexpr unsigned kLeadAfterSurrogates{0xEE};
        static constexpr unsigned kFirstFourByteLead{0xF0};
        static constexpr unsigned kLastFourByteLead{0xF4};
        static constexpr unsigned kFourByteLeads{kLastFourByteLead - kFirstFourByteLead + 1};
        if (byte >= kFirstFourByteLead && byte <= kLastFourByteLead)
            return byte - kFirstFourByteLead + kLeadAfterSurrogates;
        if (byte >= kLeadAfterSurrogates && byte < kFirstFourByteLead)
            return byte + kFourByteLeads;
        return byte;
    }

    [[nodiscard]] bool jcs_key_less(std::string_view lhs, std::string_view rhs)
    {
        const auto order{[](char glyph)
                         { return utf16_order_of(static_cast<unsigned char>(glyph)); }};
        return std::ranges::lexicographical_compare(lhs, rhs, std::less<>{}, order, order);
    }

    // invariant: an RFC 8785 object under construction; the caller names members in the
    // canonical key order, which for this producer's fixed ASCII member names is byte order.
    class JcsObject
    {
      public:
        // pre: `out` outlives this object.
        explicit JcsObject(std::string& out) : out_{&out}
        {
            out_->push_back('{');
        }
        void key(std::string_view name)
        {
            if (!first_)
                out_->push_back(',');
            first_ = false;
            append_jcs_string(*out_, name);
            out_->push_back(':');
        }
        void close()
        {
            out_->push_back('}');
        }

      private:
        std::string* out_;
        bool first_{true};
    };

    // post: the source block exactly as the serializer writes it, members in canonical order.
    // refs: F-SRC-insight-metalog:serialize.cpp:make_source
    void append_jcs_source(std::string& out, const SourceBlock& source, NumberForm form)
    {
        JcsObject object{out};
        if (source.fleet)
        {
            object.key("fleet");
            append_jcs_string(out, *source.fleet);
        }
        if (source.host)
        {
            object.key("host");
            append_jcs_string(out, *source.host);
        }
        if (source.host_count)
        {
            object.key("host_count");
            append_jcs_number(out, *source.host_count, form);
        }
        if (source.service)
        {
            object.key("service");
            append_jcs_string(out, *source.service);
        }
        if (!source.tags.empty())
        {
            object.key("tags");
            std::vector<const std::pair<const std::string, std::string>*> tags;
            tags.reserve(source.tags.size());
            for (const auto& tag : source.tags)
                tags.push_back(&tag);
            std::ranges::sort(tags, [](const auto* lhs, const auto* rhs)
                              { return jcs_key_less(lhs->first, rhs->first); });
            JcsObject tag_object{out};
            for (const auto* tag : tags)
            {
                tag_object.key(tag->first);
                append_jcs_string(out, tag->second);
            }
            tag_object.close();
        }
        object.close();
    }

    // post: the coordinate exactly as the serializer writes it, recursing into children.
    // refs: F-SRC-insight-metalog:serialize.cpp:make_coordinate
    void append_jcs_coordinate(std::string& out, const ReDerivationCoordinate& coord,
                               NumberForm form)
    {
        JcsObject object{out};
        if (coord.bounds)
        {
            object.key("bounds");
            JcsObject bounds{out};
            bounds.key("end_tick");
            append_jcs_number(out, coord.bounds->end_tick, form);
            bounds.key("start_tick");
            append_jcs_number(out, coord.bounds->start_tick, form);
            bounds.close();
        }
        if (coord.canonicalization_version)
        {
            object.key("canonicalization_version");
            append_jcs_string(out, *coord.canonicalization_version);
        }
        if (coord.children)
        {
            object.key("children");
            out.push_back('[');
            for (std::size_t index{0}; index < coord.children->size(); ++index)
            {
                if (index > 0)
                    out.push_back(',');
                append_jcs_coordinate(out, (*coord.children)[index], form);
            }
            out.push_back(']');
        }
        if (coord.config_hash)
        {
            object.key("config_hash");
            append_jcs_string(out, *coord.config_hash);
        }
        if (coord.source_ref)
        {
            object.key("source_ref");
            JcsObject source_ref{out};
            source_ref.key("handle");
            append_jcs_string(out, coord.source_ref->handle);
            source_ref.key("resolver_kind");
            append_jcs_string(out, coord.source_ref->resolver_kind);
            source_ref.close();
        }
        object.close();
    }

    [[nodiscard]] std::string jcs_bytes_of(const ReDerivationCoordinate& coord, NumberForm form)
    {
        std::string out;
        append_jcs_coordinate(out, coord, form);
        return out;
    }

    // post: the provenance entry exactly as the serializer writes it -- an empty source omitted,
    // the window always present and empty when the input carried no bounds.
    // refs: F-SRC-insight-metalog:serialize.cpp:make_provenance
    [[nodiscard]] std::string jcs_bytes_of(const ProvenanceEntry& entry, NumberForm form)
    {
        std::string out;
        JcsObject object{out};
        if (entry.coordinate)
        {
            object.key("coordinate");
            append_jcs_coordinate(out, *entry.coordinate, form);
        }
        if (entry.document_id)
        {
            object.key("document_id");
            append_jcs_string(out, *entry.document_id);
        }
        object.key("lines_observed");
        append_jcs_number(out, entry.lines_observed, form);
        const SourceBlock& source{entry.source};
        if (source.service || source.fleet || source.host_count || source.host ||
            !source.tags.empty())
        {
            object.key("source");
            append_jcs_source(out, source, form);
        }
        object.key("window");
        JcsObject window{out};
        if (entry.window)
        {
            window.key("end");
            append_jcs_string(out, entry.window->end_iso);
            window.key("start");
            append_jcs_string(out, entry.window->start_iso);
        }
        window.close();
        object.close();
        return out;
    }

    // post: the RFC 8785 bytes, then the exact-decimal bytes that order two values RFC 8785's
    // double rounding ties (integers above 2^53), so two keys tie only on equal values.
    using CanonicalKey = std::pair<std::string, std::string>;

    template <class Value> [[nodiscard]] CanonicalKey canonical_key_of(const Value& value)
    {
        return {jcs_bytes_of(value, NumberForm::Ecma), jcs_bytes_of(value, NumberForm::Exact)};
    }

    // post: SPEC 12.1's provenance: both inputs' own entries plus one entry per input, whether or
    // not an input already carried provenance.
    // post: sorted by window.start (no bounds first), then window.end, then the canonical key.
    // note: two entries equal under that order serialise identically, so their order is moot.
    std::vector<ProvenanceEntry> merge_provenance(const MetaLogDocument& lhs,
                                                  const MetaLogDocument& rhs)
    {
        std::vector<std::pair<CanonicalKey, ProvenanceEntry>> keyed;
        keyed.reserve((lhs.provenance ? lhs.provenance->size() : 0) +
                      (rhs.provenance ? rhs.provenance->size() : 0) + 2);
        const auto add{[&keyed](ProvenanceEntry entry)
                       {
                           CanonicalKey key{canonical_key_of(entry)};
                           keyed.emplace_back(std::move(key), std::move(entry));
                       }};
        for (const MetaLogDocument* doc : {&lhs, &rhs})
        {
            if (doc->provenance)
                for (const ProvenanceEntry& entry : *doc->provenance)
                    add(entry);
            add({.window = window_bounds_of(doc->window),
                 .source = doc->source,
                 .lines_observed = doc->window.lines_observed,
                 .document_id = std::nullopt,
                 .coordinate = doc->coordinate});
        }
        std::ranges::sort(keyed,
                          [](const auto& lhs_keyed, const auto& rhs_keyed)
                          {
                              const auto& lhs_window{lhs_keyed.second.window};
                              const auto& rhs_window{rhs_keyed.second.window};
                              if (lhs_window.has_value() != rhs_window.has_value())
                                  return !lhs_window.has_value();
                              if (lhs_window && rhs_window)
                              {
                                  if (lhs_window->start_iso != rhs_window->start_iso)
                                      return lhs_window->start_iso < rhs_window->start_iso;
                                  if (lhs_window->end_iso != rhs_window->end_iso)
                                      return lhs_window->end_iso < rhs_window->end_iso;
                              }
                              return lhs_keyed.first < rhs_keyed.first;
                          });
        std::vector<ProvenanceEntry> prov;
        prov.reserve(keyed.size());
        for (auto& [key, entry] : keyed)
            prov.push_back(std::move(entry));
        return prov;
    }

    // post: the SET of the inputs' raw-leaf coordinates, never a coarse single bound, in the
    // canonical-key order of each child; nullopt when neither input carried one.
    // note: SPEC 15.5 names a set and no order, and 12.2 requires one independent of the operands.
    std::optional<ReDerivationCoordinate> merge_coordinate(const MetaLogDocument& lhs,
                                                           const MetaLogDocument& rhs)
    {
        std::vector<std::pair<CanonicalKey, ReDerivationCoordinate>> keyed;
        const auto add{[&keyed](const ReDerivationCoordinate& child)
                       { keyed.emplace_back(canonical_key_of(child), child); }};
        const auto collect_leaves{[&add](const MetaLogDocument& doc)
                                  {
                                      if (!doc.coordinate)
                                          return;
                                      if (doc.coordinate->children)
                                          for (const auto& child : *doc.coordinate->children)
                                              add(child);
                                      else
                                          add(*doc.coordinate);
                                  }};
        collect_leaves(lhs);
        collect_leaves(rhs);
        if (keyed.empty())
            return std::nullopt;
        std::ranges::sort(keyed, [](const auto& lhs_keyed, const auto& rhs_keyed)
                          { return lhs_keyed.first < rhs_keyed.first; });
        std::vector<ReDerivationCoordinate> child_coords;
        child_coords.reserve(keyed.size());
        for (auto& [key, child] : keyed)
            child_coords.push_back(std::move(child));
        // invariant: a COMPOSED coordinate has children and no source_ref or bounds, and consumers
        // discriminate on the presence of children.
        ReDerivationCoordinate composed;
        composed.children = std::move(child_coords);
        return composed;
    }

    // post: carried only when both inputs supplied one; the gate already made their
    // semantic_identity equal.
    // post: two package lists under that one key resolve to the smaller list, never to an operand.
    // refs: ADR-17.D3
    [[nodiscard]] std::optional<RulesetIdentity> merge_ruleset(const MetaLogDocument& lhs,
                                                               const MetaLogDocument& rhs)
    {
        if (!lhs.ruleset || !rhs.ruleset)
            return std::nullopt;
        const auto package_key{[](const RulesetPackageRef& ref)
                               { return std::tie(ref.name, ref.version); }};
        const bool rhs_smaller{std::ranges::lexicographical_compare(
            rhs.ruleset->packages, lhs.ruleset->packages, std::less<>{}, package_key, package_key)};
        return rhs_smaller ? rhs.ruleset : lhs.ruleset;
    }

    // post: both inputs' dropped_ngram_observations summed, an absent block or count adding zero.
    [[nodiscard]] std::uint64_t dropped_ngram_sum(const MetaLogDocument& lhs,
                                                  const MetaLogDocument& rhs) noexcept
    {
        return (lhs.behavior ? lhs.behavior->dropped_ngram_observations.value_or(0) : 0) +
               (rhs.behavior ? rhs.behavior->dropped_ngram_observations.value_or(0) : 0);
    }

    // post: top_ngrams merged by summing counts on identical sequences; branching, dominant_path
    // and graph_edge_count are deliberately NOT re-derived.
    // post: nullopt when both inputs carry a block at DIFFERENT ngram_size: two orders' keys
    // denote different objects, so it is omitted, never a min and never one side's block (§12.1).
    // invariant: reached only by unstamped pairs; the retention_profile gate refuses a stamped one.
    // note: structural signals are diffed at raw scales; salient structure rides the reservoir.
    // refs: ADR-20.D17
    std::optional<BehaviorBlock> merge_behavior(const MetaLogDocument& lhs,
                                                const MetaLogDocument& rhs)
    {
        if (!lhs.behavior && !rhs.behavior)
            return std::nullopt;
        if (lhs.behavior && rhs.behavior && lhs.behavior->ngram_size != rhs.behavior->ngram_size)
            return std::nullopt;
        BehaviorBlock behavior;
        behavior.ngram_size = lhs.behavior ? lhs.behavior->ngram_size : rhs.behavior->ngram_size;
        if (lhs.behavior && rhs.behavior)
            behavior.top_ngrams_size =
                std::min(lhs.behavior->top_ngrams_size, rhs.behavior->top_ngrams_size);
        else
            // note: the cap comes from the one side that has a behavior block when only one does.
            // refs: ADR-25.D5
            behavior.top_ngrams_size =
                lhs.behavior ? lhs.behavior->top_ngrams_size : rhs.behavior->top_ngrams_size;
        // invariant: the sum is OMITTED at zero -- an absent key AFFIRMS nothing was dropped, so a
        // written 0 and a silent omission on inputs that did drop are both wrong.
        if (const std::uint64_t dropped{dropped_ngram_sum(lhs, rhs)}; dropped > 0)
            behavior.dropped_ngram_observations = dropped;
        // note: one accumulator keyed on the scalar id replaces three sequence-keyed maps.
        // refs: F-SRC-insight-canon:canon.api.cppm:NgramId, ADR-16.D1
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

    // invariant: a document's window envelope as two instants -- both bounds, or none at all.
    struct StampedEnvelope
    {
        std::chrono::sys_seconds start;
        std::chrono::sys_seconds end;
    };

    // post: nullopt for an UNSTAMPED window (no envelope), the two instants otherwise.
    // post: throws std::invalid_argument on a bound that is not the 20-byte form, so iso_min and
    // iso_max never order a width that form does not have.
    // refs: DN-50.D13, DN-137.O1
    [[nodiscard]] std::optional<StampedEnvelope> stamped_envelope(const MetaLogDocument& doc,
                                                                  std::string_view side)
    {
        if (!doc.window.envelope)
            return std::nullopt;
        const WindowBounds& window{doc.window.envelope->bounds};
        const auto refuse{[side](std::string_view bound, const std::string& value)
                          {
                              return std::invalid_argument{
                                  std::string{"metalog::compose: "} + std::string{side} +
                                  " window." + std::string{bound} + " \"" + value +
                                  "\" is not a 20-byte RFC 3339 UTC instant (SPEC §2.2)"};
                          }};
        const auto start{parse_rfc3339_utc(window.start_iso)};
        if (!start)
            throw refuse("start_iso", window.start_iso);
        const auto end{parse_rfc3339_utc(window.end_iso)};
        if (!end)
            throw refuse("end_iso", window.end_iso);
        return StampedEnvelope{.start = *start, .end = *end};
    }

    // post: the merged envelope — min start, max end, and their span — on every pair of stamped
    // envelopes; the stamped side's own envelope when the other is unstamped; none when neither is.
    // note: an inverted merged span reads 0, the clamp stamp_envelope applies to one window.
    // refs: DN-50.D13, DN-137.O1, F-SRC-insight-metalog:engine.cpp:stamp_envelope
    [[nodiscard]] std::optional<WindowEnvelope> composed_envelope(
        const MetaLogDocument& lhs, const std::optional<StampedEnvelope>& lhs_envelope,
        const MetaLogDocument& rhs, const std::optional<StampedEnvelope>& rhs_envelope)
    {
        // invariant: stamped_envelope is engaged exactly when the window's envelope is, so the two
        // tests per side agree and both are spelled for the optional-access check.
        if (!lhs_envelope || !lhs.window.envelope)
            return rhs.window.envelope;
        if (!rhs_envelope || !rhs.window.envelope)
            return lhs.window.envelope;
        const WindowBounds& lhs_bounds{lhs.window.envelope->bounds};
        const WindowBounds& rhs_bounds{rhs.window.envelope->bounds};
        const auto span{std::max(lhs_envelope->end, rhs_envelope->end) -
                        std::min(lhs_envelope->start, rhs_envelope->start)};
        return WindowEnvelope{
            .bounds = {.start_iso =
                           std::string{iso_min(lhs_bounds.start_iso, rhs_bounds.start_iso)},
                       .end_iso = std::string{iso_max(lhs_bounds.end_iso, rhs_bounds.end_iso)}},
            .duration_seconds = span.count() < 0 ? 0 : static_cast<std::uint64_t>(span.count())};
    }

    // invariant: Unordered covers every pair with no derivable time order -- overlapping, nested,
    // identical, equal-start, or either side unstamped.
    enum class WindowOrder : std::uint8_t
    {
        LhsEarlier,
        RhsEarlier,
        Unordered,
    };

    // post: an order exists only between DISJOINT envelopes, `earlier.end <= later.start` with
    // `earlier.start < later.start`; argument order never supplies one.
    // refs: DN-50.D11
    [[nodiscard]] WindowOrder window_order(const std::optional<StampedEnvelope>& lhs,
                                           const std::optional<StampedEnvelope>& rhs)
    {
        if (!lhs || !rhs)
            return WindowOrder::Unordered;
        if (lhs->end <= rhs->start && lhs->start < rhs->start)
            return WindowOrder::LhsEarlier;
        if (rhs->end <= lhs->start && rhs->start < lhs->start)
            return WindowOrder::RhsEarlier;
        return WindowOrder::Unordered;
    }

    // post: one product per template of the composed retained set, plus the root roll-up -- the
    // time-ordered monoid product over a DISJOINT pair, the symmetric spatial join otherwise.
    // invariant: the churn product is not commutative, so a wrong-order fold is deterministic and
    // WRONG -- the failure shape a determinism gate cannot see; the join has no order to get wrong.
    // note: the scratch indices are sized by the inputs' already-declared salience memory.
    // refs: DN-50.D4, DN-50.D11
    void fold_presence_churn(MetaLogDocument& out, const MetaLogDocument& lhs,
                             const MetaLogDocument& rhs, WindowOrder order)
    {
        const bool joined{order == WindowOrder::Unordered};
        const bool rhs_first{order == WindowOrder::RhsEarlier};
        const MetaLogDocument& earlier{rhs_first ? rhs : lhs};
        const MetaLogDocument& later{rhs_first ? lhs : rhs};

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

        PresenceChurnSummary summary{.span_windows = joined ? std::max(earlier_span, later_span)
                                                            : earlier_span + later_span,
                                     .templates_with_churn = 0,
                                     .total_transitions = 0,
                                     .total_indeterminate = 0};
        const auto fold_row{
            [&](const TemplateId& tid)
            {
                const PresenceChurn earlier_element{element_of(earlier_index, earlier_absent, tid)};
                const PresenceChurn later_element{element_of(later_index, later_absent, tid)};
                const PresenceChurn folded{
                    joined ? join_presence_churn(earlier_element, later_element)
                           : compose_presence_churn(earlier_element, later_element)};
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

MetaLogDocument compose(const MetaLogDocument& lhs, const MetaLogDocument& rhs,
                        const ProducerBlock& composer)
{
    // assert: the comparability gate fires BEFORE any merging, so an incompatible pair fails loudly
    // rather than producing a hybrid document.
    check_processing_identifier_gate(lhs.canonicalization_version, rhs.canonicalization_version,
                                     "canonicalization_version", "compose");
    check_processing_identifier_gate(lhs.retention_profile, rhs.retention_profile,
                                     "retention_profile", "compose");
    // note: composing across different ruleset identities merges vocabularies -- refuse.
    // refs: ADR-17.D3, ADR-17.D8
    check_processing_identifier_gate(
        lhs.ruleset ? std::optional<std::string>{lhs.ruleset->semantic_identity} : std::nullopt,
        rhs.ruleset ? std::optional<std::string>{rhs.ruleset->semantic_identity} : std::nullopt,
        "semantic_identity", "compose");
    const auto lhs_envelope{stamped_envelope(lhs, "lhs")};
    const auto rhs_envelope{stamped_envelope(rhs, "rhs")};

    MetaLogDocument out;
    // post: the composer produced C, so C carries its block and the edition it writes, never an
    // input's (SPEC 12.1).
    out.metalog_version = std::string{kMetaLogSpecVersion};
    out.producer = composer;
    out.canonicalization_version =
        carry_processing_identifier(lhs.canonicalization_version, rhs.canonicalization_version);
    out.retention_profile =
        carry_processing_identifier(lhs.retention_profile, rhs.retention_profile);
    out.ruleset = merge_ruleset(lhs, rhs);
    // invariant: the transport declaration is carried only when both inputs declared the SAME
    // stack, and it is NOT a compose gate and must not become one.
    // note: an empty names[] here would claim the merged runs declared NOTHING -- a wrong claim.
    // refs: ADR-23.D4
    out.transport = (lhs.transport && rhs.transport && *lhs.transport == *rhs.transport)
                        ? lhs.transport
                        : std::nullopt;
    out.window.lines_observed = lhs.window.lines_observed + rhs.window.lines_observed;
    // post: the merged envelope's real-time span in every geometry, never a sum of the inputs'.
    // refs: DN-50.D13
    out.window.envelope = composed_envelope(lhs, lhs_envelope, rhs, rhs_envelope);
    out.source = common_source(lhs.source, rhs.source);

    ComposeState state;
    aggregate_and_order(state, lhs, rhs);
    // invariant: the top_k cap is declared here because both composed builders cut at it.
    // refs: ADR-25.D5
    out.stats.top_k_size = std::min(lhs.stats.top_k_size, rhs.stats.top_k_size);
    // invariant: each histogram cap is the minimum over the declared caps; the value cap falls
    // back to the composer's own default, declared, when neither input declared one.
    // refs: DN-148.D5, ADR-25.D5
    out.stats.param_histograms_size =
        min_declared_cap(lhs.stats.param_histograms_size, rhs.stats.param_histograms_size);
    out.stats.value_counts_size =
        min_declared_cap(lhs.stats.value_counts_size, rhs.stats.value_counts_size)
            .value_or(MetaLogConfig::kDefaultMaxHistogramValues);
    out.stats.unique_templates = state.ordered.size();

    build_composed_top_k(out, state, lhs, rhs);
    rederive_reservoir(out, state, lhs, rhs);
    build_composed_tail(out, state, lhs, rhs);
    recompute_composed_entropy(out, state, lhs, rhs);
    // assert: the churn fold runs after the composed retained set exists.
    fold_presence_churn(out, lhs, rhs, window_order(lhs_envelope, rhs_envelope));

    // refs: F-SRC-insight-metalog:metalog.api.cppm:TemplateRegistry
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
