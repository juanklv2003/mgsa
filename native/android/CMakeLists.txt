cmake_minimum_required(VERSION 3.22)
project(mgsa_touch C CXX)

# Android 15+ on the Pixel 9a rejects 4 KiB-aligned LOAD segments.
set(ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(_mgsa_ndk "${CMAKE_ANDROID_NDK}")
if(NOT _mgsa_ndk)
    set(_mgsa_ndk "$ENV{ANDROID_NDK}")
endif()
if(NOT _mgsa_ndk)
    get_filename_component(_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_prebuilt "${_bin}/../.." ABSOLUTE)
    get_filename_component(_mgsa_ndk "${_prebuilt}/../../.." ABSOLUTE)
endif()
set(GLUE "${_mgsa_ndk}/sources/android/native_app_glue/android_native_app_glue.c")
if(NOT EXISTS "${GLUE}")
    message(FATAL_ERROR "android_native_app_glue.c not found at ${GLUE}")
endif()

add_library(mgsa_touch SHARED
    ../src/game.cpp
    ../src/render.cpp
    jni/android_main.cpp
    "${GLUE}"
)
target_include_directories(mgsa_touch PRIVATE
    ../include
    "${_mgsa_ndk}/sources/android/native_app_glue"
)
target_compile_options(mgsa_touch PRIVATE -O2)

find_library(ANDROID_LOG log)
target_link_libraries(mgsa_touch ${ANDROID_LOG} android dl)
