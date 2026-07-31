# Cross toolchain for building the experimental GooseBoot firmware from Linux.
#
# The firmware targets are PE/COFF freestanding images, so they need the same
# mingw-w64 GNU toolchain the Windows instructions use. On Debian or Ubuntu:
#
#   apt-get install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 \
#                   binutils-mingw-w64-x86-64
#
# Then configure with:
#
#   cmake -S boot -B build-firmware -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=boot/cmake/toolchain-mingw-w64.cmake \
#         -DGOOSEBOOT_BUILD_FIRMWARE=ON -DGOOSEBOOT_BUILD_PREVIEW=OFF
#
# The system binutils `ld`, `objcopy` and `objdump` are used, which is what the
# firmware rules expect; Ubuntu ships them with the pei-i386 and pei-x86-64
# targets and the i386pe/i386pep emulations already enabled.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_ASM_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
