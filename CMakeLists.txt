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
        glaze::glaze
        picosha2::picosha2
)

include(GNUInstallDirs)
install(TARGETS insight_metalog
    EXPORT insight_metalogTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY api/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT insight_metalogTargets
    NAMESPACE insight::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/insight_metalog
)

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
            GTest::gtest
            GTest::gtest_main
        )
        target_include_directories(insight_metalog_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
        )
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
