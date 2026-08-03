cmake_minimum_required(VERSION 3.20)
project(MANXLARush VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MANX_FRAMEWORK_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../MANXFramework" CACHE PATH
    "Path to a MANXFramework source checkout")
if(EXISTS "${MANX_FRAMEWORK_SOURCE_DIR}/CMakeLists.txt")
    set(MANX_FRAMEWORK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${MANX_FRAMEWORK_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/_deps/manx-framework" EXCLUDE_FROM_ALL)
else()
    find_package(MANXFramework CONFIG REQUIRED)
endif()

add_executable(LARushProbe src/larush_probe.cpp)
target_link_libraries(LARushProbe PRIVATE MANXFramework::XboxD3D8)
target_compile_options(LARushProbe PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME larush_xbe_parser_test COMMAND LARushProbe --self-test)
