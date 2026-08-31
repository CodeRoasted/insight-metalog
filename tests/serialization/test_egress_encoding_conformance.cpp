// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns.
//
// test_egress_encoding_conformance.cpp — the EGRESS ENCODING gate: `metalog::to_json` emits
// RFC 8259-conformant JSON for EVERY string input, including log-derived bytes below 0x20.
//
// ── THE INVARIANT, AND WHY IT IS OWNED HERE ────────────────────────────────────────────────────
// "Every byte this surface emits into a declared encoding is legal in that encoding" is a MUST on
// the EMITTING surface, unconditional, quantified over ALL string inputs — never a precondition on
// an upstream producer (ADR-26 at drain; the argument is DN-65.D1). `insight-metalog` writes the
// MetaLog document and diff wire, so `insight-metalog` owns the legality of those bytes. Sift
// embeds the diff verbatim as `glz::raw_json` BY DESIGN (so spec-open members survive with no parse
// round-trip), which makes Sift's report validity a function of THIS writer with no type-level
// guarantee at the seam (DN-65.D5). The postcondition is discharged here or nowhere.
//
// ── THE ORACLE IS A DIFFERENT IMPLEMENTATION, ON PURPOSE ───────────────────────────────────────
// Re-reading Glaze's output with Glaze is SUT == ORACLE: it proves ROUND-TRIP, never CONFORMANCE,
// because a writer and its paired reader can agree on bytes no third party accepts — which is
// exactly the defect this gate exists for (the engine exits 0; Node's `JSON.parse` throws).
// `ConformanceScanner` below is therefore a hand-written recursive-descent JSON validator with no
// dependency on the serializer under test. Its declared scope: RFC 8259 grammar + §7's rule that
// U+0000..U+001F may not appear unescaped inside a string. It deliberately does NOT validate UTF-8
// well-formedness — that is a different claim about a different byte class, and folding it in here
// would let this gate go red for a reason it does not own.
//
// ── WHY THE INJECTION POINT IS A `where` COORDINATE AND NOT A MESSAGE BODY ─────────────────────
// This is the load-bearing part of the homing call, and it is the correction of a live blind spot.
// `F-SRC-insight-eidos:change_report_test.cpp:JsonStripsAnsiAndEscapesSurvivingControlBytes`
// asserts THIS EXACT PROPERTY, is GREEN, and was
// BLIND for the whole of 1.10.x: it injects \001 into a MESSAGE BODY, which becomes template TEXT
// and surfaces only in Sift's own — correctly escaped — fields. The MetaLog diff wire carries
// template IDs, not template text, so a byte injected into a message can never reach the
// `glz::raw_json` seam. A green arm sitting off the path the bytes actually take.
// The path the bytes DO take is the WHERE coordinate: canon's `component` -> the cube's interned
// WHERE label -> `dto::CubeCoord::where` -> the wire. That is why the injection below is a
// `component`, and the marker assertion in `wire_carries_marker` is what keeps this arm from
// becoming the same kind of green-blind row: it proves the tainted field REACHED the wire before
// anything is asserted about its encoding.
//
// ── SCOPE OF THE C0 SET ───────────────────────────────────────────────────────────────────────
// All 32 bytes 0x00..0x1F are driven, not just NUL. Five of them (\b \t \n \f \r) have short JSON
// escapes the writer emits regardless of the escape option, so a gate that probed only those would
// be green and vacuous; the other 27 are the defect's real domain. Asserting over the whole set is
// what makes the arm a statement about the ALPHABET rather than about one byte.

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

// ── The independent oracle ────────────────────────────────────────────────────────────────────

struct Violation
{
    std::size_t offset{0};
    std::string reason;
};

// A hand-written RFC 8259 validator. Independent of Glaze by construction: it shares no code, no
// table and no header with the writer under test.
class ConformanceScanner
{
  public:
    // Nesting bound. Our documents are shallow (single digits); the bound exists so a malformed
    // input cannot recurse the test binary off its stack, and it is reported as a violation rather
    // than silently accepted.
    static constexpr std::size_t kMaxDepth{64};

    explicit ConformanceScanner(std::string_view text) noexcept : text_{text} {}

    // std::nullopt == the text is conformant JSON.
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

    // RFC 8259 §2: these four bytes are the ONLY insignificant whitespace between tokens.
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

    // THE RULE THIS GATE EXISTS FOR — RFC 8259 §7: a string is a sequence of Unicode scalar values
    // wrapped in quotes, and "all Unicode characters may be placed within the quotation marks,
    // except for the characters that MUST be escaped: quotation mark, reverse solidus, and the
    // control characters (U+0000 through U+001F)".
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
        ++position_; // the reverse solidus
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

// Verbose-on-failure: the bytes around the violation, escaped so a terminal cannot eat them.
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

// ── The subject ───────────────────────────────────────────────────────────────────────────────

// A printable sentinel carried alongside the injected byte. Its presence on the wire is what
// proves the tainted field REACHED the serializer's output — the anti-vacuity guard. It survives
// the fix unchanged (only the control byte's encoding moves), so this assertion means the same
// thing before and after `escape_control_characters` is forced.
constexpr std::string_view kMarker{"kleioEgressProbe"};

// A CanonicalEvent carrying template, level and component. The caller owns the component storage
// (CanonicalEvent::component is a string_view).
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

// `component` = <marker> <injected byte> "tail". The byte sits mid-string on purpose: Glaze's
// string writer has a vectorised body and a scalar tail that corrupt differently (the body
// substitutes NUL bytes for the offending byte, the tail copies it verbatim), and a probe that
// only ever landed in one of them would under-report the defect's shapes.
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

// Deterministic: fixed epoch-anchored time points, no wall clock, no RNG, single-threaded.
constexpr std::chrono::system_clock::time_point kEpoch{};
constexpr std::chrono::seconds kWindowSpan{60};

// One closed window carrying the tainted component plus two ordinary neighbours (so the WHERE axis
// keeps full depth and nothing collapses the tainted label away).
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

// The anti-vacuity guard, stated as a function so both arms use the same definition.
[[nodiscard]] bool wire_carries_marker(std::string_view json)
{
    return json.find(kMarker) != std::string_view::npos;
}

// ── The oracle's own kill-switch ──────────────────────────────────────────────────────────────
//
// A gate whose oracle cannot FAIL is not a gate. These two rows mutate the input and pin both
// verdicts, so a later refactor that neutered the scanner would red here rather than turn the two
// conformance arms silently green.
TEST(EgressEncodingConformance, TheScannerAcceptsLegalJsonAndRejectsARawControlByte)
{
    // The conformant twin carries 0x01 as its legal \u0001 escape. Pairing the two rows is what
    // makes this a DISCRIMINATION test rather than two unrelated assertions: the scanner must
    // separate the byte's ENCODING from its presence as content, which is exactly the distinction
    // the defect under test gets wrong. Written as an escape sequence in a raw literal, never as a
    // literal control byte in this source file.
    constexpr std::string_view kLegal{
        R"({"where":["auth\u0001tail"],"count":3,"ratio":-1.5e2,"ok":true,"none":null,"list":[]})"};
    const auto clean{ConformanceScanner{kLegal}.scan()};
    ASSERT_FALSE(clean.has_value())
        << "the scanner rejected conformant JSON at offset " << (clean ? clean->offset : 0U) << ": "
        << (clean ? clean->reason : std::string{}) << "\n"
        << kLegal;

    // The same document with that escape replaced by the raw byte it denotes.
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

// ── The two arms ──────────────────────────────────────────────────────────────────────────────
//
// Both overloads of `metalog::to_json` share one `kWriteOpts` (serialize.cpp), so the document and
// the diff stand or fall together — which is precisely why BOTH are driven here rather than one
// standing in for the other. A future change that split the options would be caught by whichever
// arm kept the unsafe one.

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
        // Baseline without the tainted component, current with it: the label EMERGES, so it lands
        // on the cube diff's emerging border and its WHERE coordinate reaches the diff wire.
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
// NOLINTEND
