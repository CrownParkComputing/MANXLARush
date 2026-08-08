// nat_arena.c — guest RAM reservation.  The XBE loader (section copy,
// header page) is added in Stage C1.c; this unit currently provides just
// the identity-mapped reservation the smoke test asserts.

#include "nat_arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

int nat_arena_reserve(void) {
    /* MAP_FIXED_NOREPLACE fails (rather than clobbering) if anything
     * already occupies the window — the harness must own [BASE, END)
     * before any library maps into it. */
    void *want = (void *)(uintptr_t)NAT_ARENA_BASE;
    void *got = mmap(want, NAT_ARENA_SIZE,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                     -1, 0);
    if (got != want) {
        fprintf(stderr, "nat_arena: reserve at %p failed (got %p, errno=%d %s)\n",
                want, got, errno, strerror(errno));
        return -1;
    }
    return 0;
}

/* ── XBE header field offsets (from the image base) ──────────── */
#define XBE_OFF_BASE        0x104   /* image base                     */
#define XBE_OFF_SIZEOFHDRS  0x108   /* SizeOfHeaders                  */
#define XBE_OFF_SIZEOFIMAGE 0x10C
#define XBE_OFF_NSECTIONS   0x11C
#define XBE_OFF_SECHDRS     0x120   /* VA of the section-header table  */
#define XBE_OFF_ENTRY       0x128   /* entry point ^ key               */
#define XBE_OFF_THUNK       0x158   /* kernel thunk table ^ key        */
#define XBE_KEY_RETAIL      0xA8FC57ABu   /* entry-point key           */
#define XBE_KEY_DEBUG       0x94859D4Bu
#define XBE_KTHUNK_RETAIL   0x5B6D40B6u   /* kernel-thunk key (distinct)*/
#define XBE_KTHUNK_DEBUG    0xEFB1F152u
#define XBE_SECHDR_STRIDE   0x38

static uint32_t rd32(const uint8_t *p, size_t off) {
    uint32_t v;
    memcpy(&v, p + off, 4);
    return v;
}

int nat_load_xbe(const char *data_dir, nat_xbe *out) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/default.xbe", data_dir);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "nat_load_xbe: cannot open %s\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc((size_t)fsz);
    if (!file || fread(file, 1, (size_t)fsz, f) != (size_t)fsz) {
        fprintf(stderr, "nat_load_xbe: read failed\n");
        free(file); fclose(f); return -1;
    }
    fclose(f);

    uint32_t base    = rd32(file, XBE_OFF_BASE);
    uint32_t hdrs    = rd32(file, XBE_OFF_SIZEOFHDRS);
    uint32_t imgsz   = rd32(file, XBE_OFF_SIZEOFIMAGE);
    uint32_t nsec    = rd32(file, XBE_OFF_NSECTIONS);
    uint32_t sechdrs = rd32(file, XBE_OFF_SECHDRS);
    uint32_t entryx  = rd32(file, XBE_OFF_ENTRY);
    uint32_t thunkx  = rd32(file, XBE_OFF_THUNK);

    if (base != NAT_ARENA_BASE) {
        fprintf(stderr, "nat_load_xbe: base 0x%08X != arena base 0x%08X\n",
                base, NAT_ARENA_BASE);
        free(file); return -1;
    }

    /* Resolve entry/thunk with whichever key lands inside the image. */
    uint32_t entry_r = entryx ^ XBE_KEY_RETAIL;
    uint32_t key = (entry_r >= base && entry_r < base + imgsz)
                   ? XBE_KEY_RETAIL : XBE_KEY_DEBUG;
    uint32_t entry = entryx ^ key;
    /* The kernel thunk field uses its own XOR key, not the entry key. */
    uint32_t thunk = thunkx ^ (key == XBE_KEY_RETAIL ? XBE_KTHUNK_RETAIL
                                                     : XBE_KTHUNK_DEBUG);

    /* 1. Header page: the entry reads header fields [0x10108]/[0x10118],
     *    but no section covers [base, first-section-VA).  Copy the first
     *    SizeOfHeaders bytes of the file to the image base. */
    if (hdrs == 0 || hdrs > NAT_ARENA_SIZE) hdrs = 0x1000;
    if ((long)hdrs > fsz) hdrs = (uint32_t)fsz;
    memcpy((void *)(uintptr_t)base, file, hdrs);

    /* 2. Sections at their true VAs. */
    uint32_t loaded = 0;
    for (uint32_t i = 0; i < nsec; i++) {
        size_t sh = (size_t)(sechdrs - base) + (size_t)i * XBE_SECHDR_STRIDE;
        if (sh + XBE_SECHDR_STRIDE > (size_t)fsz) break;
        uint32_t va   = rd32(file, sh + 0x04);
        uint32_t vsz  = rd32(file, sh + 0x08);
        uint32_t raw  = rd32(file, sh + 0x0C);
        uint32_t rsz  = rd32(file, sh + 0x10);
        if (va < NAT_ARENA_BASE || (uint64_t)va + vsz > NAT_ARENA_END)
            continue;
        if ((uint64_t)raw + rsz > (uint64_t)fsz) continue;
        memcpy((void *)(uintptr_t)va, file + raw, rsz);
        /* vsize>rsize tail is already zero (fresh anonymous pages). */
        loaded++;
    }
    free(file);

    if (out) {
        out->base = base;
        out->entry = entry;
        out->size_of_image = imgsz;
        out->thunk_va = thunk;
        out->xor_key = key;
        out->nsections = loaded;
    }
    fprintf(stderr,
        "nat_load_xbe: base 0x%08X entry 0x%08X thunk 0x%08X "
        "key 0x%08X, %u/%u sections, hdr page %u B\n",
        base, entry, thunk, key, loaded, nsec, hdrs);
    return 0;
}
