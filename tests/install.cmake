# Installs the built innertube into a scratch prefix, then builds tests/install/ against it
# twice: with find_package(innertube) and with pkg-config. Run as:
#   cmake -DBIN=<build dir> -DSRC=<repo root> -DCXX=<compiler> -DFLAGS=<CXX flags>
#         -DGEN=<generator> -P install.cmake
set(_root "${BIN}/install-test")
file(REMOVE_RECURSE "${_root}")
function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
    if(NOT _rc EQUAL 0)
        string(REPLACE ";" " " _cmd "${ARGN}")
        message(FATAL_ERROR "failed: ${_cmd}\n${_out}")
    endif()
endfunction()

run(${CMAKE_COMMAND} --install "${BIN}" --prefix "${_root}/prefix")

run(${CMAKE_COMMAND} -S "${SRC}/tests/install" -B "${_root}/cmake" -G "${GEN}"
    "-DCMAKE_PREFIX_PATH=${_root}/prefix" "-DCMAKE_CXX_COMPILER=${CXX}" "-DCMAKE_CXX_FLAGS=${FLAGS}")
run(${CMAKE_COMMAND} --build "${_root}/cmake")
run("${_root}/cmake/consumer")

find_program(_pkgconfig pkg-config)
if(_pkgconfig)
    file(GLOB _pcdirs "${_root}/prefix/*/pkgconfig" "${_root}/prefix/*/*/pkgconfig")
    string(REPLACE ";" ":" _pcpath "${_pcdirs}")
    set(ENV{PKG_CONFIG_PATH} "${_pcpath}")
    execute_process(COMMAND ${_pkgconfig} --cflags --libs innertube
                    RESULT_VARIABLE _rc OUTPUT_VARIABLE _pc ERROR_VARIABLE _err OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "pkg-config --cflags --libs innertube: ${_err}")
    endif()
    separate_arguments(_pc UNIX_COMMAND "${_pc}")
    separate_arguments(_flags UNIX_COMMAND "${FLAGS}")
    run(${CXX} -std=c++17 ${_flags} "${SRC}/tests/install/main.cpp" -o "${_root}/consumer-pc" ${_pc})
    run("${_root}/consumer-pc")
else()
    message(STATUS "pkg-config not found: the .pc check is skipped")
endif()
