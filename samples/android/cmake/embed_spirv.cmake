# Copyright © 2026 Arm Limited.
# SPDX-License-Identifier: MIT
#
# Script-mode helper: turn a .spv binary into a C header holding a uint32_t array.
# Invoked as: cmake -DSPV_IN=... -DHEADER_OUT=... -DSYMBOL=... -P embed_spirv.cmake

file(READ "${SPV_IN}" SPV_HEX HEX)
string(LENGTH "${SPV_HEX}" HEX_LENGTH)
math(EXPR BYTE_COUNT "${HEX_LENGTH} / 2")
math(EXPR REMAINDER "${BYTE_COUNT} % 4")
if(NOT REMAINDER EQUAL 0)
    message(FATAL_ERROR "${SPV_IN} is ${BYTE_COUNT} bytes, not a multiple of 4")
endif()
math(EXPR WORD_COUNT "${BYTE_COUNT} / 4")

set(BODY "")
math(EXPR LAST_WORD "${WORD_COUNT} - 1")
foreach(WORD_INDEX RANGE ${LAST_WORD})
    math(EXPR OFFSET "${WORD_INDEX} * 8")
    string(SUBSTRING "${SPV_HEX}" ${OFFSET} 8 WORD_HEX)
    # SPIR-V is little-endian on disk; reassemble each 32-bit word.
    string(SUBSTRING "${WORD_HEX}" 0 2 B0)
    string(SUBSTRING "${WORD_HEX}" 2 2 B1)
    string(SUBSTRING "${WORD_HEX}" 4 2 B2)
    string(SUBSTRING "${WORD_HEX}" 6 2 B3)
    string(APPEND BODY "0x${B3}${B2}${B1}${B0}u,")
    math(EXPR NEWLINE "(${WORD_INDEX} + 1) % 8")
    if(NEWLINE EQUAL 0)
        string(APPEND BODY "\n    ")
    else()
        string(APPEND BODY " ")
    endif()
endforeach()

file(WRITE "${HEADER_OUT}"
"// Generated from ${SPV_IN}. Do not edit.
#pragma once
#include <cstdint>

static const uint32_t ${SYMBOL}[] = {
    ${BODY}
};
")
