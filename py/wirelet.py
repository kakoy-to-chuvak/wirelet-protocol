# wirelet-protocol
#
# Copyright (c) 2014-2017 JK Energy Ltd.
# Copyright (c) 2026 kto-to
#
# SPDX-License-Identifier: MIT
#
# This file is part of wirelet-protocol, a modified version of the MIN protocol.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

from binascii import crc32
from enum import IntEnum



HEADER_BYTE = 0xAA
STUFF_BYTE  = 0x55
END_BYTE    = 0x55

class State(IntEnum):
    HEADER  = 0
    ID0     = 1
    ID1     = 2
    TYPE0   = 3
    TYPE1   = 4
    LEN0    = 5
    LEN1    = 6
    PAYLOAD = 7
    CRC0    = 8
    CRC1    = 9
    CRC2    = 10
    CRC3    = 11
    END     = 12

class Handler:
    def __init__(self, frame_handler, send_byte, send_bytes, max_frame_len = 0xFFFF):
        if frame_handler is None and send_byte is None and send_bytes is None:
            raise ValueError("wirelet.Handler: no valid RX or TX path")

        self.handle_frame = frame_handler
        self.send_byte = send_byte
        self.send_bytes = send_bytes

        self.max_frame_len = max_frame_len

        self.rx_frame_state: State = State.HEADER
        self.rx_frame_buf: bytearray = bytearray()
        self.rx_frame_id: int = 0
        self.rx_frame_type: int = 0
        self.rx_frame_len: int = 0
        self.rx_frame_crc: int = 0
        self.rx_header_bytes_seen: int = 0

        self.tx_buf: bytearray = bytearray()
        self.tx_header_bytes_seen: int = 0

    def reset(self):
        self.rx_frame_state = State.HEADER
        self.rx_frame_buf = bytearray()
        self.rx_frame_id = 0
        self.rx_frame_type = 0
        self.rx_frame_len = 0
        self.rx_frame_crc = 0
        self.rx_header_bytes_seen = 0

        self.tx_buf = bytearray()
        self.tx_header_bytes_seen = 0

    def send_frame(self, frame_id: int, frame_type: int, frame: bytes) -> bool:
        if self.send_bytes is None and self.send_byte is None:
            return False

        if len(frame) > 65535:
            return False

        raw = (frame_id.to_bytes(2, 'big')
           + frame_type.to_bytes(2, 'big')
           + len(frame).to_bytes(2, 'big')
           + bytes(frame))
        crc = crc32(raw) & 0xFFFFFFFF

        self.tx_buf = bytearray()
        self.tx_header_bytes_seen = 0
        self._tx_buf_add_bytes_stuffed(raw)
        self._tx_buf_add_bytes_stuffed(crc.to_bytes(4, 'big'))

        out = bytearray([HEADER_BYTE, HEADER_BYTE, HEADER_BYTE])
        out += self.tx_buf
        out.append(END_BYTE)

        if self.send_bytes is not None:
            self.send_bytes(bytes(out))
            return True
        elif self.send_byte is not None:
            for byte in out:
                self.send_byte(byte)
            return True

    def poll_byte(self, byte) -> bool:
        if self.handle_frame is None or byte is None:
            return False

        if self.rx_header_bytes_seen == 2:
            self.rx_header_bytes_seen = 0
            if byte == HEADER_BYTE:
                self.rx_frame_state = State.ID0
                return True
            elif byte == STUFF_BYTE:
                return True
            else:
                self.rx_frame_state = State.HEADER
                return True

        if byte == HEADER_BYTE:
            self.rx_header_bytes_seen += 1
        else:
            self.rx_header_bytes_seen = 0

        if self.rx_frame_state == State.ID0:
            self.rx_frame_id = byte << 8
            self.rx_frame_state = State.ID1
        elif self.rx_frame_state == State.ID1:
            self.rx_frame_id |= byte
            self.rx_frame_state = State.TYPE0

        elif self.rx_frame_state == State.TYPE0:
            self.rx_frame_type = byte << 8
            self.rx_frame_state = State.TYPE1
        elif self.rx_frame_state == State.TYPE1:
            self.rx_frame_type |= byte
            self.rx_frame_state = State.LEN0

        elif self.rx_frame_state == State.LEN0:
                self.rx_frame_len = byte << 8
                self.rx_frame_state = State.LEN1
        elif self.rx_frame_state == State.LEN1:
            self.rx_frame_len |= byte
            self.rx_frame_buf = bytearray()
            if self.rx_frame_len == 0:
                self.rx_frame_state = State.CRC0
            elif self.rx_frame_len > self.max_frame_len:
                self.rx_frame_state = State.HEADER
                self.rx_header_bytes_seen = 0
            else:
                self.rx_frame_state = State.PAYLOAD

        elif self.rx_frame_state == State.PAYLOAD:
            self.rx_frame_buf.append(byte)
            if len(self.rx_frame_buf) == self.rx_frame_len:
                self.rx_frame_state = State.CRC0

        elif self.rx_frame_state == State.CRC0:
            self.rx_frame_crc = byte << 24
            self.rx_frame_state = State.CRC1
        elif self.rx_frame_state == State.CRC1:
            self.rx_frame_crc |= byte << 16
            self.rx_frame_state = State.CRC2
        elif self.rx_frame_state == State.CRC2:
            self.rx_frame_crc |= byte << 8
            self.rx_frame_state = State.CRC3
        elif self.rx_frame_state == State.CRC3:
            self.rx_frame_crc |= byte
            self.rx_frame_crc &= 0xFFFFFFFF
            crc = crc32(
                self.rx_frame_id.to_bytes(2, 'big') +
                self.rx_frame_type.to_bytes(2, 'big') +
                self.rx_frame_len.to_bytes(2, 'big') +
                self.rx_frame_buf
            )
            if crc == self.rx_frame_crc:
                    self.rx_frame_state = State.END
            else:
                self.rx_frame_state = State.HEADER
                self.rx_header_bytes_seen = 0

        elif self.rx_frame_state == State.END:
            if byte == END_BYTE and self.handle_frame is not None:
                self.handle_frame(self.rx_frame_id, self.rx_frame_type, bytes(self.rx_frame_buf))
            self.rx_frame_state = State.HEADER

        return True

    def poll_bytes(self, data: bytes) -> bool:
        if self.handle_frame is None or data is None:
            return False
        for byte in data:
            self.poll_byte(byte)
        return True

    def _tx_buf_add_bytes_stuffed(self, data: bytes):
        for byte in data:
            self.tx_buf.append(byte)
            if byte == HEADER_BYTE:
                self.tx_header_bytes_seen += 1
                if self.tx_header_bytes_seen == 2:
                    self.tx_buf.append(STUFF_BYTE)
                    self.tx_header_bytes_seen = 0
            else:
                self.tx_header_bytes_seen = 0