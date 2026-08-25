// Copyright © 2026 Arm Limited.
// SPDX-License-Identifier: MIT
//
// Procedural test scene: a camera flying forward over an infinite checkerboard
// plane. Everything is analytic, so the depth buffer and the motion vectors are
// exact rather than approximated - which is what makes this a meaningful input
// for Arm ASR rather than just something that renders.
#version 450

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec2 oMotion;

layout(push_constant) uniform PushConstants {
    vec2  renderSize;
    vec2  jitter;       // sub-pixel camera jitter, in pixels
    float camZ;         // camera position this frame
    float camZPrev;     // camera position last frame
    float tanHalfFov;
    float aspect;
    float nearZ;
    float farZ;
    float planeY;
    float time;
} pc;

// Map a view-space distance along the view direction to a Vulkan [0,1] depth.
float viewDistToDepth(float d)
{
    return (pc.farZ / (pc.farZ - pc.nearZ)) * (1.0 - pc.nearZ / d);
}

// Project a point given in the space of a camera at the origin looking down -Z
// back to pixel coordinates.
vec2 projectToPixels(vec3 viewPos)
{
    float d = -viewPos.z;
    vec2 ndc = vec2(viewPos.x / (d * pc.tanHalfFov * pc.aspect),
                    viewPos.y / (d * pc.tanHalfFov));
    return (vec2(ndc.x, -ndc.y) * 0.5 + 0.5) * pc.renderSize;
}

vec3 checker(vec2 p)
{
    vec2  c   = floor(p);
    float odd = mod(c.x + c.y, 2.0);
    vec3  a   = vec3(0.82, 0.36, 0.18);
    vec3  b   = vec3(0.10, 0.13, 0.20);
    vec3  base = mix(a, b, odd);

    // A thin bright grid line gives the upscaler high-frequency detail to
    // reconstruct, which is where temporal upscaling either works or does not.
    vec2  g    = abs(fract(p) - 0.5);
    float line = 1.0 - smoothstep(0.46, 0.5, max(g.x, g.y));
    return mix(base, vec3(0.95, 0.93, 0.85), line * 0.7);
}

void main()
{
    vec2 pixel         = vUV * pc.renderSize;
    vec2 jitteredPixel = pixel + pc.jitter;

    // Unjittered ray is what the motion vectors are derived from; the jittered
    // ray is what gets shaded. This is the usual convention, and it is why
    // FFXM_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION is left unset.
    vec2 ndcShade = (jitteredPixel / pc.renderSize) * 2.0 - 1.0;
    vec2 ndcMv    = (pixel / pc.renderSize) * 2.0 - 1.0;

    vec3 dirShade = normalize(vec3(ndcShade.x * pc.tanHalfFov * pc.aspect,
                                   -ndcShade.y * pc.tanHalfFov, -1.0));

    if (dirShade.y >= -1e-4) {
        // Sky: no parallax from a pure forward translation.
        vec3 sky = mix(vec3(0.35, 0.45, 0.62), vec3(0.06, 0.08, 0.14),
                       clamp(dirShade.y * 3.0, 0.0, 1.0));
        oColor       = vec4(sky, 1.0);
        oMotion      = vec2(0.0);
        gl_FragDepth = 1.0;
        return;
    }

    // Intersect the ground plane. The camera sits at the origin of its own
    // space, so the plane is at planeY relative to it.
    float t     = pc.planeY / dirShade.y;
    vec3  hitVS = dirShade * t;                          // view space, this frame

    // World-space XZ for the pattern: Z advances with the camera.
    vec2 planeUV = vec2(hitVS.x, hitVS.z + pc.camZ) * 0.35;

    float dist = -hitVS.z;
    vec3  col  = checker(planeUV);

    // Distance fade keeps the far field from aliasing into noise.
    float fog = clamp(dist / (pc.farZ * 0.35), 0.0, 1.0);
    col = mix(col, vec3(0.35, 0.45, 0.62), fog);

    oColor       = vec4(col, 1.0);
    gl_FragDepth = clamp(viewDistToDepth(max(dist, pc.nearZ)), 0.0, 1.0);

    // Motion vectors: prev = cur + mv, in render-resolution pixels, derived
    // from the unjittered ray. projectToPixels(hitMvVS) is `pixel` by
    // construction, so only the previous position needs projecting.
    vec3 dirMv = normalize(vec3(ndcMv.x * pc.tanHalfFov * pc.aspect,
                                -ndcMv.y * pc.tanHalfFov, -1.0));
    oMotion = vec2(0.0);
    if (dirMv.y < -1e-4) {
        vec3 hitMvVS = dirMv * (pc.planeY / dirMv.y);
        // The camera only translates along Z, so the same world point sits at
        // hitMvVS + (0, 0, camZ - camZPrev) relative to last frame's camera.
        vec3 hitMvPrevVS = hitMvVS + vec3(0.0, 0.0, pc.camZ - pc.camZPrev);
        if (hitMvPrevVS.z < -pc.nearZ) {
            oMotion = projectToPixels(hitMvPrevVS) - pixel;
        }
    }
}
