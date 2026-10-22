#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wirelet.h"
#include "wirelet_crc32.h"
#include "vector_io.h"

static int g_failures;

#define CHECK(cond) do { \
        if (!(cond)) { \
                fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
                g_failures++; \
        } \
} while (0)

struct capture {
        uint8_t *data;
        size_t len;
        size_t cap;
};

static struct capture g_capture;

static void capture_push(const uint8_t *data, size_t len)
{
        if (g_capture.len + len > g_capture.cap) {
                size_t cap = g_capture.cap ? g_capture.cap : 256;
                uint8_t *grown;
                while (cap < g_capture.len + len)
                        cap *= 2;
                grown = (uint8_t *)realloc(g_capture.data, cap);
                if (grown == NULL) {
                        fprintf(stderr, "out of memory in test\n");
                        exit(2);
                }
                g_capture.data = grown;
                g_capture.cap = cap;
        }
        if (len != 0)
                memcpy(g_capture.data + g_capture.len, data, len);
        g_capture.len += len;
}

static void send_byte_cb(uint8_t byte)
{
        capture_push(&byte, 1);
}

static void send_bytes_cb(uint8_t *bytes, uint32_t bytes_len)
{
        capture_push(bytes, bytes_len);
}

struct expected_frame {
        uint16_t frame_id;
        uint16_t frame_type;
        uint16_t payload_len;
        const uint8_t *payload;
        int seen;
        int failed;
};

static struct expected_frame g_expected;
static int g_print_frames;

static void frame_cb(uint16_t frame_id, uint16_t frame_type,
                     uint16_t payload_len, uint8_t *payload)
{
        if (g_print_frames) {
                uint16_t i;
                printf("%04x %04x ", (unsigned)frame_id, (unsigned)frame_type);
                for (i = 0; i < payload_len; i++)
                        printf("%02x", payload[i]);
                printf("\n");
                return;
        }

        g_expected.seen++;
        if (frame_id != g_expected.frame_id ||
            frame_type != g_expected.frame_type ||
            payload_len != g_expected.payload_len ||
            (payload_len != 0 &&
             memcmp(payload, g_expected.payload, payload_len) != 0)) {
                g_expected.failed = 1;
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

static size_t min_size(size_t a, size_t b)
{
        return a < b ? a : b;
}

enum tx_mode {
        TX_MODE_BYTE,
        TX_MODE_BUFFERED,
        TX_MODE_BOTH,
};

static int init_tx(struct wirelet_context *context, enum tx_mode mode,
                   uint8_t *tx_buf, uint32_t tx_buf_len)
{
        return wirelet_context_init(context, NULL,
                                    (mode == TX_MODE_BYTE || mode == TX_MODE_BOTH)
                                            ? send_byte_cb : NULL,
                                    (mode == TX_MODE_BUFFERED || mode == TX_MODE_BOTH)
                                            ? send_bytes_cb : NULL,
                                    NULL, 0, tx_buf, tx_buf_len);
}

static void test_crc(void)
{
        static const uint8_t data[] = "123456789";
        uint32_t crc;
        uint8_t i;

        wirelet_crc32_init(&crc);
        for (i = 0; i < 9; i++)
                wirelet_crc32_step(&crc, data[i]);
        wirelet_crc32_finalize(&crc);
        CHECK(crc == 0xCBF43926u);

        wirelet_crc32_init(&crc);
        wirelet_crc32_step_bytes(&crc, data, 9);
        wirelet_crc32_finalize(&crc);
        CHECK(crc == 0xCBF43926u);
}

static void test_encode_one(const struct test_vector *vector)
{
        static const uint32_t buffer_sizes[] = {1, 2, 3, 7, 16, 64, 257};
        uint8_t tx_buf[512];
        size_t i;

        for (i = 0; i < sizeof(buffer_sizes) / sizeof(buffer_sizes[0]); i++) {
                enum tx_mode mode;
                for (mode = TX_MODE_BYTE; mode <= TX_MODE_BOTH; mode++) {
                        struct wirelet_context context;
                        uint32_t tx_len = (mode == TX_MODE_BYTE) ? 0 : buffer_sizes[i];

                        if (mode == TX_MODE_BYTE && i != 0)
                                continue;

                        g_capture.len = 0;
                        CHECK(init_tx(&context, mode,
                                      tx_len ? tx_buf : NULL, tx_len));
                        CHECK(wirelet_send_frame(&context,
                                                 vector->frame_id,
                                                 vector->frame_type,
                                                 vector->payload_len,
                                                 vector->payload));
                        if (g_capture.len != vector->encoded_len ||
                            (vector->encoded_len != 0 &&
                             memcmp(g_capture.data, vector->encoded,
                                    vector->encoded_len) != 0)) {
                                fprintf(stderr,
                                        "encode mismatch id=%04x type=%04x len=%u "
                                        "mode=%d tx_buf_len=%u got=%zu want=%u\n",
                                        (unsigned)vector->frame_id,
                                        (unsigned)vector->frame_type,
                                        (unsigned)vector->payload_len,
                                        (int)mode, (unsigned)tx_len,
                                        g_capture.len, (unsigned)vector->encoded_len);
                                g_failures++;
                        }
                }
        }
}

static void test_decode_one(const struct test_vector *vector)
{
        uint32_t rx_len = vector->payload_len ? vector->payload_len : 1;
        uint8_t *rx_buf = (uint8_t *)malloc(rx_len);
        struct wirelet_context context;
        size_t offset;

        if (rx_buf == NULL) {
                fprintf(stderr, "out of memory in test\n");
                exit(2);
        }

        memset(&g_expected, 0, sizeof(g_expected));
        g_expected.frame_id = vector->frame_id;
        g_expected.frame_type = vector->frame_type;
        g_expected.payload_len = vector->payload_len;
        g_expected.payload = vector->payload;
        g_print_frames = 0;

        CHECK(wirelet_context_init(&context, frame_cb, NULL, NULL,
                                   rx_buf, rx_len, NULL, 0));
        for (offset = 0; offset < vector->encoded_len; offset += 7) {
                uint32_t chunk = (uint32_t)min_size(7, vector->encoded_len - offset);
                CHECK(wirelet_poll_bytes(&context, vector->encoded + offset, chunk));
        }
        CHECK(g_expected.seen == 1);
        CHECK(g_expected.failed == 0);

        free(rx_buf);
}

static void test_vectors(const char *path)
{
        struct test_vectors vectors;
        uint32_t i;

        if (!test_vectors_load(path, &vectors)) {
                g_failures++;
                return;
        }
        for (i = 0; i < vectors.count; i++) {
                test_encode_one(&vectors.items[i]);
                test_decode_one(&vectors.items[i]);
        }
        fprintf(stderr, "C: checked %u vectors\n", (unsigned)vectors.count);
        test_vectors_free(&vectors);
}

static void test_config(void)
{
        uint8_t rx_buf[8];
        uint8_t tx_buf[2];
        struct wirelet_context context;

        test_crc();

        CHECK(!wirelet_context_init(NULL, dummy_frame_cb, send_byte_cb, NULL,
                                    rx_buf, sizeof(rx_buf), NULL, 0));
        CHECK(!wirelet_context_init(&context, NULL, NULL, NULL, NULL, 0, NULL, 0));
        CHECK(!wirelet_context_init(&context, dummy_frame_cb, NULL, NULL,
                                    NULL, sizeof(rx_buf), NULL, 0));
        CHECK(!wirelet_context_init(&context, NULL, NULL, send_bytes_cb,
                                    NULL, 0, NULL, 0));
        CHECK(!wirelet_context_init(&context, dummy_frame_cb, NULL, send_bytes_cb,
                                    rx_buf, sizeof(rx_buf), tx_buf, 0));
        CHECK(!wirelet_context_init(&context, dummy_frame_cb, NULL, NULL,
                                    rx_buf, 0, NULL, 0));
        CHECK(!wirelet_context_init(&context, NULL, NULL, NULL,
                                    rx_buf, 0, NULL, 0));

        CHECK(wirelet_context_init(&context, dummy_frame_cb, NULL, NULL,
                                   rx_buf, sizeof(rx_buf), NULL, 0));
        CHECK(!wirelet_send_frame(&context, 1, 2, 0, NULL));
        CHECK(wirelet_poll_bytes(&context, NULL, 0));
        CHECK(wirelet_context_reset(&context));
        CHECK(!wirelet_context_reset(NULL));

        CHECK(wirelet_context_init(&context, NULL, send_byte_cb, NULL,
                                   NULL, 0, NULL, 0));
        CHECK(wirelet_send_frame(&context, 1, 2, 0, NULL));
        CHECK(wirelet_poll_bytes(&context, NULL, 0));

        CHECK(wirelet_context_init(&context, NULL, NULL, send_bytes_cb,
                                   NULL, 0, tx_buf, sizeof(tx_buf)));
        CHECK(wirelet_send_frame(&context, 1, 2, 0, NULL));
        CHECK(!wirelet_send_frame(&context, 1, 2, 1, NULL));
        CHECK(!wirelet_send_frame(NULL, 1, 2, 0, NULL));
        CHECK(!wirelet_poll_bytes(NULL, NULL, 0));

        CHECK(wirelet_context_init(&context, dummy_frame_cb, send_byte_cb,
                                   send_bytes_cb, rx_buf, sizeof(rx_buf),
                                   tx_buf, sizeof(tx_buf)));
        CHECK(wirelet_send_frame(&context, 0xFFFF, 0xFFFF, 0, NULL));
}

static void test_max_payload(void)
{
        const uint16_t payload_len = 0xFFFF;
        static const uint32_t buffer_sizes[] = {1, 2, 512};
        uint8_t *payload = (uint8_t *)malloc(payload_len);
        uint8_t *rx_buf = (uint8_t *)malloc(payload_len);
        uint8_t tx_buf[512];
        size_t i;
        uint16_t j;

        if (payload == NULL || rx_buf == NULL) {
                fprintf(stderr, "out of memory in test\n");
                exit(2);
        }
        for (j = 0; j < payload_len; j++)
                payload[j] = (uint8_t)(j * 37u + 11u);

        for (i = 0; i < sizeof(buffer_sizes) / sizeof(buffer_sizes[0]); i++) {
                enum tx_mode mode;
                for (mode = TX_MODE_BYTE; mode <= TX_MODE_BOTH; mode++) {
                        struct wirelet_context context;
                        uint32_t tx_len = (mode == TX_MODE_BYTE) ? 0 : buffer_sizes[i];
                        size_t offset;

                        if (mode == TX_MODE_BYTE && i != 0)
                                continue;

                        g_capture.len = 0;
                        CHECK(init_tx(&context, mode,
                                      tx_len ? tx_buf : NULL, tx_len));
                        CHECK(wirelet_send_frame(&context, 0x1234, 0xABCD,
                                                 payload_len, payload));
                        CHECK(g_capture.len > payload_len);

                        memset(&g_expected, 0, sizeof(g_expected));
                        g_expected.frame_id = 0x1234;
                        g_expected.frame_type = 0xABCD;
                        g_expected.payload_len = payload_len;
                        g_expected.payload = payload;
                        g_print_frames = 0;

                        CHECK(wirelet_context_init(&context, frame_cb, NULL, NULL,
                                                   rx_buf, payload_len, NULL, 0));
                        for (offset = 0; offset < g_capture.len; offset += 97) {
                                uint32_t chunk = (uint32_t)min_size(97, g_capture.len - offset);
                                CHECK(wirelet_poll_bytes(&context,
                                                         g_capture.data + offset,
                                                         chunk));
                        }
                        CHECK(g_expected.seen == 1);
                        CHECK(g_expected.failed == 0);
                }
        }

        free(payload);
        free(rx_buf);
}

static void test_stream(const char *path, uint32_t rx_len)
{
        FILE *f = fopen(path, "rb");
        uint8_t *rx_buf;
        struct wirelet_context context;
        uint8_t chunk[97];
        size_t got;

        if (f == NULL) {
                fprintf(stderr, "cannot open %s\n", path);
                g_failures++;
                return;
        }
        rx_buf = (uint8_t *)malloc(rx_len ? rx_len : 1);
        if (rx_buf == NULL) {
                fprintf(stderr, "out of memory in test\n");
                exit(2);
        }

        g_print_frames = 1;
        if (!wirelet_context_init(&context, frame_cb, NULL, NULL,
                                  rx_buf, rx_len, NULL, 0)) {
                fprintf(stderr, "cannot initialize stream decoder\n");
                g_failures++;
        } else {
                while ((got = fread(chunk, 1, sizeof(chunk), f)) != 0) {
                        CHECK(wirelet_poll_bytes(&context, chunk, (uint32_t)got));
                }
        }

        fclose(f);
        free(rx_buf);
}

int main(int argc, char **argv)
{
        if (argc >= 3 && strcmp(argv[1], "vectors") == 0) {
                test_vectors(argv[2]);
        } else if (argc >= 4 && strcmp(argv[1], "stream") == 0) {
                test_stream(argv[2], (uint32_t)strtoul(argv[3], NULL, 0));
                return g_failures != 0;
        } else if (argc >= 2 && strcmp(argv[1], "config") == 0) {
                test_config();
        } else if (argc >= 2 && strcmp(argv[1], "max") == 0) {
                test_max_payload();
        } else {
                fprintf(stderr, "usage: %s vectors FILE | stream FILE RX_LEN | config | max\n",
                        argv[0]);
                return 2;
        }

        if (g_failures != 0) {
                fprintf(stderr, "C tests FAILED (%d failure(s))\n", g_failures);
                return 1;
        }
        fprintf(stderr, "C tests passed\n");
        return 0;
}
