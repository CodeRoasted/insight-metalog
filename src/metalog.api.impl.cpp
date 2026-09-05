module insight.metalog.api;
import insight.metalog.internal;
import insight.canon;

namespace insight::metalog
{

std::string_view TemplateRegistry::intern(TemplateId template_id, std::string_view template_str)
{
    const auto [iter, inserted]{table_.try_emplace(template_id, template_str)};
    return iter->second;
}

std::string_view TemplateRegistry::lookup(TemplateId template_id) const noexcept
{
    const auto iter{table_.find(template_id)};
    return iter != table_.end() ? std::string_view{iter->second} : std::string_view{};
}

bool TemplateRegistry::contains(TemplateId template_id) const noexcept
{
    return table_.contains(template_id);
}

std::size_t TemplateRegistry::size() const noexcept
{
    return table_.size();
}

void TemplateRegistry::clear() noexcept
{
    table_.clear();
}

void TemplateRegistry::merge(const TemplateRegistry& other)
{
    for (const auto& [template_id, template_str] : other.table_)
        table_.try_emplace(template_id, template_str);
}

} // namespace insight::metalog
