#!/usr/bin/env python3
"""Build and run the Wirelet cross-language test suite."""

import os
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
BUILD = TESTS / "build"

sys.path.insert(0, str(ROOT / "py"))
sys.path.insert(0, str(TESTS))

import reference  # noqa: E402
import wirelet  # noqa: E402

VECTOR_MAGIC = 0x56544C57


def write_vectors(path, vectors):
    with path.open("wb") as stream:
        stream.write(struct.pack("<II", VECTOR_MAGIC, len(vectors)))
        for vector in vectors:
            stream.write(struct.pack("<HHHI", vector.frame_id,
                                     vector.frame_type,
                                     len(vector.payload),
                                     len(vector.encoded)))
            stream.write(vector.payload)
            stream.write(vector.encoded)


def build_tests():
    cc = os.environ.get("CC", "cc")
    cxx = os.environ.get("CXX", "c++")
    common_c = ["-std=c99", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
    common_cpp = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]

    commands = [
        [cc, *common_c, "-I", str(ROOT / "c"), "-I", str(TESTS),
         str(TESTS / "test_c.c"), str(ROOT / "c" / "wirelet.c"),
         str(ROOT / "c" / "crc32.c"), "-o", str(BUILD / "test_c")],
        [cc, *common_c, "-I", str(ROOT / "cpp"), "-c",
         str(ROOT / "cpp" / "crc32.c"), "-o", str(BUILD / "crc32.o")],
        [cxx, *common_cpp, "-DNDEBUG", "-I", str(ROOT / "cpp"), "-I", str(TESTS),
         str(TESTS / "test_cpp.cpp"), str(ROOT / "cpp" / "wirelet.cpp"),
         str(BUILD / "crc32.o"), "-o", str(BUILD / "test_cpp")],
        # Keep an assert-enabled build honest as well.  The test binary itself
        # is built with NDEBUG so that release-only validation can be tested.
        [cxx, *common_cpp, "-I", str(ROOT / "cpp"), "-c",
         str(ROOT / "cpp" / "wirelet.cpp"), "-o", str(BUILD / "wirelet_debug.o")],
    ]

    for command in commands:
        print("+", " ".join(command), flush=True)
        subprocess.run(command, cwd=ROOT, check=True)


def run(command):
    subprocess.run([str(part) for part in command], cwd=ROOT, check=True)


def run_stream(binary, path, rx_len):
    result = subprocess.run(
        [str(binary), "stream", str(path), str(rx_len)],
        cwd=ROOT, check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    return result.stdout


def parse_frames(text):
    frames = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) == 2:
            payload = b""
        elif len(parts) == 3:
            payload = bytes.fromhex(parts[2])
        else:
            raise SystemExit(f"cannot parse decoder output line: {line!r}")
        frames.append((int(parts[0], 16), int(parts[1], 16), payload))
    return frames


def python_frames(data, max_frame_len):
    frames = []

    def on_frame(frame_id, frame_type, payload):
        frames.append((frame_id, frame_type, payload))

    handler = wirelet.Handler(on_frame, None, None,
                              max_frame_len=max_frame_len)
    handler.poll_bytes(data)
    return frames


def first_difference(left, right):
    for index, (a, b) in enumerate(zip(left, right)):
        if a != b:
            return index, a, b
    if len(left) != len(right):
        return min(len(left), len(right)), None, None
    return None


def check_agreement(name, data, rx_len, expected=None, expected_last=None):
    path = BUILD / f"{name}.bin"
    path.write_bytes(data)

    c_frames = parse_frames(run_stream(BUILD / "test_c", path, rx_len))
    cpp_frames = parse_frames(run_stream(BUILD / "test_cpp", path, rx_len))
    py_frames = python_frames(data, rx_len)

    if c_frames != cpp_frames:
        diff = first_difference(c_frames, cpp_frames)
        raise SystemExit(f"{name}: C/C++ disagree at {diff}")
    if c_frames != py_frames:
        diff = first_difference(c_frames, py_frames)
        raise SystemExit(f"{name}: C/Python disagree at {diff}")

    if expected is not None and c_frames != expected:
        diff = first_difference(c_frames, expected)
        raise SystemExit(f"{name}: wrong decoded frames at {diff}")
    if expected_last is not None:
        if not c_frames:
            raise SystemExit(f"{name}: no frame recovered")
        if c_frames[-1] != expected_last:
            raise SystemExit(
                f"{name}: last frame {c_frames[-1]!r}, expected {expected_last!r}")

    return c_frames


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    vectors = reference.vectors()
    write_vectors(BUILD / "vectors.bin", vectors)
    clean = reference.clean_stream(vectors)
    (BUILD / "clean.bin").write_bytes(clean)

    print(f"generated {len(vectors)} reference vectors", flush=True)

    build_tests()

    run([BUILD / "test_c", "vectors", BUILD / "vectors.bin"])
    run([BUILD / "test_c", "config"])
    run([BUILD / "test_c", "max"])
    run([BUILD / "test_cpp", "vectors", BUILD / "vectors.bin"])
    run([BUILD / "test_cpp", "config"])
    run([BUILD / "test_cpp", "max"])

    run([sys.executable, "-m", "unittest", "discover", "-s", str(TESTS),
         "-p", "test_python.py", "-v"])

    expected_all = [(v.frame_id, v.frame_type, v.payload) for v in vectors]
    max_payload = max(len(v.payload) for v in vectors)
    for rx_len in (64, 256, 4096, 65535):
        expected = expected_all if rx_len >= max_payload else None
        frames = check_agreement(f"clean_{rx_len}", clean, rx_len,
                                 expected=expected)
        print(f"clean stream, rx_len={rx_len}: {len(frames)} frames agree",
              flush=True)

    for name, (stream, expected_last) in reference.resync_cases().items():
        frames = check_agreement(f"resync_{name}", stream, 4096,
                                 expected_last=expected_last)
        if name == "back_to_back":
            expected = [reference.GOOD_FRAME_1, reference.GOOD_FRAME_2,
                        reference.GOOD_FRAME_3]
            if frames != expected:
                raise SystemExit(f"{name}: wrong frame sequence {frames!r}")
        print(f"resync {name}: {len(frames)} frames agree", flush=True)

    print(f"All tests passed ({len(vectors)} vectors, "
          f"{len(reference.resync_cases())} resync cases)", flush=True)


if __name__ == "__main__":
    main()
