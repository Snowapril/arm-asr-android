// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT

plugins {
    id("com.android.application")
}

android {
    namespace = "com.arm.asr.sample"
    compileSdk = 35
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "com.arm.asr.sample"
        minSdk = 30
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        ndk {
            // arm64-v8a is the only ABI expected to pass the runtime fp16 /
            // SPIR-V 1.4 gate on real hardware. The other two are built so the
            // port keeps compiling for them; see samples/android/README.md.
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1+"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
        debug {
            // Arm ASR compiles its debug-only resource naming behind _DEBUG.
            externalNativeBuild {
                cmake {
                    cppFlags += "-D_DEBUG"
                }
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}