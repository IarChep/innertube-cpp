cmake_minimum_required(VERSION 3.20)   # CXX_STANDARD 23
project(innertube VERSION 0.1.0 LANGUAGES CXX)
# Standards go through CXX_STANDARD (portable: GCC, Clang, MSVC), strict ISO dialects.
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_it_top ON)
else()
    set(_it_top OFF)
endif()
option(INNERTUBE_BUILD_TESTS "Build the innertube test suites" ${_it_top})
option(INNERTUBE_BUILD_EXAMPLES "Build the examples (and their conformance tests)" ${_it_top})
set(INNERTUBE_GLAZE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glaze" CACHE PATH
    "Glaze source root (contains include/glaze)")

add_library(innertube STATIC
    src/version.cpp
    src/innertube/clientconfig.cpp
    src/parsers/textmatch.cpp
    src/parsers/continuation.cpp
    src/parsers/videolistparser.cpp
    src/parsers/watchparser.cpp
    src/parsers/playlistparser.cpp
    src/parsers/channelparser.cpp
    src/parsers/commentparser.cpp
    src/parsers/accountparser.cpp
    src/parsers/playerparser.cpp
    src/parsers/suggestparser.cpp
    src/innertube/bodies.cpp
    src/innertube/contextbuilder.cpp
    src/innertube/playerjs.cpp
    src/json/json3.cpp
    src/net/loop.cpp
    src/net/url.cpp
    src/core/pct.cpp
    src/net/httpclient.cpp
    src/core/chains.cpp
    src/catalog.cpp
    src/player.cpp
    src/link.cpp
    src/client.cpp
    src/groups.cpp
    src/auth.cpp)
add_library(innertube::innertube ALIAS innertube)
# The impl needs C++23 (Glaze v7); consumers only see the C++17 floor (the PUBLIC feature).
set_target_properties(innertube PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 23)
target_compile_features(innertube PUBLIC cxx_std_17)
target_include_directories(innertube
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

# Glaze: PRIVATE, header-only. Reuse a consumer's `glaze` target if present.
if(TARGET glaze)
    target_link_libraries(innertube PRIVATE glaze)
else()
    target_include_directories(innertube SYSTEM PRIVATE ${INNERTUBE_GLAZE_DIR}/include)
endif()

# HTTP backend (include/yt/transport.h, docs/backends.md):
#   curl    backends/curl — libcurl multi on the engine thread (default). A consumer
#           may pre-define CURL::libcurl (MeeTube's N9 build points it at the static
#           curl+wolfSSL archives); otherwise the system libcurl is found.
#   none    no backend compiled in: ClientOptions::transport must be supplied.
#   <path>  your own .cpp defining yt::makeDefaultTransport (+ INNERTUBE_HTTP_BACKEND_LIBS).
set(INNERTUBE_HTTP_BACKEND "curl" CACHE STRING
    "HTTP backend: curl, none, or the path of a .cpp defining yt::makeDefaultTransport")
set(INNERTUBE_HTTP_BACKEND_LIBS "" CACHE STRING "Extra link libraries for a custom backend")
if(INNERTUBE_HTTP_BACKEND STREQUAL "curl")
    if(NOT TARGET CURL::libcurl)
        find_package(CURL 7.68 REQUIRED)
    endif()
    target_sources(innertube PRIVATE backends/curl/curltransport.cpp backends/curl/default.cpp)
    target_link_libraries(innertube PRIVATE CURL::libcurl)
elseif(INNERTUBE_HTTP_BACKEND STREQUAL "none")
    target_sources(innertube PRIVATE backends/none/default.cpp)
else()
    get_filename_component(_it_backend "${INNERTUBE_HTTP_BACKEND}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(NOT EXISTS "${_it_backend}")
        message(FATAL_ERROR "INNERTUBE_HTTP_BACKEND: no such file: ${INNERTUBE_HTTP_BACKEND}")
    endif()
    target_sources(innertube PRIVATE "${_it_backend}")
    target_link_libraries(innertube PRIVATE ${INNERTUBE_HTTP_BACKEND_LIBS})
endif()
message(STATUS "innertube HTTP backend: ${INNERTUBE_HTTP_BACKEND}")

# What every backend must pass — a plain static lib (no test framework) so backend
# authors can run it from any test runner.
add_library(innertube_conformance STATIC conformance/transport_conformance.cpp)
target_link_libraries(innertube_conformance PUBLIC innertube)
target_include_directories(innertube_conformance PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(innertube_conformance PROPERTIES CXX_STANDARD 23)
find_package(Threads REQUIRED)
target_link_libraries(innertube_conformance PUBLIC Threads::Threads)

if(INNERTUBE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(INNERTUBE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
