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

#ifndef WIRELET_CRC32_H
#define WIRELET_CRC32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void wirelet_crc32_init(uint32_t *crc);

void wirelet_crc32_step(uint32_t *crc, uint8_t byte);

void wirelet_crc32_step_bytes(uint32_t *crc, uint8_t *bytes, uint32_t bytes_len);

void wirelet_crc32_finalize(uint32_t *crc);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // WIRELET_CRC32_H