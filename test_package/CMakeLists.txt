cmake_minimum_required(VERSION 3.28)

# This consumer imports insight.metalog (a pure module built against the stdlib `std`
# module), so it must scan for modules AND build/provide the same `std` module. 451f2fe2-… is
# CMake 4.3.1's CMAKE_EXPERIMENTAL_CXX_IMPORT_STD (match the main lib + conan tool-cmake), set
# before project().
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

project(insight_metalog_test_package LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(insight_metalog REQUIRED CONFIG)
# The test TU imports insight.canon directly (Timestamp/LogLevel/CanonicalEvent are
# reachable-not-visible through insight.metalog), so the consumer must find_package +
# link canon for CMake to provide the insight.canon BMI/module-map to this TU.
find_package(insight_canon REQUIRED CONFIG)
find_package(GTest REQUIRED CONFIG)
find_package(glaze REQUIRED CONFIG)

include(CTest)

add_executable(insight_metalog_test_package test_package.cpp)
target_link_libraries(insight_metalog_test_package PRIVATE
    insight::metalog
    insight::canon
    glaze::glaze
    GTest::gtest
    GTest::gtest_main
)
target_compile_features(insight_metalog_test_package PRIVATE cxx_std_23)
set_target_properties(insight_metalog_test_package PROPERTIES
    CXX_SCAN_FOR_MODULES ON CXX_MODULE_STD ON)

enable_testing()
include(GoogleTest)
gtest_discover_tests(insight_metalog_test_package)
