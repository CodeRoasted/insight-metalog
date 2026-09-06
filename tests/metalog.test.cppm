// post: importing this module gives a test TU the whole metalog surface -- public facade, the
// sealed detail modules and canon; gtest stays textual and is included separately.
export module insight.metalog.test;
export import std;
export import insight.metalog;
export import insight.metalog.detail.stats;
export import insight.metalog.detail.operations;
export import insight.metalog.detail.cube;
export import insight.canon;

export namespace insight::metalog::test
{

[[nodiscard]] inline insight::tokenization::CanonicalEvent
make_event(std::string_view tmpl, insight::LogLevel level = insight::LogLevel::Info)
{
    insight::tokenization::CanonicalEvent ev;
    ev.template_str = tmpl;
    ev.level = level;
    return ev;
}

// invariant: views point into owned_values, which is reserved to its final size before any view is
// taken; a ParamEvent must outlive every use of its event and must never be copied.
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
