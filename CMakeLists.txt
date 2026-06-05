###########################################################
#  insight_metalog — MetaLog v0.5.0 spec-conformant producer.
#  Spec: https://github.com/CodeRoasted/metalog-spec
###########################################################
cmake_minimum_required(VERSION 3.28)

project(insight_metalog LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(INSIGHT_METALOG_BUILD_TESTS "Build metalog unit tests" ${PROJECT_IS_TOP_LEVEL})
option(INSIGHT_METALOG_BUILD_BENCHMARKS "Build metalog micro-benchmarks" ${PROJECT_IS_TOP_LEVEL})

find_package(insight_canon REQUIRED CONFIG)
find_package(glaze REQUIRED CONFIG)
find_package(picosha2 REQUIRED CONFIG)

file(GLOB_RECURSE API_HEADERS     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/api/*.hpp")
file(GLOB_RECURSE PRIVATE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")
file(GLOB_RECURSE LIB_SOURCES     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(insight_metalog ${API_HEADERS} ${PRIVATE_HEADERS} ${LIB_SOURCES})
set_target_properties(insight_metalog PROPERTIES EXPORT_NAME metalog)
add_library(insight::metalog ALIAS insight_metalog)

target_include_directories(insight_metalog
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(insight_metalog
    PUBLIC
        insight::canon
    PRIVATE
        picosha2::picosha2
)

# glaze (visible=False, header-only) must NOT reach the export interface
# (§10.9 / glaze-gotchas #5): a PRIVATE link leaks it as $<LINK_ONLY:glaze::glaze>
# into the NATIVE Targets.cmake → consumers' find_package(insight_metalog) under
# cmake_find_mode=none then needs a glaze target they don't have. Pull just its
# include dirs, build-only, so metalog's impl still compiles glaze's headers.
target_include_directories(insight_metalog PRIVATE
    $<BUILD_INTERFACE:$<TARGET_PROPERTY:glaze::glaze,INTERFACE_INCLUDE_DIRECTORIES>>)

# Float determinism: the MetaLog document is deterministic content, so
# its math must be cross-machine bit-identical. Disable FMA contraction (no fused
# a*b+c) — set explicitly here, not relying on transitive propagation from canon.
# SSE (not x87) is the x86-64 ABI default; arm has no x87.
target_compile_options(insight_metalog PUBLIC
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-ffp-contract=off>
)

###########################################################
#  Named-module wrapper (insight.metalog) — §8.1 (1.5.0)
###########################################################
# Re-exports insight_metalog's public surface (insight::metalog — 32 symbols:
# MetaLogDocument/Diff/Engine/Config + the blocks + compose/diff/to_json) so
# downstream (eidos) `import insight.metalog` instead of #include "insight/…".
# The public api header textually pulls canon (types/canonical_event) → that
# coupling stays textual in the wrapper stage (1.5.1 unwrap). The header does
# NOT use glaze/picosha2 (impl-only), so the module needs neither. Links the lib
# PUBLIC (api includes + canon, -ffp-contract=off); cxx_std_23 PUBLIC is §10.4.
add_library(insight_metalog_module STATIC)
target_sources(insight_metalog_module
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/modules
        FILES    ${CMAKE_CURRENT_SOURCE_DIR}/modules/insight_metalog.cppm
)
target_link_libraries(insight_metalog_module PUBLIC insight_metalog)
target_compile_features(insight_metalog_module PUBLIC cxx_std_23)
add_library(insight::metalog_module ALIAS insight_metalog_module)

###########################################################
#  Install rules  (used by `conan create`)
###########################################################
include(GNUInstallDirs)
# Library + module wrapper in ONE export set → single file-set owner (§10.5/§10.8).
install(TARGETS insight_metalog insight_metalog_module
    EXPORT insight_metalogTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILE_SET cxx_modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/insight_metalog_modules
)
install(DIRECTORY api/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT insight_metalogTargets
    NAMESPACE insight::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/insight_metalog
)

###########################################################
#  Cross-package module shipping (Argos — §10.7/§10.9)
###########################################################
# Ship the package's OWN cmake config (carries FILE_SET CXX_MODULES); conan
# defers via cmake_find_mode=none. The config find_dependency()s canon + picosha2
# (the export-visible link deps); glaze is deliberately ABSENT (build-only, above).
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

if(INSIGHT_METALOG_BUILD_TESTS)
    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/*.cpp"
    )

    if(TEST_SOURCES)
        include(CTest)
        find_package(GTest REQUIRED CONFIG)
        enable_testing()

        add_executable(insight_metalog_tests ${TEST_SOURCES})
        target_link_libraries(insight_metalog_tests PRIVATE
            insight_metalog
            glaze::glaze
            picosha2::picosha2   # SHA-256 for the document determinism golden
            GTest::gtest
            GTest::gtest_main
        )
        target_include_directories(insight_metalog_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
        )
        include(GoogleTest)
        gtest_discover_tests(insight_metalog_tests)

        # Module-import proof (§8.1 / cxx_modules_migration_contract §10.15): import
        # the wrapper + exercise a src-defined fn → proves it is soundly importable,
        # both toolchains. Links the MODULE target so CMake scans the .cppm.
        add_executable(insight_metalog_module_test
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/module/test_metalog_module_import.cpp")
        target_link_libraries(insight_metalog_module_test PRIVATE
            insight_metalog_module GTest::gtest GTest::gtest_main)
        gtest_discover_tests(insight_metalog_module_test)
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
        add_executable(insight_metalog_bench ${BENCH_SOURCES})
        target_link_libraries(insight_metalog_bench PRIVATE
            insight_metalog
            benchmark::benchmark
            spdlog::spdlog
        )
        target_include_directories(insight_metalog_bench PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
        )
    endif()
endif()
