# A consumer of an INSTALLED innertube (tests/install.cmake builds it): find_package only,
# no source tree, C++17 like any consumer.
cmake_minimum_required(VERSION 3.20)
project(innertube_consumer LANGUAGES CXX)
find_package(innertube 0.1 REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE innertube::innertube)
set_target_properties(consumer PROPERTIES CXX_STANDARD 17)
