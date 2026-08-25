# Copyright © 2026 Arm Limited.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.22)
project(ArmAsrSample LANGUAGES C CXX)

set(SAMPLE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(ARM_ASR_ROOT ${SAMPLE_ROOT}/../../../../../.. ABSOLUTE)
set(COMMON_DIR ${ARM_ASR_ROOT}/samples/common)
set(SAMPLE_CMAKE_DIR ${COMMON_DIR}/cmake)
set(SHADER_DIR ${COMMON_DIR}/shaders)

# ---------------------------------------------------------------------------
# Arm ASR
#
# The Android defaults in the root CMakeLists build the standalone Vulkan
# backend against the checked-in prebuilt SPIR-V and link the NDK's libvulkan,
# so no extra options are needed here.
add_subdirectory(${ARM_ASR_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/arm_asr)

# ---------------------------------------------------------------------------
# Sample shaders -> SPIR-V -> embedded C arrays
find_program(GLSLC_EXECUTABLE glslc
    HINTS
        ${ANDROID_NDK}/shader-tools/darwin-x86_64
        ${ANDROID_NDK}/shader-tools/linux-x86_64
        ${ANDROID_NDK}/shader-tools/windows-x86_64
    REQUIRED)
message(STATUS "Using glslc: ${GLSLC_EXECUTABLE}")

set(GENERATED_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${GENERATED_SHADER_DIR})

set(SHADER_HEADERS)
include(${SAMPLE_CMAKE_DIR}/sample_shaders.cmake)
compile_all_sample_shaders()

# ---------------------------------------------------------------------------
# native_app_glue
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue)

# ---------------------------------------------------------------------------
# The sample itself
add_library(armasrsample SHARED
    main.cpp
    vk_context.cpp
    ${COMMON_DIR}/scene.cpp
    ${COMMON_DIR}/asr_upscaler.cpp)

add_dependencies(armasrsample sample_shaders)

target_include_directories(armasrsample PRIVATE
    ${SAMPLE_ROOT}
    ${COMMON_DIR}
    ${GENERATED_SHADER_DIR})

target_compile_features(armasrsample PRIVATE cxx_std_20)
target_compile_options(armasrsample PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

target_link_libraries(armasrsample PRIVATE
    Arm_ASR_api
    Arm_ASR_backend
    native_app_glue
    android
    vulkan
    log)

# NativeActivity looks the entry point up by name, so it must survive --gc-sections.
target_link_options(armasrsample PRIVATE "-u" "ANativeActivity_onCreate")
