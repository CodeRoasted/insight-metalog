cmake_minimum_required(VERSION 3.28)

project(insight_metalog_test_package LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(insight_metalog REQUIRED CONFIG)
find_package(GTest REQUIRED CONFIG)
find_package(glaze REQUIRED CONFIG)

include(CTest)

add_executable(insight_metalog_test_package test_package.cpp)
target_link_libraries(insight_metalog_test_package PRIVATE
    insight::metalog
    glaze::glaze
    GTest::gtest
    GTest::gtest_main
)

enable_testing()
include(GoogleTest)
gtest_discover_tests(insight_metalog_test_package)
