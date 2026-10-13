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
        target_compile_options(tst_conformance_threaded PRIVATE -std=c++23)
        add_test(NAME tst_conformance_threaded COMMAND tst_conformance_threaded)
    endif()
endif()
