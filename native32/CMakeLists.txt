# native32/CMakeLists.txt — the 32-bit native-execution harness.
#
# Built as a separate ABI via ExternalProject from the top-level project
# (a single CMake build tree cannot mix 64- and 32-bit targets cleanly),
# driven by cmake/i686-linux.cmake.

cmake_minimum_required(VERSION 3.20)
project(LARushNative32 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# Shared, ABI-neutral sources reused from the 64-bit tree.
set(SHARED_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/larush_k9_vfs.c)

add_executable(LARushNative
    src/nat_main.c
    src/nat_arena.c
    src/nat_fs.c
    src/nat_stubs.c
    ${SHARED_SRC})
target_include_directories(LARushNative PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(LARushNative PRIVATE _GNU_SOURCE)
target_link_libraries(LARushNative PRIVATE ZLIB::ZLIB Threads::Threads)

# -no-pie + a high text segment keep the harness ELF out of the guest
# arena window [0x10000, 0x4000000); the default i386 non-PIE base of
# 0x08048000 sits inside it.
target_link_options(LARushNative PRIVATE
    -no-pie -Wl,-Ttext-segment=0x60000000)

target_compile_options(LARushNative PRIVATE
    -O2 -Wall -Wextra -Wno-unused-function)
