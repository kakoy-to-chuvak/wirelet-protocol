"""Independent reference codec and deterministic test data for Wirelet.

This module deliberately does not import the implementation under test.  It is
used by the test runner to produce expected wire data and by the Python unit
tests as a second opinion.
"""

import binascii
import random
import struct
from collections import namedtuple

HEADER_BYTE = 0xAA
STUFF_BYTE = 0x55
END_BYTE = 0x55


TestVector = namedtuple("TestVector", "frame_id frame_type payload encoded")


def _stuff(body):
    out = bytearray()
    seen = 0
    for byte in body:
        out.append(byte)
        if byte == HEADER_BYTE:
            seen += 1
            if seen == 2:
                out.append(STUFF_BYTE)
                seen = 0
        else:
            seen = 0
    return bytes(out)


def _wire_from_body(body):
    return bytes((HEADER_BYTE, HEADER_BYTE, HEADER_BYTE)) + _stuff(body) + bytes((END_BYTE,))


def body(frame_id, frame_type, payload):
    """Return the unescaped body (header fields, payload and CRC)."""
    header = struct.pack(">HHH", frame_id, frame_type, len(payload))
    crc = binascii.crc32(header + payload) & 0xFFFFFFFF
    return header + payload + struct.pack(">I", crc)


def encode(frame_id, frame_type, payload):
    """Reference encoder, independent of py/wirelet.py."""
    if len(payload) > 0xFFFF:
        raise ValueError("payload too large")
    return _wire_from_body(body(frame_id, frame_type, payload))


def vectors():
    """Return a deterministic set of frame vectors covering stuffing edges."""
    result = []

    def add(frame_id, frame_type, payload):
        result.append(TestVector(frame_id, frame_type, payload,
                                 encode(frame_id, frame_type, payload)))

    structured = [
        (0x0001, 0x0002, b""),
        (0x0102, 0x0304, b"hello"),
        (0xAAAA, 0x5555, b"\xaa"),
        (0xAAAA, 0xAAAA, b"\xaa\xaa"),
        (0x00AA, 0xAA00, b"\xaa\xaa\xaa"),
        (0xAA55, 0x55AA, b"\xaa\xaa\x55"),
        (0x0055, 0x00AA, b"\x55\x55"),
        (0xAAAA, 0x0000, b"\xaa\x00\xaa"),
        (0x1234, 0x5678, bytes(range(256))),
        (0xAAAA, 0xAAAA, b"\xaa" * 255),
        (0x5555, 0xAAAA, b"\xaa" * 256),
        (0x1234, 0x5678, b"\x55" * 300),
        (0xAA00, 0x00AA, b"\xaa" * 0x00AA),
        (0x0000, 0xFFFF, b"\xaa\x55" * 100),
        (0xFFFF, 0xFFFF, bytes(range(256)) * 4),
    ]
    for frame_id, frame_type, payload in structured:
        add(frame_id, frame_type, payload)

    rng = random.Random(20261023)
    for _ in range(100):
        length = rng.randrange(0, 201)
        payload = bytes(rng.choice((HEADER_BYTE, STUFF_BYTE, rng.randrange(256)))
                        for _ in range(length))
        add(rng.randrange(0x10000), rng.randrange(0x10000), payload)

    return result


def clean_stream(test_vectors=None):
    """Concatenate all reference frames into one valid stream."""
    if test_vectors is None:
        test_vectors = vectors()
    return b"".join(v.encoded for v in test_vectors)


GOOD_FRAME_1 = (0x1111, 0x2222, b"one")
GOOD_FRAME_2 = (0xAAAA, 0x55AA, b"\xaa\xaa\x00\x55")
GOOD_FRAME_3 = (0x3333, 0x4444, b"three")


def resync_cases():
    """Return {name: (stream, expected_last_frame)} recovery scenarios.

    The parser is allowed to lose the frame immediately following ambiguous
    noise, but the last frame in every case must be recovered.
    """
    good1 = encode(*GOOD_FRAME_1)
    good2 = encode(*GOOD_FRAME_2)
    good3 = encode(*GOOD_FRAME_3)
    cases = {}

    def add(name, stream, expected=GOOD_FRAME_2):
        cases[name] = (stream, expected)

    bad = bytearray(body(1, 2, b"bad"))
    bad[-1] = HEADER_BYTE
    add("bad_crc_crc3_aa", _wire_from_body(bytes(bad)) + good1 + good2)

    bad = bytearray(body(1, 2, b"bad"))
    bad[-2] = HEADER_BYTE
    bad[-1] = HEADER_BYTE
    add("bad_crc_crc23_aa", _wire_from_body(bytes(bad)) + good1 + good2)

    add("truncated", encode(1, 2, b"truncated")[:9] + good1 + good2)
    add("missing_end", encode(1, 2, b"x")[:-1] + good1 + good2)

    head = struct.pack(">HHH", 1, 2, 0xFFFF)
    crc = binascii.crc32(head) & 0xFFFFFFFF
    oversize = _wire_from_body(head + struct.pack(">I", crc))
    add("oversize_then_good", oversize + good1 + good2)

    add("noise_one_aa", b"\x42\xaa" + good1 + good2)
    add("noise_four_aa", b"\xaa\xaa\xaa\xaa" + good1 + good2)
    add("noise_five_aa", b"\xaa\xaa\xaa\xaa\xaa" + good1 + good2)

    corrupt = bytearray(encode(1, 2, b"corruptme"))
    corrupt[8] ^= 0xFF
    add("corrupt_payload", bytes(corrupt) + good1 + good2)

    cases["back_to_back"] = (good1 + good2 + good3, GOOD_FRAME_3)
    return cases
