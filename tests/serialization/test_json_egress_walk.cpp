// refs: DN-99.D8
// invariant: the metalog egress refuses a document whose NaN, +inf or -inf sits behind any category
// the walk dispatches, naming the path the writer would have written, a renamed key included.
// invariant: an empty optional is not refused, a finite document's bytes are the unwalked writer's,
// and that writer spells the same non-finite number `null`, which is what the walk exists to stop.
#include <gtest/gtest.h>

#include <glaze/glaze.hpp>

#include "serialization/json_egress.hpp"

import insight.metalog.test;

// note: a NAMED namespace — clang refuses glaze reflection over anonymous-namespace types.
namespace metalog_walk_fixture
{

struct Leafy
{
    double plain{0.0};
    double renamed{0.0};
    std::optional<double> maybe;
    std::vector<double> many;
    std::map<std::string, double> keyed;

    struct glaze
    {
        using T = Leafy;
        static constexpr auto value =
            glz::object("plain", &T::plain, "fr.coderoast.renamed", &T::renamed, "maybe", &T::maybe,
                        "many", &T::many, "keyed", &T::keyed);
    };
};

struct Document
{
    std::string label;
    std::vector<Leafy> rows;
};

} // namespace metalog_walk_fixture

namespace
{

namespace egress = insight::metalog::json_egress;
using metalog_walk_fixture::Document;
using metalog_walk_fixture::Leafy;

constexpr glz::opts kWriteOpts{.skip_null_members = true};

struct NonFinite
{
    std::string_view label;
    double value;
};

constexpr std::array kNonFinite{
    NonFinite{.label = "NaN", .value = std::numeric_limits<double>::quiet_NaN()},
    NonFinite{.label = "+inf", .value = std::numeric_limits<double>::infinity()},
    NonFinite{.label = "-inf", .value = -std::numeric_limits<double>::infinity()},
};

struct Placement
{
    std::string_view path;
    void (*place)(Leafy&, double);
};

constexpr std::array kPlacements{
    Placement{.path = "rows[0].plain",
              .place = [](Leafy& leaf, double number) { leaf.plain = number; }},
    Placement{.path = "rows[0].fr.coderoast.renamed",
              .place = [](Leafy& leaf, double number) { leaf.renamed = number; }},
    Placement{.path = "rows[0].maybe",
              .place = [](Leafy& leaf, double number) { leaf.maybe = number; }},
    Placement{.path = "rows[0].many[1]",
              .place = [](Leafy& leaf, double number) { leaf.many[1] = number; }},
    Placement{.path = "rows[0].keyed.beta",
              .place = [](Leafy& leaf, double number) { leaf.keyed["beta"] = number; }},
};

// post: a finite document with an EMPTY optional, a three-element vector and a two-key map.
[[nodiscard]] Document finite_document()
{
    Leafy leaf;
    leaf.plain = 0.25;
    leaf.renamed = 1.5;
    leaf.many = {1.0, 2.0, 3.0};
    leaf.keyed = {{"alpha", 0.5}, {"beta", 0.75}};
    Document document;
    document.label = "window";
    document.rows.push_back(leaf);
    return document;
}

} // namespace

TEST(MetalogJsonEgressWalk, EachCategoryRefusesNanAndBothInfinitiesNamingTheWrittenPath)
{
    for (const auto& non_finite : kNonFinite)
    {
        for (const auto& placement : kPlacements)
        {
            Document document{finite_document()};
            placement.place(document.rows[0], non_finite.value);
            const auto written{egress::to_string<kWriteOpts>(document)};
            ASSERT_FALSE(written.has_value()) << non_finite.label << " at " << placement.path
                                              << " was written as '" << written.value_or("") << "'";
            EXPECT_EQ(written.error(), placement.path) << non_finite.label;
        }
    }
}

TEST(MetalogJsonEgressWalk, AnEmptyOptionalIsNotRefusedAndAFiniteDocumentIsTheUnwalkedWritersBytes)
{
    const Document document{finite_document()};
    ASSERT_FALSE(document.rows[0].maybe.has_value()) << "fixture guard: the optional must be empty";
    const auto written{egress::to_string<kWriteOpts>(document)};
    ASSERT_TRUE(written.has_value()) << "a finite document was refused at '"
                                     << (written ? std::string{} : written.error()) << "'";
    std::string unwalked;
    ASSERT_FALSE(glz::write<egress::conformant<kWriteOpts>>(document, unwalked))
        << "fixture guard: the direct write failed";
    EXPECT_EQ(*written, unwalked);
}

TEST(MetalogJsonEgressWalk, TheUnwalkedWriterSpellsANonFiniteNumberNull)
{
    Document document{finite_document()};
    document.rows[0].plain = std::numeric_limits<double>::quiet_NaN();
    std::string unwalked;
    ASSERT_FALSE(glz::write<egress::conformant<kWriteOpts>>(document, unwalked))
        << "fixture guard: the direct write failed";
    EXPECT_NE(unwalked.find(R"("plain":null)"), std::string::npos)
        << "the writer the walk guards must spell NaN as null, or the refusal guards nothing; "
           "wrote '"
        << unwalked << "'";
}
