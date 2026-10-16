add_library(innertube_doctest STATIC doctest.cpp)
target_include_directories(innertube_doctest PUBLIC ${PROJECT_SOURCE_DIR}/third_party/doctest)

# it_test(name [STD17] [extra sources...]) — one doctest suite. STD17 compiles the
# suite at C++17 (public-header checks); otherwise C++23 like the library.
function(it_test name)
    set(_std 23)
    set(_srcs ${ARGN})
    if(_srcs AND "${ARGV1}" STREQUAL "STD17")
        set(_std 17)
        list(REMOVE_AT _srcs 0)
    endif()
    add_executable(${name} ${name}.cpp ${_srcs})
    target_link_libraries(${name} PRIVATE innertube innertube_doctest)
    set_target_properties(${name} PROPERTIES CXX_STANDARD ${_std})
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
    if(TARGET glaze)
        target_link_libraries(${name} PRIVATE glaze)
    else()
        target_include_directories(${name} SYSTEM PRIVATE ${INNERTUBE_GLAZE_DIR}/include)
    endif()
    target_compile_definitions(${name} PRIVATE IT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

it_test(tst_smoke)
it_test(tst_types STD17)
it_test(tst_link STD17)
it_test(tst_parsers)
it_test(tst_json STD17)
it_test(tst_bodies)
it_test(tst_context)
it_test(tst_log)
it_test(tst_loop)
it_test(tst_faketransport)
it_test(tst_url)
it_test(tst_http)
it_test(tst_fakehttp)
it_test(tst_chains)
it_test(tst_catalog)
it_test(tst_client)
target_link_libraries(tst_client PRIVATE innertube_conformance)
it_test(tst_auth)

add_test(NAME innertube_no_qt
         COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/no_qt.cmake)

if(INNERTUBE_HTTP_BACKEND STREQUAL "curl")
    it_test(tst_conformance_curl)
    target_link_libraries(tst_conformance_curl PRIVATE innertube_conformance)
    it_test(tst_curl_logging)
    target_link_libraries(tst_curl_logging PRIVATE innertube_conformance)
endif()

# The installed package works for a consumer (find_package and pkg-config).
if(INNERTUBE_INSTALL)
    add_test(NAME innertube_install
             COMMAND ${CMAKE_COMMAND} -DBIN=${PROJECT_BINARY_DIR} -DSRC=${PROJECT_SOURCE_DIR}
                     -DCXX=${CMAKE_CXX_COMPILER} "-DFLAGS=${CMAKE_CXX_FLAGS}" "-DGEN=${CMAKE_GENERATOR}"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/install.cmake)
endif()
