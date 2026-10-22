#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "wirelet.hpp"
#include "vector_io.h"

static int g_failures;

#define CHECK(cond) do { \
        if (!(cond)) { \
                std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
                g_failures++; \
        } \
} while (0)

static std::vector<uint8_t> g_capture;

static void send_byte_cb(uint8_t byte)
{
        g_capture.push_back(byte);
}

static void send_bytes_cb(uint8_t *bytes, uint32_t bytes_len)
{
        g_capture.insert(g_capture.end(), bytes, bytes + bytes_len);
}

struct ExpectedFrame {
        uint16_t frame_id = 0;
        uint16_t frame_type = 0;
        uint16_t payload_len = 0;
        const uint8_t *payload = nullptr;
        int seen = 0;
        bool failed = false;
};

static ExpectedFrame g_expected;
static bool g_print_frames = false;

static void frame_cb(uint16_t frame_id, uint16_t frame_type,
                     uint16_t payload_len, uint8_t *payload)
{
        if (g_print_frames) {
                std::printf("%04x %04x ", static_cast<unsigned>(frame_id),
                            static_cast<unsigned>(frame_type));
                for (uint16_t i = 0; i < payload_len; i++)
                        std::printf("%02x", payload[i]);
                std::printf("\n");
                return;
        }

        g_expected.seen++;
        if (frame_id != g_expected.frame_id ||
            frame_type != g_expected.frame_type ||
            payload_len != g_expected.payload_len ||
            (payload_len != 0 &&
             std::memcmp(payload, g_expected.payload, payload_len) != 0)) {
                g_expected.failed = true;
        }
}

static void dummy_frame_cb(uint16_t frame_id, uint16_t frame_type,
                           uint16_t payload_len, uint8_t *payload)
{
        (void)frame_id;
        (void)frame_type;
        (void)payload_len;
        (void)payload;
}

static std::size_t min_size(std::size_t a, std::size_t b)
{
        return a < b ? a : b;
}

enum TxMode {
        TX_MODE_BYTE,
        TX_MODE_BUFFERED,
        TX_MODE_BOTH,
};

static void test_encode_one(const struct test_vector &vector)
{
        static const uint32_t buffer_sizes[] = {1, 2, 3, 7, 16, 64, 257};
        static const TxMode modes[] = {TX_MODE_BYTE, TX_MODE_BUFFERED, TX_MODE_BOTH};
        uint8_t tx_buf[512];

        for (std::size_t i = 0; i < sizeof(buffer_sizes) / sizeof(buffer_sizes[0]); i++) {
                for (TxMode mode : modes) {
                        if (mode == TX_MODE_BYTE && i != 0)
                                continue;

                        uint32_t tx_len = (mode == TX_MODE_BYTE) ? 0 : buffer_sizes[i];
                        uint8_t *tx_ptr = tx_len ? tx_buf : nullptr;
                        g_capture.clear();

                        wirelet::Handler context(
                                nullptr,
                                (mode == TX_MODE_BYTE || mode == TX_MODE_BOTH)
                                        ? send_byte_cb : nullptr,
                                (mode == TX_MODE_BUFFERED || mode == TX_MODE_BOTH)
                                        ? send_bytes_cb : nullptr,
                                nullptr, 0, tx_ptr, tx_len);
                        CHECK(context.is_valid());
                        CHECK(context.is_tx_valid());
                        CHECK(context.send_frame(vector.frame_id, vector.frame_type,
                                                 vector.payload_len, vector.payload));

                        if (g_capture.size() != vector.encoded_len ||
                            (vector.encoded_len != 0 &&
                             std::memcmp(g_capture.data(), vector.encoded,
                                         vector.encoded_len) != 0)) {
                                std::fprintf(stderr,
                                             "encode mismatch id=%04x type=%04x len=%u "
                                             "mode=%d tx_buf_len=%u got=%zu want=%u\n",
                                             static_cast<unsigned>(vector.frame_id),
                                             static_cast<unsigned>(vector.frame_type),
                                             static_cast<unsigned>(vector.payload_len),
                                             static_cast<int>(mode),
                                             static_cast<unsigned>(tx_len),
                                             g_capture.size(),
                                             static_cast<unsigned>(vector.encoded_len));
                                g_failures++;
                        }
                }
        }
}

static void test_decode_one(const struct test_vector &vector)
{
        uint32_t rx_len = vector.payload_len ? vector.payload_len : 1;
        std::vector<uint8_t> rx_buf(rx_len);

        g_expected = ExpectedFrame{};
        g_expected.frame_id = vector.frame_id;
        g_expected.frame_type = vector.frame_type;
        g_expected.payload_len = vector.payload_len;
        g_expected.payload = vector.payload;
        g_print_frames = false;

        wirelet::Handler context(frame_cb, nullptr, nullptr,
                                 rx_buf.data(), rx_len, nullptr, 0);
        CHECK(context.is_valid());
        CHECK(context.is_rx_valid());
        for (std::size_t offset = 0; offset < vector.encoded_len; offset += 7) {
                uint32_t chunk = static_cast<uint32_t>(
                        min_size(7, vector.encoded_len - offset));
                CHECK(context.poll_bytes(vector.encoded + offset, chunk));
        }
        CHECK(g_expected.seen == 1);
        CHECK(!g_expected.failed);
}

static void test_vectors(const char *path)
{
        struct test_vectors vectors;

        if (!test_vectors_load(path, &vectors)) {
                g_failures++;
                return;
        }
        for (uint32_t i = 0; i < vectors.count; i++) {
                test_encode_one(vectors.items[i]);
                test_decode_one(vectors.items[i]);
        }
        std::fprintf(stderr, "C++: checked %u vectors\n",
                     static_cast<unsigned>(vectors.count));
        test_vectors_free(&vectors);
}

static void test_config(void)
{
        uint8_t rx_buffer[8] = {0};
        uint8_t tx_buffer[2] = {0};

        {
                wirelet::Handler none(nullptr, nullptr, nullptr, nullptr, 0, nullptr, 0);
                CHECK(!none.is_valid());
                CHECK(!none.is_tx_valid());
                CHECK(!none.is_rx_valid());
                CHECK(!none.send_frame(1, 2, 0, nullptr));
                CHECK(!none.poll_bytes(nullptr, 0));
        }
        {
                wirelet::Handler rx_only(frame_cb, nullptr, nullptr,
                                         rx_buffer, sizeof(rx_buffer), nullptr, 0);
                CHECK(rx_only.is_valid());
                CHECK(rx_only.is_rx_valid());
                CHECK(!rx_only.is_tx_valid());
                CHECK(!rx_only.send_frame(1, 2, 0, nullptr));
                CHECK(rx_only.poll_bytes(nullptr, 0));
        }
        {
                wirelet::Handler tx_byte(nullptr, send_byte_cb, nullptr,
                                         nullptr, 0, nullptr, 0);
                CHECK(tx_byte.is_valid());
                CHECK(tx_byte.is_tx_valid());
                CHECK(!tx_byte.is_rx_valid());
                CHECK(tx_byte.send_frame(1, 2, 0, nullptr));
                CHECK(!tx_byte.poll_bytes(nullptr, 0));
        }
        {
                wirelet::Handler tx_buffered(nullptr, nullptr, send_bytes_cb,
                                             nullptr, 0, tx_buffer,
                                             sizeof(tx_buffer));
                CHECK(tx_buffered.is_valid());
                CHECK(tx_buffered.is_tx_valid());
                CHECK(!tx_buffered.is_rx_valid());
                CHECK(tx_buffered.send_frame(1, 2, 0, nullptr));
                CHECK(!tx_buffered.send_frame(1, 2, 1, nullptr));
        }
        {
                wirelet::Handler both(dummy_frame_cb, send_byte_cb, send_bytes_cb,
                                      rx_buffer, sizeof(rx_buffer),
                                      tx_buffer, sizeof(tx_buffer));
                CHECK(both.is_valid());
                CHECK(both.is_tx_valid());
                CHECK(both.is_rx_valid());
                CHECK(both.send_frame(0xFFFF, 0xFFFF, 0, nullptr));
                CHECK(!both.send_frame(1, 2, 1, nullptr));
                both.reset();
        }
        {
                /* Invalid configurations are rejected by is_valid() in release
                   builds.  This test is compiled with NDEBUG. */
                wirelet::Handler no_rx_or_tx(frame_cb, nullptr, nullptr,
                                             nullptr, 0, nullptr, 0);
                CHECK(!no_rx_or_tx.is_valid());

                wirelet::Handler bad_send(nullptr, nullptr, send_bytes_cb,
                                          nullptr, 0, nullptr, 0);
                CHECK(!bad_send.is_valid());

                wirelet::Handler bad_tx(nullptr, nullptr, send_bytes_cb,
                                        nullptr, 0, tx_buffer, 0);
                CHECK(!bad_tx.is_valid());
        }
}

static void test_max_payload(void)
{
        const uint16_t payload_len = 0xFFFF;
        static const uint32_t buffer_sizes[] = {1, 2, 512};
        static const TxMode modes[] = {TX_MODE_BYTE, TX_MODE_BUFFERED, TX_MODE_BOTH};
        std::vector<uint8_t> payload(payload_len);
        std::vector<uint8_t> rx_buf(payload_len);
        uint8_t tx_buf[512];

        for (uint16_t i = 0; i < payload_len; i++)
                payload[i] = static_cast<uint8_t>(i * 37u + 11u);

        for (std::size_t i = 0; i < sizeof(buffer_sizes) / sizeof(buffer_sizes[0]); i++) {
                for (TxMode mode : modes) {
                        if (mode == TX_MODE_BYTE && i != 0)
                                continue;

                        uint32_t tx_len = (mode == TX_MODE_BYTE) ? 0 : buffer_sizes[i];
                        uint8_t *tx_ptr = tx_len ? tx_buf : nullptr;
                        g_capture.clear();

                        wirelet::Handler context(
                                nullptr,
                                (mode == TX_MODE_BYTE || mode == TX_MODE_BOTH)
                                        ? send_byte_cb : nullptr,
                                (mode == TX_MODE_BUFFERED || mode == TX_MODE_BOTH)
                                        ? send_bytes_cb : nullptr,
                                nullptr, 0, tx_ptr, tx_len);
                        CHECK(context.send_frame(0x1234, 0xABCD,
                                                 payload_len, payload.data()));
                        CHECK(g_capture.size() > payload_len);

                        g_expected = ExpectedFrame{};
                        g_expected.frame_id = 0x1234;
                        g_expected.frame_type = 0xABCD;
                        g_expected.payload_len = payload_len;
                        g_expected.payload = payload.data();
                        g_print_frames = false;

                        wirelet::Handler decoder(frame_cb, nullptr, nullptr,
                                                 rx_buf.data(), payload_len,
                                                 nullptr, 0);
                        for (std::size_t offset = 0; offset < g_capture.size(); offset += 97) {
                                uint32_t chunk = static_cast<uint32_t>(
                                        min_size(97, g_capture.size() - offset));
                                CHECK(decoder.poll_bytes(g_capture.data() + offset, chunk));
                        }
                        CHECK(g_expected.seen == 1);
                        CHECK(!g_expected.failed);
                }
        }
}

static void test_stream(const char *path, uint32_t rx_len)
{
        std::FILE *f = std::fopen(path, "rb");
        std::vector<uint8_t> data;
        std::vector<uint8_t> rx_buf(rx_len ? rx_len : 1);
        uint8_t chunk[97];

        if (f == nullptr) {
                std::fprintf(stderr, "cannot open %s\n", path);
                g_failures++;
                return;
        }
        std::size_t got;
        while ((got = std::fread(chunk, 1, sizeof(chunk), f)) != 0)
                data.insert(data.end(), chunk, chunk + got);
        std::fclose(f);

        g_print_frames = true;
        wirelet::Handler context(frame_cb, nullptr, nullptr,
                                 rx_buf.data(), rx_len, nullptr, 0);
        if (!context.is_valid()) {
                std::fprintf(stderr, "cannot initialize stream decoder\n");
                g_failures++;
                return;
        }
        for (std::size_t offset = 0; offset < data.size(); offset += 97) {
                uint32_t n = static_cast<uint32_t>(min_size(97, data.size() - offset));
                CHECK(context.poll_bytes(data.data() + offset, n));
        }
}

int main(int argc, char **argv)
{
        if (argc >= 3 && std::strcmp(argv[1], "vectors") == 0) {
                test_vectors(argv[2]);
        } else if (argc >= 4 && std::strcmp(argv[1], "stream") == 0) {
                test_stream(argv[2],
                            static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0)));
                return g_failures != 0;
        } else if (argc >= 2 && std::strcmp(argv[1], "config") == 0) {
                test_config();
        } else if (argc >= 2 && std::strcmp(argv[1], "max") == 0) {
                test_max_payload();
        } else {
                std::fprintf(stderr,
                             "usage: %s vectors FILE | stream FILE RX_LEN | config | max\n",
                             argv[0]);
                return 2;
        }

        if (g_failures != 0) {
                std::fprintf(stderr, "C++ tests FAILED (%d failure(s))\n", g_failures);
                return 1;
        }
        std::fprintf(stderr, "C++ tests passed\n");
        return 0;
}
