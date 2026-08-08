// larush_k9_vfs.h — Midway k9 archive reader for L.A. Rush (original Xbox).
//
// Mirrors the flatout1_bfs_vfs API so the kernel shim's file-handle
// plumbing works identically across MANX games.  Two layers:
//
//   1. k9CP payload codec: larush_k9cp_unpack() inflates a single
//      "k9CP"-tagged compressed blob.  Zlib/deflate is assumed first
//      (same heuristic that cracked FlatOut's BFS); if no deflate
//      stream is found the call fails with K9_ERR_UNKNOWN_CODEC so a
//      custom-LZ fallback can be slotted in without API changes.
//
//   2. Archive layer ("k9SF" filelists / "k9b\0" blobs): entry-table
//      layout is not yet reverse engineered.  larush_k9_open()
//      recognises the magic and holds the raw file so tools can
//      classify it, but find/iterate report zero entries until the
//      Stage B format pass fills them in.
//
// Usage (target shape, same as BFS):
//   larush_k9 *k9 = larush_k9_open("streams.k9z");
//   const uint8_t *data;  uint32_t len;
//   if (larush_k9_find_by_path(k9, "cars/hummer/body.xpr",
//                              &data, &len, NULL, NULL)) { ... }
//   larush_k9_close(k9);

#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct larush_k9 larush_k9;

/* ── Error codes (negative), shared by both layers ───────────── */
enum {
    K9_OK                = 0,
    K9_ERR_BAD_MAGIC     = -1,  /* not a k9CP/k9SF/k9b blob            */
    K9_ERR_TRUNCATED     = -2,  /* blob shorter than its header claims */
    K9_ERR_UNKNOWN_CODEC = -3,  /* no deflate stream found after magic */
    K9_ERR_INFLATE       = -4,  /* zlib reported corrupt data          */
    K9_ERR_TOO_BIG       = -5,  /* decompressed output over safety cap */
    K9_ERR_NOMEM         = -6,
    K9_ERR_NOT_INDEXED   = -7,  /* archive layer not yet implemented   */
};

/* Safety cap for a single decompressed payload.  Retail L.A. Rush
 * k9CP headers declare sizes past 30 MB (land.col.k9z ≈ 0x01E55800),
 * so this is far larger than the BFS-era 16 MB cap. */
#define K9_UNPACK_MAX (256u * 1024u * 1024u)

/* ── Layer 1: k9CP payload codec ─────────────────────────────── */

/* Returns 1 if `blob` starts with the "k9CP" magic. */
int larush_k9cp_probe(const uint8_t *blob, size_t len);

/* Unpacks one "k9CP" compressed payload.  On success returns K9_OK
 * and stores a malloc'd buffer in *out (caller frees) with its size
 * in *out_len.  On failure returns a K9_ERR_* code and leaves *out
 * untouched.  Never reads past blob+len. */
int larush_k9cp_unpack(const uint8_t *blob, size_t len,
                       uint8_t **out, size_t *out_len);

/* ── Layer 2: archive VFS (k9SF / k9b) ───────────────────────── */

/* Opens the k9 file at `path`.  Recognises "k9SF", "k9b\0" and "k9CP"
 * magics; returns NULL for anything else or on I/O failure.  Until
 * the entry-table format is reverse engineered the archive exposes
 * zero entries — larush_k9_magic()/larush_k9_raw() still give tools
 * access to the bytes for classification. */
larush_k9 *larush_k9_open(const char *path);
void       larush_k9_close(larush_k9 *k9);

/* Four-byte magic of the opened file (e.g. "k9SF"). */
const char *larush_k9_magic(const larush_k9 *k9);

/* Raw file bytes (valid for the lifetime of the handle). */
const uint8_t *larush_k9_raw(const larush_k9 *k9, size_t *out_len);

/* Number of indexed entries (0 for single-file opens). */
uint32_t larush_k9_entry_count(const larush_k9 *k9);

/* Record name of entry `idx` (NULL if out of range / not a pair). */
const char *larush_k9_entry_name(const larush_k9 *k9, uint32_t idx);

/* The three sub-blocks of entry `idx` as offsets into the .res data
 * (block 0 = info/name table, 1 = descriptors, 2 = payload).
 * Returns 1 on success. */
int larush_k9_entry_blocks(const larush_k9 *k9, uint32_t idx,
                           uint32_t off[3], uint32_t size[3]);

/* Lookups mirror flatout1_bfs_*: decompressed pointers are valid
 * until the next larush_k9_* call on the same handle; raw pointers
 * outlive that.  All return 0 / K9_ERR_NOT_INDEXED while the entry
 * table is unimplemented. */
int larush_k9_find_by_index(larush_k9 *k9, uint32_t idx,
                            const uint8_t **out_data, uint32_t *out_len);
const uint8_t *larush_k9_peek(larush_k9 *k9, uint32_t idx,
                              uint32_t *out_len);
int larush_k9_find(larush_k9 *k9, uint32_t name_hash,
                   const uint8_t **out_data, uint32_t *out_len,
                   const uint8_t **out_data_raw, uint32_t *out_len_raw);
int larush_k9_find_by_path(larush_k9 *k9, const char *path,
                           const uint8_t **out_data, uint32_t *out_len,
                           const uint8_t **out_data_raw,
                           uint32_t *out_len_raw);

typedef void (*larush_k9_iter_cb)(void *user,
                                  uint32_t index,
                                  uint32_t name_hash,
                                  uint32_t data_offset,
                                  uint32_t size,
                                  uint32_t csize,
                                  uint32_t flags,
                                  const uint8_t *data_peek,
                                  uint32_t data_peek_len);
uint32_t larush_k9_iterate(larush_k9 *k9, larush_k9_iter_cb cb, void *user);

/* ── CRC32 (same polynomial as the BFS VFS) ──────────────────── */
uint32_t larush_k9_crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif
