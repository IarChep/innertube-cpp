cmake_minimum_required(VERSION 3.18)
project(innertube VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_it_top ON)
else()
    set(_it_top OFF)
endif()
option(INNERTUBE_BUILD_TESTS "Build the innertube test suites" ${_it_top})
set(INNERTUBE_GLAZE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glaze" CACHE PATH
    "Glaze source root (contains include/glaze)")

add_library(innertube STATIC
    src/version.cpp)
add_library(innertube::innertube ALIAS innertube)
set_target_properties(innertube PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(innertube PUBLIC cxx_std_17)
# The impl needs C++23 (Glaze v7); consumers only see the C++17 floor above.
target_compile_options(innertube PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=c++23>)
target_include_directories(innertube
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Glaze: PRIVATE, header-only. Reuse a consumer's `glaze` target if present.
if(TARGET glaze)
    target_link_libraries(innertube PRIVATE glaze)
else()
    target_include_directories(innertube SYSTEM PRIVATE ${INNERTUBE_GLAZE_DIR}/include)
endif()

# libcurl: a consumer may pre-define CURL::libcurl (MeeTube's N9 build points it at
# the static curl+wolfSSL archives); otherwise find the system one.
if(NOT TARGET CURL::libcurl)
    find_package(CURL 7.68 REQUIRED)
endif()
target_link_libraries(innertube PUBLIC CURL::libcurl)

if(INNERTUBE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
