cmake_minimum_required(VERSION 3.20)
project(MANXLARush VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
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

find_package(SDL3 REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

add_library(larush_port STATIC
    src/larush_k9_vfs.c
    src/larush_kernel_shim.c)
target_include_directories(larush_port PUBLIC src)
target_compile_definitions(larush_port PRIVATE _GNU_SOURCE)
target_link_libraries(larush_port PUBLIC
    ZLIB::ZLIB Threads::Threads ${CMAKE_DL_LIBS})

add_executable(LARushProbe src/larush_probe.cpp)
target_link_libraries(LARushProbe PRIVATE MANXFramework::XboxD3D8)

add_executable(LARushFirstBoot src/larush_first_boot.c)
target_link_libraries(LARushFirstBoot PRIVATE larush_port)

add_executable(LARushD3DProbe src/larush_game_native.c)
target_link_libraries(LARushD3DProbe PRIVATE
    larush_port MANXFramework::XboxD3D8)

add_executable(LARushK9Test tools/larush_k9_test.c)
target_link_libraries(LARushK9Test PRIVATE larush_port)

add_executable(LARushBoot tools/larush_boot.c)
target_include_directories(LARushBoot PRIVATE tools)
target_link_libraries(LARushBoot PRIVATE larush_port SDL3::SDL3)

enable_testing()
add_executable(larush_k9_unit_test tests/larush_k9_unit_test.c)
target_link_libraries(larush_k9_unit_test PRIVATE larush_port)
add_test(NAME larush_k9_unit_test COMMAND larush_k9_unit_test)
add_test(NAME larush_xbe_parser_test COMMAND LARushProbe --self-test)
add_test(NAME larush_first_boot_self_test COMMAND LARushFirstBoot --self-test)

foreach(target LARushProbe LARushFirstBoot LARushD3DProbe LARushK9Test
               LARushBoot larush_k9_unit_test)
    target_compile_options(${target} PRIVATE
        -O2 -Wall -Wextra -Wpedantic -UNDEBUG -Wno-unused-function)
endforeach()
