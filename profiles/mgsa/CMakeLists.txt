cmake_minimum_required(VERSION 3.20)

# Minimal bring-up profile: no renderer, no audio, no SDL, no Vulkan.
# Generates nothing on its own; scripts/bringup.ps1 produces generated/ with psp_recomp.

set(MGSA_PROFILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(MGSA_GENERATED "")
file(GLOB MGSA_GENERATED CONFIGURE_DEPENDS "${MGSA_PROFILE_DIR}/generated/*.cpp")

if(NOT MGSA_GENERATED)
    message(FATAL_ERROR "mgsa: no generated AOT units in ${MGSA_PROFILE_DIR}/generated; run scripts/bringup.ps1")
endif()
list(LENGTH MGSA_GENERATED MGSA_GENERATED_COUNT)
message(STATUS "mgsa: ${MGSA_GENERATED_COUNT} generated AOT units")

# The corpus is large; -O0 keeps iteration fast and the first bring-up honest.
set_source_files_properties(${MGSA_GENERATED} PROPERTIES COMPILE_OPTIONS "-O0;-g0")

add_library(mgsa_generated OBJECT ${MGSA_GENERATED})
target_include_directories(mgsa_generated PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${MGSA_PROFILE_DIR}/generated")

add_executable(mgsa_boot
    host/main.cpp
    $<TARGET_OBJECTS:mgsa_generated>)

target_include_directories(mgsa_boot PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${MGSA_PROFILE_DIR}/generated")

target_link_libraries(mgsa_boot PRIVATE psprecomp_core ${CMAKE_DL_LIBS})

find_package(Threads REQUIRED)
target_link_libraries(mgsa_boot PRIVATE Threads::Threads)

set_target_properties(mgsa_boot PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
