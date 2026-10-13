# Fails if any library source mentions Qt. Run as: cmake -DROOT=<repo root> -P no_qt.cmake
file(GLOB_RECURSE _srcs
    ${ROOT}/include/*.h ${ROOT}/src/*.h ${ROOT}/src/*.cpp
    ${ROOT}/backends/*.h ${ROOT}/backends/*.cpp ${ROOT}/media/*.h ${ROOT}/media/*.cpp)
set(_bad "")
foreach(f ${_srcs})
    file(STRINGS ${f} _hits REGEX "#include <Q[A-Za-z]|Q_OBJECT|qint64|qreal|QString")
    if(_hits)
        list(APPEND _bad "${f}: ${_hits}")
    endif()
endforeach()
if(_bad)
    message(FATAL_ERROR "Qt found in libinnertube:\n${_bad}")
endif()
