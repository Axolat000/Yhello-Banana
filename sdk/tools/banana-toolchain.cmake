# SPDX-License-Identifier: BSD-2-Clause
# banana-toolchain.cmake - CMake's toolchain file for Banana OS programs:
#
#     cmake -DCMAKE_TOOLCHAIN_FILE=sdk/tools/banana-toolchain.cmake ...
#
# The compiler is banana-cc (x86_64; BANANA_ARCH=i686 for 32-bit): static
# programs and libraries only. CMAKE_PREFIX_PATH says where the other
# libraries built for Banana OS are; the build machine's own (/usr,
# /usr/local) are never found.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/banana-cc")
set(CMAKE_AR ar CACHE FILEPATH "")
set(CMAKE_RANLIB ranlib CACHE FILEPATH "")
set(CMAKE_SYSTEM_IGNORE_PREFIX_PATH /usr /usr/local /)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# (a program of the build machine's pkg-config would name its libraries)
set(PKG_CONFIG_EXECUTABLE "" CACHE FILEPATH "")
