// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT
#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(set = 0, binding = 0) uniform sampler2D uUpscaled;

void main()
{
    oColor = vec4(texture(uUpscaled, vUV).rgb, 1.0);
}
