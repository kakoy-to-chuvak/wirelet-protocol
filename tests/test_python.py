"""Tests for the Python implementation of Wirelet."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "py"))
sys.path.insert(0, str(ROOT / "tests"))

import reference  # noqa: E402
import wirelet  # noqa: E402


class WireletTests(unittest.TestCase):
    def test_encode_matches_reference(self):
        for vector in reference.vectors():
            with self.subTest(frame_id=vector.frame_id,
                              frame_type=vector.frame_type,
                              payload_len=len(vector.payload)):
                sent = []
                handler = wirelet.Handler(None, None, sent.append)
                self.assertTrue(handler.send_frame(
                    vector.frame_id, vector.frame_type, vector.payload))
                self.assertEqual(sent, [vector.encoded])

                byte_sent = []
                handler = wirelet.Handler(None, byte_sent.append, None)
                self.assertTrue(handler.send_frame(
                    vector.frame_id, vector.frame_type, vector.payload))
                self.assertEqual(bytes(byte_sent), vector.encoded)

    def test_loopback(self):
        vector = reference.TestVector(
            0x1234, 0x5678, b"\xaa\x55\xaa\xaa",
            reference.encode(0x1234, 0x5678, b"\xaa\x55\xaa\xaa"))
        sent = []
        received = []
        handler = wirelet.Handler(
            lambda frame_id, frame_type, payload:
                received.append((frame_id, frame_type, payload)),
            None, sent.append)

        self.assertTrue(handler.send_frame(
            vector.frame_id, vector.frame_type, vector.payload))
        self.assertTrue(handler.poll_bytes(sent[0]))
        self.assertEqual(received, [(vector.frame_id, vector.frame_type,
                                     vector.payload)])

    def test_clean_stream(self):
        vectors = reference.vectors()
        received = []
        handler = wirelet.Handler(
            lambda frame_id, frame_type, payload:
                received.append((frame_id, frame_type, payload)),
            None, None)

        self.assertTrue(handler.poll_bytes(reference.clean_stream(vectors)))
        self.assertEqual(received, [(v.frame_id, v.frame_type, v.payload)
                                    for v in vectors])

    def test_poll_byte(self):
        vector = reference.vectors()[1]
        received = []
        handler = wirelet.Handler(
            lambda frame_id, frame_type, payload:
                received.append((frame_id, frame_type, payload)),
            None, None)

        for byte in vector.encoded:
            self.assertTrue(handler.poll_byte(byte))
        self.assertEqual(received, [(vector.frame_id, vector.frame_type,
                                     vector.payload)])

    def test_resync(self):
        for name, (stream, expected) in reference.resync_cases().items():
            with self.subTest(name=name):
                received = []
                handler = wirelet.Handler(
                    lambda frame_id, frame_type, payload:
                        received.append((frame_id, frame_type, payload)),
                    None, None, max_frame_len=4096)
                self.assertTrue(handler.poll_bytes(stream))
                self.assertTrue(received, "no frame recovered")
                self.assertEqual(received[-1], expected)

    def test_max_frame_len(self):
        payload = b"x" * 65
        encoded = reference.encode(1, 2, payload)

        received = []
        handler = wirelet.Handler(
            lambda frame_id, frame_type, data:
                received.append((frame_id, frame_type, data)),
            None, None, max_frame_len=64)
        handler.poll_bytes(encoded)
        self.assertEqual(received, [])

        handler = wirelet.Handler(
            lambda frame_id, frame_type, data:
                received.append((frame_id, frame_type, data)),
            None, None, max_frame_len=65)
        handler.poll_bytes(encoded)
        self.assertEqual(received, [(1, 2, payload)])

        empty = reference.encode(3, 4, b"")
        handler = wirelet.Handler(
            lambda frame_id, frame_type, data:
                received.append((frame_id, frame_type, data)),
            None, None, max_frame_len=0)
        handler.poll_bytes(empty)
        self.assertEqual(received[-1], (3, 4, b""))

    def test_configuration(self):
        with self.assertRaises(ValueError):
            wirelet.Handler(None, None, None)

        received = []
        rx_only = wirelet.Handler(
            lambda frame_id, frame_type, payload:
                received.append((frame_id, frame_type, payload)),
            None, None)
        self.assertFalse(rx_only.send_frame(1, 2, b""))
        self.assertTrue(rx_only.poll_bytes(b""))

        sent = []
        tx_only = wirelet.Handler(None, None, sent.append)
        self.assertTrue(tx_only.send_frame(1, 2, b""))
        self.assertEqual(len(sent), 1)
        self.assertFalse(tx_only.poll_bytes(b""))

        huge = wirelet.Handler(None, None, sent.append)
        before = len(sent)
        self.assertFalse(huge.send_frame(1, 2, b"\x00" * 65536))
        self.assertEqual(len(sent), before)

    def test_reset(self):
        vector = reference.vectors()[2]
        received = []
        handler = wirelet.Handler(
            lambda frame_id, frame_type, payload:
                received.append((frame_id, frame_type, payload)),
            None, None)

        handler.poll_bytes(vector.encoded[:5])
        handler.reset()
        handler.poll_bytes(vector.encoded)
        self.assertEqual(received, [(vector.frame_id, vector.frame_type,
                                     vector.payload)])

    def test_max_payload(self):
        payload = b"\xaa" * 0xFFFF
        sent = []
        received = []
        handler = wirelet.Handler(
            lambda frame_id, frame_type, data:
                received.append((frame_id, frame_type, data)),
            None, sent.append)

        self.assertTrue(handler.send_frame(0xFFFF, 0xAAAA, payload))
        self.assertTrue(handler.poll_bytes(sent[0]))
        self.assertEqual(received, [(0xFFFF, 0xAAAA, payload)])


if __name__ == "__main__":
    unittest.main()
