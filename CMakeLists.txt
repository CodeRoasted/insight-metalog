###########################################################
#  insight_metalog — MetaLog spec-conformant producer (1.5.2 template-aligned named modules).
#  Spec: https://github.com/CodeRoasted/metalog-spec
###########################################################
cmake_minimum_required(VERSION 3.28)

# 1.5.1 unwrap: the pure module does `import std` → the import-std gate UUID MUST be set before
# project()/CXX-enable (451f2fe2-… = CMake 4.3.1's CMAKE_EXPERIMENTAL_CXX_IMPORT_STD).
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

project(insight_metalog LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(INSIGHT_METALOG_BUILD_TESTS "Build metalog unit tests" ${PROJECT_IS_TOP_LEVEL})
option(INSIGHT_METALOG_BUILD_BENCHMARKS "Build metalog micro-benchmarks" ${PROJECT_IS_TOP_LEVEL})

# Guard: when metalog is add_subdirectory()'d alongside an in-tree canon (e.g. the
# determinism proof harness builds the canon+metalog module tower from source per cell),
# the insight::canon target already exists — don't re-find the packaged config.
if(NOT TARGET insight::canon)
    find_package(insight_canon REQUIRED CONFIG)
endif()
find_package(glaze REQUIRED CONFIG)
find_package(picosha2 REQUIRED CONFIG)

###########################################################
#  Sources (1.5.2 domain decomposition, §11.9.11)
#  - PUBLIC module interface units (installed): internal (src/) + api + facade (api/).
#  - PRIVATE (sealed, build-only) detail shards (src/<domain>/): three domain shards —
#    detail.stats (HLL, statistics, salience, wire_format), detail.operations
#    (comparability gate), and detail.cube (closed cube + emerging border, SPEC §16/§13.6).
#    Impl units live alongside their shard in src/<domain>/.
#  - Module impl units (regular sources, scanned): src/**/*.cpp attach via
#    `module insight.metalog[.detail.<domain>];`.
#  - No textual headers survive (metalog uses no logging macro layer).
###########################################################
set(METALOG_PUBLIC_MODULE_UNITS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/metalog.internal.cppm
    ${CMAKE_CURRENT_SOURCE_DIR}/api/metalog.api.cppm
    ${CMAKE_CURRENT_SOURCE_DIR}/api/metalog.cppm
)
set(METALOG_DETAIL_MODULE_UNITS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/stats/metalog.detail.stats.cppm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/operations/metalog.detail.operations.cppm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cube/metalog.detail.cube.cppm
)
file(GLOB_RECURSE METALOG_IMPL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

###########################################################
#  Module-bearing static library
###########################################################
# §11.9.7/§11.9.11 install seal: the PUBLIC file set (internal/api/facade) is installed; the
# PRIVATE cxx_modules_detail set is build-only — legal because no external target imports the
# lib's private module (tests/benchmarks are module MEMBERS below).
add_library(insight_metalog STATIC ${METALOG_IMPL_SOURCES})
target_sources(insight_metalog
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/api ${CMAKE_CURRENT_SOURCE_DIR}/src
        FILES    ${METALOG_PUBLIC_MODULE_UNITS}
    PRIVATE
        FILE_SET cxx_modules_detail TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
        FILES    ${METALOG_DETAIL_MODULE_UNITS}
)
set_target_properties(insight_metalog PROPERTIES EXPORT_NAME metalog
    CXX_SCAN_FOR_MODULES ON CXX_MODULE_STD ON)
target_compile_features(insight_metalog PUBLIC cxx_std_23)
add_library(insight::metalog ALIAS insight_metalog)

# canon is imported by metalog's module units (consumers recompile them) → PUBLIC module link.
# picosha2 is impl-only (engine.cpp, textual GMF). glaze (visible=False) must NOT reach the export
# interface (§10.9/glaze-gotchas #5) — pull its includes build-only so the impl compiles its headers.
target_link_libraries(insight_metalog
    PUBLIC  insight::canon
    PRIVATE picosha2::picosha2)
# glaze (visible=False, impl-only): scope its include to ONLY the .cpp that needs it (metalog_serialize),
# NOT the target — a target-level include leaks into IMPORTED_CXX_MODULES_INCLUDE_DIRECTORIES, which
# editable module consumers (detection/eidos) then evaluate → "glaze::glaze not found" in their graph.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/serialization/serialize.cpp
    PROPERTIES INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:glaze::glaze,INTERFACE_INCLUDE_DIRECTORIES>")

# Float determinism: the MetaLog document is deterministic content → cross-machine bit-identity.
# Disable FMA contraction (no fused a*b+c). PUBLIC so consumers inherit it.
target_compile_options(insight_metalog PUBLIC
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-ffp-contract=off>
)

###########################################################
#  Install rules  (used by `conan create`)
###########################################################
include(GNUInstallDirs)
install(TARGETS insight_metalog
    EXPORT insight_metalogTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILE_SET cxx_modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/insight_metalog_modules
)
install(EXPORT insight_metalogTargets
    NAMESPACE insight::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/insight_metalog
)

###########################################################
#  Cross-package module shipping (Argos — §10.7/§10.9)
###########################################################
export(EXPORT insight_metalogTargets
    NAMESPACE insight::
    FILE ${CMAKE_CURRENT_BINARY_DIR}/insight_metalogTargets.cmake)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/insight_metalog-config.cmake
"include(CMakeFindDependencyMacro)
find_dependency(insight_canon)
find_dependency(picosha2)
include(\"\${CMAKE_CURRENT_LIST_DIR}/insight_metalogTargets.cmake\")
")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/insight_metalog-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/insight_metalog)

###########################################################
#  Tests
###########################################################
if(INSIGHT_METALOG_BUILD_TESTS)
    # Per-domain test mirror (§11.9.11): tests/<domain>/*.cpp — engine (window/stats/behavior/
    # stability/histograms/HLL), serialize, diff, compose, reservoir, determinism.
    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )
    if(TEST_SOURCES)
        include(CTest)
        find_package(GTest REQUIRED CONFIG)
        enable_testing()

        # §11.9.7 white-box tests are MODULE MEMBERS: the test target RECOMPILES the module
        # (interface units + impl .cpp) + the test TUs, so a test may import the sealed detail
        # module (via the insight.metalog.test aggregate) WITHOUT the lib exporting it;
        # CXX_MODULE_STD ON provides the same std module.
        add_executable(insight_metalog_tests ${TEST_SOURCES} ${METALOG_IMPL_SOURCES})
        target_sources(insight_metalog_tests
            PRIVATE
                FILE_SET cxx_modules TYPE CXX_MODULES
                BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/api ${CMAKE_CURRENT_SOURCE_DIR}/src
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests
                FILES    ${METALOG_PUBLIC_MODULE_UNITS} ${METALOG_DETAIL_MODULE_UNITS}
                         tests/metalog.test.cppm)
        target_link_libraries(insight_metalog_tests PRIVATE
            insight::canon glaze::glaze picosha2::picosha2
            GTest::gtest GTest::gtest_main)
        target_include_directories(insight_metalog_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts) # shared reservoir_nearfull_scenario.hpp (F5-M8 golden)
        target_compile_features(insight_metalog_tests PRIVATE cxx_std_23)
        set_target_properties(insight_metalog_tests PROPERTIES CXX_SCAN_FOR_MODULES ON CXX_MODULE_STD ON)
        target_compile_options(insight_metalog_tests PRIVATE
            $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-ffp-contract=off>)
        include(GoogleTest)
        gtest_discover_tests(insight_metalog_tests)
    endif()
endif()

###########################################################
#  Benchmarks (Google Benchmark)
###########################################################
if(INSIGHT_METALOG_BUILD_BENCHMARKS)
    file(GLOB_RECURSE BENCH_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp"
    )
    if(BENCH_SOURCES)
        find_package(benchmark REQUIRED CONFIG)
        find_package(spdlog   REQUIRED CONFIG)
        # §11.9.7/§11.9.11: like the white-box tests, benchmarks are MODULE MEMBERS, not consumers —
        # the bench target RECOMPILES the module (interface + detail + impl .cpp) + the bench TUs
        # (via the insight.metalog.bench aggregate) and does NOT link insight_metalog; it pulls the
        # lib's deps directly. This is what lets the detail module stay a PRIVATE (build-only,
        # never-installed) file set on the shipped lib.
        add_executable(insight_metalog_bench ${BENCH_SOURCES} ${METALOG_IMPL_SOURCES})
        target_sources(insight_metalog_bench
            PRIVATE
                FILE_SET cxx_modules TYPE CXX_MODULES
                BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/api ${CMAKE_CURRENT_SOURCE_DIR}/src
                         ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
                FILES    ${METALOG_PUBLIC_MODULE_UNITS} ${METALOG_DETAIL_MODULE_UNITS}
                         benchmarks/metalog.bench.cppm)
        target_link_libraries(insight_metalog_bench PRIVATE
            insight::canon glaze::glaze picosha2::picosha2
            benchmark::benchmark spdlog::spdlog)
        target_include_directories(insight_metalog_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_compile_features(insight_metalog_bench PRIVATE cxx_std_23)
        set_target_properties(insight_metalog_bench PROPERTIES CXX_SCAN_FOR_MODULES ON CXX_MODULE_STD ON)
        target_compile_options(insight_metalog_bench PRIVATE
            $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-ffp-contract=off>)
    endif()
endif()
