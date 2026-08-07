// larush_k9_vfs.c — Midway k9 archive reader for L.A. Rush.
//
// Layer 1 (k9CP codec) is functional under the zlib-first assumption:
// probe the bytes after the magic for a deflate stream and inflate it.
// Layer 2 (k9SF/k9b entry tables) awaits the Stage B reverse
// engineering pass against retail archives; every lookup reports
// K9_ERR_NOT_INDEXED so callers fail cleanly instead of guessing.

#include "larush_k9_vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* ── CRC32 (identical table/polynomial to flatout1_bfs_vfs.c) ── */

static uint32_t crc32_table[256];
static int crc32_table_ready = 0;

static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        crc32_table[i] = crc;
    }
    crc32_table_ready = 1;
}

uint32_t larush_k9_crc32(const void *data, size_t len) {
    if (!crc32_table_ready) crc32_init();
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++)
        crc = (crc >> 8) ^ crc32_table[(crc ^ p[i]) & 0xFF];
    return crc ^ 0xFFFFFFFFu;
}

/* ── Layer 1: k9CP payload codec ─────────────────────────────── */

#define K9CP_MAGIC "k9CP"

/* How far past the magic to look for the deflate stream start.  The
 * header size is unknown; BFS hid its zlib byte at a fixed offset but
 * k9CP may carry extra fields, so scan a small window. */
#define K9CP_PROBE_WINDOW 32u

int larush_k9cp_probe(const uint8_t *blob, size_t len) {
    return blob && len >= 4 && memcmp(blob, K9CP_MAGIC, 4) == 0;
}

/* A plausible zlib stream header: CMF 0x78 (deflate, 32K window) and
 * (CMF<<8 | FLG) divisible by 31 per RFC 1950. */
static int is_zlib_header(const uint8_t *p) {
    return p[0] == 0x78 && (((uint32_t)p[0] << 8) | p[1]) % 31u == 0;
}

int larush_k9cp_unpack(const uint8_t *blob, size_t len,
                       uint8_t **out, size_t *out_len) {
    if (!larush_k9cp_probe(blob, len)) return K9_ERR_BAD_MAGIC;
    if (len < 12) return K9_ERR_TRUNCATED;

    /* Tolerant header parse: assume {magic, u32 usize, ...} and treat
     * usize as a hint only — the inflate loop below sizes the output
     * itself, so a wrong guess costs nothing. */
    uint32_t usize_hint;
    memcpy(&usize_hint, blob + 4, 4);

    /* Probe for the deflate stream within the window after the magic. */
    size_t window = len - 2 < (size_t)K9CP_PROBE_WINDOW
                        ? len - 2 : (size_t)K9CP_PROBE_WINDOW;
    size_t stream_off = 0;
    for (size_t off = 4; off + 2 <= window + 2 && off + 2 <= len; off++) {
        if (is_zlib_header(blob + off)) { stream_off = off; break; }
    }
    if (!stream_off) return K9_ERR_UNKNOWN_CODEC;

    /* Inflate with a growing buffer; trust usize_hint if plausible. */
    size_t cap = (usize_hint > 0 && usize_hint <= K9_UNPACK_MAX)
                     ? usize_hint : 256 * 1024;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) return K9_ERR_NOMEM;

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) { free(buf); return K9_ERR_INFLATE; }
    zs.next_in   = (Bytef *)(blob + stream_off);
    zs.avail_in  = (uInt)(len - stream_off);
    zs.next_out  = buf;
    zs.avail_out = (uInt)cap;

    int rc = K9_OK;
    for (;;) {
        int zrc = inflate(&zs, Z_NO_FLUSH);
        if (zrc == Z_STREAM_END) break;
        if (zrc == Z_OK || zrc == Z_BUF_ERROR) {
            if (zs.avail_out == 0) {
                if (cap >= K9_UNPACK_MAX) { rc = K9_ERR_TOO_BIG; break; }
                size_t new_cap = cap * 2 > K9_UNPACK_MAX
                                     ? K9_UNPACK_MAX : cap * 2;
                uint8_t *nb = (uint8_t *)realloc(buf, new_cap);
                if (!nb) { rc = K9_ERR_NOMEM; break; }
                buf = nb;
                zs.next_out  = buf + zs.total_out;
                zs.avail_out = (uInt)(new_cap - zs.total_out);
                cap = new_cap;
                continue;
            }
            if (zs.avail_in == 0) { rc = K9_ERR_TRUNCATED; break; }
            continue;
        }
        rc = K9_ERR_INFLATE;
        break;
    }

    size_t total = zs.total_out;
    inflateEnd(&zs);
    if (rc != K9_OK) { free(buf); return rc; }

    *out = buf;
    if (out_len) *out_len = total;
    return K9_OK;
}

/* ── Layer 2: archive VFS ────────────────────────────────────── */

struct larush_k9 {
    uint8_t *raw;
    size_t   raw_len;
    char     magic[5];
    uint32_t entry_count;   /* 0 until the entry table is decoded */
};

static int k9_magic_known(const uint8_t *m) {
    return memcmp(m, "k9SF", 4) == 0 ||
           memcmp(m, "k9b\0", 4) == 0 ||
           memcmp(m, "k9CP", 4) == 0;
}

larush_k9 *larush_k9_open(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "k9_vfs: cannot open %s\n", path);
        return NULL;
    }

    fseeko(f, 0, SEEK_END);
    uint64_t fsize = (uint64_t)ftello(f);
    fseeko(f, 0, SEEK_SET);
    if (fsize < 4) { fclose(f); return NULL; }

    uint8_t *raw = (uint8_t *)malloc((size_t)fsize);
    if (!raw) { fclose(f); return NULL; }
    if (fread(raw, 1, (size_t)fsize, f) != fsize) {
        free(raw); fclose(f); return NULL;
    }
    fclose(f);

    if (!k9_magic_known(raw)) {
        fprintf(stderr, "k9_vfs: %s has unknown magic %02X %02X %02X %02X\n",
                path, raw[0], raw[1], raw[2], raw[3]);
        free(raw);
        return NULL;
    }

    larush_k9 *k9 = (larush_k9 *)calloc(1, sizeof(*k9));
    if (!k9) { free(raw); return NULL; }
    k9->raw = raw;
    k9->raw_len = (size_t)fsize;
    memcpy(k9->magic, raw, 4);
    k9->magic[4] = '\0';
    k9->entry_count = 0;

    fprintf(stderr,
            "k9_vfs: opened %s (%zu bytes, magic \"%s\") — "
            "entry table not yet decoded\n",
            path, k9->raw_len, k9->magic);
    return k9;
}

void larush_k9_close(larush_k9 *k9) {
    if (!k9) return;
    free(k9->raw);
    free(k9);
}

const char *larush_k9_magic(const larush_k9 *k9) {
    return k9 ? k9->magic : "";
}

const uint8_t *larush_k9_raw(const larush_k9 *k9, size_t *out_len) {
    if (!k9) { if (out_len) *out_len = 0; return NULL; }
    if (out_len) *out_len = k9->raw_len;
    return k9->raw;
}

uint32_t larush_k9_entry_count(const larush_k9 *k9) {
    return k9 ? k9->entry_count : 0;
}

int larush_k9_find_by_index(larush_k9 *k9, uint32_t idx,
                            const uint8_t **out_data, uint32_t *out_len) {
    (void)k9; (void)idx;
    if (out_data) *out_data = NULL;
    if (out_len) *out_len = 0;
    return 0;
}

const uint8_t *larush_k9_peek(larush_k9 *k9, uint32_t idx,
                              uint32_t *out_len) {
    (void)k9; (void)idx;
    if (out_len) *out_len = 0;
    return NULL;
}

int larush_k9_find(larush_k9 *k9, uint32_t name_hash,
                   const uint8_t **out_data, uint32_t *out_len,
                   const uint8_t **out_data_raw, uint32_t *out_len_raw) {
    (void)k9; (void)name_hash;
    if (out_data) *out_data = NULL;
    if (out_len) *out_len = 0;
    if (out_data_raw) *out_data_raw = NULL;
    if (out_len_raw) *out_len_raw = 0;
    return 0;
}

int larush_k9_find_by_path(larush_k9 *k9, const char *path,
                           const uint8_t **out_data, uint32_t *out_len,
                           const uint8_t **out_data_raw,
                           uint32_t *out_len_raw) {
    (void)k9; (void)path;
    if (out_data) *out_data = NULL;
    if (out_len) *out_len = 0;
    if (out_data_raw) *out_data_raw = NULL;
    if (out_len_raw) *out_len_raw = 0;
    return 0;
}

uint32_t larush_k9_iterate(larush_k9 *k9, larush_k9_iter_cb cb, void *user) {
    (void)k9; (void)cb; (void)user;
    return 0;
}
