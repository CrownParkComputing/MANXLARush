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

/* ── Layer 2: archive VFS (.dir/.res pairs) ──────────────────── */
/*
 * Retail L.A. Rush archives are Midway .dir/.res pairs (optionally
 * k9CP-wrapped as .dir.k9z/.res.k9z):
 *
 *   .dir = u32 (0x80000000 | entry_count), then 0x38-byte records:
 *          char name[0x20];
 *          u32 off1, size1;   // info block   (name→descriptor map)
 *          u32 off2, size2;   // descriptors  (== off1+size1)
 *          u32 off3, size3;   // payload      (== off2+size2)
 *          The three blocks are contiguous: entry spans [off1, off3+size3).
 *
 * Verified invariants (cars/pcars/frontend/EngineRes): header count
 * matches record count; off2==off1+size1, off3==off2+size2; the last
 * record's end equals the .res size.
 */

typedef struct {
    char     name[0x21];
    uint32_t off[3];
    uint32_t size[3];
} k9_dir_entry;

struct larush_k9 {
    uint8_t      *raw;        /* decompressed .res (pair) or payload  */
    size_t        raw_len;
    char          magic[5];   /* "dir/" for pairs, else file magic    */
    uint32_t      entry_count;
    k9_dir_entry *entries;    /* NULL unless a pair was opened        */
};

static int k9_magic_known(const uint8_t *m) {
    return memcmp(m, "k9SF", 4) == 0 ||
           memcmp(m, "k9b\0", 4) == 0 ||
           memcmp(m, "k9CP", 4) == 0;
}

/* Read a whole file; k9CP-unpack it transparently if wrapped. */
static uint8_t *k9_slurp(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseeko(f, 0, SEEK_END);
    uint64_t fsize = (uint64_t)ftello(f);
    fseeko(f, 0, SEEK_SET);
    if (fsize < 4) { fclose(f); return NULL; }
    uint8_t *raw = (uint8_t *)malloc((size_t)fsize);
    if (!raw || fread(raw, 1, (size_t)fsize, f) != fsize) {
        free(raw); fclose(f); return NULL;
    }
    fclose(f);

    if (larush_k9cp_probe(raw, (size_t)fsize)) {
        uint8_t *plain = NULL; size_t plain_len = 0;
        int rc = larush_k9cp_unpack(raw, (size_t)fsize, &plain, &plain_len);
        free(raw);
        if (rc != K9_OK) {
            fprintf(stderr, "k9_vfs: %s k9CP unpack failed (%d)\n", path, rc);
            return NULL;
        }
        *out_len = plain_len;
        return plain;
    }
    *out_len = (size_t)fsize;
    return raw;
}

/* If `path` names a .dir or .dir.k9z, write the sibling .res path
 * into out and return 1. */
static int k9_res_sibling(const char *path, char *out, size_t out_sz) {
    size_t n = strlen(path);
    if (n > 8 && strcmp(path + n - 8, ".dir.k9z") == 0) {
        snprintf(out, out_sz, "%.*s.res.k9z", (int)(n - 8), path);
        return 1;
    }
    if (n > 4 && strcmp(path + n - 4, ".dir") == 0) {
        snprintf(out, out_sz, "%.*s.res", (int)(n - 4), path);
        return 1;
    }
    return 0;
}

static larush_k9 *k9_open_pair(const char *dir_path, const char *res_path) {
    size_t dir_len = 0, res_len = 0;
    uint8_t *dir = k9_slurp(dir_path, &dir_len);
    if (!dir) {
        fprintf(stderr, "k9_vfs: cannot load %s\n", dir_path);
        return NULL;
    }
    uint8_t *res = k9_slurp(res_path, &res_len);
    if (!res) {
        fprintf(stderr, "k9_vfs: cannot load %s\n", res_path);
        free(dir);
        return NULL;
    }

    uint32_t head;
    memcpy(&head, dir, 4);
    uint32_t count = head & 0x7FFFFFFFu;
    if (!(head & 0x80000000u) || count == 0 ||
        4 + (size_t)count * 0x38 > dir_len) {
        fprintf(stderr, "k9_vfs: %s: bad .dir header 0x%08X\n",
                dir_path, head);
        free(dir); free(res);
        return NULL;
    }

    larush_k9 *k9 = (larush_k9 *)calloc(1, sizeof(*k9));
    k9->entries = (k9_dir_entry *)calloc(count, sizeof(k9_dir_entry));
    if (!k9 || !k9->entries) {
        free(k9 ? k9->entries : NULL); free(k9);
        free(dir); free(res);
        return NULL;
    }

    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *rec = dir + 4 + (size_t)i * 0x38;
        k9_dir_entry *e = &k9->entries[kept];
        memcpy(e->name, rec, 0x20);
        e->name[0x20] = '\0';
        for (int b = 0; b < 3; b++) {
            memcpy(&e->off[b],  rec + 0x20 + b * 8, 4);
            memcpy(&e->size[b], rec + 0x24 + b * 8, 4);
        }
        /* Drop records that point outside the .res (corrupt tail). */
        uint64_t end = (uint64_t)e->off[2] + e->size[2];
        if (e->name[0] == '\0' || end > res_len) continue;
        kept++;
    }

    k9->raw = res;
    k9->raw_len = res_len;
    memcpy(k9->magic, "dir/", 5);
    k9->entry_count = kept;
    free(dir);

    fprintf(stderr, "k9_vfs: opened pair %s + %s (%u entries, %zu B res)\n",
            dir_path, res_path, kept, res_len);
    return k9;
}

larush_k9 *larush_k9_open(const char *path) {
    /* .dir/.res pair? */
    char res_path[1024];
    if (k9_res_sibling(path, res_path, sizeof(res_path)))
        return k9_open_pair(path, res_path);

    /* Single file: magic-check, k9CP-unwrap for raw access. */
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "k9_vfs: cannot open %s\n", path);
        return NULL;
    }
    uint8_t head[4];
    size_t got = fread(head, 1, 4, f);
    fclose(f);
    if (got != 4 || !k9_magic_known(head)) {
        fprintf(stderr, "k9_vfs: %s has unknown magic\n", path);
        return NULL;
    }

    size_t len = 0;
    uint8_t *raw = k9_slurp(path, &len);
    if (!raw) return NULL;

    larush_k9 *k9 = (larush_k9 *)calloc(1, sizeof(*k9));
    if (!k9) { free(raw); return NULL; }
    k9->raw = raw;
    k9->raw_len = len;
    memcpy(k9->magic, head, 4);
    k9->magic[4] = '\0';
    k9->entry_count = 0;

    fprintf(stderr, "k9_vfs: opened %s (%zu bytes, magic \"%s\")\n",
            path, k9->raw_len, k9->magic);
    return k9;
}

void larush_k9_close(larush_k9 *k9) {
    if (!k9) return;
    free(k9->entries);
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

const char *larush_k9_entry_name(const larush_k9 *k9, uint32_t idx) {
    if (!k9 || !k9->entries || idx >= k9->entry_count) return NULL;
    return k9->entries[idx].name;
}

int larush_k9_entry_blocks(const larush_k9 *k9, uint32_t idx,
                           uint32_t off[3], uint32_t size[3]) {
    if (!k9 || !k9->entries || idx >= k9->entry_count) return 0;
    for (int b = 0; b < 3; b++) {
        if (off)  off[b]  = k9->entries[idx].off[b];
        if (size) size[b] = k9->entries[idx].size[b];
    }
    return 1;
}

int larush_k9_find_by_index(larush_k9 *k9, uint32_t idx,
                            const uint8_t **out_data, uint32_t *out_len) {
    if (!k9 || !k9->entries || idx >= k9->entry_count) {
        if (out_data) *out_data = NULL;
        if (out_len) *out_len = 0;
        return 0;
    }
    const k9_dir_entry *e = &k9->entries[idx];
    if (out_data) *out_data = k9->raw + e->off[0];
    if (out_len) *out_len = e->off[2] + e->size[2] - e->off[0];
    return 1;
}

const uint8_t *larush_k9_peek(larush_k9 *k9, uint32_t idx,
                              uint32_t *out_len) {
    const uint8_t *data; uint32_t len;
    if (!larush_k9_find_by_index(k9, idx, &data, &len)) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    if (out_len) *out_len = len < 256 ? len : 256;
    return data;
}

int larush_k9_find(larush_k9 *k9, uint32_t name_hash,
                   const uint8_t **out_data, uint32_t *out_len,
                   const uint8_t **out_data_raw, uint32_t *out_len_raw) {
    /* No hash scheme observed in the .dir format — names are literal. */
    (void)name_hash;
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
    if (out_data_raw) *out_data_raw = NULL;
    if (out_len_raw) *out_len_raw = 0;
    if (!k9 || !k9->entries || !path) {
        if (out_data) *out_data = NULL;
        if (out_len) *out_len = 0;
        return 0;
    }
    for (uint32_t i = 0; i < k9->entry_count; i++) {
        if (strcmp(k9->entries[i].name, path) == 0)
            return larush_k9_find_by_index(k9, i, out_data, out_len);
    }
    if (out_data) *out_data = NULL;
    if (out_len) *out_len = 0;
    return 0;
}

uint32_t larush_k9_iterate(larush_k9 *k9, larush_k9_iter_cb cb, void *user) {
    if (!k9 || !k9->entries || !cb) return 0;
    for (uint32_t i = 0; i < k9->entry_count; i++) {
        const k9_dir_entry *e = &k9->entries[i];
        uint32_t total = e->off[2] + e->size[2] - e->off[0];
        uint32_t peek_len = total < 256 ? total : 256;
        cb(user, i, 0, e->off[0], total, e->size[2], 0,
           k9->raw + e->off[0], peek_len);
    }
    return k9->entry_count;
}
