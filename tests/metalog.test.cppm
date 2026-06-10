// insight.metalog.test — shared white-box test infrastructure (§11.9.11, the logcraft.test
// pattern). All test TUs import this instead of spelling out the full import block. Re-exports the
// complete metalog module surface (public facade + the sealed detail module + canon), so a test TU
// needs no further imports beyond gtest (textual, third-party).
// Exports: make_event(), ParamEvent — the shared event fixtures the suites build windows from.
export module insight.metalog.test;
export import std;
export import insight.metalog;
export import insight.metalog.detail;
export import insight.canon;

export namespace insight::metalog::test
{

/// Minimal CanonicalEvent carrying just a template string and a level.
[[nodiscard]] inline insight::tokenization::CanonicalEvent
make_event(std::string_view tmpl, insight::LogLevel level = insight::LogLevel::Info)
{
    insight::tokenization::CanonicalEvent ev;
    ev.template_str = tmpl;
    ev.level = level;
    return ev;
}

// ── Helper: CanonicalEvent with owned param values ────────────────────────────
//
// CanonicalEvent::params is a span<const string_view> into arena-stable storage.
// In tests (no arena) we own the strings here; the views and span stay valid
// for the lifetime of ParamEvent.
struct ParamEvent
{
    std::vector<std::string> owned_values;
    std::vector<std::string_view> views;
    insight::tokenization::CanonicalEvent event;

    static ParamEvent make(std::string_view tmpl, std::initializer_list<std::string_view> params,
                           insight::LogLevel level = insight::LogLevel::Info)
    {
        ParamEvent p;
        p.owned_values.reserve(params.size());
        p.views.reserve(params.size());
        for (auto s : params)
        {
            p.owned_values.emplace_back(s);
            p.views.push_back(p.owned_values.back());
        }
        p.event.template_str = tmpl;
        p.event.params = p.views;
        p.event.level = level;
        return p;
    }
};

} // namespace insight::metalog::test
