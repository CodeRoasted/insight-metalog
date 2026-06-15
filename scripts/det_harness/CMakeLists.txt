# Private cross-stdlib determinism diagonal — per-leg harness (Approach B, Daidalos ruling).
#
# The twin of canon's public proof, scoped to the canon+metalog tower. The 1.5.1 unwrap turned
# both into C++20 modules, deleting the textual headers determinism_bitidentity.sh's single-shot
# `$cxx canon/src metalog/src fixture.cpp` recompile #include'd. "N ways" (here the 2 cross-stdlib
# legs gcc-15/libstdc++ × clang-21/libc++) is now: build canon+metalog as a MODULE STATIC-LIB tower
# per leg via their real CXX_MODULE_STD build, and link the constant determinism_fixture against it.
# Building the libs IS recompiling canon+metalog under the leg's codegen; only the trivial fixture
# moves from recompile to link, so the diagonal's coverage is unchanged. determinism_bitidentity.sh
# drives one configure per leg (compiler×stdlib via the conan toolchain); the fixture/digest core
# stays byte-untouched, so the 2 legs staying IDENTICAL is the same invariant the old script asserted.
cmake_minimum_required(VERSION 3.28)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

project(metalog_det_harness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED CANON_ROOT)
    message(FATAL_ERROR "CANON_ROOT must point at the insight-canon repo root")
endif()
if(NOT DEFINED META_ROOT)
    get_filename_component(META_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

# The leg's flags (-ffp-contract=off + SPDLOG off so engine logs never reach the digest), appended
# after the conan toolchain's profile flags. Applied to canon, metalog AND the fixture.
if(DEFINED CELL_FLAGS)
    separate_arguments(_cell_flags NATIVE_COMMAND "${CELL_FLAGS}")
    add_compile_options(${_cell_flags})
endif()

# canon's impl (src/*.cpp JSON strategies) #includes simdjson.h directly — a PRIVATE impl-only
# dep. Built from source here inside metalog's conan env, simdjson is a transitive-private-of-canon
# whose generated target carries only the .a + its compile-define, NOT the header include dir (a
# normal `conan create` builds canon as a finished package, so never hits this). Expose simdjson's
# headers so canon's source compiles. The legacy simdjson_INCLUDE_DIRS variable (CMakeDeps) carries
# the path on Linux but is EMPTY under MSVC/conan-2.29 (a C1083 'simdjson.h not found' mid-build) —
# so locate the header DIRECTLY over the conan prefix path, robust to either conan generator. REQUIRED
# turns a miss into a clear configure-time error, never a cryptic mid-build failure.
find_package(simdjson REQUIRED CONFIG)
if(simdjson_INCLUDE_DIRS)
    include_directories(${simdjson_INCLUDE_DIRS})
else()
    find_path(SIMDJSON_SCRATCH_INCLUDE_DIR NAMES simdjson.h REQUIRED)
    include_directories(${SIMDJSON_SCRATCH_INCLUDE_DIR})
endif()

# Build the module tower from source: canon first (so metalog's guarded find_package sees the
# in-build insight::canon target), then metalog. Tests/benchmarks default OFF (not top-level).
add_subdirectory(${CANON_ROOT} canon_build)
add_subdirectory(${META_ROOT}  metalog_build)

# The constant fixture, consuming the tower as external modules.
add_executable(det_fixture "${META_ROOT}/scripts/determinism_fixture.cpp")
target_link_libraries(det_fixture PRIVATE insight::metalog insight::canon)
target_compile_features(det_fixture PRIVATE cxx_std_23)
set_target_properties(det_fixture PROPERTIES CXX_SCAN_FOR_MODULES ON CXX_MODULE_STD ON)
