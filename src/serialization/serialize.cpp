module;
#include <glaze/glaze.hpp>

// refs: DN-65.D2
#include "json_egress.hpp"

module insight.metalog;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats;

namespace insight::metalog
{

// invariant: each DTO field name IS the JSON key, so the struct declaration reads as the schema and
// glaze reflects it with no stringly-typed mapping.
// invariant: every field the spec omits-when-absent is an optional here, populated only when
// present, so one document yields one byte sequence.
namespace dto
{

    struct Producer
    {
        std::string name;
        std::string version;
        std::string implementation_uri;
    };

    struct Window
    {
        std::string start;
        std::string end;
        std::uint64_t duration_seconds{0};
        std::uint64_t lines_observed{0};
    };

    // note: an all-empty source still serialises as {} -- the block itself is always present.
    struct Source
    {
        std::optional<std::string> service;
        std::optional<std::string> fleet;
        std::optional<std::uint64_t> host_count;
        std::optional<std::string> host;
        std::optional<std::map<std::string, std::string>> tags;
    };

    struct TailSummary
    {
        std::uint64_t tail_template_count{0};
        double tail_entropy_bits{0.0};
        double tail_max_rate{0.0};
    };

    // invariant: value_counts is a std::map so glaze emits it KEY-SORTED; the domain's
    // unordered_map order is not portable, so it is copied at this boundary.
    // post: entropy_bits is deliberately NOT on the wire -- it is losslessly derivable, and
    // dropping it keeps the distribution fields integer and cross-machine bit-identical.
    // note: approximate_cardinality is HLL-derived: same-machine replay only, not cross-machine.
    struct ParamHistogram
    {
        std::uint32_t param_index{0};
        std::map<std::string, std::uint64_t> value_counts;
        std::uint64_t total{0};
        std::optional<std::uint64_t> approximate_cardinality;
    };

    // note: counts is the full uncapped tail; schedule_id is the consumer's comparability key.
    // refs: SRC-D-W1-2
    struct OrdinalHistogram
    {
        std::string field_name;
        std::string schedule_id;
        std::vector<std::uint64_t> counts;
        std::uint64_t total{0};
    };

    // invariant: a reverse-DNS key cannot be a C++ identifier, so every container spells its keys
    // in a glaze meta instead of being reflected.
    // invariant: the spec's placement table grants each container object by object, so a further
    // placement is an issue on the standard, never a bare member written here.
    // note: member and container are both optional, so an empty container never reaches the wire.
    struct PresenceChurn
    {
        std::uint32_t span_windows{0};
        std::uint32_t transitions{0};
        std::uint32_t indeterminate{0};
        std::optional<bool> first;
        std::optional<bool> last;
    };

    // invariant: horizon names the presence predicate, so indeterminate means the same thing across
    // producers; without it the counters are uncomparable.
    struct PresenceChurnSummary
    {
        std::uint32_t span_windows{0};
        std::uint32_t templates_with_churn{0};
        std::uint64_t total_transitions{0};
        std::uint64_t total_indeterminate{0};
        std::string horizon;
    };

    // refs: SRC-D-W1-4
    struct TopKExtensions
    {
        // note: an unfrozen ladder and an engine-side schedule id make two producers' bins differ.
        std::optional<std::vector<OrdinalHistogram>> ordinal_histograms;
        std::optional<PresenceChurn> presence_churn;

        struct glaze
        {
            using T = TopKExtensions;
            static constexpr auto value =
                glz::object("fr.coderoast.ordinal_histograms", &T::ordinal_histograms,
                            "fr.coderoast.presence_churn", &T::presence_churn);
        };
    };

    // refs: SRC-D-WHERE-2
    struct TopKEntry
    {
        std::string template_id;
        std::uint64_t count{0};
        double frequency{0.0};
        std::optional<std::string> tmpl;
        std::optional<std::string> level;
        std::optional<std::string> component;
        std::optional<std::vector<ParamHistogram>> param_histograms;
        std::optional<TopKExtensions> extensions;

        // note: glaze renames tmpl to "template", a C++ keyword; every other field reflects.
        struct glaze
        {
            using T = TopKEntry;
            static constexpr auto value = glz::object(
                "template_id", &T::template_id, "count", &T::count, "frequency", &T::frequency,
                "template", &T::tmpl, "level", &T::level, "component", &T::component,
                "param_histograms", &T::param_histograms, "extensions", &T::extensions);
        };
    };

    // invariant: the coord is an OPEN object keyed by axis name, so an absent (aggregated) axis is
    // omitted and a future axis is one more optional field.
    struct CubeCoord
    {
        std::optional<std::string> level;
        std::optional<std::vector<std::string>> where;
        std::optional<std::string> structural_role;
        // note: the shift axis is present only on a diff border cell, absent on a stored cell.
        std::optional<std::string> latency_shift;
    };

    // note: chain and floor_depth are present only for a chain-kind axis.
    struct CubeAxis
    {
        std::string name;
        std::string kind;
        std::optional<std::vector<std::string>> chain;
        std::optional<std::uint32_t> floor_depth;
        std::optional<std::uint32_t> band_floor;
    };

    struct CubeCell
    {
        CubeCoord coord;
        std::uint64_t count{0};
    };

    // note: floor_saturation is omitted -- degenerate at depth 1 and a diff-time concept.
    struct CubeBlock
    {
        std::vector<CubeAxis> axes;
        std::vector<CubeCell> cells;
        std::uint64_t cell_count{0};
        std::uint64_t raw_cell_count{0};
        // invariant: the closed-cell budget dominates the envelope sum, so a consumer that cannot
        // read it cannot price the block at all.
        std::optional<std::uint64_t> cell_budget;
    };

    // invariant: a reservoir row is self-describing -- it carries WHY it was kept, so a consumer
    // can attribute it without the producer.
    // refs: SRC-D-WHERE-2
    struct ReservoirEntry
    {
        std::string template_id;
        std::uint64_t count{0};
        double frequency{0.0};
        std::optional<std::string> tmpl;
        std::optional<std::string> level;
        std::optional<std::string> component;
        std::optional<std::string> structural_role;
        std::uint32_t structural_surprise{0};
        std::uint32_t novelty{0};
        std::uint32_t salience{0};
        std::optional<std::uint64_t> within_window_ordinal;
        std::optional<CubeCoord> cube_coord;

        struct glaze
        {
            using T = ReservoirEntry;
            static constexpr auto value = glz::object(
                "template_id", &T::template_id, "count", &T::count, "frequency", &T::frequency,
                "template", &T::tmpl, "level", &T::level, "component", &T::component,
                "structural_role", &T::structural_role, "structural_surprise",
                &T::structural_surprise, "novelty", &T::novelty, "salience", &T::salience,
                "within_window_ordinal", &T::within_window_ordinal, "cube_coord", &T::cube_coord);
        };
    };

    // post: all-integer, so the block is genuinely cross-machine bit-identical.
    // note: the window's raw structural facts; a consumer applies its own predicate.
    // refs: SRC-D-WHERE-4, SRC-D-WHERE-5, SRC-D-OTEL-13, SRC-D-OTEL-11, SRC-D-OTEL-9
    struct Acquisition
    {
        std::uint64_t records_with_component{0};
        std::uint64_t distinct_components{0};
        std::uint64_t level_cardinality{0};
        std::uint64_t role_cardinality{0};
        std::vector<std::uint64_t> where_cardinality_per_depth;
        std::uint64_t closed_cells{0};
        std::uint64_t span_records{0};
        std::uint64_t orphan_parent_edges{0};
        std::uint64_t orphan_link_edges{0};
    };

    // invariant: a present-but-empty edges array means "no topology" and is NOT absence; block
    // absence is the optional on the document.
    // refs: SRC-D-OTEL-21
    struct ServiceEdge
    {
        std::string caller;
        std::string callee;
        std::uint64_t weight{0};
    };

    struct ServiceEdgeBlock
    {
        std::vector<ServiceEdge> edges;
        std::uint64_t dropped_edges{0};
    };

    // invariant: names is a plain vector and never an optional -- the EMPTY array is the payload
    // for an undeclared run, and omitting it would erase the statement being made.
    // note: a name without its catalogue version is unresolvable by a later reader.
    // refs: ADR-23.D3
    struct TransportDeclaration
    {
        std::string catalog_version;
        std::vector<std::string> names;
    };

    // note: omitted for a legacy producer, and packages renders in package-sorted order.
    // refs: SRC-II-7
    struct RulesetPackageRef
    {
        std::string name;
        std::string version;
    };

    struct RulesetIdentity
    {
        std::string semantic_identity;
        std::vector<RulesetPackageRef> packages;
    };

    // invariant: every member here describes the observed window but fails comparability, so
    // namespacing keeps the content while telling a reader which half is the standard's.
    struct DocumentExtensions
    {
        std::optional<Acquisition> acquisition;
        std::optional<ServiceEdgeBlock> service_edges;
        std::optional<TransportDeclaration> transport;
        std::optional<RulesetIdentity> ruleset;
        std::optional<PresenceChurnSummary> presence_churn_summary;

        struct glaze
        {
            using T = DocumentExtensions;
            static constexpr auto value = glz::object(
                "fr.coderoast.acquisition", &T::acquisition, "fr.coderoast.service_edges",
                &T::service_edges, "fr.coderoast.transport", &T::transport, "fr.coderoast.ruleset",
                &T::ruleset, "fr.coderoast.presence_churn_summary", &T::presence_churn_summary);
        };
    };

    struct Stats
    {
        std::uint64_t unique_templates{0};
        std::size_t top_k_size{0};
        // invariant: the reservoir cap is declared only on a document that CARRIES a reservoir -- a
        // cap beside an absent array prices a term the document does not have.
        std::optional<std::size_t> reservoir_size;
        std::uint64_t tail_count{0};
        std::uint64_t tail_unique{0};
        std::vector<TopKEntry> top_k;
        std::optional<double> entropy_bits;
        std::optional<TailSummary> tail_summary;
        std::optional<std::vector<ReservoirEntry>> reservoir;
    };

    struct NGramEntry
    {
        std::vector<std::string> sequence;
        std::uint64_t count{0};
        double probability{0.0};
    };

    struct BranchingEntry
    {
        std::string template_id;
        std::uint64_t fanout{0};
        std::uint64_t total_outgoing{0};
        double entropy_bits{0.0};
    };

    struct Behavior
    {
        std::size_t ngram_size{2};
        std::vector<NGramEntry> top_ngrams;
        std::size_t top_ngrams_size{0};
        // invariant: a PASS-THROUGH of the domain optional, never a second place that decides the
        // omission -- re-deciding here would let a broken producer emit correct bytes.
        std::optional<std::uint64_t> dropped_ngram_observations;
        // invariant: the branching cap's ABSENCE asserts "no cap", so it travels with its array.
        std::optional<std::size_t> branching_size;
        std::optional<std::uint64_t> graph_edge_count;
        std::optional<std::vector<std::string>> dominant_path;
        std::optional<std::vector<BranchingEntry>> branching;
    };

    struct Stability
    {
        std::string previous_window_end;
        double kl_divergence{0.0};
        double js_divergence{0.0};
        std::uint64_t new_templates{0};
        std::uint64_t vanished_templates{0};
        double stability_score{1.0};
    };

    // invariant: recursive -- a composed coordinate carries the set of child coordinates.
    struct SourceRef
    {
        std::string resolver_kind;
        std::string handle;
    };

    struct Bounds
    {
        std::uint64_t start_tick{0};
        std::uint64_t end_tick{0};
    };

    struct Coordinate
    {
        // invariant: a raw coordinate has source_ref and bounds, a composed one has children only;
        // consumers discriminate on the presence of children.
        std::optional<SourceRef> source_ref;
        std::optional<Bounds> bounds;
        std::optional<std::string> canonicalization_version;
        std::optional<std::string> config_hash;
        std::optional<std::vector<Coordinate>> children;
    };

    struct ProvenanceWindow
    {
        std::string start;
        std::string end;
    };

    struct Provenance
    {
        ProvenanceWindow window;
        std::optional<Source> source;
        std::uint64_t lines_observed{0};
        std::optional<std::string> document_id;
        std::optional<Coordinate> coordinate;
    };

    struct Document
    {
        std::string metalog_version;
        Producer producer;
        Window window;
        Source source;
        Stats stats;
        std::optional<Behavior> behavior;
        std::optional<Stability> stability;
        std::optional<std::vector<Provenance>> provenance;
        std::optional<std::string> canonicalization_version;
        std::optional<std::string> retention_profile;
        // note: the run verdict is the standard's own LOWER-CASE minted vocabulary.
        // refs: LSRC-8
        std::optional<Coordinate> coordinate;
        std::optional<CubeBlock> cube;
        std::optional<std::string> run_outcome;
        // invariant: the standard's members are declared first and the vendor container last, so
        // the wire says plainly which half of the document is ours.
        std::optional<DocumentExtensions> extensions;
    };

    // note: a border cell is a constraint coord plus the (was, now) counts it bounds.
    struct CubeBorderCell
    {
        CubeCoord coord;
        std::uint64_t previous_count{0};
        std::uint64_t current_count{0};
    };

    struct CubeBorder
    {
        std::vector<CubeBorderCell> lower;
        std::vector<CubeBorderCell> upper;
    };

    struct CubeDiff
    {
        std::vector<CubeAxis> axes;
        std::optional<CubeBorder> emerging;
        std::optional<CubeBorder> vanishing;
    };

    // note: this member rode the wire before the standard gave it a section.
    struct ReservoirDeltaEntry
    {
        std::string template_id;
        std::optional<std::string> level;
        std::optional<std::string> structural_role;
        std::uint32_t salience{0};
        std::uint64_t count{0};
    };

    // invariant: the frontier is the level SET, decided as a set test and never as an ordinal
    // compare, and direction is polarity-mute because the reading is the consumer's.
    struct FrontierCrossing
    {
        std::string template_id;
        std::string direction;
        std::optional<std::string> previous_level;
        std::optional<std::string> current_level;
    };

    // invariant: the block is omitted when all three lists are empty, and an emitted block carries
    // at least one non-empty list.
    struct ReservoirDelta
    {
        std::optional<std::vector<ReservoirDeltaEntry>> new_salient;
        std::optional<std::vector<ReservoirDeltaEntry>> vanished_salient;
        std::optional<std::vector<FrontierCrossing>> frontier_crossings;
    };

    struct DocRef
    {
        ProvenanceWindow window;
        std::optional<std::string> document_id;
    };

    struct TemplateDelta
    {
        std::string template_id;
        std::uint64_t previous_count{0};
        std::uint64_t current_count{0};
        std::int64_t delta{0};
        std::optional<double> previous_frequency;
        std::optional<double> current_frequency;
    };

    struct BranchingDelta
    {
        std::string template_id;
        double previous_entropy_bits{0.0};
        double current_entropy_bits{0.0};
        double delta_bits{0.0};
    };

    struct NGramRateChange
    {
        std::vector<std::string> sequence;
        double previous_probability{0.0};
        double current_probability{0.0};
        double delta{0.0};
    };

    struct NGramDelta
    {
        std::size_t ngram_size{2};
        std::optional<std::vector<std::vector<std::string>>> new_ngrams;
        std::optional<std::vector<std::vector<std::string>>> vanished_ngrams;
        std::optional<std::vector<NGramRateChange>> rate_changed;
    };

    // invariant: the whole block is present iff BOTH documents carried a service_edges block;
    // absence means unknown.
    // refs: SRC-D-OTEL-21
    struct ServiceEdgeWeightChange
    {
        std::string caller;
        std::string callee;
        std::uint64_t previous_weight{0};
        std::uint64_t current_weight{0};
        std::int64_t delta{0};
    };

    struct ServiceEdgeDelta
    {
        std::optional<std::vector<ServiceEdge>> emerged;
        std::optional<std::vector<ServiceEdge>> vanished;
        std::optional<std::vector<ServiceEdgeWeightChange>> weight_changed;
    };

    // note: a diff of vendor data is vendor data, which is why the topology delta sits here.
    // refs: SRC-D-OTEL-21
    struct DiffExtensions
    {
        std::optional<ServiceEdgeDelta> service_edge_delta;

        struct glaze
        {
            using T = DiffExtensions;
            static constexpr auto value =
                glz::object("fr.coderoast.service_edge_delta", &T::service_edge_delta);
        };
    };

    struct TailDelta
    {
        std::uint64_t previous_tail_template_count{0};
        std::uint64_t current_tail_template_count{0};
        std::int64_t tail_template_count_delta{0};
        double previous_tail_entropy_bits{0.0};
        double current_tail_entropy_bits{0.0};
        double tail_entropy_bits_delta{0.0};
        double previous_tail_max_rate{0.0};
        double current_tail_max_rate{0.0};
        double tail_max_rate_delta{0.0};
    };

    struct Diff
    {
        std::string diff_version;
        DocRef previous;
        DocRef current;
        // invariant: comparison_outcome is declared with the required members and before every
        // optional signal, because it is the assertion the witness rule judges them against.
        std::string comparison_outcome;
        std::optional<double> kl_divergence;
        std::optional<double> js_divergence;
        std::optional<double> stability_score;
        std::optional<std::vector<TemplateDelta>> template_deltas;
        std::optional<std::vector<std::string>> new_templates;
        std::optional<std::vector<std::string>> vanished_templates;
        std::optional<std::vector<BranchingDelta>> branching_delta;
        std::optional<NGramDelta> ngram_delta;
        std::optional<TailDelta> tail_delta;
        // invariant: withheld_signals is omitted when empty rather than written as an empty array,
        // so a document that withholds nothing stays byte-identical to its pre-0.10.0 self.
        std::optional<CubeDiff> cube_diff;
        std::optional<ReservoirDelta> reservoir_delta;
        std::optional<std::vector<std::string>> withheld_signals;
        // note: the vendor container is declared last, the same discipline as the document.
        std::optional<DiffExtensions> extensions;
    };

} // namespace dto

namespace
{

    // note: presentational only -- the egress wrapper forces the escape member regardless.
    // refs: DN-65.D2
    constexpr glz::opts kWriteOpts{.skip_null_members = true};

    dto::Source make_source(const SourceBlock& src)
    {
        dto::Source out;
        out.service = src.service;
        out.fleet = src.fleet;
        out.host_count = src.host_count;
        out.host = src.host;
        if (!src.tags.empty())
            out.tags = src.tags;
        return out;
    }

    [[nodiscard]] bool source_is_empty(const SourceBlock& src) noexcept
    {
        return !src.service && !src.fleet && !src.host_count && !src.host && src.tags.empty();
    }

    // post: the wire shape of a re-derivation coordinate, recursing into composed children.
    dto::Coordinate make_coordinate(const ReDerivationCoordinate& coord)
    {
        dto::Coordinate out;
        if (coord.source_ref)
            out.source_ref = dto::SourceRef{.resolver_kind = coord.source_ref->resolver_kind,
                                            .handle = coord.source_ref->handle};
        if (coord.bounds)
            out.bounds = dto::Bounds{.start_tick = coord.bounds->start_tick,
                                     .end_tick = coord.bounds->end_tick};
        out.canonicalization_version = coord.canonicalization_version;
        out.config_hash = coord.config_hash;
        if (coord.children)
        {
            std::vector<dto::Coordinate> kids;
            kids.reserve(coord.children->size());
            for (const auto& child : *coord.children)
                kids.push_back(make_coordinate(child));
            out.children = std::move(kids);
        }
        return out;
    }

    dto::CubeCoord make_cube_coord(const CubeCoord& coord)
    {
        dto::CubeCoord out;
        out.level = coord.level;
        out.where = coord.where;
        out.structural_role = coord.structural_role;
        out.latency_shift = coord.latency_shift;
        return out;
    }

    dto::CubeAxis make_cube_axis(const CubeAxis& axis)
    {
        dto::CubeAxis out;
        out.name = axis.name;
        out.kind = axis.kind;
        out.chain = axis.chain;
        out.floor_depth = axis.floor_depth;
        out.band_floor = axis.band_floor;
        return out;
    }

    std::vector<dto::CubeAxis> make_cube_axes(const std::vector<CubeAxis>& axes)
    {
        std::vector<dto::CubeAxis> out;
        out.reserve(axes.size());
        for (const auto& axis : axes)
            out.push_back(make_cube_axis(axis));
        return out;
    }

    dto::CubeBlock make_cube(const CubeBlock& cube)
    {
        dto::CubeBlock out;
        out.axes = make_cube_axes(cube.axes);
        out.cells.reserve(cube.cells.size());
        for (const auto& cell : cube.cells)
            out.cells.push_back(
                dto::CubeCell{.coord = make_cube_coord(cell.coord), .count = cell.count});
        out.cell_count = cube.cell_count;
        out.raw_cell_count = cube.raw_cell_count;
        out.cell_budget = cube.cell_budget;
        return out;
    }

    dto::CubeBorder make_cube_border(const CubeBorder& border)
    {
        const auto convert{
            [](const std::vector<CubeBorderCell>& cells)
            {
                std::vector<dto::CubeBorderCell> rows;
                rows.reserve(cells.size());
                for (const auto& cell : cells)
                    rows.push_back(dto::CubeBorderCell{.coord = make_cube_coord(cell.coord),
                                                       .previous_count = cell.previous_count,
                                                       .current_count = cell.current_count});
                return rows;
            }};
        dto::CubeBorder out;
        out.lower = convert(border.lower);
        out.upper = convert(border.upper);
        return out;
    }

    dto::CubeDiff make_cube_diff(const CubeDiffBlock& diff)
    {
        dto::CubeDiff out;
        out.axes = make_cube_axes(diff.axes);
        if (diff.has_emerging)
            out.emerging = make_cube_border(diff.emerging);
        if (diff.has_vanishing)
            out.vanishing = make_cube_border(diff.vanishing);
        return out;
    }

    // note: this is the ONLY place a template id materialises as a string.
    // refs: SRC-D-TIR-2
    [[nodiscard]] std::vector<std::string> render_sequence(const std::vector<TemplateId>& ids)
    {
        std::vector<std::string> out;
        out.reserve(ids.size());
        for (const TemplateId tid : ids)
            out.push_back(insight::render(tid));
        return out;
    }
    [[nodiscard]] std::vector<std::vector<std::string>>
    render_sequences(const std::vector<std::vector<TemplateId>>& sequences)
    {
        std::vector<std::vector<std::string>> out;
        out.reserve(sequences.size());
        for (const auto& sequence : sequences)
            out.push_back(render_sequence(sequence));
        return out;
    }

    // pre: the registry holds every template id the document names; a hand-built document must seed
    // one with its strings.
    // refs: SRC-D-TIR-5
    [[nodiscard]] std::string resolve_template_str(const TemplateRegistry& registry,
                                                   TemplateId template_id)
    {
        return std::string{registry.lookup(template_id)};
    }

    // post: Present and Absent are the two definite bits; Unretained has no boolean to be, so an
    // omitted member is how the wire says so.
    // assert: EmptyRange is unreachable here -- the emit gate only writes a block whose span is at
    // least the informative minimum.
    [[nodiscard]] std::optional<bool> wire_presence(PresenceSymbol symbol) noexcept
    {
        switch (symbol)
        {
        case PresenceSymbol::Present:
            return true;
        case PresenceSymbol::Absent:
            return false;
        case PresenceSymbol::Unretained:
        case PresenceSymbol::EmptyRange:
            break;
        }
        return std::nullopt;
    }

    // post: false below the informative span, so a one-window document omits the block rather than
    // emitting members another member already fixes.
    // note: the base-window element stays recoverable from the standard members.
    [[nodiscard]] bool churn_is_informative(const PresenceChurn& churn) noexcept
    {
        return churn.span_windows >= PresenceChurnSummary::kMinimumInformativeSpan;
    }

    dto::TopKEntry make_top_k_entry(const TopKEntry& entry, const TemplateRegistry& registry)
    {
        dto::TopKEntry row;
        row.template_id = insight::render(entry.template_id);
        row.count = entry.count;
        row.frequency = entry.frequency;
        // note: the per-entry template is optional on the wire and this producer emits it.
        if (std::string str{resolve_template_str(registry, entry.template_id)}; !str.empty())
            row.tmpl = std::move(str);
        // invariant: the wire carries the level alone -- the provenance half is domain-only, and an
        // absence is omitted rather than rendered.
        // refs: DN-32.D3, DN-43.D10
        row.level = spec_level_of(entry.dominant_level);
        if (entry.dominant_component)
            row.component = *entry.dominant_component;
        if (!entry.field_histograms.empty())
        {
            std::vector<dto::ParamHistogram> hists;
            hists.reserve(entry.field_histograms.size());
            for (const auto& hist : entry.field_histograms)
            {
                dto::ParamHistogram param_hist;
                param_hist.param_index = hist.param_index;
                param_hist.value_counts = {hist.value_counts.begin(), hist.value_counts.end()};
                param_hist.total = hist.total;
                if (hist.approximate_cardinality > 0)
                    param_hist.approximate_cardinality = hist.approximate_cardinality;
                hists.push_back(std::move(param_hist));
            }
            row.param_histograms = std::move(hists);
        }
        // invariant: the row's vendor container is filled member by member and installed only if
        // some member landed -- assigning per member would silently erase an earlier datum.
        dto::TopKExtensions extensions;
        if (!entry.ordinal_histograms.empty())
        {
            std::vector<dto::OrdinalHistogram> ordinals;
            ordinals.reserve(entry.ordinal_histograms.size());
            for (const auto& hist : entry.ordinal_histograms)
                ordinals.push_back(dto::OrdinalHistogram{.field_name = hist.field_name,
                                                         .schedule_id = hist.schedule_id,
                                                         .counts = hist.counts,
                                                         .total = hist.total});
            extensions.ordinal_histograms = std::move(ordinals);
        }
        // note: the per-row churn block is gated on the range being long enough to carry a claim.
        // refs: DN-50.D5
        if (churn_is_informative(entry.presence_churn))
            extensions.presence_churn =
                dto::PresenceChurn{.span_windows = entry.presence_churn.span_windows,
                                   .transitions = entry.presence_churn.transitions,
                                   .indeterminate = entry.presence_churn.indeterminate,
                                   .first = wire_presence(entry.presence_churn.first),
                                   .last = wire_presence(entry.presence_churn.last)};
        if (extensions.ordinal_histograms || extensions.presence_churn)
            row.extensions = std::move(extensions);
        return row;
    }

    dto::ReservoirEntry make_reservoir_entry(const ReservoirEntry& entry,
                                             const TemplateRegistry& registry)
    {
        dto::ReservoirEntry row;
        row.template_id = insight::render(entry.template_id);
        row.count = entry.count;
        row.frequency = entry.frequency;
        if (std::string str{resolve_template_str(registry, entry.template_id)}; !str.empty())
            row.tmpl = std::move(str);
        // refs: DN-32.D3, DN-43.D10
        row.level = spec_level_of(entry.dominant_level);
        if (entry.dominant_component)
            row.component = *entry.dominant_component;
        if (entry.structural_role != StructuralRole::None)
            row.structural_role = std::string{to_string(entry.structural_role)};
        row.structural_surprise = entry.structural_surprise;
        row.novelty = entry.novelty;
        row.salience = entry.salience;
        row.within_window_ordinal = entry.within_window_ordinal;
        if (entry.cube_coord)
            row.cube_coord = make_cube_coord(*entry.cube_coord);
        return row;
    }

    dto::Stats make_stats(const StatsBlock& stats, const TemplateRegistry& registry)
    {
        dto::Stats out;
        out.unique_templates = stats.unique_templates;
        out.top_k_size = stats.top_k_size;
        out.tail_count = stats.tail_count;
        out.tail_unique = stats.tail_unique;
        out.entropy_bits = stats.entropy_bits;
        out.top_k.reserve(stats.top_k.size());
        for (const auto& entry : stats.top_k)
            out.top_k.push_back(make_top_k_entry(entry, registry));
        if (stats.tail_summary)
            out.tail_summary =
                dto::TailSummary{.tail_template_count = stats.tail_summary->tail_template_count,
                                 .tail_entropy_bits = stats.tail_summary->tail_entropy_bits,
                                 .tail_max_rate = stats.tail_summary->tail_max_rate};
        if (!stats.reservoir.empty())
        {
            std::vector<dto::ReservoirEntry> rows;
            rows.reserve(stats.reservoir.size());
            for (const auto& entry : stats.reservoir)
                rows.push_back(make_reservoir_entry(entry, registry));
            out.reservoir = std::move(rows);
            out.reservoir_size = stats.reservoir_size;
        }
        return out;
    }

    dto::Behavior make_behavior(const BehaviorBlock& behavior)
    {
        dto::Behavior out_bh;
        out_bh.ngram_size = behavior.ngram_size;
        out_bh.top_ngrams_size = behavior.top_ngrams_size;
        out_bh.top_ngrams.reserve(behavior.top_ngrams.size());
        for (const auto& ngram : behavior.top_ngrams)
            out_bh.top_ngrams.push_back({.sequence = render_sequence(ngram.sequence),
                                         .count = ngram.count,
                                         .probability = ngram.probability});
        out_bh.dropped_ngram_observations = behavior.dropped_ngram_observations;
        out_bh.graph_edge_count = behavior.graph_edge_count;
        if (behavior.dominant_path && !behavior.dominant_path->empty())
            out_bh.dominant_path = render_sequence(*behavior.dominant_path);
        if (behavior.branching && !behavior.branching->empty())
        {
            std::vector<dto::BranchingEntry> rows;
            rows.reserve(behavior.branching->size());
            for (const auto& branch : *behavior.branching)
                rows.push_back({.template_id = insight::render(branch.template_id),
                                .fanout = branch.fanout,
                                .total_outgoing = branch.total_outgoing,
                                .entropy_bits = branch.entropy_bits});
            out_bh.branching = std::move(rows);
            out_bh.branching_size = behavior.branching_size;
        }
        return out_bh;
    }

    std::vector<dto::Provenance> make_provenance(const std::vector<ProvenanceEntry>& provenance)
    {
        std::vector<dto::Provenance> prov;
        prov.reserve(provenance.size());
        for (const auto& entry : provenance)
        {
            dto::Provenance row;
            row.window = {.start = entry.window_start_iso, .end = entry.window_end_iso};
            if (!source_is_empty(entry.source))
                row.source = make_source(entry.source);
            row.lines_observed = entry.lines_observed;
            row.document_id = entry.document_id;
            if (entry.coordinate)
                row.coordinate = make_coordinate(*entry.coordinate);
            prov.push_back(std::move(row));
        }
        return prov;
    }

    dto::Document make_document(const MetaLogDocument& doc, const TemplateRegistry& registry)
    {
        dto::Document out;
        out.metalog_version = doc.metalog_version;
        out.producer = {.name = doc.producer.name,
                        .version = doc.producer.version,
                        .implementation_uri = doc.producer.implementation_uri};
        out.window = {.start = doc.window.start_iso,
                      .end = doc.window.end_iso,
                      .duration_seconds = doc.window.duration_seconds,
                      .lines_observed = doc.window.lines_observed};
        out.source = make_source(doc.source);
        // note: this producer emits the INLINE template mode, which the spec makes a producer MAY.
        out.stats = make_stats(doc.stats, registry);
        if (doc.behavior)
            out.behavior = make_behavior(*doc.behavior);
        if (doc.stability)
        {
            const auto& stability = *doc.stability;
            out.stability = dto::Stability{.previous_window_end = stability.previous_window_end_iso,
                                           .kl_divergence = stability.kl_divergence,
                                           .js_divergence = stability.js_divergence,
                                           .new_templates = stability.new_templates,
                                           .vanished_templates = stability.vanished_templates,
                                           .stability_score = stability.stability_score};
        }
        if (doc.provenance && !doc.provenance->empty())
            out.provenance = make_provenance(*doc.provenance);
        out.canonicalization_version = doc.canonicalization_version;
        out.retention_profile = doc.retention_profile;
        if (doc.coordinate)
            out.coordinate = make_coordinate(*doc.coordinate);
        if (doc.has_cube)
            out.cube = make_cube(doc.cube);
        dto::DocumentExtensions extensions;
        if (doc.acquisition)
            extensions.acquisition = dto::Acquisition{
                .records_with_component = doc.acquisition->records_with_component,
                .distinct_components = doc.acquisition->distinct_components,
                .level_cardinality = doc.acquisition->level_cardinality,
                .role_cardinality = doc.acquisition->role_cardinality,
                .where_cardinality_per_depth = doc.acquisition->where_cardinality_per_depth,
                .closed_cells = doc.acquisition->closed_cells,
                .span_records = doc.acquisition->span_records,
                .orphan_parent_edges = doc.acquisition->orphan_parent_edges,
                .orphan_link_edges = doc.acquisition->orphan_link_edges};
        if (doc.service_edges)
        {
            dto::ServiceEdgeBlock block{.edges = {},
                                        .dropped_edges = doc.service_edges->dropped_edges};
            block.edges.reserve(doc.service_edges->edges.size());
            for (const ServiceEdge& edge : doc.service_edges->edges)
                block.edges.push_back(
                    {.caller = edge.caller, .callee = edge.callee, .weight = edge.weight});
            extensions.service_edges = std::move(block);
        }
        // note: present on every produced document; absent only on a disagreeing compose.
        // refs: ADR-23.D1
        if (doc.transport)
            extensions.transport = dto::TransportDeclaration{
                .catalog_version = doc.transport->catalog_version, .names = doc.transport->names};
        // note: the opaque identifiers already own the ruleset's comparability role.
        // refs: SRC-II-7
        if (doc.ruleset)
        {
            dto::RulesetIdentity ruleset{.semantic_identity = doc.ruleset->semantic_identity,
                                         .packages = {}};
            ruleset.packages.reserve(doc.ruleset->packages.size());
            for (const RulesetPackageRef& pkg : doc.ruleset->packages)
                ruleset.packages.push_back({.name = pkg.name, .version = pkg.version});
            extensions.ruleset = std::move(ruleset);
        }
        // note: a one-window document has no oscillation to report and says nothing, not zero.
        // refs: DN-50.D5
        if (doc.presence_churn &&
            doc.presence_churn->span_windows >= PresenceChurnSummary::kMinimumInformativeSpan)
            extensions.presence_churn_summary = dto::PresenceChurnSummary{
                .span_windows = doc.presence_churn->span_windows,
                .templates_with_churn = doc.presence_churn->templates_with_churn,
                .total_transitions = doc.presence_churn->total_transitions,
                .total_indeterminate = doc.presence_churn->total_indeterminate,
                .horizon = std::string{PresenceChurnSummary::kHorizon}};
        // invariant: the container is emitted when ANY member is present, never gated on a SUBSET
        // -- a conditionally-present key is indistinguishable from a dead one to any gate.
        if (extensions.acquisition || extensions.service_edges || extensions.transport ||
            extensions.ruleset || extensions.presence_churn_summary)
            out.extensions = std::move(extensions);
        // note: spec_run_outcome_of, never insight::to_string -- the two tokens differ.
        // refs: LSRC-8
        out.run_outcome = spec_run_outcome_of(doc.run_outcome);
        return out;
    }

    dto::DocRef make_doc_ref(const DocumentRef& ref)
    {
        return {.window = {.start = ref.window_start_iso, .end = ref.window_end_iso},
                .document_id = ref.document_id};
    }

    dto::ReservoirDeltaEntry make_reservoir_delta_entry(const ReservoirDeltaEntry& entry)
    {
        dto::ReservoirDeltaEntry row;
        row.template_id = insight::render(entry.template_id);
        // refs: DN-32.D3, DN-43.D10
        row.level = spec_level_of(entry.dominant_level);
        if (entry.structural_role != StructuralRole::None)
            row.structural_role = std::string{to_string(entry.structural_role)};
        row.salience = entry.salience;
        row.count = entry.count;
        return row;
    }

    dto::ReservoirDelta make_reservoir_delta(const ReservoirDelta& delta)
    {
        dto::ReservoirDelta out;
        if (!delta.new_salient.empty())
        {
            std::vector<dto::ReservoirDeltaEntry> rows;
            rows.reserve(delta.new_salient.size());
            for (const auto& entry : delta.new_salient)
                rows.push_back(make_reservoir_delta_entry(entry));
            out.new_salient = std::move(rows);
        }
        if (!delta.vanished_salient.empty())
        {
            std::vector<dto::ReservoirDeltaEntry> rows;
            rows.reserve(delta.vanished_salient.size());
            for (const auto& entry : delta.vanished_salient)
                rows.push_back(make_reservoir_delta_entry(entry));
            out.vanished_salient = std::move(rows);
        }
        if (!delta.frontier_crossings.empty())
        {
            std::vector<dto::FrontierCrossing> rows;
            rows.reserve(delta.frontier_crossings.size());
            for (const auto& crossing : delta.frontier_crossings)
            {
                dto::FrontierCrossing row;
                row.template_id = insight::render(crossing.template_id);
                row.direction = crossing.direction == FrontierDirection::Up ? "up" : "down";
                // refs: DN-32.D3, DN-43.D10
                row.previous_level = spec_level_of(crossing.previous_level);
                row.current_level = spec_level_of(crossing.current_level);
                rows.push_back(std::move(row));
            }
            out.frontier_crossings = std::move(rows);
        }
        return out;
    }

    dto::NGramDelta make_ngram_delta(const NGramDelta& ngram)
    {
        dto::NGramDelta out;
        out.ngram_size = ngram.ngram_size;
        if (!ngram.new_ngrams.empty())
            out.new_ngrams = render_sequences(ngram.new_ngrams);
        if (!ngram.vanished_ngrams.empty())
            out.vanished_ngrams = render_sequences(ngram.vanished_ngrams);
        if (!ngram.rate_changed.empty())
        {
            std::vector<dto::NGramRateChange> changes;
            changes.reserve(ngram.rate_changed.size());
            for (const auto& rate_change : ngram.rate_changed)
                changes.push_back({.sequence = render_sequence(rate_change.sequence),
                                   .previous_probability = rate_change.previous_probability,
                                   .current_probability = rate_change.current_probability,
                                   .delta = rate_change.delta});
            out.rate_changed = std::move(changes);
        }
        return out;
    }

    dto::ServiceEdgeDelta make_service_edge_delta(const ServiceEdgeDelta& edges_delta)
    {
        dto::ServiceEdgeDelta delta;
        const auto edge_rows{
            [](const std::vector<ServiceEdge>& edges)
            {
                std::vector<dto::ServiceEdge> rows;
                rows.reserve(edges.size());
                for (const ServiceEdge& edge : edges)
                    rows.push_back(
                        {.caller = edge.caller, .callee = edge.callee, .weight = edge.weight});
                return rows;
            }};
        if (!edges_delta.emerged.empty())
            delta.emerged = edge_rows(edges_delta.emerged);
        if (!edges_delta.vanished.empty())
            delta.vanished = edge_rows(edges_delta.vanished);
        if (!edges_delta.weight_changed.empty())
        {
            std::vector<dto::ServiceEdgeWeightChange> rows;
            rows.reserve(edges_delta.weight_changed.size());
            for (const auto& change : edges_delta.weight_changed)
                rows.push_back({.caller = change.caller,
                                .callee = change.callee,
                                .previous_weight = change.previous_weight,
                                .current_weight = change.current_weight,
                                .delta = change.delta});
            delta.weight_changed = std::move(rows);
        }
        return delta;
    }

    dto::Diff make_diff(const MetaLogDiff& diff)
    {
        dto::Diff out;
        out.diff_version = diff.diff_version;
        out.previous = make_doc_ref(diff.previous);
        out.current = make_doc_ref(diff.current);
        // invariant: the outcome is derived from the diff's FINDINGS by the one predicate that
        // mirrors the schema's vacuity declarations, never from which members happen to fill.
        out.comparison_outcome = to_string(comparison_outcome_of(diff));
        // assert: written from the same predicate that decided the outcome, so a document asserting
        // changed on a withheld finding always carries the name that witnesses it.
        if (std::vector<std::string> withheld{withheld_signals_of(diff)}; !withheld.empty())
            out.withheld_signals = std::move(withheld);
        out.kl_divergence = diff.kl_divergence;
        out.js_divergence = diff.js_divergence;
        out.stability_score = diff.stability_score;

        if (!diff.template_deltas.empty())
        {
            std::vector<dto::TemplateDelta> deltas;
            deltas.reserve(diff.template_deltas.size());
            for (const auto& template_delta : diff.template_deltas)
                deltas.push_back({.template_id = insight::render(template_delta.template_id),
                                  .previous_count = template_delta.previous_count,
                                  .current_count = template_delta.current_count,
                                  .delta = template_delta.delta,
                                  .previous_frequency = template_delta.previous_frequency,
                                  .current_frequency = template_delta.current_frequency});
            out.template_deltas = std::move(deltas);
        }
        if (!diff.new_templates.empty())
            out.new_templates = render_sequence(diff.new_templates);
        if (!diff.vanished_templates.empty())
            out.vanished_templates = render_sequence(diff.vanished_templates);
        if (!diff.branching_delta.empty())
        {
            std::vector<dto::BranchingDelta> deltas;
            deltas.reserve(diff.branching_delta.size());
            for (const auto& branch_delta : diff.branching_delta)
                deltas.push_back({.template_id = insight::render(branch_delta.template_id),
                                  .previous_entropy_bits = branch_delta.previous_entropy_bits,
                                  .current_entropy_bits = branch_delta.current_entropy_bits,
                                  .delta_bits = branch_delta.delta_bits});
            out.branching_delta = std::move(deltas);
        }
        if (diff.ngram_delta)
            out.ngram_delta = make_ngram_delta(*diff.ngram_delta);
        if (diff.tail_delta)
        {
            const auto& tail = *diff.tail_delta;
            out.tail_delta =
                dto::TailDelta{.previous_tail_template_count = tail.previous_tail_template_count,
                               .current_tail_template_count = tail.current_tail_template_count,
                               .tail_template_count_delta = tail.tail_template_count_delta,
                               .previous_tail_entropy_bits = tail.previous_tail_entropy_bits,
                               .current_tail_entropy_bits = tail.current_tail_entropy_bits,
                               .tail_entropy_bits_delta = tail.tail_entropy_bits_delta,
                               .previous_tail_max_rate = tail.previous_tail_max_rate,
                               .current_tail_max_rate = tail.current_tail_max_rate,
                               .tail_max_rate_delta = tail.tail_max_rate_delta};
        }
        if (diff.has_cube_diff)
            out.cube_diff = make_cube_diff(diff.cube_diff);
        if (!diff.reservoir_delta.empty())
            out.reservoir_delta = make_reservoir_delta(diff.reservoir_delta);
        dto::DiffExtensions extensions;
        if (diff.service_edge_delta)
            extensions.service_edge_delta = make_service_edge_delta(*diff.service_edge_delta);
        // note: emitted when ANY member is present -- the document gate discipline.
        if (extensions.service_edge_delta)
            out.extensions = std::move(extensions);
        return out;
    }

} // namespace

std::string to_json(const MetaLogDocument& doc, const TemplateRegistry& registry)
{
    return json_egress::to_string<kWriteOpts>(make_document(doc, registry));
}

std::string to_json(const MetaLogDiff& diff)
{
    return json_egress::to_string<kWriteOpts>(make_diff(diff));
}

} // namespace insight::metalog
