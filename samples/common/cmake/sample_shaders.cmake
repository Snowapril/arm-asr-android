# Copyright © 2026 Arm Limited.
# SPDX-License-Identifier: MIT
#
# Shared between the platform samples: compile a GLSL source in
# samples/common/shaders to SPIR-V and embed it as a C array.
#
# The caller must set, before including this file:
#   GLSLC_EXECUTABLE     path to glslc (NDK shader-tools on Android, SDK on Windows)
#   SHADER_DIR           samples/common/shaders
#   SAMPLE_CMAKE_DIR     samples/common/cmake
#   GENERATED_SHADER_DIR where the .spv and .spv.h land
#
# Each call appends the generated header to SHADER_HEADERS in the caller's scope.

function(compile_sample_shader SOURCE SYMBOL)
    set(SPV ${GENERATED_SHADER_DIR}/${SYMBOL}.spv)
    set(HEADER ${GENERATED_SHADER_DIR}/${SYMBOL}.spv.h)
    add_custom_command(
        OUTPUT ${HEADER}
        COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.1 -O -o ${SPV} ${SHADER_DIR}/${SOURCE}
        COMMAND ${CMAKE_COMMAND} -DSPV_IN=${SPV} -DHEADER_OUT=${HEADER} -DSYMBOL=g_${SYMBOL}
                -P ${SAMPLE_CMAKE_DIR}/embed_spirv.cmake
        DEPENDS ${SHADER_DIR}/${SOURCE} ${SAMPLE_CMAKE_DIR}/embed_spirv.cmake
        COMMENT "Compiling ${SOURCE}"
        VERBATIM)
    set(SHADER_HEADERS ${SHADER_HEADERS} ${HEADER} PARENT_SCOPE)
endfunction()

# The three shaders every sample uses. Call after the variables above are set.
macro(compile_all_sample_shaders)
    compile_sample_shader(fullscreen.vert fullscreen_vert)
    compile_sample_shader(scene.frag      scene_frag)
    compile_sample_shader(blit.frag       blit_frag)
    add_custom_target(sample_shaders DEPENDS ${SHADER_HEADERS})
endmacro()
