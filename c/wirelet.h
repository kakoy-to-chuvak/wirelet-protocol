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

#ifndef WIRELET_H
#define WIRELET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

enum wirelet_state {
        WIRELET_STATE_HEADER,
        WIRELET_STATE_ID0,
        WIRELET_STATE_ID1,
        WIRELET_STATE_TYPE0,
        WIRELET_STATE_TYPE1,
        WIRELET_STATE_LEN0,
        WIRELET_STATE_LEN1,
        WIRELET_STATE_PAYLOAD,
        WIRELET_STATE_CRC0,
        WIRELET_STATE_CRC1,
        WIRELET_STATE_CRC2,
        WIRELET_STATE_CRC3,
        WIRELET_STATE_END,
};

typedef void (*wirelet_handle_frame)(uint16_t id, uint16_t type, uint16_t payload_len, uint8_t *payload);
typedef void (*wirelet_send_byte)(uint8_t byte);
typedef void (*wirelet_send_bytes)(uint8_t *bytes, uint32_t bytes_len);

struct wirelet_context {
        enum wirelet_state rx_frame_state;
        uint8_t *rx_frame_buf;
        uint32_t rx_buf_offset;
        uint32_t rx_buf_len;
        uint16_t rx_frame_id;
        uint16_t rx_frame_type;
        uint16_t rx_frame_len;
        uint32_t rx_frame_crc;
        uint32_t rx_crc;
        uint8_t rx_header_bytes_seen;

        uint8_t *tx_buf;
        uint32_t tx_buf_offset;
        uint32_t tx_buf_len;

        wirelet_handle_frame handle_frame;
        wirelet_send_byte send_byte;
        wirelet_send_bytes send_bytes;
};


bool wirelet_context_init(struct wirelet_context *context,
                      wirelet_handle_frame frame_handler,
                      wirelet_send_byte send_byte,
                      wirelet_send_bytes send_bytes,
                      uint8_t *rx_buf,
                      uint32_t rx_buf_len,
                      uint8_t *tx_buf,
                      uint32_t tx_buf_len);

bool wirelet_context_reset(struct wirelet_context *context);

bool wirelet_send_frame(struct wirelet_context *context,
                    uint16_t frame_id,
                    uint16_t frame_type,
                    uint16_t frame_len,
                    const uint8_t *frame);

void wirelet_poll_byte(struct wirelet_context *context, uint8_t byte);

bool wirelet_poll_bytes(struct wirelet_context *context, const uint8_t *bytes, uint32_t bytes_len);


#ifdef __cplusplus
}
#endif



#endif // WIRELET_H