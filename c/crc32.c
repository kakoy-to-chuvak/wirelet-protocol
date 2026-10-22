/*
 * wirelet-protocol
 *
 * Copyright (c) 2014-2017 JK Energy Ltd.
 * Copyright (c) 2026 kto-to
 *
 * SPDX-License-Identifier: MIT
 *
 * This file is part of wirelet-protocol, a modified version of the MIN protocol.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "wirelet_crc32.h"
#include <stdint.h>
#include <stddef.h>

void wirelet_crc32_init(uint32_t *crc)
{
        if (crc == NULL)
                return;

        *crc = 0xffffffffU;
}

void wirelet_crc32_step(uint32_t *crc_p, uint8_t byte)
{
        if (crc_p == NULL)
                return;

        uint32_t crc = *crc_p;
        crc ^= byte;
        for (uint8_t j = 0; j < 8; j++) {
                uint32_t mask = (uint32_t) - (crc & 1U);
                crc = (crc >> 1) ^ (0xedb88320U & mask);
        }
        *crc_p = crc;
}

void wirelet_crc32_step_bytes(uint32_t *crc_p, const uint8_t *bytes, uint32_t bytes_len)
{
        if (crc_p == NULL)
                return;

        uint32_t crc = *crc_p;
        for (uint32_t i = 0; i < bytes_len; i++) {
                crc ^= bytes[i];
                for (uint8_t j = 0; j < 8; j++) {
                        uint32_t mask = (uint32_t) - (crc & 1U);
                        crc = (crc >> 1) ^ (0xedb88320U & mask);
                }
        }
        *crc_p = crc;
}

void wirelet_crc32_finalize(uint32_t *crc)
{
        if (crc == NULL)
                return;

        *crc = ~(*crc);
}