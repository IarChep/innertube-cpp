# Examples. threaded-transport: a self-driven reference HTTP backend (one blocking
# curl_easy_perform per request on its own thread) checked against the conformance suite.
if(INNERTUBE_BUILD_TESTS)
    if(NOT TARGET CURL::libcurl)
        find_package(CURL 7.68)
    endif()
    if(TARGET CURL::libcurl)
        add_executable(tst_conformance_threaded
            threaded-transport/tst_conformance_threaded.cpp
            threaded-transport/threadedtransport.cpp)
        target_link_libraries(tst_conformance_threaded PRIVATE innertube_conformance innertube_doctest CURL::libcurl)
        set_target_properties(tst_conformance_threaded PROPERTIES CXX_STANDARD 23)
        add_test(NAME tst_conformance_threaded COMMAND tst_conformance_threaded)
    endif()
endif()

# yt-cli: the public API with no framework (live network; not a ctest).
add_executable(yt-cli yt-cli/main.cpp)
target_link_libraries(yt-cli PRIVATE innertube)
set_target_properties(yt-cli PROPERTIES CXX_STANDARD 23)

# Single-file public-API examples (live network; not ctests). All drive the Client
# from main() with ownThread=false + runOnce().
foreach(_ex watch channel suggest)
    add_executable(yt-${_ex} ${_ex}/main.cpp)
    target_link_libraries(yt-${_ex} PRIVATE innertube)
endforeach()
