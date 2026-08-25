<!-- Copyright © 2026 Arm Limited.
SPDX-License-Identifier: MIT -->

# Arm ASR Windows x64 Vulkan sample

A minimal Win32 app that renders a procedural scene at reduced resolution,
upscales it 2x with Arm ASR, and presents the result. CI already compiles the
library for Windows x64, but nothing there is ever *run* — this sample makes the
desktop path executable, and is the one place the Khronos validation layer
actually runs against the Arm ASR backend.

It shares `scene.*`, `asr_upscaler.*` and the GLSL shaders with the Android
sample; see [`samples/common`](../common). Only `main.cpp`, `vk_context.*` and
`common.h` are Windows-specific.

## Prerequisites

| | |
|---|---|
| Vulkan SDK | 1.3.296 or newer (`VULKAN_SDK` must be set — the installer does this). LunarG no longer serves older SDKs |
| Compiler | Visual Studio 2022 / MSVC v143, x64 |
| CMake | 3.22 or newer |
| GPU | Vulkan 1.2, or Vulkan 1.1 + `VK_KHR_spirv_1_4`, with `VK_KHR_shader_float16_int8` and 16-bit storage |

## Build and run

```sh
cmake -B build-sample -S samples/windows -A x64
cmake --build build-sample --config Debug
build-sample\Debug\ArmAsrSample.exe
```

Build `Debug` first when working on the library: MSVC defines `_DEBUG` on its
own, which turns on Arm ASR's internal asserts and debug resource naming.
(The Android sample has to inject `-D_DEBUG` by hand, because Clang does not
define it.)

Expected output on a supported GPU:

```
ArmASR/I: Vulkan loader reports instance version 1.3.x
ArmASR/I: Using device 'NVIDIA GeForce RTX ...' (Vulkan 1.3.x)
ArmASR/I: Arm ASR device gate: SPIR-V 1.4 yes | shaderFloat16 yes | 16-bit storage yes | ...
ArmASR/I: Swapchain: 1920x1080, 3 images, format 44
ArmASR/I: Rendering at 960x540, presenting at 1920x1080 (ratio 2.00)
ArmASR/I: Arm ASR scratch buffer: ... bytes
ArmASR/I: Arm ASR context created: 960x540 -> 1920x1080, jitter phase count 32
ArmASR/I: Arm ASR sample ready. Press ESC to quit.
ArmASR/I: First frame dispatched successfully
```

The reported scratch-buffer size **will not match the Android sample's
2,904,240 bytes**. `ffxmGetScratchMemorySizeVK` is `sizeof`-based, and `wchar_t`
is 2 bytes on Windows against 4 on Android — the job-description arrays that
dominate the buffer are correspondingly smaller. This is expected; do not
hand-tune the size.

ESC or closing the window exits. Log output goes to the console and to
`OutputDebugStringA`, so it is also visible in the Visual Studio output pane.

## Validation layers

```sh
set ASR_SAMPLE_VALIDATION=1
build-sample\Debug\ArmAsrSample.exe
```

or pass `--validation`. If the layer is not installed the sample logs a warning
and carries on. Zero validation errors is the real acceptance criterion for this
sample — it is coverage the CI jobs cannot provide.

## Build options

The sample forces four options on the Arm ASR subdirectory, because the
non-Android defaults do not suit a desktop app:

| Option | Root default off Android | Forced here | Why |
|---|---|---|---|
| `FFXM_REMOVE_ARM_ASR_VK_STANDALONE_BACKEND` | `ON` | `OFF` | Otherwise there is no `Arm_ASR_backend` target to link |
| `FFXM_VKLOADER_VOLK` | `ON` | `OFF` | The tree never calls `volkInitialize`/`volkLoadDevice` and does not vendor volk, so the volk path links but leaves the backend's function table full of nulls at dispatch time |
| `FFXM_USE_PREBUILT_SHADERS` | `OFF` on Windows | `ON` | Avoids depending on the Windows-only `FidelityFX_SC.exe` toolchain. `-DFFXM_USE_PREBUILT_SHADERS=OFF` still works |
| `FFXM_VULKAN_PATH` | empty | SDK includes | The backend uses directory-scoped `include_directories()`, so its include paths do not propagate to this target |

With volk off, `vulkan-1.lib` (via `Vulkan::Vulkan`) resolves the seven `vk*`
symbols the backend calls directly rather than through its `VKFunctionTable`:
`vkGetDeviceProcAddr`, `vkEnumerateDeviceExtensionProperties`,
`vkGetPhysicalDeviceMemoryProperties`, `vkGetPhysicalDeviceProperties2`,
`vkGetPhysicalDeviceFeatures2`, `vkCmdSetViewport` and `vkCmdSetScissor`.

## What the sample demonstrates

[`samples/common/asr_upscaler.cpp`](../common/asr_upscaler.cpp) is the
integration proper, and follows the "quick integration" path from the top-level
`README.md`: `ffxmGetScratchMemorySizeVK` → `ffxmGetDeviceVK` →
`ffxmGetInterfaceVK` → `ffxmFsr2ContextCreate` → per-frame
`ffxmFsr2ContextDispatch` → `ffxmFsr2ContextDestroy`.

[`samples/common/shaders/scene.frag`](../common/shaders/scene.frag) renders a
camera flying over an infinite checkerboard. Depth and motion vectors are
computed analytically rather than approximated, so the inputs Arm ASR receives
are exact — which is what makes the result meaningful rather than merely
non-crashing. Watch the checker edges: temporal stability there is the visible
proof the upscaler is working.

Sample shaders are compiled to SPIR-V at build time with the SDK's `glslc` and
embedded as C arrays by `samples/common/cmake/embed_spirv.cmake`. This is
unrelated to Arm ASR's own shaders, which come from the committed prebuilt blobs.

## Window resizing

The swapchain, the scene G-buffer and the Arm ASR context are all sized from the
window, so a resize tears down and rebuilds all three and restarts the jitter
sequence with `reset = true`. One ghosted frame immediately after a resize is
expected and correct. Minimising the window idles the loop rather than spinning.
