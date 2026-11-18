cmake_minimum_required(VERSION 3.22)
project(mgsa_android CXX)

# std::bit_cast and other C++20 facilities are used throughout the runtime.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build with the Android NDK toolchain:
#   cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
#         -S . -B build-android
# The AOT corpus and framework live in work/tools/Yakumo, copied into the
# source tree by scripts/bringup.ps1. This CMake file only wires them into a
# shared library the NativeActivity loads.

set(YAKUMO "${CMAKE_CURRENT_SOURCE_DIR}/../../../work/tools/Yakumo")
set(MGSA_HOST "${YAKUMO}/profiles/mgsa/host")
set(MGSA_GEN "${YAKUMO}/profiles/mgsa/generated")

file(GLOB AOT_UNITS "${MGSA_GEN}/generated_unit_*.cpp")
file(GLOB AOT_REGISTRY "${MGSA_GEN}/generated_registry.cpp")
if(NOT AOT_UNITS)
    message(FATAL_ERROR "No AOT units in ${MGSA_GEN}; run scripts/bringup.ps1 first")
endif()

add_library(mgsa SHARED
    ${MGSA_HOST}/main.cpp
    ${MGSA_HOST}/touch.cpp
    ${MGSA_HOST}/framebuffer.cpp
    ${AOT_UNITS}
    ${AOT_REGISTRY}
    ${CMAKE_CURRENT_SOURCE_DIR}/jni/mgsa_jni.cpp
    ${YAKUMO}/src/runtime.cpp
    ${YAKUMO}/src/interpreter.cpp
    ${YAKUMO}/src/guest_memory.cpp
    ${YAKUMO}/src/elf32.cpp
    ${YAKUMO}/src/decoder.cpp
    ${YAKUMO}/src/deflate.cpp
    ${YAKUMO}/src/nid_registry.cpp
    ${YAKUMO}/src/program_analysis.cpp
    ${YAKUMO}/src/sha256.cpp
)

target_include_directories(mgsa PRIVATE
    ${YAKUMO}/include
    ${MGSA_GEN}
    ${MGSA_HOST}
)

target_compile_definitions(mgsa PRIVATE MGSA_ANDROID_LIBRARY)
# The corpus is large; -O0 matches the desktop bring-up build. The host uses
# C++ exceptions for error handling, so keep them enabled.
target_compile_options(mgsa PRIVATE -O0)
set_source_files_properties(${AOT_UNITS} PROPERTIES COMPILE_OPTIONS "-O0")

# android_native_app_glue is source shipped with the NDK, not a prebuilt lib.
set(GLUE "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
if(EXISTS "${GLUE}")
    target_sources(mgsa PRIVATE "${GLUE}")
    target_include_directories(mgsa PRIVATE "${ANDROID_NDK}/sources/android/native_app_glue")
endif()

find_library(ANDROID_LOG log)
target_link_libraries(mgsa ${ANDROID_LOG} android dl)
