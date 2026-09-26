// invariant: every byte this writer emits into a declared encoding is legal there -- a MUST on the
// emitting surface, over ALL string inputs, never a precondition on an upstream producer.
// refs: ADR-26.D12, ADR-24.D8, DN-43.D20
// refs: F-SRC-insight-eidos:change_report_test.cpp:JsonStripsAnsiAndEscapesSurvivingControlBytes
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glaze/glaze.hpp>

#include "serialization/json_egress.hpp"

import insight.metalog.test;

#include "../written_or_fail.hpp"

// note: a NAMED namespace — clang refuses glaze reflection over anonymous-namespace types.
namespace metalog_utf8_fixture
{

struct Carrier
{
    std::map<std::string, std::string> keyed;
    std::string value;
};

} // namespace metalog_utf8_fixture

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
// invariant: its scope is the RFC 8259 grammar, the ban on unescaped U+0000..U+001F inside a
// string, and UTF-8 well-formedness, decoded here rather than read from any table the writer uses.
// refs: DN-65.D7, DN-43.D20
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
            if (byte >= 0x80U)
            {
                if (!parse_utf8_character())
                    return false;
                continue;
            }
            ++position_;
        }
    }

    // post: consumes one well-formed UTF-8 character, or records why the bytes at the cursor are
    // not one: a bad lead, a missing continuation, an overlong form, a surrogate, or > U+10FFFF.
    [[nodiscard]] bool parse_utf8_character()
    {
        const std::uint8_t lead{peek()};
        std::size_t length{0};
        std::uint32_t code_point{0};
        if ((lead & 0xE0U) == 0xC0U)
        {
            length = 2;
            code_point = lead & 0x1FU;
        }
        else if ((lead & 0xF0U) == 0xE0U)
        {
            length = 3;
            code_point = lead & 0x0FU;
        }
        else if ((lead & 0xF8U) == 0xF0U)
        {
            length = 4;
            code_point = lead & 0x07U;
        }
        else
        {
            record(std::string{"ill-formed UTF-8: 0x"} + hex_byte(lead) +
                   " cannot lead a character");
            return false;
        }
        for (std::size_t index{1}; index < length; ++index)
        {
            if (position_ + index >= text_.size() ||
                (static_cast<std::uint8_t>(text_[position_ + index]) & 0xC0U) != 0x80U)
            {
                record(std::string{"ill-formed UTF-8: the character led by 0x"} + hex_byte(lead) +
                       " is missing continuation byte " + std::to_string(index));
                return false;
            }
            code_point =
                (code_point << 6U) | (static_cast<std::uint8_t>(text_[position_ + index]) & 0x3FU);
        }
        constexpr std::array<std::uint32_t, 5> kShortestForm{0, 0, 0x80U, 0x800U, 0x10000U};
        if (code_point < kShortestForm[length])
        {
            record("ill-formed UTF-8: an overlong " + std::to_string(length) + "-byte form");
            return false;
        }
        if (code_point >= 0xD800U && code_point <= 0xDFFFU)
        {
            record("ill-formed UTF-8: an encoded surrogate");
            return false;
        }
        if (code_point > 0x10FFFFU)
        {
            record("ill-formed UTF-8: a code point above U+10FFFF");
            return false;
        }
        position_ += length;
        return true;
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

// invariant: the byte is driven both inside the string, with literal bytes after it, and as its
// last byte, so no claim about how the writer walks a string is needed to cover either.
enum class Placement : std::uint8_t
{
    Interior,
    Terminal,
};
constexpr std::array<Placement, 2> kPlacements{Placement::Interior, Placement::Terminal};

[[nodiscard]] std::string_view placement_name(Placement placement)
{
    return placement == Placement::Interior ? "interior" : "terminal";
}

[[nodiscard]] std::string tainted_component(std::uint8_t injected, Placement placement)
{
    std::string out{kMarker};
    out.push_back(static_cast<char>(injected));
    if (placement == Placement::Interior)
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

[[nodiscard]] std::unique_ptr<BuiltDocument>
build_tainted_document(std::uint8_t injected, Placement placement, bool include_tainted)
{
    auto built{std::make_unique<BuiltDocument>()};
    built->component = tainted_component(injected, placement);
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

// post: true when the marker reaches the wire followed by the injected byte, raw or escaped — so
// the row measured the byte and not merely the string that carried it.
[[nodiscard]] bool wire_carries_injected_byte(std::string_view json, std::uint8_t injected)
{
    const std::size_t marker{json.find(kMarker)};
    if (marker == std::string_view::npos || marker + kMarker.size() >= json.size())
        return false;
    const char next{json[marker + kMarker.size()]};
    return next == '\\' || static_cast<std::uint8_t>(next) == injected;
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

[[nodiscard]] std::string bytes_of(std::string_view hex)
{
    std::string out;
    for (std::size_t at{0}; at + 1 < hex.size(); at += 2)
        out.push_back(static_cast<char>(std::stoi(std::string{hex.substr(at, 2)}, nullptr, 16)));
    return out;
}

[[nodiscard]] std::string hex_of(std::string_view bytes)
{
    std::string out;
    for (const char byte : bytes)
        out += ConformanceScanner::hex_byte(static_cast<std::uint8_t>(byte));
    return out;
}

// invariant: the ill-formed shapes the wiring arms drive, each a maximal-subpart case of its own.
constexpr std::array<std::string_view, 6> kIllFormed{"80",     "C341",     "8022",
                                                     "EDA080", "F4908080", "E282"};

TEST(EgressEncodingConformance, TheScannerRejectsIllFormedUtf8AndAcceptsWellFormedText)
{
    const std::string legal{"[\"" + bytes_of("C3A9E282ACF09F9880EFBFBD") + "\"]"};
    const auto clean{ConformanceScanner{legal}.scan()};
    EXPECT_FALSE(clean.has_value())
        << "the scanner rejected well-formed UTF-8 at offset " << (clean ? clean->offset : 0U)
        << ": " << (clean ? clean->reason : std::string{}) << "\n"
        << hex_window(legal, 0);

    for (const std::string_view hex : {"80", "C341", "C0AF", "EDA080", "F4908080", "E282"})
    {
        const std::string illegal{"[\"x" + bytes_of(hex) + "\"]"};
        const auto dirty{ConformanceScanner{illegal}.scan()};
        ASSERT_TRUE(dirty.has_value())
            << "the scanner accepted the ill-formed bytes " << hex
            << " — the oracle is blind to UTF-8, so every UTF-8 row in this file is vacuous.\n"
            << hex_window(illegal, 0);
        EXPECT_EQ(dirty->offset, 3U) << hex << ": " << dirty->reason << "\n"
                                     << hex_window(illegal, dirty->offset);
    }
}

// refs: DN-43.D20
// invariant: A2 for this package's one write entry point — a string value AND a map key carrying
// every shape in kIllFormed, checked against exact bytes; the quote after `80` stays escaped.
// note: expected bytes from CPython 3.12.3 decode('utf-8', 'replace') over the escaped string.
TEST(EgressEncodingConformance, TheWrapperReplacesIllFormedUtf8InAValueAndAKeyWithExactBytes)
{
    const std::string hostile{bytes_of("61"
                                       "80"
                                       "62"
                                       "C341"
                                       "63"
                                       "8022"
                                       "64"
                                       "EDA080"
                                       "65"
                                       "F4908080"
                                       "66"
                                       "E282")};
    const std::string replaced{
        bytes_of("61EFBFBD62EFBFBD4163EFBFBD5C2264EFBFBDEFBFBDEFBFBD65EFBFBDEFBFBDEFBFBDEFBFBD66"
                 "EFBFBD")};
    const metalog_utf8_fixture::Carrier carrier{.keyed = {{hostile, hostile}}, .value = hostile};

    const std::string written{
        written_or_fail(insight::metalog::json_egress::to_string<glz::opts{}>(carrier))};
    const std::string expected{R"({"keyed":{")" + replaced + R"(":")" + replaced +
                               R"("},"value":")" + replaced + R"("})"};
    EXPECT_EQ(hex_of(written), hex_of(expected))
        << "expected:\n  " << hex_of(expected) << "\nactual:\n  " << hex_of(written);
    const auto broken{ConformanceScanner{written}.scan()};
    EXPECT_FALSE(broken.has_value()) << (broken ? broken->reason : std::string{}) << "\n"
                                     << hex_window(written, broken ? broken->offset : 0U);
}

// refs: DN-43.D20
// invariant: the two production callers, document and diff, emit well-formed UTF-8 when the
// `where` coordinate carries ill-formed bytes, and the bytes reach the wire as U+FFFD.
TEST(EgressEncodingConformance, MetaLogDocumentAndDiffEmitWellFormedUtf8ForIllFormedBytes)
{
    const std::string replacement{bytes_of("EFBFBD")};
    std::vector<std::string> failures;
    for (const std::string_view hex : kIllFormed)
    {
        const std::string component{std::string{kMarker} + bytes_of(hex) + "tail"};
        const auto baseline{build_tainted_document(0x20U, Placement::Interior, false)};
        const auto current{std::make_unique<BuiltDocument>()};
        current->component = component;
        current->engine.open_window(kEpoch);
        for (int repeat{0}; repeat < 4; ++repeat)
            current->engine.ingest_event(
                make_event("upload failed", LogLevel::Error, current->component));
        current->document = current->engine.close_window(kEpoch + kWindowSpan);

        const std::array<std::pair<std::string_view, std::string>, 2> wires{
            std::pair{
                std::string_view{"document"},
                written_or_fail(meta::to_json(current->document, current->engine.registry()))},
            std::pair{std::string_view{"diff"}, written_or_fail(meta::to_json(meta::diff(
                                                    baseline->document, current->document)))}};
        for (const auto& [name, json] : wires)
        {
            const std::size_t marker{json.find(kMarker)};
            if (marker == std::string::npos ||
                json.compare(marker + kMarker.size(), replacement.size(), replacement) != 0)
            {
                failures.push_back(std::string{name} + " " + std::string{hex} +
                                   ": the marker is not followed by U+FFFD on the wire\n    " +
                                   hex_window(json, marker == std::string::npos ? 0 : marker));
                continue;
            }
            if (const auto broken{ConformanceScanner{json}.scan()})
                failures.push_back(std::string{name} + " " + std::string{hex} + ": " +
                                   broken->reason + " at offset " + std::to_string(broken->offset) +
                                   "\n    " + hex_window(json, broken->offset));
        }
    }
    std::string report;
    for (const auto& line : failures)
        report += "  " + line + "\n";
    EXPECT_TRUE(failures.empty()) << failures.size() << " of " << (kIllFormed.size() * 2)
                                  << " rows (6 ill-formed shapes x document and diff) failed:\n"
                                  << report;
}

TEST(EgressEncodingConformance, MetaLogDocumentEmitsConformantJsonForEveryC0Byte)
{
    std::vector<std::string> failures;
    std::size_t reached_the_wire{0};

    for (const Placement placement : kPlacements)
        for (std::uint16_t value{0}; value <= 0x1FU; ++value)
        {
            const auto injected{static_cast<std::uint8_t>(value)};
            const auto built{build_tainted_document(injected, placement, /*include_tainted=*/true)};
            const std::string json{
                written_or_fail(meta::to_json(built->document, built->engine.registry()))};
            const std::string row{std::string{placement_name(placement)} + " byte 0x" +
                                  ConformanceScanner::hex_byte(injected)};

            if (!wire_carries_injected_byte(json, injected))
            {
                failures.push_back(row +
                                   ": the injected byte never reached the wire — this row would "
                                   "have been VACUOUS, not passing");
                continue;
            }
            ++reached_the_wire;

            if (const auto broken{ConformanceScanner{json}.scan()})
                failures.push_back(row + ": " + broken->reason + " at offset " +
                                   std::to_string(broken->offset) + "\n    " +
                                   hex_window(json, broken->offset));
        }

    EXPECT_EQ(reached_the_wire, 64U)
        << "all 32 C0 injections at both placements must reach the wire, or the arm proves "
           "nothing about the ones that did not.";

    std::string report;
    for (const auto& line : failures)
        report += "  " + line + "\n";
    EXPECT_TRUE(failures.empty())
        << "metalog::to_json(MetaLogDocument) emitted non-conformant JSON for " << failures.size()
        << " of 64 rows (32 C0 bytes x 2 placements) driven into a `where` coordinate:\n"
        << report;
}

TEST(EgressEncodingConformance, MetaLogDiffEmitsConformantJsonForEveryC0Byte)
{
    std::vector<std::string> failures;
    std::size_t reached_the_wire{0};

    for (const Placement placement : kPlacements)
        for (std::uint16_t value{0}; value <= 0x1FU; ++value)
        {
            const auto injected{static_cast<std::uint8_t>(value)};
            const auto baseline{
                build_tainted_document(injected, placement, /*include_tainted=*/false)};
            const auto current{
                build_tainted_document(injected, placement, /*include_tainted=*/true)};
            const std::string json{
                written_or_fail(meta::to_json(meta::diff(baseline->document, current->document)))};
            const std::string row{std::string{placement_name(placement)} + " byte 0x" +
                                  ConformanceScanner::hex_byte(injected)};

            if (!wire_carries_injected_byte(json, injected))
            {
                failures.push_back(row + ": the injected byte never reached the diff wire — this "
                                         "row would have been VACUOUS, not passing");
                continue;
            }
            ++reached_the_wire;

            if (const auto broken{ConformanceScanner{json}.scan()})
                failures.push_back(row + ": " + broken->reason + " at offset " +
                                   std::to_string(broken->offset) + "\n    " +
                                   hex_window(json, broken->offset));
        }

    EXPECT_EQ(reached_the_wire, 64U)
        << "all 32 C0 injections at both placements must reach the diff wire, or the arm proves "
           "nothing about the ones that did not.";

    std::string report;
    for (const auto& line : failures)
        report += "  " + line + "\n";
    EXPECT_TRUE(failures.empty())
        << "metalog::to_json(MetaLogDiff) emitted non-conformant JSON for " << failures.size()
        << " of 64 rows (32 C0 bytes x 2 placements) driven into a `where` coordinate — these are "
           "the bytes Sift embeds "
           "verbatim as glz::raw_json and the Action feeds to JSON.parse:\n"
        << report;
}

} // namespace
