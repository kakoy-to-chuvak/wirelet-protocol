/*
 * Shared binary test-vector reader for the C and C++ tests.
 *
 * File format (all integers little-endian):
 *   u32 magic ("WLTV")
 *   u32 count
 *   repeated count times:
 *     u16 frame_id
 *     u16 frame_type
 *     u16 payload_len
 *     u32 encoded_len
 *     u8  payload[payload_len]
 *     u8  encoded[encoded_len]
 */

#ifndef WIRELET_TEST_VECTOR_IO_H
#define WIRELET_TEST_VECTOR_IO_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIRELET_TEST_VECTOR_MAGIC 0x56544C57u

struct test_vector {
        uint16_t frame_id;
        uint16_t frame_type;
        uint16_t payload_len;
        uint32_t encoded_len;
        const uint8_t *payload;
        const uint8_t *encoded;
};

struct test_vectors {
        uint32_t count;
        struct test_vector *items;
        uint8_t *blob;
};

static uint16_t wt_rd16(const uint8_t *p)
{
        return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t wt_rd32(const uint8_t *p)
{
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void test_vectors_free(struct test_vectors *vectors);

static int test_vectors_load(const char *path, struct test_vectors *out)
{
        FILE *f;
        long file_size;
        uint8_t *blob;
        const uint8_t *p;
        const uint8_t *end;
        uint32_t count;
        uint32_t i;

        memset(out, 0, sizeof(*out));
        f = fopen(path, "rb");
        if (f == NULL) {
                fprintf(stderr, "cannot open %s\n", path);
                return 0;
        }
        if (fseek(f, 0, SEEK_END) != 0 || (file_size = ftell(f)) < 0 ||
            fseek(f, 0, SEEK_SET) != 0) {
                fprintf(stderr, "cannot size %s\n", path);
                fclose(f);
                return 0;
        }
        if (file_size < 8) {
                fprintf(stderr, "%s is too short\n", path);
                fclose(f);
                return 0;
        }
        blob = (uint8_t *)malloc((size_t)file_size);
        if (blob == NULL) {
                fprintf(stderr, "out of memory reading %s\n", path);
                fclose(f);
                return 0;
        }
        if (fread(blob, 1, (size_t)file_size, f) != (size_t)file_size) {
                fprintf(stderr, "cannot read %s\n", path);
                free(blob);
                fclose(f);
                return 0;
        }
        fclose(f);

        if (wt_rd32(blob) != WIRELET_TEST_VECTOR_MAGIC) {
                fprintf(stderr, "%s has a bad magic\n", path);
                free(blob);
                return 0;
        }
        count = wt_rd32(blob + 4);
        p = blob + 8;
        end = blob + (size_t)file_size;

        out->items = (struct test_vector *)calloc(count ? count : 1,
                                                  sizeof(*out->items));
        if (out->items == NULL) {
                fprintf(stderr, "out of memory for %s\n", path);
                free(blob);
                return 0;
        }
        out->blob = blob;
        out->count = count;

        for (i = 0; i < count; i++) {
                struct test_vector *v = &out->items[i];
                uint16_t payload_len;
                uint32_t encoded_len;

                if ((size_t)(end - p) < 10) {
                        fprintf(stderr, "%s is truncated at vector %u\n", path, i);
                        test_vectors_free(out);
                        return 0;
                }
                v->frame_id = wt_rd16(p);
                v->frame_type = wt_rd16(p + 2);
                payload_len = wt_rd16(p + 4);
                encoded_len = wt_rd32(p + 6);
                p += 10;
                if ((size_t)(end - p) < (size_t)payload_len + (size_t)encoded_len) {
                        fprintf(stderr, "%s is truncated at vector %u data\n", path, i);
                        test_vectors_free(out);
                        return 0;
                }
                v->payload_len = payload_len;
                v->encoded_len = encoded_len;
                v->payload = p;
                p += payload_len;
                v->encoded = p;
                p += encoded_len;
        }

        if (p != end) {
                fprintf(stderr, "%s has trailing data\n", path);
                test_vectors_free(out);
                return 0;
        }
        return 1;
}

static void test_vectors_free(struct test_vectors *vectors)
{
        free(vectors->items);
        free(vectors->blob);
        memset(vectors, 0, sizeof(*vectors));
}

#endif /* WIRELET_TEST_VECTOR_IO_H */
