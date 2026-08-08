# i686-linux.cmake — 32-bit toolchain for the Stage C native-execution
# harness (LARushNative).  The retail L.A. Rush XBE is 32-bit x86; the
# harness runs that code in-process at its true virtual addresses, so it
# must itself be a 32-bit ELF.
#
# Usage: -DCMAKE_TOOLCHAIN_FILE=cmake/i686-linux.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(CMAKE_C_FLAGS_INIT   "-m32")
set(CMAKE_CXX_FLAGS_INIT "-m32")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-m32")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32")

# Resolve 32-bit libraries from the multilib path and keep find_package()
# away from the 64-bit ones (linking the 64-bit libz into a -m32 target
# fails at the linker with a cryptic "incompatible" error).
set(CMAKE_LIBRARY_PATH /usr/lib32)
set(CMAKE_FIND_ROOT_PATH /usr/lib32)
set(CMAKE_IGNORE_PATH /usr/lib /usr/lib64)
