// NOLINTBEGIN Module-import proof: allow short identifiers / test patterns.
// Proof that `insight.metalog` imports cleanly and re-exports its surface, both
// toolchains. to_json() is declared out-of-line (defined in src/) → reaching it
// through import proves the re-export resolves to the library .a, not just headers
// (cxx_modules_migration_contract §10.15). gtest (textual std) precedes the import.
#include <gtest/gtest.h>

#include <string>

import insight.metalog;

TEST(MetalogModuleImport, DocumentAndSerializeResolveThroughModule)
{
    insight::metalog::MetaLogDocument doc{};
    const std::string json{insight::metalog::to_json(doc)}; // src-defined → .a linkage
    EXPECT_FALSE(json.empty());
}
// NOLINTEND
