# Wirelet

Wirelet is a small, dependency-free framed serial protocol for communication
between microcontrollers and host devices.

It is inspired by [MIN](https://github.com/min-protocol/min), but **it is not
wire-compatible with MIN 2.0**. Do not use Wirelet as a drop-in replacement for
MIN.

## Features

- C99, C++11+ and Python 3.10+ implementations.
- 16-bit frame IDs and 16-bit frame types.
- Payloads from 0 to 65535 bytes.
- CRC-32 protection against accidental corruption.
- Byte stuffing for reliable frame delimiting.
- Non-blocking byte-oriented state machine.
- No OS dependencies.
- No dynamic allocation in the C and C++ implementations; buffers are provided
  by the caller.

Wirelet is a framing layer, not a complete transport. It does not provide
acknowledgements, retransmission, flow control, addressing, authentication or
encryption.

## Protocol

All multi-byte fields are transmitted in **big-endian** order.

### Frame format

| Field          | Size       | Description                                      |
|----------------|------------|--------------------------------------------------|
| Preamble       | 3 bytes    | `0xAA 0xAA 0xAA`                                 |
| Frame ID       | 2 bytes    | Application-defined `uint16_t`                   |
| Frame type     | 2 bytes    | Application-defined `uint16_t`                   |
| Payload length | 2 bytes    | Number of payload bytes, `0..65535`              |
| Payload        | N bytes    | Application data                                 |
| CRC-32         | 4 bytes    | CRC over ID, type, length and payload            |
| End            | 1 byte     | `0x55`                                           |

The CRC is standard CRC-32/ISO-HDLC, compatible with `zlib.crc32` and Python's
`binascii.crc32`. It uses the reflected polynomial `0xEDB88320`, an initial
value of `0xFFFFFFFF`, and a final XOR with `0xFFFFFFFF`.

### Byte stuffing

The frame body is escaped so that the preamble and end marker remain
unambiguous:

- After every two consecutive `AA` bytes in the body, insert a `55` byte.
- On receive, after seeing two consecutive `AA` bytes, skip a following `55`
  byte.
- The 3-byte preamble and the final `55` end marker are sent unescaped.
- The CRC covers the unescaped values, and the stuffed `55` bytes are not part
  of the CRC.

For example, the body bytes

```text
AA AA
```

are transmitted as

```text
AA AA 55
```

### Limits and recovery

- The maximum payload length is 65535 bytes.
- The C and C++ receivers discard a frame if its declared payload length is
  larger than the supplied RX buffer.
- The Python receiver discards a frame if its length is larger than
  `max_frame_len` (default `0xFFFF`).
- A frame with an invalid CRC is discarded.
- After a discarded frame, the receiver looks for the next preamble and
  resynchronizes.

## Repository layout

```text
c/       C implementation
cpp/     C++ implementation
py/      Python implementation
protocol.txt
LICENSE
```

## Usage

### C

Compile `c/wirelet.c` and `c/crc32.c` with the rest of your application:

```bash
cc -std=c99 -Wall -Wextra -Wpedantic -I c \
   c/wirelet.c c/crc32.c your_app.c -o your_app
```

Minimal example:

```c
#include <stddef.h>
#include <stdint.h>

#include "wirelet.h"

static struct wirelet_context link;
static uint8_t rx_buf[256];

static void uart_write_byte(uint8_t byte)
{
    (void)byte; /* write one byte to the UART */
}

static void on_frame(uint16_t id, uint16_t type,
                     uint16_t payload_len, uint8_t *payload)
{
    (void)id;
    (void)type;
    (void)payload_len;
    (void)payload;
    /* Handle the complete, CRC-checked frame here. */
}

void wirelet_init(void)
{
    if (!wirelet_context_init(&link,
                              on_frame,
                              uart_write_byte,
                              NULL,
                              rx_buf, sizeof rx_buf,
                              NULL, 0)) {
        /* Invalid configuration: no usable RX or TX path. */
    }
}

void wirelet_send_example(void)
{
    const uint8_t payload[] = {0x01, 0x02, 0x03};

    (void)wirelet_send_frame(&link, 0x0001, 0x0002,
                             sizeof payload, payload);
}

void wirelet_receive_byte(uint8_t byte)
{
    wirelet_poll_byte(&link, byte);
}
```

`on_frame` receives a pointer to the RX buffer. The pointer is valid only for
the duration of the callback.

### C++

Compile `cpp/wirelet.cpp` and compile `cpp/crc32.c` as C, then link them
together:

```bash
cc -std=c99 -I cpp -c cpp/crc32.c -o crc32.o
c++ -std=c++17 -Wall -Wextra -Wpedantic -I cpp \
    cpp/wirelet.cpp crc32.o your_app.cpp -o your_app
```

Minimal example:

```cpp
#include <cstddef>
#include <cstdint>

#include "wirelet.hpp"

static uint8_t rx_buf[256];

static void uart_write_byte(uint8_t byte)
{
    (void)byte; // write one byte to the UART
}

static void on_frame(uint16_t id, uint16_t type,
                     uint16_t payload_len, uint8_t *payload)
{
    (void)id;
    (void)type;
    (void)payload_len;
    (void)payload;
    // Handle the complete, CRC-checked frame here.
}

static wirelet::Handler link(
    on_frame,
    nullptr,                         // send_bytes
    uart_write_byte,                 // send_byte
    rx_buf, static_cast<uint32_t>(sizeof rx_buf),
    nullptr, 0);                     // tx_buf, tx_buf_len

void wirelet_init()
{
    if (!link.is_valid()) {
        // Invalid configuration.
    }
}

void wirelet_send_example()
{
    const uint8_t payload[] = {0x01, 0x02, 0x03};

    (void)link.send_frame(0x0001, 0x0002,
                          sizeof payload, payload);
}

void wirelet_receive_byte(uint8_t byte)
{
    link.poll_byte(byte);
}
```

The C++ constructor uses `assert` for invalid configurations in debug builds.
Check `is_valid()`, `is_tx_valid()` or `is_rx_valid()` in release builds.

### Python

Add the `py/` directory to `PYTHONPATH` and import the `wirelet` module:

```bash
PYTHONPATH=py python3 your_script.py
```

Minimal loopback example:

```python
from wirelet import Handler


def on_frame(frame_id, frame_type, payload):
    print(
        f"id=0x{frame_id:04X} "
        f"type=0x{frame_type:04X} "
        f"payload={payload.hex()}"
    )


sent = []
link = Handler(on_frame, None, sent.append)

link.send_frame(0x0001, 0x0002, b"hello")
link.poll_bytes(b"".join(sent))
```

For a real serial port, replace `sent.append` with a function that writes the
encoded bytes to the port, and feed received bytes into `poll_byte()` or
`poll_bytes()`.

Python 3.10 or newer is required because the implementation uses structural
pattern matching.

## API summary

| Operation      | C                          | C++                     | Python                  |
|----------------|----------------------------|-------------------------|-------------------------|
| Initialize     | `wirelet_context_init()`   | `wirelet::Handler()`    | `Handler()`             |
| Send a frame   | `wirelet_send_frame()`     | `link.send_frame()`     | `link.send_frame()`     |
| Receive byte   | `wirelet_poll_byte()`      | `link.poll_byte()`      | `link.poll_byte()`      |
| Receive bytes  | `wirelet_poll_bytes()`     | `link.poll_bytes()`     | `link.poll_bytes()`     |
| Reset state    | `wirelet_context_reset()`  | `link.reset()`          | `link.reset()`          |
| Validate setup | return value of init       | `link.is_valid()`       | constructor `ValueError` |

## Configuration

For the C and C++ implementations, a handler needs at least one valid RX or TX
path:

| Path        | Required callbacks and buffers                       |
|-------------|------------------------------------------------------|
| Receive     | `frame_handler`, `rx_buf`, `rx_buf_len > 0`          |
| Send (byte) | `send_byte`                                          |
| Send (buf)  | `send_bytes`, `tx_buf`, `tx_buf_len > 0`             |

You can use `send_byte` and `send_bytes` together. If both are configured, the
C and C++ implementations prefer the buffered `send_bytes` path when a TX
buffer is available, except for very small buffers where they fall back to
`send_byte`.

The argument order differs slightly between the implementations:

| Language | Configuration order |
|----------|---------------------|
| C        | `context, frame_handler, send_byte, send_bytes, rx_buf, rx_buf_len, tx_buf, tx_buf_len` |
| C++      | `frame_handler, send_bytes, send_byte, rx_buf, rx_buf_len, tx_buf, tx_buf_len` |
| Python   | `frame_handler, send_byte, send_bytes, max_frame_len=0xFFFF` |

`send_byte` is called once per encoded byte. In C and C++, `send_bytes` is a
chunk callback and may be called multiple times per frame; in Python,
`send_bytes` receives the complete encoded frame as one `bytes` object. In
Python, the frame handler receives the payload as a `bytes` copy.

Callbacks are synchronous. Keep them short, especially when calling the
handler from an interrupt context. The implementations are not thread-safe;
use one handler per link and serialize access if multiple threads or interrupt
contexts can reach it.

## Limitations

- Wirelet is not wire-compatible with MIN 2.0.
- There are no acknowledgements, retransmissions, sequence numbers or flow
  control.
- There is no authentication or encryption. CRC-32 detects accidental
  corruption, not malicious modification.
- The protocol is point-to-point. Frame IDs and types are application-defined.
- There is no fragmentation; the maximum payload is 65535 bytes.
- The C and C++ implementations do not allocate memory. The caller must keep
  the RX/TX buffers alive for the lifetime of the handler.

## License

MIT License. See [LICENSE](LICENSE).

Wirelet is a modified version of the MIN protocol. The original code is
copyright (c) 2014-2017 JK Energy Ltd.; modifications are copyright (c) 2026
kto-to.
