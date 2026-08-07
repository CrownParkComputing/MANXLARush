// larush_k9_unit_test.c — synthetic-data tests for the k9 VFS.
//
// No retail data: the k9CP round-trip deflates a known buffer in-test,
// prepends a synthetic k9CP header, and checks the unpack path bit-for-bit.

#include "larush_k9_vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int failures = 0;

#define CHECK(cond, what) do { \
    if (cond) { printf("PASS  %s\n", what); } \
    else { printf("FAIL  %s\n", what); failures++; } \
} while (0)

static void test_crc32(void) {
    /* Standard CRC32 known-answer test vector. */
    CHECK(larush_k9_crc32("123456789", 9) == 0xCBF43926u,
          "crc32(\"123456789\") == 0xCBF43926");
    CHECK(larush_k9_crc32("", 0) == 0x00000000u, "crc32(\"\") == 0");
}

static void test_k9cp_round_trip(void) {
    /* Build a compressible payload. */
    enum { PAYLOAD = 64 * 1024 };
    uint8_t *plain = malloc(PAYLOAD);
    for (int i = 0; i < PAYLOAD; i++)
        plain[i] = (uint8_t)((i / 256) ^ (i % 7));

    uLongf clen = compressBound(PAYLOAD);
    uint8_t *blob = malloc(12 + clen);
    memcpy(blob, "k9CP", 4);
    uint32_t usize = PAYLOAD;
    memcpy(blob + 4, &usize, 4);
    if (compress2(blob + 12, &clen, plain, PAYLOAD, Z_BEST_SPEED) != Z_OK) {
        printf("FAIL  compress2 setup\n");
        failures++;
        free(plain); free(blob);
        return;
    }
    uint32_t csize = (uint32_t)clen;
    memcpy(blob + 8, &csize, 4);

    CHECK(larush_k9cp_probe(blob, 12 + clen), "k9CP magic probe");

    uint8_t *out = NULL;
    size_t out_len = 0;
    int rc = larush_k9cp_unpack(blob, 12 + clen, &out, &out_len);
    CHECK(rc == K9_OK, "k9CP unpack returns K9_OK");
    CHECK(out_len == PAYLOAD, "k9CP unpack size matches");
    CHECK(out && memcmp(out, plain, PAYLOAD) == 0, "k9CP round-trip bytes match");
    free(out);

    /* Same blob but with a lying usize hint — inflate must still size
     * the output itself. */
    uint32_t bad_hint = 16;
    memcpy(blob + 4, &bad_hint, 4);
    out = NULL; out_len = 0;
    rc = larush_k9cp_unpack(blob, 12 + clen, &out, &out_len);
    CHECK(rc == K9_OK && out_len == PAYLOAD, "unpack survives wrong usize hint");
    free(out);

    free(plain);
    free(blob);
}

static void test_k9cp_negative(void) {
    /* Garbage after the magic: must fail cleanly, not crash. */
    uint8_t garbage[64];
    memcpy(garbage, "k9CP", 4);
    for (int i = 4; i < 64; i++) garbage[i] = (uint8_t)(0xA5 ^ i);

    uint8_t *out = (uint8_t *)0x1;   /* sentinel: must not be written */
    int rc = larush_k9cp_unpack(garbage, sizeof(garbage), &out, NULL);
    CHECK(rc == K9_ERR_UNKNOWN_CODEC, "garbage payload → K9_ERR_UNKNOWN_CODEC");
    CHECK(out == (uint8_t *)0x1, "output untouched on failure");

    /* Wrong magic entirely. */
    rc = larush_k9cp_unpack((const uint8_t *)"NOPE0000####", 12, &out, NULL);
    CHECK(rc == K9_ERR_BAD_MAGIC, "wrong magic → K9_ERR_BAD_MAGIC");

    /* Truncated: magic only. */
    rc = larush_k9cp_unpack((const uint8_t *)"k9CP", 4, &out, NULL);
    CHECK(rc == K9_ERR_TRUNCATED, "magic-only blob → K9_ERR_TRUNCATED");

    /* Truncated deflate stream: header + zlib start, then cut off. */
    uint8_t plain[4096];
    memset(plain, 7, sizeof(plain));
    uLongf clen = compressBound(sizeof(plain));
    uint8_t *blob = malloc(12 + clen);
    memcpy(blob, "k9CP", 4);
    uint32_t usize = sizeof(plain);
    memcpy(blob + 4, &usize, 4);
    compress2(blob + 12, &clen, plain, sizeof(plain), Z_BEST_SPEED);
    int rc2 = larush_k9cp_unpack(blob, 12 + clen / 2, &out, NULL);
    CHECK(rc2 == K9_ERR_TRUNCATED || rc2 == K9_ERR_INFLATE,
          "cut-off stream → TRUNCATED/INFLATE error");
    free(blob);
}

static void test_archive_layer_stubs(void) {
    /* Write a tiny synthetic k9SF file and confirm open recognises it
     * while lookups honestly report nothing indexed. */
    const char *path = "larush_k9_unit_test.tmp.k9z";
    FILE *f = fopen(path, "wb");
    if (!f) { printf("FAIL  temp file create\n"); failures++; return; }
    fwrite("k9SF", 1, 4, f);
    uint8_t pad[60] = {0};
    fwrite(pad, 1, sizeof(pad), f);
    fclose(f);

    larush_k9 *k9 = larush_k9_open(path);
    CHECK(k9 != NULL, "k9SF file opens");
    if (k9) {
        CHECK(strcmp(larush_k9_magic(k9), "k9SF") == 0, "magic reported");
        size_t raw_len = 0;
        CHECK(larush_k9_raw(k9, &raw_len) && raw_len == 64, "raw bytes exposed");
        CHECK(larush_k9_entry_count(k9) == 0, "entry count 0 (not yet decoded)");
        const uint8_t *d; uint32_t l;
        CHECK(!larush_k9_find_by_path(k9, "anything", &d, &l, NULL, NULL),
              "find_by_path reports not found");
        larush_k9_close(k9);
    }

    /* Unknown magic must be rejected. */
    f = fopen(path, "wb");
    fwrite("XXXX0000", 1, 8, f);
    fclose(f);
    CHECK(larush_k9_open(path) == NULL, "unknown magic rejected");

    remove(path);
}

int main(void) {
    test_crc32();
    test_k9cp_round_trip();
    test_k9cp_negative();
    test_archive_layer_stubs();

    printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
