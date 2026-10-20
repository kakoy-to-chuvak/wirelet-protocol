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
#include "wirelet.h"
#include <stdbool.h>
#include <stdint.h>


#ifndef NULL
#define NULL ((void*)0)
#endif // NULL


#define HEADER_BYTE 0xAA
#define STUFF_BYTE 0x55
#define END_BYTE 0x55

bool wirelet_context_init(struct wirelet_context *context,
                      wirelet_handle_frame frame_handler,
                      wirelet_send_byte send_byte,
                      wirelet_send_bytes send_bytes,
                      uint8_t *rx_buf,
                      uint32_t rx_buf_len,
                      uint8_t *tx_buf,
                      uint32_t tx_buf_len)
{
        if (context == NULL)
                return false;
        
        bool rx_ok = frame_handler != NULL && rx_buf != NULL && rx_buf_len != 0;
        bool tx_ok = send_byte != NULL || (send_bytes != NULL && tx_buf != NULL && tx_buf_len != 0);

        if (!rx_ok && !tx_ok)
                return false;

        if ((frame_handler != NULL && (rx_buf == NULL || rx_buf_len == 0)) ||
            (send_bytes != NULL && (tx_buf == NULL || tx_buf_len == 0)))
                return false;

        // buf/len mismatch
        if ((rx_buf == NULL && rx_buf_len != 0) ||
            (tx_buf == NULL && tx_buf_len != 0))
                return false;

        context->handle_frame = frame_handler;
        context->send_byte = send_byte;
        context->send_bytes = send_bytes;
        context->rx_frame_buf = rx_buf;
        context->rx_buf_len = rx_buf_len;
        context->tx_buf = tx_buf;
        context->tx_buf_len = tx_buf_len;
        wirelet_context_reset(context);
        return true;
}

bool wirelet_context_reset(struct wirelet_context *context)
{
        if (context == NULL)
                return false;

        context->rx_frame_state = WIRELET_STATE_HEADER;
        context->rx_frame_id    = 0;
        context->rx_frame_type  = 0;
        context->rx_frame_len   = 0;
        context->rx_frame_crc   = 0;
        context->rx_crc         = 0;
        context->rx_buf_offset  = 0;
        context->rx_header_bytes_seen = 0;

        context->tx_buf_offset = 0;
        return true;
}


static void _send_stuffed(wirelet_send_byte send, uint32_t *crc,
                          uint8_t *header_count, uint8_t byte)
{
        if (crc != NULL)
                wirelet_crc32_step(crc, byte);

        send(byte);

        // send stuff byte only if seen 2 header bytes
        if (byte == HEADER_BYTE) {
                (*header_count)++;
                if (*header_count == 2) {
                        send(STUFF_BYTE);
                        *header_count = 0;
                }
        } else {
                *header_count = 0;
        }

}


static void _send_frame_byte(struct wirelet_context *context,
                             uint16_t frame_id,
                             uint16_t frame_type,
                             uint16_t frame_len, 
                             const uint8_t *frame)
{
        wirelet_send_byte send = context->send_byte;
        uint8_t header_count = 0;
        uint32_t crc = 0;

        wirelet_crc32_init(&crc);

        send(HEADER_BYTE);
        send(HEADER_BYTE);
        send(HEADER_BYTE);

        _send_stuffed(send, &crc, &header_count, frame_id >> 8);
        _send_stuffed(send, &crc, &header_count, frame_id & 0xff);

        _send_stuffed(send, &crc, &header_count, frame_type >> 8);
        _send_stuffed(send, &crc, &header_count, frame_type & 0xff);

        _send_stuffed(send, &crc, &header_count, frame_len >> 8);
        _send_stuffed(send, &crc, &header_count, frame_len & 0xff);

        for (uint16_t i = 0; i < frame_len; i++) {
                _send_stuffed(send, &crc, &header_count, frame[i]);
        }

        wirelet_crc32_finalize(&crc);
        _send_stuffed(send, NULL, &header_count, (crc >> 24) & 0xff);
        _send_stuffed(send, NULL, &header_count, (crc >> 16) & 0xff);
        _send_stuffed(send, NULL, &header_count, (crc >> 8) & 0xff);
        _send_stuffed(send, NULL, &header_count, (crc >> 0) & 0xff);

        send(END_BYTE);
}

static void _tx_buf_add_byte(struct wirelet_context *context, uint8_t byte)
{       
        context->tx_buf[context->tx_buf_offset] = byte;
        context->tx_buf_offset++;

        // send tx buf
        if (context->tx_buf_offset == context->tx_buf_len) {
                context->send_bytes(context->tx_buf, context->tx_buf_offset);
                context->tx_buf_offset = 0;
        }
}

static void _tx_buf_add_byte_stuffed(struct wirelet_context *context, uint32_t *crc,
                                     uint8_t *header_count, uint8_t byte)
{       
        if (crc != NULL)
                wirelet_crc32_step(crc, byte);
        
        _tx_buf_add_byte(context, byte);

        // add stuff byte only if seen 2 header bytes
        if (byte == HEADER_BYTE) {
                (*header_count)++;
                if (*header_count == 2) {
                        _tx_buf_add_byte(context, STUFF_BYTE);
                        *header_count = 0;
                }
        } else {
                *header_count = 0;
        }
}

static void _send_frame_bytes(struct wirelet_context *context,
                              uint16_t frame_id,
                              uint16_t frame_type,
                              uint16_t frame_len, 
                              const uint8_t *frame)
{
        uint8_t header_count = 0;
        uint32_t crc = 0;

        wirelet_crc32_init(&crc);

        _tx_buf_add_byte(context, HEADER_BYTE);
        _tx_buf_add_byte(context, HEADER_BYTE);
        _tx_buf_add_byte(context, HEADER_BYTE);

        _tx_buf_add_byte_stuffed(context, &crc, &header_count, frame_id >> 8);
        _tx_buf_add_byte_stuffed(context, &crc, &header_count, frame_id & 0xff);

        _tx_buf_add_byte_stuffed(context, &crc, &header_count, frame_type >> 8);
        _tx_buf_add_byte_stuffed(context, &crc, &header_count, frame_type & 0xff);

        _tx_buf_add_byte_stuffed(context, &crc, &header_count, frame_len >> 8);
        _tx_buf_add_byte_stuffed(context, &crc, &header_count, frame_len & 0xff);

        for (uint16_t i = 0; i < frame_len; i++) {
                _tx_buf_add_byte_stuffed(context, &crc, &header_count, frame[i]);
        }
        
        wirelet_crc32_finalize(&crc);
        _tx_buf_add_byte_stuffed(context, NULL, &header_count, (crc >> 24) & 0xff);
        _tx_buf_add_byte_stuffed(context, NULL, &header_count, (crc >> 16) & 0xff);
        _tx_buf_add_byte_stuffed(context, NULL, &header_count, (crc >> 8) & 0xff);
        _tx_buf_add_byte_stuffed(context, NULL, &header_count, (crc >> 0) & 0xff);

        _tx_buf_add_byte(context, END_BYTE);

        // send tx buf
        if (context->tx_buf_offset != 0) {
                context->send_bytes(context->tx_buf, context->tx_buf_offset);
                context->tx_buf_offset = 0;
        }
}

bool wirelet_send_frame(struct wirelet_context *context,
                    uint16_t frame_id,
                    uint16_t frame_type,
                    uint16_t frame_len, 
                    const uint8_t *frame)
{
        if (context == NULL || (frame == NULL && frame_len != 0))
                return false;

        if (context->send_bytes != NULL && context->tx_buf != NULL && context->tx_buf_len != 0) {
                // if buffer to small try sending by byte (should be faster)
                if (context->tx_buf_len <= 2 && context->send_byte != NULL) {
                        _send_frame_byte(context, frame_id, frame_type, frame_len, frame);
                        return true;
                }

                _send_frame_bytes(context, frame_id, frame_type, frame_len, frame);
                return true;
        }

        if (context->send_byte != NULL) {
                _send_frame_byte(context, frame_id, frame_type, frame_len, frame);
                return true;
        }
        
        return false;
}

void wirelet_poll_byte(struct wirelet_context *context, uint8_t byte)
{
        if (context == NULL)
                return;
        
        if (context->rx_header_bytes_seen == 2) {
                context->rx_header_bytes_seen = 0;
                if (byte == HEADER_BYTE) {
                        context->rx_frame_state = WIRELET_STATE_ID0;
                        return;
                }
                // skip stuff byte if seen 2 header bytes
                if (byte == STUFF_BYTE) {
                        return;
                } else {
                        context->rx_frame_state = WIRELET_STATE_HEADER;
                        return;
                }
        }

        if (byte == HEADER_BYTE) {
                context->rx_header_bytes_seen++;
        } else {
                context->rx_header_bytes_seen = 0;
        }

        switch (context->rx_frame_state) {
        case WIRELET_STATE_HEADER:
                break;
                
        case WIRELET_STATE_ID0:
                context->rx_frame_id = (uint16_t)byte << 8;
                context->rx_frame_state = WIRELET_STATE_ID1;
                wirelet_crc32_init(&context->rx_crc);
                wirelet_crc32_step(&context->rx_crc, byte);
                break;
        case WIRELET_STATE_ID1:
                context->rx_frame_id |= (uint16_t)byte;
                context->rx_frame_state = WIRELET_STATE_TYPE0;
                wirelet_crc32_step(&context->rx_crc, byte);
                break;

        case WIRELET_STATE_TYPE0:
                context->rx_frame_type = (uint16_t)byte << 8;
                context->rx_frame_state = WIRELET_STATE_TYPE1;
                wirelet_crc32_step(&context->rx_crc, byte);
                break;
        case WIRELET_STATE_TYPE1:
                context->rx_frame_type |= (uint16_t)byte;
                context->rx_frame_state = WIRELET_STATE_LEN0;
                wirelet_crc32_step(&context->rx_crc, byte);
                break;

        case WIRELET_STATE_LEN0:
                context->rx_frame_len = (uint16_t)byte << 8;
                context->rx_frame_state = WIRELET_STATE_LEN1;
                wirelet_crc32_step(&context->rx_crc, byte);
                break;
        case WIRELET_STATE_LEN1:
                context->rx_frame_len |= (uint16_t)byte;
                context->rx_frame_state = WIRELET_STATE_PAYLOAD;
                wirelet_crc32_step(&context->rx_crc, byte);
                if (context->rx_frame_len == 0) {
                        context->rx_frame_state = WIRELET_STATE_CRC0;
                } else if ( context->rx_frame_len > context->rx_buf_len) {
                        context->rx_frame_state = WIRELET_STATE_HEADER;
                        context->rx_header_bytes_seen = 0;
                } else {
                        context->rx_buf_offset = 0;
                }
                break;

        case WIRELET_STATE_PAYLOAD:
                context->rx_frame_buf[context->rx_buf_offset] = byte;
                context->rx_buf_offset++;
                wirelet_crc32_step(&context->rx_crc, byte);
                if (context->rx_buf_offset == context->rx_frame_len) {
                        context->rx_frame_state = WIRELET_STATE_CRC0;
                }
                break;

        case WIRELET_STATE_CRC0:
                context->rx_frame_crc = (uint32_t)byte << 24;
                context->rx_frame_state = WIRELET_STATE_CRC1;
                break;
        case WIRELET_STATE_CRC1:
                context->rx_frame_crc |= (uint32_t)byte << 16;
                context->rx_frame_state = WIRELET_STATE_CRC2;
                break;
        case WIRELET_STATE_CRC2:
                context->rx_frame_crc |= (uint32_t)byte << 8;
                context->rx_frame_state = WIRELET_STATE_CRC3;
                break;
        case WIRELET_STATE_CRC3:
                context->rx_frame_crc |= (uint32_t)byte;
                wirelet_crc32_finalize(&context->rx_crc);               
                if (context->rx_crc == context->rx_frame_crc) {
                        context->rx_frame_state = WIRELET_STATE_END;
                } else {
                        context->rx_frame_state = WIRELET_STATE_HEADER;
                }
                break;

        case WIRELET_STATE_END:
                if (byte == END_BYTE && context->handle_frame != NULL)
                        context->handle_frame(context->rx_frame_id, context->rx_frame_type,
                                              context->rx_frame_len, context->rx_frame_buf);
                context->rx_frame_state = WIRELET_STATE_HEADER;
        }
}

bool wirelet_poll_bytes(struct wirelet_context *context, const uint8_t *bytes, uint32_t bytes_len)
{
        if (context == NULL || (bytes == NULL && bytes_len != 0))
                return false;

        for (uint32_t i = 0; i < bytes_len; i++) {
                wirelet_poll_byte(context,  bytes[i]);
        }

        return true;
}