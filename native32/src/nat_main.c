// nat_main.c — entry point for the native-execution harness.
//
// Stage C1.a: --self-test proves the three load-bearing mechanisms on
// this host before any guest code is loaded:
//   1. the 64 MB guest arena reserves at its true base (0x10000),
//   2. %fs can be pointed at a KPCR in that arena and read back,
//   3. the k9 VFS codec works when compiled -m32.
// Later C1 steps add the XBE loader, thunk patch, threads and probes.

#include "nat_arena.h"
#include "nat_fs.h"
#include "nat_stubs.h"
#include "larush_k9_vfs.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>

static int s_fail = 0;
#define CHECK(cond, what) do { \
    int _ok = (cond); \
    printf("  %-52s %s\n", (what), _ok ? "ok" : "FAIL"); \
    if (!_ok) s_fail = 1; \
} while (0)

/* k9CP round-trip, mirroring tests/larush_k9_unit_test.c, to confirm the
 * VFS codec is intact under -m32 (zlib-ng-compat is the 32-bit provider). */
static void selftest_k9(void) {
    CHECK(larush_k9_crc32("123456789", 9) == 0xCBF43926u,
          "k9 crc32 KAT");

    enum { PAYLOAD = 64 * 1024 };
    uint8_t *plain = malloc(PAYLOAD);
    for (int i = 0; i < PAYLOAD; i++)
        plain[i] = (uint8_t)((i / 256) ^ (i % 7));
    uLongf clen = compressBound(PAYLOAD);
    uint8_t *blob = malloc(12 + clen);
    memcpy(blob, "k9CP", 4);
    uint32_t usize = PAYLOAD;
    memcpy(blob + 4, &usize, 4);
    int zok = compress2(blob + 12, &clen, plain, PAYLOAD, Z_BEST_SPEED) == Z_OK;

    uint8_t *out = NULL; size_t out_len = 0;
    int rc = zok ? larush_k9cp_unpack(blob, 12 + clen, &out, &out_len)
                 : -999;
    CHECK(rc == K9_OK && out_len == PAYLOAD &&
          memcmp(out, plain, PAYLOAD) == 0, "k9CP round-trip");
    free(out); free(blob); free(plain);
}

static int self_test(void) {
    printf("LARushNative --self-test (Stage C1.a)\n");

    CHECK(nat_arena_reserve() == 0, "arena reserve at 0x10000 (64 MB)");

    /* Put a KPCR at a fixed spot in the arena and prove %fs reaches it. */
    uint32_t kpcr_va = 0x00F20000u;
    uint16_t sel = nat_fs_install(kpcr_va);
    CHECK(sel != 0, "set_thread_area + %fs load");
    GMEM32(kpcr_va + 0x28) = 0xCAFEBABEu;   /* fake CurrentThread */
    CHECK(nat_fs_read32(0x18) == kpcr_va, "fs:[0x18] == KPCR self");
    CHECK(nat_fs_read32(0x28) == 0xCAFEBABEu, "fs:[0x28] == CurrentThread");

    selftest_k9();

    printf(s_fail ? "\nSELF-TEST FAILED\n" : "\nSelf-test passed.\n");
    return s_fail;
}

/* --load <data_dir>: reserve the arena, load the XBE at true VAs, and
 * verify the entry point disassembles to the known retail signature.
 * The section copy is confirmed by reading guest memory directly. */
static int load_mode(const char *data_dir) {
    printf("LARushNative --load %s (Stage C1.c)\n", data_dir);
    if (nat_arena_reserve() != 0) return 1;

    nat_xbe xbe;
    if (nat_load_xbe(data_dir, &xbe) != 0) return 1;

    s_fail = 0;
    CHECK(xbe.entry == 0x001B2594u, "entry == 0x001B2594");
    CHECK(xbe.thunk_va == 0x002A1620u, "thunk table == 0x002A1620");
    CHECK(xbe.xor_key == 0xA8FC57ABu, "retail XOR key");

    /* Header page landed: the entry reads [0x10108]/[0x10118]. */
    CHECK(GMEM32(0x00010104u) == 0x00010000u, "header page: base field");

    /* Entry bytes: `mov ecx,[0x10118]; mov eax,[0x10108]` =
     * 8B 0D 18 01 01 00  A1 08 01 01 00 */
    CHECK(GMEM8(0x001B2594u) == 0x8B && GMEM8(0x001B2595u) == 0x0D &&
          GMEM8(0x001B259Au) == 0xA1,
          "entry code bytes match retail disassembly");

    /* Thunk table: 144 entries of (0x80000000 | ordinal). */
    uint32_t e0 = GMEM32(0x002A1620u);
    CHECK((e0 & 0x80000000u) != 0, "thunk[0] high bit set (ordinal entry)");

    printf(s_fail ? "\nLOAD FAILED\n" : "\nLoad verified.\n");
    return s_fail;
}

/* Find the thunk slot carrying `ordinal` (scans the pre-patch table). */
static uint32_t slot_va_for_ordinal(uint32_t thunk_va, uint32_t ordinal) {
    for (uint32_t i = 0; i < 144; i++) {
        uint32_t v = GMEM32(thunk_va + i * 4u);
        if ((v & 0x80000000u) && (v & 0x7FFFFFFFu) == ordinal)
            return thunk_va + i * 4u;
    }
    return 0;
}

typedef uint32_t (__attribute__((stdcall)) *fn2_t)(uint32_t, uint32_t);

/* --thunks <data_dir>: load, patch the 144 slots, then call a real stub
 * (ExAllocatePoolWithTag, ord 15) *through its patched slot* — proving
 * guest `call [slot]` reaches host stdcall code with correct args and
 * stack cleanup. */
static int thunks_mode(const char *data_dir) {
    printf("LARushNative --thunks %s (Stage C1.d)\n", data_dir);
    if (nat_arena_reserve() != 0) return 1;
    nat_xbe xbe;
    if (nat_load_xbe(data_dir, &xbe) != 0) return 1;

    /* Record slots we will exercise BEFORE patching overwrites ordinals. */
    uint32_t slot_alloc = slot_va_for_ordinal(xbe.thunk_va, 15);  /* ExAllocatePoolWithTag */
    uint32_t slot_data  = slot_va_for_ordinal(xbe.thunk_va, 16);  /* ExEventObjectType (DATA) */

    uint32_t patched = nat_thunks_patch(&xbe);
    s_fail = 0;
    CHECK(patched == 144, "patched all 144 thunk slots");
    CHECK(slot_alloc != 0, "found ExAllocatePoolWithTag slot (ord 15)");
    CHECK(slot_data != 0, "found ExEventObjectType slot (ord 16, DATA)");

    /* DATA export slot now points into the KDATA region. */
    if (slot_data)
        CHECK(GMEM32(slot_data) >= NAT_KDATA_BASE,
              "DATA slot points at KDATA cell");

    /* Call ExAllocatePoolWithTag through the patched slot. */
    if (slot_alloc) {
        fn2_t fn = (fn2_t)(uintptr_t)GMEM32(slot_alloc);
        uint32_t va = fn(0x400, 0x74736574 /* 'test' */);
        CHECK(va >= NAT_HEAP_BASE && va < NAT_HEAP_END,
              "guest call [slot] -> ExAllocatePoolWithTag returned heap VA");
        /* Second call returns a distinct, higher address. */
        uint32_t va2 = fn(0x10, 0);
        CHECK(va2 >= va + 0x400, "second alloc advances the bump heap");
    }

    printf(s_fail ? "\nTHUNKS FAILED\n" : "\nThunk patch + dispatch verified.\n");
    return s_fail;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--self-test") == 0)
        return self_test();
    if (argc > 2 && strcmp(argv[1], "--load") == 0)
        return load_mode(argv[2]);
    if (argc > 2 && strcmp(argv[1], "--thunks") == 0)
        return thunks_mode(argv[2]);

    fprintf(stderr,
        "LARushNative: native-execution harness (Stage C1.d)\n"
        "  --self-test         verify arena + %%fs + k9 codec\n"
        "  --load <data_dir>   load the XBE at true VAs and verify\n"
        "  --thunks <data_dir> patch thunks + call a stub through a slot\n"
        "Threads and probes arrive in C1.e-f.\n");
    return 2;
}
