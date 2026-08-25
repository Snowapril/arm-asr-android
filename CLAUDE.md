<!-- Copyright © 2026 Arm Limited.
SPDX-License-Identifier: MIT -->

# CLAUDE.md

Working guide for this repository. The user-facing documentation is `README.md`;
this file covers what is not obvious from reading the code.

## What this is

Arm® Accuracy Super Resolution™ (Arm ASR), a mobile-optimized temporal upscaler
derived from AMD's FidelityFX™ Super Resolution 2 v2.2.2. Every FSR symbol was
renamed `ffx*` → `ffxm*` / `Ffx*` → `Ffxm*` and moved into `namespace arm`.
Vulkan is the only backend, by design — Arm ASR targets Vulkan mobile apps.

## Layout

| Path | What |
|---|---|
| `include/host/` | Public API. `ffxm_fsr2.h` (effect), `ffxm_interface.h` (the backend vtable), `ffxm_types.h`, `ffxm_assert.h`, `ffxm_error.h`, `ffxm_util.h` |
| `include/host/backends/vk/ffxm_vk.h` | The only public backend header |
| `include/gpu/` | Dual CPU/shader headers. Not namespaced — they are included from both C++ and HLSL/GLSL |
| `src/components/fsr2/ffxm_fsr2.cpp` | The effect runtime. Completely backend-agnostic: it only calls through `FfxmInterface` function pointers |
| `src/backends/vk/ffxm_vk.cpp` | The entire Vulkan backend (~3.6k lines) |
| `src/backends/vk/shaders/fsr2/{hlsl,glsl}/` | Shader entry points, 8 passes each |
| `src/backends/shared/blob_accessors/prebuilt_shaders/` | 154 committed SPIR-V blob headers |
| `samples/common/` | Portable sample code shared by both samples: `scene.*`, `asr_upscaler.*`, the GLSL shaders, `cmake/embed_spirv.cmake` |
| `samples/android/` | Gradle/NativeActivity Vulkan sample (see below) |
| `samples/windows/` | Win32 x64/MSVC Vulkan sample (see below) |
| `tools/` | Shader compiler binaries and the prebuilt-shader generator |

## Two integration modes

- **Quick**: link `Arm_ASR_api` + `Arm_ASR_backend`, call `ffxmGetScratchMemorySizeVK`
  → `ffxmGetDeviceVK` → `ffxmGetInterfaceVK` → `ffxmFsr2ContextCreate` →
  `ffxmFsr2ContextDispatch`. `samples/common/asr_upscaler.cpp` is a complete
  worked example, shared by both samples.
- **Tight**: implement the 15 function pointers of `FfxmInterface`
  (`include/host/ffxm_interface.h:437`) yourself and build the shaders in your own
  engine pipeline. Only `fpGetDeviceCapabilities`, `fpCreateBackendContext` and
  `fpDestroyBackendContext` are null-checked at context creation; the rest are
  called unguarded.

## Building

The tree has no `project()` of its own unless configured standalone — it is
normally pulled in with `add_subdirectory()`.

| Option | Default | Meaning |
|---|---|---|
| `FFXM_REMOVE_ARM_ASR_VK_STANDALONE_BACKEND` | `ON`, `OFF` on Android | Compiles the backend away entirely; a pure CMake switch, there is no matching `#ifdef` |
| `FFXM_USE_PREBUILT_SHADERS` | `ON` off Windows | Use the committed SPIR-V blobs instead of running the shader compiler. Configuring `OFF` on a non-Windows host is a hard error |
| `FFXM_VKLOADER_VOLK` | `ON`, `OFF` on Android | Resolve entry points through volk (needs `FFXM_VOLK_PATH`) rather than linking the loader |
| `FFXM_USE_GLSL_SHADERS` | `OFF` | Compile the GLSL sources instead of HLSL. Only meaningful when not using prebuilt shaders |

Android, all three ABIs, no extra flags needed:

```sh
cmake -B build/android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-35
cmake --build build/android-arm64
```

## Shaders — read this before touching them

**`tools/bin/` holds Windows x86-64 PE binaries** (`FidelityFX_SC.exe`,
`dxcompiler.dll`, `glslangValidator.exe`). Shaders **cannot be regenerated on
macOS or Linux**. On those hosts the build consumes the committed blobs in
`src/backends/shared/blob_accessors/prebuilt_shaders/` instead; the accessors
include the `*_permutations.h` headers with angle brackets, so switching between
generated and prebuilt is purely an include-path change.

Other things worth knowing:

- 8 permutation axes, all `{0,1}` → **256 keys per pass**, deduplicated by blob
  hash down to 1–74 unique blobs depending on the pass.
- Only the Wave32 16-bit permutation is emitted. The Wave64 and 32-bit variables
  in `src/backends/vk/CMakeLists.txt` are dead.
- Every blob is **SPIR-V 1.4 with explicit 16-bit types** (`-fspv-target-env=vulkan1.1spirv1.4
  -enable-16bit-types -DFFXM_HALF=1`). **There is no fp32 fallback set.** A device
  therefore needs Vulkan 1.2 (or 1.1 + `VK_KHR_spirv_1_4`), `VK_KHR_shader_float16_int8`
  and 16-bit storage. `samples/android/app/src/main/cpp/vk_context.cpp` gates on
  exactly this.
- `tools/generate_prebuilt_shaders.py` wipes the output directory before running,
  and duplicates the argument list in `src/backends/vk/CMakeShadersFSR2.txt` — the
  two can drift.
- `tools/ffx_shader_compiler.diff` is the patch against AMD's upstream
  `ffx_shader_compiler` that adds render-target reflection. Its header hunks are
  duplicated and it does not apply cleanly with `git apply` as-is.

## Android specifics

- NDK r29 (`29.0.14206865`), `compileSdk`/`targetSdk` 35. The library itself
  builds down to API 21; the sample sets `minSdk 30` because of the SPIR-V 1.4 /
  fp16 requirement.
- Vulkan comes from the NDK sysroot: `<vulkan/vulkan.h>` plus a direct link
  against `libvulkan.so`. volk is not used and not vendored.
- `arm64-v8a` is the only ABI expected to pass the runtime gate on real hardware.
  `x86_64` builds for emulator/CI compile coverage but SwiftShader does not
  advertise `shaderFloat16`. `armeabi-v7a` compiles but is untested on device.
- The backend calls `vkCmdSetViewport` and `vkCmdSetScissor` directly — they are
  missing from the 51-entry `VKFunctionTable` — plus five instance-level entry
  points. With direct linkage these resolve against `libvulkan.so`; the linked
  library has exactly 7 undefined `vk*` symbols.
- `ffxmAssertReport` routes through `__android_log_print` (tag `ArmASR`) when no
  assert callback is installed, so `Arm_ASR_api` links `liblog`.

## Windows specifics

- The samples split platform-bound from portable code: only `main.cpp`,
  `vk_context.{h,cpp}` and `common.h` are per-platform. `scene.*`,
  `asr_upscaler.*` and the three GLSL shaders live in `samples/common/` and are
  compiled into both. A change to the shared half must keep both building.
- `samples/windows` forces four options on the Arm ASR subdirectory, because the
  non-Android defaults are wrong for a desktop app: backend **on**, volk
  **off**, prebuilt shaders **on**, `FFXM_VULKAN_PATH` set to the SDK includes.
- **volk must stay off.** The tree never calls `volkInitialize`/`volkLoadDevice`
  and does not vendor volk, so `FFXM_VKLOADER_VOLK=ON` compiles and links but
  leaves the 51-entry `VKFunctionTable` full of nulls. It fails at dispatch
  time, not build time.
- Nothing in the tree links a Vulkan loader off Android. With volk off, the
  consumer's executable must supply `vulkan-1.lib` for the 7 `vk*` symbols the
  backend calls directly (see the Android specifics list — the same 7).
- `include/host/backends/vk/ffxm_vk.h` only defines `VK_USE_PLATFORM_WIN32_KHR`
  on the volk path. With volk off it includes `<vulkan/vulkan.h>` plainly, so a
  Win32 host must define the platform macro itself before any vulkan.h include.
- `VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR` is an Android value; Win32 WSI normally
  advertises only `OPAQUE`. Win32 may also report `currentExtent` as
  `0xFFFFFFFF`. Both bite when porting swapchain code from the Android sample.
- MSVC defines `_DEBUG` in Debug configurations, so unlike Android the
  `#ifdef _DEBUG` blocks are live for free — no `cppFlags` injection needed.
- `add_compile_definitions(_UNICODE UNICODE)` in the root `CMakeLists.txt` is
  directory-scoped to the Arm ASR subtree and does not reach a consuming target.

## Gotchas

- **`_DEBUG` is an MSVC-ism.** Clang does not define it, so every `#ifdef _DEBUG`
  block — resource naming in `ffxm_vk.cpp`, `FFXM_ASSERT`, `FFXM_DEBUG_BREAK`,
  the `name` copy in `ffxmGetResourceVK` — is compiled out on Android unless you
  pass `-D_DEBUG` explicitly. The sample's debug build type does.
- **`wchar_t` is 2 bytes on Windows and 4 on Android.** It is pervasive in the
  public ABI (`FfxmResource::name`, `FfxmPipelineDescription::name`, the
  `FfxmComputeJobDescription`/`FfxmFragmentJobDescription` name arrays,
  `ffxmGetResourceVK`, the `FfxmFsr2Message` callback). Those job descriptions are
  stored by value in arrays sized `FFXM_MAX_GPU_JOBS(64) × maxContexts`, so the
  scratch buffer is substantially larger on Android: `sizeof(FfxmGpuJobDescription)`
  is 41,040 bytes on arm64, making the job array 2.5 MiB of a 2.77 MiB scratch
  buffer for a single context. `ffxmGetScratchMemorySizeVK` is `sizeof`-based and
  adapts on its own — do not hand-tune it.
- **Process-global state**: `sVkDeviceContext`, `s_BackendRefCount` and
  `s_MaxEffectContexts` in `ffxm_vk.cpp` are file-scope statics. One Vulkan device
  per process.
- `MAX_DESCRIPTOR_SETS` is 2, so shaders may only use descriptor sets 0 and 1.
- `ffxmGetScratchMemorySizeVK` computes `resourceViewArraySize` but leaves it out
  of the sum, and counts `resourceArraySize` twice instead. On arm64 with
  `maxContexts = 1` that is 5,120 bytes counted twice against 4,096 omitted, so
  the result is safe — by 1,024 bytes per context, entirely by accident. Do not
  shrink either array without redoing this arithmetic.
- `include/host/ffxm_types.h:592` defines a non-inline `static FfxmViewDescription`
  in a public header: one copy per translation unit.
- The `#pragma clang diagnostic ignored "-Wunused-variable"` in `ffxm_fsr2.cpp` and
  `ffxm_core_cpu.h` has no matching `push`/`pop`, so it leaks into everything
  included afterwards.

## Conventions

- Every file carries an MIT + SPDX header; add your copyright year when editing one
  that does not conform (see `CONTRIBUTING.md`).
- Contributions need a `Signed-off-by:` DCO trailer.
- Naming is `ffxm`/`Ffxm`/`FFXM_`. Host code lives in `namespace arm`; GPU headers
  do not.
- Prefer guarded, additive changes over reformatting: this tree is a vendored
  derivative and diffs against upstream FidelityFX still matter.
