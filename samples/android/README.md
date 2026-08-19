<!-- Copyright © 2026 Arm Limited.
SPDX-License-Identifier: MIT -->

# Arm ASR Android Vulkan sample

A minimal NativeActivity app that renders a procedural scene at reduced
resolution, upscales it 2x with Arm ASR, and presents the result. It exists to
make the Android port verifiable on a device rather than only at compile time.

## Prerequisites

| | |
|---|---|
| Android NDK | `29.0.14206865` (r29) |
| Android SDK | Platform 35, build-tools 35.x |
| JDK | 17 |
| Device | Vulkan 1.2, or Vulkan 1.1 + `VK_KHR_spirv_1_4`, with `VK_KHR_shader_float16_int8` and 16-bit storage |

Point Gradle at the SDK with `ANDROID_HOME`, or add `sdk.dir` to
`samples/android/local.properties`.

## Build and run

```sh
cd samples/android
export ANDROID_HOME=~/Library/Android/sdk
export JAVA_HOME=/path/to/jdk17

./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.arm.asr.sample/android.app.NativeActivity
adb logcat -s ArmASR
```

Expected log on a supported device:

```
ArmASR: Using device 'Mali-G715' (Vulkan 1.3.x)
ArmASR: Arm ASR device gate: SPIR-V 1.4 yes | shaderFloat16 yes | 16-bit storage yes | ...
ArmASR: Rendering at 960x540, presenting at 1920x1080 (ratio 2.00)
ArmASR: Arm ASR scratch buffer: 2904240 bytes
ArmASR: Arm ASR context created: 960x540 -> 1920x1080, jitter phase count 32
ArmASR: First frame dispatched successfully
```

The debug build type adds `-D_DEBUG`, which turns on Arm ASR's internal asserts
and debug resource naming. Asserts report through logcat under the same tag.

## ABI support

| ABI | Status |
|---|---|
| `arm64-v8a` | Supported. The only ABI expected to pass the runtime gate on real hardware |
| `x86_64` | Builds and installs, for emulator and CI compile coverage. SwiftShader does not advertise `shaderFloat16`, so the sample is expected to stop at the capability gate with an explanatory log line rather than crash |
| `armeabi-v7a` | Compiles and links, but is untested on device. Best-effort only |

## What the sample demonstrates

`app/src/main/cpp/asr_upscaler.cpp` is the integration proper, and follows the
"quick integration" path from the top-level `README.md`:
`ffxmGetScratchMemorySizeVK` → `ffxmGetDeviceVK` → `ffxmGetInterfaceVK` →
`ffxmFsr2ContextCreate` → per-frame `ffxmFsr2ContextDispatch` →
`ffxmFsr2ContextDestroy`.

`app/src/main/shaders/scene.frag` renders a camera flying over an infinite
checkerboard. Depth and motion vectors are computed analytically rather than
approximated, so the inputs Arm ASR receives are exact — which is what makes the
result meaningful rather than merely non-crashing. Motion vectors are emitted in
render-resolution pixels (`motionVectorScale` is therefore `{1, 1}`) and are
derived from the unjittered ray, which is why
`FFXM_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION` is not set.

Sample shaders are compiled to SPIR-V at build time with the NDK's `glslc` and
embedded as C arrays by `cmake/embed_spirv.cmake`. This is unrelated to Arm ASR's
own shaders, which come from the committed prebuilt blobs.

## Validation layers

Install the Khronos validation layer for your ABI into
`app/src/main/jniLibs/<abi>/libVkLayer_khronos_validation.so` and add
`VK_LAYER_KHRONOS_validation` to the instance layer list in
`app/src/main/cpp/vk_context.cpp`.
