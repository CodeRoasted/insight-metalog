// invariant: every byte this writer emits into a declared encoding is legal there -- a MUST on the
// emitting surface, over ALL string inputs, never a precondition on an upstream producer.
// refs: DN-65.D1, DN-65.D5
// refs: F-SRC-insight-eidos:change_report_test.cpp:JsonStripsAnsiAndEscapesSurvivingControlBytes
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::LogLevel;
using insight::StructuralRole;

struct Violation
{
    std::size_t offset{0};
    std::string reason;
};

// invariant: independent of the writer under test -- it shares no code, table or header with it.
// invariant: its scope is the RFC 8259 grammar plus the ban on unescaped U+0000..U+001F inside a
// string; UTF-8 well-formedness is a different claim and is not checked here.
// refs: DN-65.D7
class ConformanceScanner
{
  public:
    // note: an overrun is a reported violation, so a malformed input cannot recurse off the stack.
    static constexpr std::size_t kMaxDepth{64};

    explicit ConformanceScanner(std::string_view text) noexcept : text_{text} {}

    // post: nullopt means the text is conformant JSON; otherwise the first violation found.
    [[nodiscard]] std::optional<Violation> scan()
    {
        skip_whitespace();
        if (!parse_value(0))
            return violation_;
        skip_whitespace();
        if (position_ != text_.size())
        {
            record("trailing bytes after the top-level value");
            return violation_;
        }
        return violation_;
    }

  private:
    std::string_view text_;
    std::size_t position_{0};
    std::optional<Violation> violation_;

    void record(std::string reason)
    {
        if (!violation_)
            violation_ = Violation{.offset = position_, .reason = std::move(reason)};
    }

    [[nodiscard]] bool at_end() const noexcept
    {
        return position_ >= text_.size();
    }
    [[nodiscard]] std::uint8_t peek() const noexcept
    {
        return static_cast<std::uint8_t>(text_[position_]);
    }

    void skip_whitespace() noexcept
    {
        while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r'))
            ++position_;
    }

    [[nodiscard]] bool expect(char expected)
    {
        if (at_end() || static_cast<char>(peek()) != expected)
        {
            record(std::string{"expected '"} + expected + "'");
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] bool parse_value(std::size_t depth)
    {
        if (depth > kMaxDepth)
        {
            record("nesting deeper than the scanner's declared bound");
            return false;
        }
        if (at_end())
        {
            record("expected a value, found end of input");
            return false;
        }
        switch (static_cast<char>(peek()))
        {
        case '{':
            return parse_object(depth);
        case '[':
            return parse_array(depth);
        case '"':
            return parse_string();
        case 't':
            return parse_literal("true");
        case 'f':
            return parse_literal("false");
        case 'n':
            return parse_literal("null");
        default:
            return parse_number();
        }
    }

    [[nodiscard]] bool parse_literal(std::string_view literal)
    {
        if (text_.substr(position_, literal.size()) != literal)
        {
            record(std::string{"malformed literal, expected "} + std::string{literal});
            return false;
        }
        position_ += literal.size();
        return true;
    }

    [[nodiscard]] bool parse_object(std::size_t depth)
    {
        if (!expect('{'))
            return false;
        skip_whitespace();
        if (!at_end() && static_cast<char>(peek()) == '}')
        {
            ++position_;
            return true;
        }
        while (true)
        {
            skip_whitespace();
            if (!parse_string())
                return false;
            skip_whitespace();
            if (!expect(':'))
                return false;
            skip_whitespace();
            if (!parse_value(depth + 1))
                return false;
            skip_whitespace();
            if (at_end())
            {
                record("unterminated object");
                return false;
            }
            if (static_cast<char>(peek()) == ',')
            {
                ++position_;
                continue;
            }
            return expect('}');
        }
    }

    [[nodiscard]] bool parse_array(std::size_t depth)
    {
        if (!expect('['))
            return false;
        skip_whitespace();
        if (!at_end() && static_cast<char>(peek()) == ']')
        {
            ++position_;
            return true;
        }
        while (true)
        {
            skip_whitespace();
            if (!parse_value(depth + 1))
                return false;
            skip_whitespace();
            if (at_end())
            {
                record("unterminated array");
                return false;
            }
            if (static_cast<char>(peek()) == ',')
            {
                ++position_;
                continue;
            }
            return expect(']');
        }
    }

    [[nodiscard]] bool parse_string()
    {
        if (!expect('"'))
            return false;
        while (true)
        {
            if (at_end())
            {
                record("unterminated string");
                return false;
            }
            const std::uint8_t byte{peek()};
            if (byte == '"')
            {
                ++position_;
                return true;
            }
            if (byte == '\\')
            {
                if (!parse_escape())
                    return false;
                continue;
            }
            if (byte < 0x20U)
            {
                record(std::string{"raw control byte 0x"} + hex_byte(byte) +
                       " inside a JSON string (RFC 8259 §7 forbids unescaped U+0000..U+001F)");
                return false;
            }
            ++position_;
        }
    }

    [[nodiscard]] bool parse_escape()
    {
        ++position_;
        if (at_end())
        {
            record("escape at end of input");
            return false;
        }
        const char kind{static_cast<char>(peek())};
        switch (kind)
        {
        case '"':
        case '\\':
        case '/':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
            ++position_;
            return true;
        case 'u':
            ++position_;
            for (std::size_t digit{0}; digit < 4; ++digit)
            {
                if (at_end() || !is_hex(peek()))
                {
                    record("\\u escape with fewer than four hex digits");
                    return false;
                }
                ++position_;
            }
            return true;
        default:
            record(std::string{"illegal escape \\"} + kind);
            return false;
        }
    }

    [[nodiscard]] bool parse_number()
    {
        const std::size_t start{position_};
        if (!at_end() && static_cast<char>(peek()) == '-')
            ++position_;
        if (!consume_digits())
        {
            position_ = start;
            record("expected a value");
            return false;
        }
        if (!at_end() && static_cast<char>(peek()) == '.')
        {
            ++position_;
            if (!consume_digits())
            {
                record("fraction with no digits");
                return false;
            }
        }
        if (!at_end() && (static_cast<char>(peek()) == 'e' || static_cast<char>(peek()) == 'E'))
        {
            ++position_;
            if (!at_end() && (static_cast<char>(peek()) == '+' || static_cast<char>(peek()) == '-'))
                ++position_;
            if (!consume_digits())
            {
                record("exponent with no digits");
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool consume_digits() noexcept
    {
        const std::size_t start{position_};
        while (!at_end() && peek() >= '0' && peek() <= '9')
            ++position_;
        return position_ > start;
    }

    [[nodiscard]] static bool is_hex(std::uint8_t byte) noexcept
    {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
               (byte >= 'A' && byte <= 'F');
    }

  public:
    [[nodiscard]] static std::string hex_byte(std::uint8_t byte)
    {
        constexpr std::string_view kDigits{"0123456789ABCDEF"};
        std::string out;
        out.push_back(kDigits[(byte >> 4U) & 0x0FU]);
        out.push_back(kDigits[byte & 0x0FU]);
        return out;
    }
};

[[nodiscard]] std::string hex_window(std::string_view text, std::size_t offset)
{
    constexpr std::size_t kRadius{48};
    const std::size_t first{offset > kRadius ? offset - kRadius : 0};
    const std::size_t last{std::min(text.size(), offset + kRadius)};
    std::string out;
    for (std::size_t index{first}; index < last; ++index)
    {
        const auto byte{static_cast<std::uint8_t>(text[index])};
        if (index == offset)
            out += ">>>";
        if (byte >= 0x20U && byte < 0x7FU)
            out.push_back(static_cast<char>(byte));
        else
            out += "\\x" + ConformanceScanner::hex_byte(byte);
        if (index == offset)
            out += "<<<";
    }
    return out;
}

constexpr std::string_view kMarker{"kleioEgressProbe"};

// pre: the caller owns `component`'s storage -- CanonicalEvent::component is a view.
[[nodiscard]] tok::CanonicalEvent make_event(std::string_view tmpl, LogLevel level,
                                             std::string_view component)
{
    tok::CanonicalEvent event;
    event.template_str = tmpl;
    event.level = level;
    event.component = component;
    event.structural_role = StructuralRole::None;
    return event;
}

[[nodiscard]] meta::MetaLogConfig probe_config()
{
    return meta::MetaLogConfig{
        .reservoir_size = 8, .reservoir_per_kind_cap = 4, .emit_stability = false};
}

[[nodiscard]] std::string tainted_component(std::uint8_t injected)
{
    std::string out{kMarker};
    out.push_back(static_cast<char>(injected));
    out += "tail";
    return out;
}

struct BuiltDocument
{
    meta::MetaLogEngine engine{probe_config()};
    std::string component;
    meta::MetaLogDocument document;
};

constexpr std::chrono::system_clock::time_point kEpoch{};
constexpr std::chrono::seconds kWindowSpan{60};

[[nodiscard]] std::unique_ptr<BuiltDocument> build_tainted_document(std::uint8_t injected,
                                                                    bool include_tainted)
{
    auto built{std::make_unique<BuiltDocument>()};
    built->component = tainted_component(injected);
    built->engine.open_window(kEpoch);
    for (int repeat{0}; repeat < 3; ++repeat)
    {
        built->engine.ingest_event(make_event("login ok", LogLevel::Info, "auth"));
        built->engine.ingest_event(make_event("cache miss", LogLevel::Info, "cache"));
    }
    if (include_tainted)
        for (int repeat{0}; repeat < 4; ++repeat)
            built->engine.ingest_event(
                make_event("upload failed", LogLevel::Error, built->component));
    built->document = built->engine.close_window(kEpoch + kWindowSpan);
    return built;
}

[[nodiscard]] bool wire_carries_marker(std::string_view json)
{
    return json.find(kMarker) != std::string_view::npos;
}

TEST(EgressEncodingConformance, TheScannerAcceptsLegalJsonAndRejectsARawControlByte)
{
    constexpr std::string_view kLegal{
        R"({"where":["auth\u0001tail"],"count":3,"ratio":-1.5e2,"ok":true,"none":null,"list":[]})"};
    const auto clean{ConformanceScanner{kLegal}.scan()};
    ASSERT_FALSE(clean.has_value())
        << "the scanner rejected conformant JSON at offset " << (clean ? clean->offset : 0U) << ": "
        << (clean ? clean->reason : std::string{}) << "\n"
        << kLegal;

    std::string illegal{R"({"where":["auth)"};
    illegal.push_back('\x01');
    illegal += R"(tail"]})";
    const auto dirty{ConformanceScanner{illegal}.scan()};
    ASSERT_TRUE(dirty.has_value())
        << "the scanner accepted a raw 0x01 inside a string — the oracle is blind, so every other "
           "row in this file is vacuous.\n"
        << hex_window(illegal, 0);
    EXPECT_EQ(dirty->offset, 15U) << "expected the violation at the injected byte.\n"
                                  << hex_window(illegal, dirty->offset);
}

TEST(EgressEncodingConformance, MetaLogDocumentEmitsConformantJsonForEveryC0Byte)
{
    std::vector<std::string> failures;
    std::size_t reached_the_wire{0};

    for (std::uint16_t value{0}; value <= 0x1FU; ++value)
    {
        const auto injected{static_cast<std::uint8_t>(value)};
        const auto built{build_tainted_document(injected, /*include_tainted=*/true)};
        const std::string json{meta::to_json(built->document, built->engine.registry())};

        if (!wire_carries_marker(json))
        {
            failures.push_back("byte 0x" + ConformanceScanner::hex_byte(injected) +
                               ": the tainted component never reached the wire — this row would "
                               "have been VACUOUS, not passing");
            continue;
        }
        ++reached_the_wire;

        if (const auto broken{ConformanceScanner{json}.scan()})
            failures.push_back("byte 0x" + ConformanceScanner::hex_byte(injected) + ": " +
                               broken->reason + " at offset " + std::to_string(broken->offset) +
                               "\n    " + hex_window(json, broken->offset));
    }

    EXPECT_EQ(reached_the_wire, 32U)
        << "all 32 C0 injections must reach the wire, or the arm proves nothing about the ones "
           "that did not.";

    std::string report;
    for (const auto& line : failures)
        report += "  " + line + "\n";
    EXPECT_TRUE(failures.empty())
        << "metalog::to_json(MetaLogDocument) emitted non-conformant JSON for " << failures.size()
        << " of 32 C0 bytes driven into a `where` coordinate:\n"
        << report;
}

TEST(EgressEncodingConformance, MetaLogDiffEmitsConformantJsonForEveryC0Byte)
{
    std::vector<std::string> failures;
    std::size_t reached_the_wire{0};

    for (std::uint16_t value{0}; value <= 0x1FU; ++value)
    {
        const auto injected{static_cast<std::uint8_t>(value)};
        const auto baseline{build_tainted_document(injected, /*include_tainted=*/false)};
        const auto current{build_tainted_document(injected, /*include_tainted=*/true)};
        const std::string json{meta::to_json(meta::diff(baseline->document, current->document))};

        if (!wire_carries_marker(json))
        {
            failures.push_back("byte 0x" + ConformanceScanner::hex_byte(injected) +
                               ": the tainted component never reached the diff wire — this row "
                               "would have been VACUOUS, not passing");
            continue;
        }
        ++reached_the_wire;

        if (const auto broken{ConformanceScanner{json}.scan()})
            failures.push_back("byte 0x" + ConformanceScanner::hex_byte(injected) + ": " +
                               broken->reason + " at offset " + std::to_string(broken->offset) +
                               "\n    " + hex_window(json, broken->offset));
    }

    EXPECT_EQ(reached_the_wire, 32U)
        << "all 32 C0 injections must reach the diff wire, or the arm proves nothing about the "
           "ones that did not.";

    std::string report;
    for (const auto& line : failures)
        report += "  " + line + "\n";
    EXPECT_TRUE(failures.empty())
        << "metalog::to_json(MetaLogDiff) emitted non-conformant JSON for " << failures.size()
        << " of 32 C0 bytes driven into a `where` coordinate — these are the bytes Sift embeds "
           "verbatim as glz::raw_json and the Action feeds to JSON.parse:\n"
        << report;
}

} // namespace
