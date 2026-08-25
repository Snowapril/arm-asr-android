# Copyright © 2026 Arm Limited.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.22)
project(ArmAsrWindowsSample LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "samples/windows targets Windows. Use samples/android on Android hosts.")
endif()

set(SAMPLE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(ARM_ASR_ROOT ${SAMPLE_ROOT}/../.. ABSOLUTE)
set(COMMON_DIR ${ARM_ASR_ROOT}/samples/common)
set(SAMPLE_CMAKE_DIR ${COMMON_DIR}/cmake)
set(SHADER_DIR ${COMMON_DIR}/shaders)

# ---------------------------------------------------------------------------
# Vulkan SDK: supplies the headers, vulkan-1.lib, and glslc.
find_package(Vulkan REQUIRED)
message(STATUS "Vulkan headers: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan library: ${Vulkan_LIBRARIES}")

# ---------------------------------------------------------------------------
# Arm ASR
#
# The non-Android defaults in the root CMakeLists would compile the standalone
# backend away entirely and route entry points through volk. Neither suits a
# desktop sample:
#   * without the backend there is no Arm_ASR_backend target to link;
#   * the tree never calls volkInitialize()/volkLoadDevice() and does not vendor
#     volk, so the volk path links but leaves the backend's function table full
#     of nulls at dispatch time.
# Prebuilt shaders are forced on so the build does not depend on the
# Windows-only FidelityFX_SC.exe toolchain; -DFFXM_USE_PREBUILT_SHADERS=OFF
# still works if you want to exercise it.
set(FFXM_REMOVE_ARM_ASR_VK_STANDALONE_BACKEND OFF CACHE BOOL "" FORCE)
set(FFXM_VKLOADER_VOLK                        OFF CACHE BOOL "" FORCE)
set(FFXM_USE_PREBUILT_SHADERS                 ON  CACHE BOOL "" FORCE)
# The backend uses directory-scoped include_directories(), so its include paths
# do not propagate to this target - it needs FFXM_VULKAN_PATH set explicitly.
set(FFXM_VULKAN_PATH "${Vulkan_INCLUDE_DIRS}" CACHE STRING "" FORCE)

add_subdirectory(${ARM_ASR_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/arm_asr)

# ---------------------------------------------------------------------------
# Sample shaders -> SPIR-V -> embedded C arrays
#
# find_program rather than the Vulkan::glslc imported target, which needs
# CMake 3.24; this tree's floor is 3.22.
find_program(GLSLC_EXECUTABLE glslc
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "${Vulkan_INCLUDE_DIR}/../Bin"
    REQUIRED)
message(STATUS "Using glslc: ${GLSLC_EXECUTABLE}")

set(GENERATED_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${GENERATED_SHADER_DIR})

set(SHADER_HEADERS)
include(${SAMPLE_CMAKE_DIR}/sample_shaders.cmake)
compile_all_sample_shaders()

# ---------------------------------------------------------------------------
# The sample itself
add_executable(ArmAsrSample
    src/main.cpp
    src/vk_context.cpp
    ${COMMON_DIR}/scene.cpp
    ${COMMON_DIR}/asr_upscaler.cpp)

add_dependencies(ArmAsrSample sample_shaders)

target_include_directories(ArmAsrSample PRIVATE
    ${SAMPLE_ROOT}/src
    ${COMMON_DIR}
    ${GENERATED_SHADER_DIR})

target_compile_features(ArmAsrSample PRIVATE cxx_std_20)

# Vulkan::Vulkan links vulkan-1.lib, which resolves both this sample's own calls
# and the seven vk* symbols the backend references directly rather than through
# its VKFunctionTable (vkGetDeviceProcAddr, vkEnumerateDeviceExtensionProperties,
# vkGetPhysicalDeviceMemoryProperties, vkGetPhysicalDeviceProperties2,
# vkGetPhysicalDeviceFeatures2, vkCmdSetViewport, vkCmdSetScissor).
target_link_libraries(ArmAsrSample PRIVATE
    Arm_ASR_api
    Arm_ASR_backend
    Vulkan::Vulkan)

if(MSVC)
    # 4127: conditional expression is constant, 4324: structure padded due to
    # alignas - both are raised by the Vulkan headers, not by sample code.
    target_compile_options(ArmAsrSample PRIVATE /W4 /wd4127 /wd4324)
endif()

# Visual Studio: make this the project that F5 launches.
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ArmAsrSample)
