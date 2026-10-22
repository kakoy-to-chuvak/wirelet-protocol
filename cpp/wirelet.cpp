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

#include "wirelet.hpp"
#include "wirelet_crc32.h"
#include <assert.h>


#define HEADER_BYTE 0xAA
#define STUFF_BYTE 0x55
#define END_BYTE 0x55


wirelet::Handler::Handler(wirelet::handle_frame frame_handler,
                          wirelet::send_byte send_byte,
                          wirelet::send_bytes send_bytes,
                          uint8_t *rx_buf,
                          uint32_t rx_buf_len,
                          uint8_t *tx_buf,
                          uint32_t tx_buf_len):
        rx_frame_buf(rx_buf), rx_buf_len(rx_buf_len),
        tx_buf(tx_buf), tx_buf_len(tx_buf_len),
        frame_handler_(frame_handler), send_byte_(send_byte), send_bytes_(send_bytes)
{
        // buf/len mismatch
        assert(!(rx_buf == nullptr && rx_buf_len != 0)
               && "wirelet::Handler: rx_buf is null but rx_buf_len != 0");
        assert(!(tx_buf == nullptr && tx_buf_len != 0)
               && "wirelet::Handler: tx_buf is null but tx_buf_len != 0");

        assert((frame_handler == nullptr ||
                (rx_buf != nullptr && rx_buf_len != 0))
               && "wirelet::Handler: frame_handler is set but rx_buf/rx_buf_len are invalid");

        assert((send_bytes == nullptr ||
                (tx_buf != nullptr && tx_buf_len != 0))
               && "wirelet::Handler: send_bytes is set but tx_buf/tx_buf_len are invalid");

        bool rx_ok = (frame_handler != nullptr && rx_buf != nullptr && rx_buf_len != 0);
        bool tx_bytes_ok = (send_bytes != nullptr && tx_buf != nullptr && tx_buf_len != 0);
        bool tx_byte_ok = (send_byte != nullptr);
        assert((rx_ok || tx_bytes_ok || tx_byte_ok)
               && "wirelet::Handler: no valid RX or TX path");

        rx_valid_ = rx_ok;
        tx_valid_ = !(tx_buf == nullptr && tx_buf_len != 0) && (tx_byte_ok || tx_bytes_ok);

        reset();
}

void wirelet::Handler::reset()
{
        rx_frame_state = STATE_HEADER;
        rx_frame_id    = 0;
        rx_frame_type  = 0;
        rx_frame_len   = 0;
        rx_frame_crc   = 0;
        rx_crc         = 0;
        rx_buf_offset  = 0;
        rx_header_bytes_seen = 0;

        tx_buf_offset = 0;
}

void wirelet::Handler::_send_stuffed(uint32_t *crc, uint8_t *header_count, uint8_t byte)
{
        if (crc != nullptr)
                wirelet_crc32_step(crc, byte);

        send_byte_(byte);

        // send stuff byte only if seen 2 header bytes
        if (byte == HEADER_BYTE) {
                (*header_count)++;
                if (*header_count == 2) {
                        send_byte_(STUFF_BYTE);
                        *header_count = 0;
                }
        } else {
                *header_count = 0;
        }
}

void wirelet::Handler::_send_frame_byte(uint16_t frame_id,
                            uint16_t frame_type,
                            uint16_t frame_len,
                            const uint8_t *frame)
{
        uint8_t header_count = 0;
        uint32_t crc = 0;

        wirelet_crc32_init(&crc);

        send_byte_(HEADER_BYTE);
        send_byte_(HEADER_BYTE);
        send_byte_(HEADER_BYTE);

        _send_stuffed(&crc, &header_count, frame_id >> 8);
        _send_stuffed(&crc, &header_count, frame_id & 0xff);

        _send_stuffed(&crc, &header_count, frame_type >> 8);
        _send_stuffed(&crc, &header_count, frame_type & 0xff);

        _send_stuffed(&crc, &header_count, frame_len >> 8);
        _send_stuffed(&crc, &header_count, frame_len & 0xff);

        for (uint16_t i = 0; i < frame_len; i++) {
                _send_stuffed(&crc, &header_count, frame[i]);
        }

        wirelet_crc32_finalize(&crc);
        _send_stuffed(nullptr, &header_count, (crc >> 24) & 0xff);
        _send_stuffed(nullptr, &header_count, (crc >> 16) & 0xff);
        _send_stuffed(nullptr, &header_count, (crc >> 8) & 0xff);
        _send_stuffed(nullptr, &header_count, (crc >> 0) & 0xff);

        send_byte_(END_BYTE);
}


void wirelet::Handler::_tx_buf_add_byte_stuffed(uint32_t *crc, uint8_t *header_count, uint8_t byte)
{
        if (crc != nullptr)
                wirelet_crc32_step(crc, byte);

        _tx_buf_add_byte(byte);

        // add stuff byte only if seen 2 header bytes
        if (byte == HEADER_BYTE) {
                (*header_count)++;
                if (*header_count == 2) {
                        _tx_buf_add_byte(STUFF_BYTE);
                        *header_count = 0;
                }
        } else {
                *header_count = 0;
        }
}

void wirelet::Handler::_tx_buf_add_byte(uint8_t byte)
{
        tx_buf[tx_buf_offset] = byte;
        tx_buf_offset++;

        // send tx buf
        if (tx_buf_offset == tx_buf_len) {
                send_bytes_(tx_buf, tx_buf_offset);
                tx_buf_offset = 0;
        }
}

void wirelet::Handler::_send_frame_bytes(uint16_t frame_id,
                                   uint16_t frame_type,
                                   uint16_t frame_len,
                                   const uint8_t *frame)
{
        uint8_t header_count = 0;
        uint32_t crc = 0;

        wirelet_crc32_init(&crc);

        _tx_buf_add_byte(HEADER_BYTE);
        _tx_buf_add_byte(HEADER_BYTE);
        _tx_buf_add_byte(HEADER_BYTE);

        _tx_buf_add_byte_stuffed(&crc, &header_count, frame_id >> 8);
        _tx_buf_add_byte_stuffed(&crc, &header_count, frame_id & 0xff);

        _tx_buf_add_byte_stuffed(&crc, &header_count, frame_type >> 8);
        _tx_buf_add_byte_stuffed(&crc, &header_count, frame_type & 0xff);

        _tx_buf_add_byte_stuffed(&crc, &header_count, frame_len >> 8);
        _tx_buf_add_byte_stuffed(&crc, &header_count, frame_len & 0xff);

        for (uint16_t i = 0; i < frame_len; i++) {
                _tx_buf_add_byte_stuffed(&crc, &header_count, frame[i]);
        }

        wirelet_crc32_finalize(&crc);
        _tx_buf_add_byte_stuffed(nullptr, &header_count, (crc >> 24) & 0xff);
        _tx_buf_add_byte_stuffed(nullptr, &header_count, (crc >> 16) & 0xff);
        _tx_buf_add_byte_stuffed(nullptr, &header_count, (crc >> 8) & 0xff);
        _tx_buf_add_byte_stuffed(nullptr, &header_count, (crc >> 0) & 0xff);

        _tx_buf_add_byte(END_BYTE);

        // send tx buf
        if (tx_buf_offset != 0) {
                send_bytes_(tx_buf, tx_buf_offset);
                tx_buf_offset = 0;
        }
}


bool wirelet::Handler::send_frame(uint16_t frame_id,
                            uint16_t frame_type,
                            uint16_t frame_len,
                            const uint8_t *frame)
{
        if (!is_tx_valid())
                return false;

        // buf/len mismatch
        if (frame == nullptr && frame_len != 0)
                return false;

        if (send_bytes_ != nullptr && tx_buf != nullptr && tx_buf_len != 0) {
                // if buffer to small try sending by byte (should be faster)
                if (tx_buf_len <= 2 && send_byte_ != nullptr) {
                        _send_frame_byte(frame_id, frame_type, frame_len, frame);
                        return true;
                }

                _send_frame_bytes(frame_id, frame_type, frame_len, frame);
                return true;
        }

        if (send_byte_ != nullptr) {
                _send_frame_byte(frame_id, frame_type, frame_len, frame);
                return true;
        }

        return false;
}

void wirelet::Handler::poll_byte(uint8_t byte)
{
        if (is_rx_valid())
                _poll_byte_unchecked(byte);
}

void wirelet::Handler::_poll_byte_unchecked(uint8_t byte)
{
        if (rx_header_bytes_seen == 2) {
                rx_header_bytes_seen = 0;
                if (byte == HEADER_BYTE) {
                        rx_frame_state = STATE_ID0;
                        return;
                }
                // skip stuff byte if seen 2 header bytes
                if (byte == STUFF_BYTE) {
                        return;
                } else {
                        rx_frame_state = STATE_HEADER;
                        return;
                }
        }

        if (byte == HEADER_BYTE) {
                rx_header_bytes_seen++;
        } else {
                rx_header_bytes_seen = 0;
        }

        switch (rx_frame_state) {
        case STATE_HEADER:
                break;

        case STATE_ID0:
                rx_frame_id = (uint16_t)byte << 8;
                rx_frame_state = STATE_ID1;
                wirelet_crc32_init(&rx_crc);
                wirelet_crc32_step(&rx_crc, byte);
                break;
        case STATE_ID1:
                rx_frame_id |= (uint16_t)byte;
                rx_frame_state = STATE_TYPE0;
                wirelet_crc32_step(&rx_crc, byte);
                break;

        case STATE_TYPE0:
                rx_frame_type = (uint16_t)byte << 8;
                rx_frame_state = STATE_TYPE1;
                wirelet_crc32_step(&rx_crc, byte);
                break;
        case STATE_TYPE1:
                rx_frame_type |= (uint16_t)byte;
                rx_frame_state = STATE_LEN0;
                wirelet_crc32_step(&rx_crc, byte);
                break;

        case STATE_LEN0:
                rx_frame_len = (uint16_t)byte << 8;
                rx_frame_state = STATE_LEN1;
                wirelet_crc32_step(&rx_crc, byte);
                break;
        case STATE_LEN1:
                rx_frame_len |= (uint16_t)byte;
                rx_frame_state = STATE_PAYLOAD;
                wirelet_crc32_step(&rx_crc, byte);
                if (rx_frame_len == 0) {
                        rx_frame_state = STATE_CRC0;
                } else if ( rx_frame_len > rx_buf_len) {
                        rx_frame_state = STATE_HEADER;
                        rx_header_bytes_seen = 0;
                } else {
                        rx_buf_offset = 0;
                }
                break;

        case STATE_PAYLOAD:
                rx_frame_buf[rx_buf_offset] = byte;
                rx_buf_offset++;
                wirelet_crc32_step(&rx_crc, byte);
                if (rx_buf_offset == rx_frame_len) {
                        rx_frame_state = STATE_CRC0;
                }
                break;

        case STATE_CRC0:
                rx_frame_crc = (uint32_t)byte << 24;
                rx_frame_state = STATE_CRC1;
                break;
        case STATE_CRC1:
                rx_frame_crc |= (uint32_t)byte << 16;
                rx_frame_state = STATE_CRC2;
                break;
        case STATE_CRC2:
                rx_frame_crc |= (uint32_t)byte << 8;
                rx_frame_state = STATE_CRC3;
                break;
        case STATE_CRC3:
                rx_frame_crc |= (uint32_t)byte;
                wirelet_crc32_finalize(&rx_crc);
                if (rx_crc == rx_frame_crc) {
                        rx_frame_state = STATE_END;
                } else {
                        rx_frame_state = STATE_HEADER;
                        rx_header_bytes_seen = 0;
                }
                break;

        case STATE_END:
                if (byte == END_BYTE && frame_handler_ != nullptr)
                        frame_handler_(rx_frame_id, rx_frame_type,
                                      rx_frame_len, rx_frame_buf);
                rx_frame_state = STATE_HEADER;
        }
}

bool wirelet::Handler::poll_bytes(const uint8_t *bytes, uint32_t bytes_len)
{
        if (!is_rx_valid())
                return false;

        if (bytes == nullptr && bytes_len != 0)
                return false;

        for (uint32_t i = 0; i < bytes_len; i++) {
                _poll_byte_unchecked(bytes[i]);
        }

        return true;
}