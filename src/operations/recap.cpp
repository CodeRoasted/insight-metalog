module;

module insight.metalog.detail.operations;
import insight.metalog.internal;
import insight.metalog.api;
import insight.metalog.detail.stats;
import insight.metalog.detail.cube;

namespace insight::metalog
{

namespace
{
    // post: the slot's values re-capped, values that coincide one value with their counts summed,
    // and the entropy recomputed only when two did, since a bijection over keys keeps it.
    void recap_slot(FieldHistogram& slot, std::optional<std::size_t> declared, std::size_t target)
    {
        std::unordered_map<std::string, std::uint64_t> mapped;
        mapped.reserve(slot.value_counts.size());
        for (const auto& [value, count] : slot.value_counts)
            mapped[recap_text(value, declared, target)] += count;
        const bool coincided{mapped.size() != slot.value_counts.size()};
        slot.value_counts = std::move(mapped);
        if (!coincided)
            return;
        std::vector<std::uint64_t> counts;
        counts.reserve(slot.value_counts.size());
        for (const auto& [value, count] : slot.value_counts)
            counts.push_back(count);
        slot.entropy_bits = shannon_entropy_bits(counts, slot.total);
    }

    void recap_component(std::optional<std::string>& component, std::optional<std::size_t> declared,
                         std::size_t target)
    {
        if (component)
            *component = recap_text(*component, declared, target);
    }

    // post: the edges re-capped, an edge whose endpoints coincide after it dropped (the derivation
    // excludes self-edges), coinciding edges one edge, sorted by (caller, callee).
    void recap_service_edges(ServiceEdgeBlock& block, std::optional<std::size_t> declared,
                             std::size_t target)
    {
        std::map<std::pair<std::string, std::string>, std::uint64_t> merged;
        for (const ServiceEdge& edge : block.edges)
        {
            std::string caller{recap_text(edge.caller, declared, target)};
            std::string callee{recap_text(edge.callee, declared, target)};
            if (caller != callee)
                merged[{std::move(caller), std::move(callee)}] += edge.weight;
        }
        block.edges.clear();
        block.edges.reserve(merged.size());
        for (auto& [pair, weight] : merged)
            block.edges.push_back(
                ServiceEdge{.caller = pair.first, .callee = pair.second, .weight = weight});
    }
} // namespace

MetaLogDocument recapped_document(const MetaLogDocument& doc, std::size_t target)
{
    const std::optional<std::size_t> declared{doc.max_text_bytes};
    MetaLogDocument out{doc};
    for (TopKEntry& entry : out.stats.top_k)
    {
        recap_component(entry.dominant_component, declared, target);
        for (FieldHistogram& slot : entry.field_histograms)
            recap_slot(slot, declared, target);
    }
    for (ReservoirEntry& entry : out.stats.reservoir)
    {
        recap_component(entry.dominant_component, declared, target);
        if (entry.cube_coord && entry.cube_coord->where)
            for (std::string& label : *entry.cube_coord->where)
                label = recap_text(label, declared, target);
    }
    if (out.has_cube)
        out.cube = cube::recap_cube(out.cube, declared, target);
    if (out.service_edges)
        recap_service_edges(*out.service_edges, declared, target);
    out.max_text_bytes = target;
    return out;
}

} // namespace insight::metalog
