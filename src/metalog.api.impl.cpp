// insight.metalog.api — implementation unit.
//
// The out-of-line home of TemplateRegistry's members. These are deliberately defined HERE, in an
// implementation unit, rather than in the interface (api/metalog.api.cppm) for a two-compiler reason
// (see the class note in the interface):
//   • gcc-15 needs them NON-inline: table_ is a std::unordered_map keyed on TemplateId, a module-
//     attached canon type, whose out-of-line std::_Hashtable members gcc emits with *internal*
//     linkage — an inlined copy/merge/intern in a consumer TU is then unresolved at link. Keeping
//     them non-inline emits them once into libinsight_metalog; consumers call the external symbol.
//   • MSVC re-emits an out-of-line `= default` special member DEFINED IN A MODULE INTERFACE into
//     every importer of that interface (e.g. engine.cpp), producing LNK2005 duplicate symbols. The
//     interface must therefore only DECLARE them; the single definition lives in this impl unit.
// Both constraints are satisfied by the standard modules split: interface declares, impl unit defines.
module insight.metalog.api;
import insight.metalog.internal;   // std (unordered_map / string / string_view / size_t)
import insight.canon;              // TemplateId (the module-attached map key)

namespace insight::metalog
{

// `= default` keeps the exact special-member semantics (incl. deduced noexcept) the implicit
// declarations had.
TemplateRegistry::TemplateRegistry() = default;
TemplateRegistry::TemplateRegistry(const TemplateRegistry&) = default;
TemplateRegistry::TemplateRegistry(TemplateRegistry&&) noexcept = default;
TemplateRegistry& TemplateRegistry::operator=(const TemplateRegistry&) = default;
TemplateRegistry& TemplateRegistry::operator=(TemplateRegistry&&) noexcept = default;
TemplateRegistry::~TemplateRegistry() = default;

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

std::size_t TemplateRegistry::size() const noexcept { return table_.size(); }

void TemplateRegistry::clear() noexcept { table_.clear(); }

void TemplateRegistry::merge(const TemplateRegistry& other)
{
    for (const auto& [template_id, template_str] : other.table_)
        table_.try_emplace(template_id, template_str);
}

}  // namespace insight::metalog
