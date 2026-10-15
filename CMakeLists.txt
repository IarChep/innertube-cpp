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
option(INNERTUBE_INSTALL "Generate install rules (find_package config + pkg-config file)" ${_it_top})
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

# Glaze: PRIVATE, header-only. Reuse a consumer's `glaze` target if present (build-only:
# an installed innertube needs nothing of it).
if(TARGET glaze)
    target_link_libraries(innertube PRIVATE $<BUILD_INTERFACE:glaze>)
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

# The engine runs on a std::thread (old glibc, e.g. the N9's, needs -pthread to link it).
find_package(Threads REQUIRED)
target_link_libraries(innertube PRIVATE Threads::Threads)

# Install: headers, the static library, innertubeConfig.cmake (find_package(innertube) →
# innertube::innertube) and innertube.pc. 0.x: a minor release may break the API.
if(INNERTUBE_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    install(TARGETS innertube EXPORT innertubeTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/yt DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    set(_it_cmakedir ${CMAKE_INSTALL_LIBDIR}/cmake/innertube)
    install(EXPORT innertubeTargets NAMESPACE innertube:: DESTINATION ${_it_cmakedir})
    if(INNERTUBE_HTTP_BACKEND STREQUAL "curl")
        set(INNERTUBE_NEEDS_CURL TRUE)
        set(INNERTUBE_PC_REQUIRES libcurl)
    else()
        set(INNERTUBE_NEEDS_CURL FALSE)
        set(INNERTUBE_PC_REQUIRES "")
    endif()
    string(REPLACE ";" " " INNERTUBE_PC_LIBS "${INNERTUBE_HTTP_BACKEND_LIBS}")
    configure_package_config_file(cmake/innertubeConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/innertubeConfig.cmake INSTALL_DESTINATION ${_it_cmakedir})
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/innertubeConfigVersion.cmake
        COMPATIBILITY SameMinorVersion)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/innertubeConfig.cmake
                  ${CMAKE_CURRENT_BINARY_DIR}/innertubeConfigVersion.cmake DESTINATION ${_it_cmakedir})
    # Relocatable: the prefix is found from the .pc's own place (cmake --install --prefix,
    # an SDK sysroot), unless the install dirs were given as absolute paths.
    file(RELATIVE_PATH _it_up /p/${CMAKE_INSTALL_LIBDIR}/pkgconfig /p)
    string(REGEX REPLACE "/$" "" _it_up "${_it_up}")
    set(INNERTUBE_PC_PREFIX "\${pcfiledir}/${_it_up}")
    foreach(_d LIBDIR INCLUDEDIR)
        if(IS_ABSOLUTE "${CMAKE_INSTALL_${_d}}")
            set(INNERTUBE_PC_${_d} "${CMAKE_INSTALL_${_d}}")
        else()
            set(INNERTUBE_PC_${_d} "\${prefix}/${CMAKE_INSTALL_${_d}}")
        endif()
    endforeach()
    configure_file(cmake/innertube.pc.in ${CMAKE_CURRENT_BINARY_DIR}/innertube.pc @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/innertube.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

# What every backend must pass — a plain static lib (no test framework) so backend
# authors can run it from any test runner.
add_library(innertube_conformance STATIC conformance/transport_conformance.cpp)
target_link_libraries(innertube_conformance PUBLIC innertube)
target_include_directories(innertube_conformance PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(innertube_conformance PROPERTIES CXX_STANDARD 23)
target_link_libraries(innertube_conformance PUBLIC Threads::Threads)

if(INNERTUBE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(INNERTUBE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
