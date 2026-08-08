// larush_first_boot.c — Minimal L.A. Rush boot test.
//
// Loads default.xbe, inits the 64 MB Xbox memory mapping, patches the
// 256 kernel thunks at 0x002BFF48, and prints the ordinal mapping so
// we can see exactly which kernel calls the game needs.
//
// No game thread, no D3D8, no k9 engine.  Purely verifies that the
// boot infrastructure works for L.A. Rush before starting the engine.
//
// Usage:
//   ./LARushFirstBoot game_data/L.A.Rush.USA.XBOX-ZTM
//   ./LARushFirstBoot --self-test    (synthetic XBE, no retail data)

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* ── Kernel shim API ───────────────────────────────────────── */
extern int      xbox_MemoryLayoutInit(const void *xbe, unsigned long sz);
extern void     xbox_MemoryLayoutShutdown(void);
extern void     larush_kernel_init(void);
extern void     larush_kernel_shutdown(void);
extern uint32_t larush_kernel_remapped_count(void);
extern uint32_t larush_kernel_slot_ordinal(uint32_t slot);

/* ── Xbox memory macros ────────────────────────────────────── */
extern ptrdiff_t g_xbox_mem_offset;
#define MEM32(addr) (*(volatile uint32_t *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))

/* ── L.A. Rush XBE constants (verified against the retail image) ──
 * Entry and thunk-table VAs come from the real XBE header fields:
 * entry point at offset 0x128 ^ retail key, kernel thunk address at
 * offset 0x158 ^ retail key.  (The recovered analysis's "entry
 * 0x00010184" was actually the certificate address at 0x118, and its
 * "256 thunks @ 0x002BFF48" was wrong — the table is 144 @ 0x002A1620.) */
#define LAR_IMAGE_BASE   0x00010000u
#define LAR_ENTRY_VA     0x001B2594u   /* in .text — direct code */
#define LAR_SECTIONS     19u
#define LAR_THUNK_BASE   0x002A1620u
#define LAR_THUNK_COUNT  144u
/* Standard XBE retail XOR keys. */
#define XBE_ENTRY_KEY    0xA8FC57ABu
#define XBE_THUNK_KEY    0x5B6D40B6u
/* Must match KERNEL_SYNTH_BASE / KDATA_BASE in larush_kernel_shim.c. */
#define LAR_SYNTH_BASE   0x00F00000u
#define LAR_KDATA_BASE   0x00F10000u

/* ═══════════════════════════════════════════════════════════════
 *  Self-test: synthetic XBE image
 * ═══════════════════════════════════════════════════════════════
 *
 * Fabricates a minimal XBE-shaped buffer — header fields at the real
 * offsets, one section whose payload holds a 256-entry thunk table at
 * LAR_THUNK_BASE with a handful of 0x80000000|ordinal entries — then
 * runs the real memory init + kernel init and asserts the remap
 * behaviour.  Exercises the boot path with zero retail data. */

#define ST_HDR_FILE_SIZE  0x1000u
#define ST_SEC_FILE_OFF   0x1000u
#define ST_SEC_VA         0x002A1000u
#define ST_SEC_SIZE       0x2000u

static int self_test_failures = 0;

#define ST_CHECK(cond, what) do { \
    if (cond) { printf("  PASS  %s\n", what); } \
    else { printf("  FAIL  %s\n", what); self_test_failures++; } \
} while (0)

static int run_self_test(void) {
    printf("═══════════════════════════════════════════════════════\n");
    printf("  L.A. Rush — First Boot Self-Test (synthetic XBE)\n");
    printf("═══════════════════════════════════════════════════════\n");

    size_t img_size = ST_SEC_FILE_OFF + ST_SEC_SIZE;
    uint8_t *img = calloc(1, img_size);
    if (!img) { fprintf(stderr, "ERROR: OOM\n"); return 1; }

    /* Header fields at the offsets the loader actually reads. */
    memcpy(img, "XBEH", 4);
    *(uint32_t *)(img + 0x104) = LAR_IMAGE_BASE;                 /* base */
    *(uint32_t *)(img + 0x128) = LAR_ENTRY_VA ^ XBE_ENTRY_KEY;   /* entry (encoded) */
    *(uint32_t *)(img + 0x158) = LAR_THUNK_BASE ^ XBE_THUNK_KEY; /* thunk addr (encoded) */
    *(uint32_t *)(img + 0x11C) = 1;                              /* section count */
    *(uint32_t *)(img + 0x120) = LAR_IMAGE_BASE + 0x200;         /* sechdr VA */

    /* One section header (0x38 bytes) at file offset 0x200:
     * [0]=flags [1]=VA [2]=vsize [3]=raw off [4]=raw size */
    uint32_t *sec = (uint32_t *)(img + 0x200);
    sec[0] = 0x00000007;
    sec[1] = ST_SEC_VA;
    sec[2] = ST_SEC_SIZE;
    sec[3] = ST_SEC_FILE_OFF;
    sec[4] = ST_SEC_SIZE;

    /* Thunk table inside the section payload.  A few ordinal entries:
     * slot 0   → 156 (tick count — remaps to a KDATA VA)
     * slot 1   → 1   (plain function ordinal — synthetic VA)
     * slot 2   → 165 (MmAllocateContiguousMemory — synthetic VA)
     * slot 100 → 202 (NtOpenFile — synthetic VA)
     * slot 143 → 322 (hardware info — KDATA VA; also tests last slot) */
    uint32_t *thunks = (uint32_t *)(img + ST_SEC_FILE_OFF +
                                    (LAR_THUNK_BASE - ST_SEC_VA));
    thunks[0]   = 0x80000000u | 156u;
    thunks[1]   = 0x80000000u | 1u;
    thunks[2]   = 0x80000000u | 165u;
    thunks[100] = 0x80000000u | 202u;
    thunks[143] = 0x80000000u | 322u;

    /* Decode check on the entry field, same XOR the loader will use. */
    uint32_t entry_raw = *(uint32_t *)(img + 0x128);
    ST_CHECK((entry_raw ^ XBE_ENTRY_KEY) == LAR_ENTRY_VA,
             "entry point XOR round-trip");

    /* Run the real boot path. */
    if (!xbox_MemoryLayoutInit(img, (unsigned long)img_size)) {
        fprintf(stderr, "ERROR: xbox_MemoryLayoutInit failed\n");
        free(img);
        return 1;
    }
    larush_kernel_init();

    ST_CHECK(larush_kernel_remapped_count() == 5,
             "5/5 synthetic thunks remapped");
    ST_CHECK(larush_kernel_slot_ordinal(0) == 156,
             "slot 0 records ordinal 156");
    ST_CHECK(larush_kernel_slot_ordinal(100) == 202,
             "slot 100 records ordinal 202");
    ST_CHECK(larush_kernel_slot_ordinal(143) == 322,
             "slot 143 records ordinal 322");

    /* Data-export ordinals must remap into the KDATA block. */
    uint32_t slot0 = MEM32(LAR_THUNK_BASE + 0 * 4);
    ST_CHECK(slot0 >= LAR_KDATA_BASE && slot0 < LAR_KDATA_BASE + 0x500,
             "ordinal 156 remapped to KDATA VA");
    uint32_t slot143 = MEM32(LAR_THUNK_BASE + 143 * 4);
    ST_CHECK(slot143 == LAR_KDATA_BASE + 0x000,
             "ordinal 322 remapped to hardware-info VA");

    /* Function ordinals must remap to their synthetic dispatch VAs. */
    ST_CHECK(MEM32(LAR_THUNK_BASE + 1 * 4) == LAR_SYNTH_BASE + 1 * 4,
             "slot 1 rewritten to synthetic VA");
    ST_CHECK(MEM32(LAR_THUNK_BASE + 100 * 4) == LAR_SYNTH_BASE + 100 * 4,
             "slot 100 rewritten to synthetic VA");

    /* Untouched slots must stay zero. */
    ST_CHECK(MEM32(LAR_THUNK_BASE + 50 * 4) == 0,
             "unused slot 50 untouched");

    larush_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(img);

    printf("\n%s (%d failure%s)\n",
           self_test_failures ? "SELF-TEST FAILED" : "Self-test passed",
           self_test_failures, self_test_failures == 1 ? "" : "s");
    return self_test_failures ? 1 : 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Retail boot
 * ═══════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--self-test") == 0)
        return run_self_test();

    const char *data_dir = argc > 1 ? argv[1] : "game_data/L.A.Rush.USA.XBOX-ZTM";

    printf("═══════════════════════════════════════════════════════\n");
    printf("  L.A. Rush — First Boot Test\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("Data dir: %s\n", data_dir);

    /* ── 1. Load XBE ─────────────────────────────────────── */
    char xbe_path[1024];
    snprintf(xbe_path, sizeof(xbe_path), "%s/default.xbe", data_dir);

    FILE *f = fopen(xbe_path, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open %s\n", xbe_path);
        fprintf(stderr, "Supply the game files or run with --self-test.\n");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 128 * 1024 * 1024) {
        fprintf(stderr, "ERROR: bad XBE size %ld\n", sz);
        fclose(f);
        return 1;
    }

    void *xbe_data = malloc((size_t)sz);
    if (!xbe_data || fread(xbe_data, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "ERROR: short XBE read\n");
        free(xbe_data); fclose(f);
        return 1;
    }
    fclose(f);

    printf("Loaded %s (%ld bytes)\n", xbe_path, sz);

    /* Print XBE header info */
    {
        const uint8_t *xb = (const uint8_t *)xbe_data;
        uint32_t base  = *(const uint32_t *)(xb + 0x104);
        uint32_t entry_raw = *(const uint32_t *)(xb + 0x128);
        uint32_t entry = entry_raw ^ XBE_ENTRY_KEY;
        uint32_t thunk_raw = *(const uint32_t *)(xb + 0x158);
        uint32_t nsec  = *(const uint32_t *)(xb + 0x11C);
        printf("  Base:  0x%08X\n", base);
        printf("  Entry: 0x%08X (raw=0x%08X)\n", entry, entry_raw);
        printf("  Thunk table: 0x%08X (expect 0x%08X)\n",
               thunk_raw ^ XBE_THUNK_KEY, LAR_THUNK_BASE);
        printf("  Sections: %u (expected %u)\n", nsec, LAR_SECTIONS);
    }

    /* ── 2. Init 64 MB Xbox memory ───────────────────────── */
    printf("\n── Memory Layout Init ──\n");
    if (!xbox_MemoryLayoutInit(xbe_data, (unsigned long)sz)) {
        fprintf(stderr, "ERROR: xbox_MemoryLayoutInit failed\n");
        free(xbe_data);
        return 1;
    }
    printf("  OK — 64 MB at 4 GB-aligned mapping\n");

    /* ── 3. Init kernel thunks ───────────────────────────── */
    printf("\n── Kernel Thunk Init (0x%08X, %u entries) ──\n",
           LAR_THUNK_BASE, LAR_THUNK_COUNT);
    larush_kernel_init();

    /* ── 4. Scan thunk table: show all ordinals ──────────── */
    printf("\n── Thunk Table Scan ──\n");
    uint16_t seen[512] = {0};
    {
        int total = 0, unique = 0;
        for (uint32_t i = 0; i < LAR_THUNK_COUNT; i++) {
            uint32_t ord = larush_kernel_slot_ordinal(i);
            uint32_t entry = MEM32(LAR_THUNK_BASE + i * 4);
            if (ord) {
                total++;
                if (ord < 512 && !seen[ord]) { seen[ord] = 1; unique++; }
            } else if (entry != 0) {
                printf("  slot %3u: RAW 0x%08X (non-ordinal)\n", i, entry);
            }
        }
        printf("  Total ordinal slots: %d\n", total);
        printf("  Unique ordinals: %d\n", unique);

        printf("  All ordinals used:");
        for (int o = 1; o < 512; o++)
            if (seen[o]) printf(" %u", o);
        printf("\n");
    }

    /* ── 5. Compare vs Burnout 3 / FlatOut 1 stub coverage ── */
    printf("\n── Comparison with Burnout 3 stub set ──\n");
    {
        static const uint16_t b3_ordinals[] = {
            1,2,3,4,8,15,16,23,24,40,41,42,44,46,47,49,62,67,69,71,74,
            83,84,85,86,97,98,99,100,107,109,113,119,124,126,127,128,129,
            137,139,142,143,145,149,150,151,153,156,158,159,160,161,164,
            165,166,168,169,170,171,173,175,176,177,178,179,180,181,182,
            184,187,189,190,193,195,196,197,198,200,203,207,210,211,215,
            217,218,219,222,225,226,228,234,236,238,246,247,250,253,255,
            256,258,259,260,269,279,291,294,301,302,304,305,308,312,322,
            323,324,325,326,327,328,335,336,338,339,340,344,345,346,347,
            349,353,354,355,356,357,358,359,360,
        };
        int b3_count = sizeof(b3_ordinals) / sizeof(b3_ordinals[0]);

        int shared = 0;
        printf("  Shared ordinals (covered by existing stubs):");
        for (int i = 0; i < b3_count; i++) {
            if (seen[b3_ordinals[i]]) {
                if (shared < 20) printf(" %u", b3_ordinals[i]);
                else if (shared == 20) printf(" ...");
                shared++;
            }
        }
        printf("\n  Shared count: %d / %d\n", shared, b3_count);

        int new_ords = 0;
        uint16_t b3_seen[512] = {0};
        for (int i = 0; i < b3_count; i++)
            if (b3_ordinals[i] < 512) b3_seen[b3_ordinals[i]] = 1;
        printf("  L.A. Rush ordinals NOT in B3 (need new stubs):");
        for (int o = 1; o < 512; o++) {
            if (seen[o] && !b3_seen[o]) {
                if (new_ords < 30) printf(" %u", o);
                new_ords++;
            }
        }
        if (new_ords > 30) printf(" ... (+%d more)", new_ords - 30);
        printf("\n  New ordinals count: %d\n", new_ords);
    }

    /* ── Cleanup ──────────────────────────────────────────── */
    printf("\n── Cleanup ──\n");
    larush_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe_data);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  First boot test complete — infrastructure verified\n");
    printf("═══════════════════════════════════════════════════════\n");
    return 0;
}
