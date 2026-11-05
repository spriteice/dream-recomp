# Cross-compile for Windows x64 from Linux: clang targeting MinGW-w64, linked with lld.
#
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-clang.cmake \
#     -DDREAM_WIN_PREFIX=/path/to/prefix -DDREAM_HOST_TRANSLATE=/path/to/dream-translate
#
# DREAM_WIN_PREFIX holds what was built for Windows: SDL3, and a Vulkan import library made from
# vulkan-1.def in this folder. docs/dev-setup.md, "Cross-building for Windows", has the steps.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(_triple x86_64-w64-mingw32)

# CMake reads a toolchain file again for each try_compile, where only the environment and the
# variables named in CMAKE_TRY_COMPILE_PLATFORM_VARIABLES survive.
if(NOT DEFINED DREAM_WIN_PREFIX AND DEFINED ENV{DREAM_WIN_PREFIX})
  set(DREAM_WIN_PREFIX "$ENV{DREAM_WIN_PREFIX}")
endif()
set(DREAM_WIN_PREFIX "${DREAM_WIN_PREFIX}" CACHE PATH "Libraries built for Windows (SDL3, Vulkan import)")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES DREAM_WIN_PREFIX)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET ${_triple})
set(CMAKE_CXX_COMPILER_TARGET ${_triple})
set(CMAKE_RC_COMPILER ${_triple}-windres)
find_program(CMAKE_AR ${_triple}-ar REQUIRED)
find_program(CMAKE_RANLIB ${_triple}-ranlib REQUIRED)
find_program(CMAKE_DLLTOOL ${_triple}-dlltool REQUIRED)

# MinGW-w64's fmaf isn't fused: it differed from the CPU's FMA in about half of a million random
# cases, which made a Windows build drift from Linux. -mfma makes std::fmaf the instruction, and
# runtime/include/dream/runtime/sh4/ops.h refuses a MinGW build without it. -ffp-contract=off
# (DreamTargetDefaults.cmake) keeps this from fusing anything but the explicit fmaf. The build then
# needs a CPU with FMA3.
set(CMAKE_C_FLAGS_INIT "-mfma")
set(CMAKE_CXX_FLAGS_INIT "-mfma")

set(CMAKE_FIND_ROOT_PATH /usr/${_triple} ${DREAM_WIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -static-libgcc -static-libstdc++ -Wl,-Bstatic -lpthread -Wl,-Bdynamic")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld -static-libgcc -static-libstdc++")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld -static-libgcc -static-libstdc++")
