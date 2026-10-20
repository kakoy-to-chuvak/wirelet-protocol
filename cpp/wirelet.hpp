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

#ifndef WIRELET_HPP
#define WIRELET_HPP


#include <cstdint>

namespace wirelet {
enum State {
        STATE_HEADER,
        STATE_ID0,
        STATE_ID1,
        STATE_TYPE0,
        STATE_TYPE1,
        STATE_LEN0,
        STATE_LEN1,
        STATE_PAYLOAD,
        STATE_CRC0,
        STATE_CRC1,
        STATE_CRC2,
        STATE_CRC3,
        STATE_END,
};

typedef void (*handle_frame)(uint16_t id, uint16_t type, uint16_t payload_len, uint8_t *payload);
typedef void (*send_byte)(uint8_t byte);
typedef void (*send_bytes)(uint8_t *bytes, uint32_t bytes_len);



class Handler {
private:
        State rx_frame_state = STATE_HEADER;
        uint8_t *rx_frame_buf  = nullptr;
        uint32_t rx_buf_offset = 0; 
        uint32_t rx_buf_len    = 0;
        uint16_t rx_frame_id   = 0;
        uint16_t rx_frame_type = 0;
        uint16_t rx_frame_len  = 0;
        uint32_t rx_frame_crc  = 0;
        uint32_t rx_crc        = 0;
        uint8_t rx_header_bytes_seen = 0;

        uint8_t *tx_buf = nullptr;
        uint32_t tx_buf_offset = 0;
        uint32_t tx_buf_len = 0;

        handle_frame frame_handler_ = nullptr;
        send_byte send_byte_        = nullptr;
        send_bytes send_bytes_      = nullptr;

        bool tx_valid_ = false;
        bool rx_valid_ = false;

        void _send_stuffed(uint32_t *crc,
                   uint8_t *header_count, uint8_t byte);

        void _send_frame_byte(uint16_t frame_id,
                              uint16_t frame_type,
                              uint16_t frame_len, 
                              const uint8_t *frame);

        void _tx_buf_add_byte_stuffed(uint32_t *crc, uint8_t *header_count, uint8_t byte);

        void _tx_buf_add_byte(uint8_t byte);

        void _send_frame_bytes(uint16_t frame_id,
                               uint16_t frame_type,
                               uint16_t frame_len, 
                               const uint8_t *frame);
        
        void _poll_byte_unchecked(uint8_t byte);
        
public:
        Handler(wirelet::handle_frame _frame_handler,
                wirelet::send_bytes _send_bytes,
                wirelet::send_byte _send_byte,
                uint8_t *_rx_buf,
                uint32_t _rx_buf_len,
                uint8_t *_tx_buf,
                uint32_t _tx_buf_len);
        
        bool is_valid() const noexcept { return tx_valid_ || rx_valid_; }
        bool is_tx_valid() const noexcept { return tx_valid_; }
        bool is_rx_valid() const noexcept { return rx_valid_; }

        void reset();
        
        bool send_frame(uint16_t _frame_id,
                        uint16_t _frame_type,
                        uint16_t _frame_len, 
                        const uint8_t *_frame);

        void poll_byte(uint8_t _byte);

        bool poll_bytes(const uint8_t *_bytes, uint32_t _bytes_len);
};
}



#endif // WIRELET_HPP