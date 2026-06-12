module;
#include <glaze/glaze.hpp>

module insight.metalog;
import insight.metalog.internal;
import insight.metalog.api;
import insight.canon;
import insight.metalog.detail.stats;

// MetaLog JSON serialiser (SPEC v0.5.0 envelope). The restrictive, omit-empty
// glaze DTO layer: a per-wire struct mirror of the domain types + the make_*
// builders that translate domain -> DTO, behind the two free `to_json`
// overloads. Single responsibility — serialization only (no producer state, no
// compose/diff semantics).





namespace insight::metalog
{

// ── JSON serialiser (glaze, DTO layer) ─────────────────────────
//
// Serialization is a thin DTO mirroring the MetaLog v0.5.0 envelope: each
// DTO field name IS the JSON key, so the struct declaration reads as the
// schema and glaze reflects it with zero stringly-typed mapping. The domain
// types stay free of any serialization concern.
//
// Restrictive/canonical output: every field that the spec omits-when-absent
// is a std::optional here, populated only when present; glaze's
// skip_null_members then drops it. One document -> one byte sequence.
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

// All fields optional: each omitted when absent. An all-empty source still
// serialises as `{}` (the block itself is always present in a document).
struct Source
{
    std::optional<std::string> service;
    std::optional<std::string> fleet;
    std::optional<std::uint64_t> host_count;
    std::optional<std::string> host;
    std::optional<std::map<std::string, std::string>> tags; // omitted when empty
};

struct TailSummary
{
    std::uint64_t tail_template_count{0};
    double tail_entropy_bits{0.0};
    double tail_max_rate{0.0};
};

// Per-wildcard-position value-count histogram (SPEC §3.5). value_counts is a
// std::map so glaze emits it KEY-SORTED — the §15.6 replay bit-identity
// requirement; the domain FieldHistogram::value_counts is an unordered_map whose
// iteration order is not portable across runs/impls, so it is copied into a map
// at the serialization boundary. approximate_cardinality is omitted when 0
// (== not computed). Reflected (no glaze meta): member names are the wire keys,
// matching $defs/param_histogram exactly.
//
// entropy_bits is intentionally NOT on the wire (§3.5 MUST NOT): it is losslessly
// derivable from value_counts, so dropping it keeps the DISTRIBUTION fields
// (param_index / value_counts / total) integer and genuinely cross-machine
// bit-identical — independent of float-hardening.
// approximate_cardinality stays (it is the UNCAPPED distinct count, NOT derivable
// from the capped value_counts) but is uint64-typed yet HLL-float-derived: it is
// deterministic under same-machine replay (v1's pairwise batch), while its
// cross-machine bit-identity is a separate guarantee not yet landed. Any
// cross-build / history signal derived from it is therefore not yet cross-machine stable.
struct ParamHistogram
{
    std::uint32_t param_index{0};
    std::map<std::string, std::uint64_t> value_counts;
    std::uint64_t total{0};
    std::optional<std::uint64_t> approximate_cardinality; // omit when 0 (not computed)
};

struct TopKEntry
{
    std::string template_id;
    std::uint64_t count{0};
    double frequency{0.0};
    std::optional<std::string> tmpl;  // key "template"; omitted when empty
    std::optional<std::string> level; // spec level string; omitted when absent
    // SPEC §3.5 per-param histograms. Present only when the producer enabled
    // max_param_histograms (batch / full-fidelity path); omitted otherwise
    // (skip_null_members), so default and streaming documents are byte-unchanged.
    std::optional<std::vector<ParamHistogram>> param_histograms;

    // glaze rename: `tmpl` -> "template" (a C++ keyword), every other field by
    // reflection.
    struct glaze
    {
        using T = TopKEntry;
        static constexpr auto value = glz::object(
            "template_id", &T::template_id, "count", &T::count, "frequency", &T::frequency,
            "template", &T::tmpl, "level", &T::level, "param_histograms", &T::param_histograms);
    };
};

// Salience reservoir entry (Tier 2). Self-describing: carries WHY it was kept
// (salience + the per-axis bands) so a consumer/explainer can attribute it without
// the producer. Emitted under `compose()` and serialise→reparse.
struct ReservoirEntry
{
    std::string template_id;
    std::uint64_t count{0};
    double frequency{0.0};
    std::optional<std::string> tmpl;            // key "template"; omitted when empty
    std::optional<std::string> level;           // spec level string; omitted when absent
    std::optional<std::string> structural_role; // omitted when None
    std::uint32_t structural_surprise{0};
    std::uint32_t novelty{0};
    std::uint32_t salience{0};
    std::optional<std::uint64_t> within_window_ordinal; // §15.4 sub-coordinate; omit when absent

    struct glaze
    {
        using T = ReservoirEntry;
        static constexpr auto value = glz::object(
            "template_id", &T::template_id, "count", &T::count, "frequency", &T::frequency,
            "template", &T::tmpl, "level", &T::level, "structural_role", &T::structural_role,
            "structural_surprise", &T::structural_surprise, "novelty", &T::novelty, "salience",
            &T::salience, "within_window_ordinal", &T::within_window_ordinal);
    };
};

struct Stats
{
    std::uint64_t unique_templates{0};
    std::size_t top_k_size{0};
    std::uint64_t tail_count{0};
    std::uint64_t tail_unique{0};
    std::vector<TopKEntry> top_k;
    std::optional<double> entropy_bits;
    std::optional<TailSummary> tail_summary;
    // Omitted when the reservoir is empty (disabled / nothing salient below top_k).
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

// Re-derivation coordinate wire shape (SPEC §15). Field names == JSON keys via
// reflection; optionals + skip_null_members omit guarantee-2 aids / children when
// absent. Recursive: a composed coordinate carries the set of child coordinates.
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
    // §15.2 XOR: a raw coordinate has source_ref + bounds (children absent); a
    // composed coordinate has children only. skip_null_members omits whichever
    // group is absent — consumers discriminate by the presence of `children`.
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
    std::optional<Source> source; // omitted when empty
    std::uint64_t lines_observed{0};
    std::optional<std::string> document_id;
    std::optional<Coordinate> coordinate; // §15.5 — the raw child's coordinate
};

struct Document
{
    std::string metalog_version;
    Producer producer;
    Window window;
    Source source;
    std::optional<std::map<std::string, std::string>> templates; // omitted when empty
    Stats stats;
    std::optional<Behavior> behavior;
    std::optional<Stability> stability;
    std::optional<std::vector<Provenance>> provenance;
    std::optional<std::string> canonicalization_version; // §2.4 processing identifiers
    std::optional<std::string> retention_profile;
    std::optional<Coordinate> coordinate; // §15 re-derivation coordinate
};

// ── Diff DTO (SPEC §13) ──
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
    std::optional<std::vector<std::vector<std::string>>> new_ngrams;      // omit when empty
    std::optional<std::vector<std::vector<std::string>>> vanished_ngrams; // omit when empty
    std::optional<std::vector<NGramRateChange>> rate_changed;             // omit when empty
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
    std::optional<double> kl_divergence;
    std::optional<double> js_divergence;
    std::optional<double> stability_score;
    std::optional<std::vector<TemplateDelta>> template_deltas; // omit when empty
    std::optional<std::vector<std::string>> new_templates;     // omit when empty
    std::optional<std::vector<std::string>> vanished_templates;
    std::optional<std::vector<BranchingDelta>> branching_delta;
    std::optional<NGramDelta> ngram_delta;
    std::optional<TailDelta> tail_delta;
};

} // namespace dto

namespace
{

constexpr glz::opts kWriteOpts{.skip_null_members = true};

// Build the serialization Source DTO from the domain SourceBlock.
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

// Map a domain re-derivation coordinate to its wire shape (§15), recursing into
// composed children.
dto::Coordinate make_coordinate(const ReDerivationCoordinate& coord)
{
    dto::Coordinate out;
    if (coord.source_ref)
        out.source_ref = dto::SourceRef{.resolver_kind = coord.source_ref->resolver_kind,
                                        .handle = coord.source_ref->handle};
    if (coord.bounds)
        out.bounds =
            dto::Bounds{.start_tick = coord.bounds->start_tick, .end_tick = coord.bounds->end_tick};
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

// One top_k row, incl. the optional §3.5 per-param histograms (value_counts is
// copied into a std::map so the wire is key-sorted, §15.6).
dto::TopKEntry make_top_k_entry(const TopKEntry& entry)
{
    dto::TopKEntry row;
    row.template_id = entry.template_id;
    row.count = entry.count;
    row.frequency = entry.frequency;
    // SPEC §3.4: inline `template` is optional.
    if (!entry.template_str.empty())
        row.tmpl = entry.template_str;
    if (entry.dominant_level)
        row.level = level_to_spec_string(*entry.dominant_level);
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
    return row;
}

// One salience-reservoir row: the rare-salient template plus why it was kept.
dto::ReservoirEntry make_reservoir_entry(const ReservoirEntry& entry)
{
    dto::ReservoirEntry row;
    row.template_id = entry.template_id;
    row.count = entry.count;
    row.frequency = entry.frequency;
    if (!entry.template_str.empty())
        row.tmpl = entry.template_str;
    if (entry.dominant_level)
        row.level = level_to_spec_string(*entry.dominant_level);
    if (entry.structural_role != StructuralRole::None)
        row.structural_role = std::string{to_string(entry.structural_role)};
    row.structural_surprise = entry.structural_surprise;
    row.novelty = entry.novelty;
    row.salience = entry.salience;
    row.within_window_ordinal = entry.within_window_ordinal;
    return row;
}

dto::Stats make_stats(const StatsBlock& stats)
{
    dto::Stats out;
    out.unique_templates = stats.unique_templates;
    out.top_k_size = stats.top_k_size;
    out.tail_count = stats.tail_count;
    out.tail_unique = stats.tail_unique;
    out.entropy_bits = stats.entropy_bits;
    out.top_k.reserve(stats.top_k.size());
    for (const auto& entry : stats.top_k)
        out.top_k.push_back(make_top_k_entry(entry));
    if (stats.tail_summary)
        out.tail_summary =
            dto::TailSummary{.tail_template_count = stats.tail_summary->tail_template_count,
                             .tail_entropy_bits = stats.tail_summary->tail_entropy_bits,
                             .tail_max_rate = stats.tail_summary->tail_max_rate};
    // Salience reservoir: part of the external contract so a serialised metalog
    // document carries the rare-salient templates (and why they were kept). Omitted
    // when empty.
    if (!stats.reservoir.empty())
    {
        std::vector<dto::ReservoirEntry> rows;
        rows.reserve(stats.reservoir.size());
        for (const auto& entry : stats.reservoir)
            rows.push_back(make_reservoir_entry(entry));
        out.reservoir = std::move(rows);
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
        out_bh.top_ngrams.push_back(
            {.sequence = ngram.sequence, .count = ngram.count, .probability = ngram.probability});
    out_bh.graph_edge_count = behavior.graph_edge_count;
    if (behavior.dominant_path && !behavior.dominant_path->empty())
        out_bh.dominant_path = *behavior.dominant_path;
    if (behavior.branching && !behavior.branching->empty())
    {
        std::vector<dto::BranchingEntry> rows;
        rows.reserve(behavior.branching->size());
        for (const auto& branch : *behavior.branching)
            rows.push_back({.template_id = branch.template_id,
                            .fanout = branch.fanout,
                            .total_outgoing = branch.total_outgoing,
                            .entropy_bits = branch.entropy_bits});
        out_bh.branching = std::move(rows);
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

dto::Document make_document(const MetaLogDocument& doc)
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
    if (!doc.templates.empty())
        out.templates = doc.templates;
    out.stats = make_stats(doc.stats);
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
    return out;
}

dto::DocRef make_doc_ref(const DocumentRef& ref)
{
    return {.window = {.start = ref.window_start_iso, .end = ref.window_end_iso},
            .document_id = ref.document_id};
}

dto::Diff make_diff(const MetaLogDiff& diff)
{
    dto::Diff out;
    out.diff_version = diff.diff_version;
    out.previous = make_doc_ref(diff.previous);
    out.current = make_doc_ref(diff.current);
    out.kl_divergence = diff.kl_divergence;
    out.js_divergence = diff.js_divergence;
    out.stability_score = diff.stability_score;

    if (!diff.template_deltas.empty())
    {
        std::vector<dto::TemplateDelta> deltas;
        deltas.reserve(diff.template_deltas.size());
        for (const auto& template_delta : diff.template_deltas)
            deltas.push_back({.template_id = template_delta.template_id,
                              .previous_count = template_delta.previous_count,
                              .current_count = template_delta.current_count,
                              .delta = template_delta.delta,
                              .previous_frequency = template_delta.previous_frequency,
                              .current_frequency = template_delta.current_frequency});
        out.template_deltas = std::move(deltas);
    }
    if (!diff.new_templates.empty())
        out.new_templates = diff.new_templates;
    if (!diff.vanished_templates.empty())
        out.vanished_templates = diff.vanished_templates;
    if (!diff.branching_delta.empty())
    {
        std::vector<dto::BranchingDelta> deltas;
        deltas.reserve(diff.branching_delta.size());
        for (const auto& branch_delta : diff.branching_delta)
            deltas.push_back({.template_id = branch_delta.template_id,
                              .previous_entropy_bits = branch_delta.previous_entropy_bits,
                              .current_entropy_bits = branch_delta.current_entropy_bits,
                              .delta_bits = branch_delta.delta_bits});
        out.branching_delta = std::move(deltas);
    }
    if (diff.ngram_delta)
    {
        dto::NGramDelta ngram_delta;
        ngram_delta.ngram_size = diff.ngram_delta->ngram_size;
        if (!diff.ngram_delta->new_ngrams.empty())
            ngram_delta.new_ngrams = diff.ngram_delta->new_ngrams;
        if (!diff.ngram_delta->vanished_ngrams.empty())
            ngram_delta.vanished_ngrams = diff.ngram_delta->vanished_ngrams;
        if (!diff.ngram_delta->rate_changed.empty())
        {
            std::vector<dto::NGramRateChange> changes;
            changes.reserve(diff.ngram_delta->rate_changed.size());
            for (const auto& rate_change : diff.ngram_delta->rate_changed)
                changes.push_back({.sequence = rate_change.sequence,
                                   .previous_probability = rate_change.previous_probability,
                                   .current_probability = rate_change.current_probability,
                                   .delta = rate_change.delta});
            ngram_delta.rate_changed = std::move(changes);
        }
        out.ngram_delta = std::move(ngram_delta);
    }
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
    return out;
}

} // namespace

std::string to_json(const MetaLogDocument& doc)
{
    const dto::Document out{make_document(doc)};
    std::string buf;
    // Serialising a fully-formed value to a growable string cannot fail.
    (void)glz::write<kWriteOpts>(out, buf);
    return buf;
}

std::string to_json(const MetaLogDiff& diff)
{
    const dto::Diff out{make_diff(diff)};
    std::string buf;
    (void)glz::write<kWriteOpts>(out, buf);
    return buf;
}

} // namespace insight::metalog
