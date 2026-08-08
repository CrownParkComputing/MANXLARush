// larush_crt_test.c — L.A. Rush CRT entry-chain diagnostic.
//
// Retail mode: loads the XBE, runs the hand-recompiled CRT chain, and
// cross-checks every value the chain writes against an independent
// recomputation from the loaded image (TLS sizing, ctor table counts,
// main dispatch).
//
// --self-test: fabricates header fields, TLS directory, and a ctor
// table in Xbox memory, registers native markers, and asserts the
// chain computes and dispatches exactly as the retail code would.
//
// Usage: ./LARushCRTTest [game_data/L.A.Rush.USA.XBOX-ZTM | --self-test]

#include "larush_crt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int       xbox_MemoryLayoutInit(const void *xbe, unsigned long sz);
extern void      xbox_MemoryLayoutShutdown(void);
extern void      larush_kernel_init(void);
extern void      larush_kernel_shutdown(void);
extern ptrdiff_t g_xbox_mem_offset;
extern uint32_t  g_eax;

#define MEM32(addr) (*(volatile uint32_t *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))

static int s_fail = 0;

#define CHECK(cond, ...) do { \
    printf("  %-58s %s\n", #cond, (cond) ? "✓" : "✗"); \
    if (!(cond)) { s_fail = 1; printf("    "); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* ── self-test native markers ──────────────────────────────── */

static int s_ctor_a_ran, s_ctor_b_ran, s_main_ran;
static void ctor_a(void)  { s_ctor_a_ran = 1; }
static void ctor_b(void)  { s_ctor_b_ran = 1; }
static void fake_main(void) { s_main_ran = 1; g_eax = 42; }

static int self_test(void) {
    printf("── CRT chain self-test (synthetic image) ──\n");

    /* Minimal XBE: base 0x10000, zero sections. */
    uint8_t xbe[0x400] = {0};
    *(uint32_t *)(xbe + 0x104) = 0x00010000u;  /* base */
    *(uint32_t *)(xbe + 0x11C) = 0;            /* section count */
    *(uint32_t *)(xbe + 0x120) = 0x00010000u;  /* section headers */
    if (!xbox_MemoryLayoutInit(xbe, sizeof(xbe))) return 1;

    /* Fabricated header: certificate at 0x10200 claiming 0x9000 bytes,
     * headers span 0x2C0 bytes from the base → the space left for the
     * certificate is 0x2C0 - (0x10200 - 0x10000) = 0xC0. */
    MEM32(LARUSH_HDR_CERT_ADDR)       = 0x00010200u;
    MEM32(LARUSH_HDR_SIZEOF_HEADERS)  = 0x000002C0u;
    MEM32(0x00010200u)                = 0x00009000u;
    MEM32(LARUSH_HDR_PE_STACK_COMMIT) = 0x00040000u;
    MEM32(LARUSH_HDR_PE_HEAP_RESERVE) = 0x00100000u;
    MEM32(LARUSH_HDR_PE_HEAP_COMMIT)  = 0x00001000u;
    MEM32(LARUSH_HDR_INIT_FLAGS)      = 0x00000008u;

    /* Fabricated TLS directory: 0x21 template bytes at 0x20000,
     * 0x17 zero-fill → slab = ((0x21+0x17+0xF)&~0xF)+4 = 0x44. */
    MEM32(LARUSH_TLS_DATA_START) = 0x00020000u;
    MEM32(LARUSH_TLS_DATA_END)   = 0x00020021u;
    MEM32(LARUSH_TLS_ZEROFILL)   = 0x00000017u;
    MEM32(LARUSH_TLS_INDEX_PTR)  = 0x00020100u;  /* index cell */

    /* Ctor tables: __xi and pre-main empty; C++ table gets two live
     * entries among nulls/-1s.  Native markers for both plus main. */
    for (uint32_t va = LARUSH_CTOR_TABLE_LO; va < LARUSH_CTOR_TABLE_HI; va += 4)
        MEM32(va) = 0;
    MEM32(LARUSH_CTOR_TABLE_LO + 0)  = 0x00111111u;
    MEM32(LARUSH_CTOR_TABLE_LO + 4)  = 0xFFFFFFFFu;
    MEM32(LARUSH_CTOR_TABLE_LO + 8)  = 0x00222222u;
    larush_crt_register_native(0x00111111u, ctor_a);
    larush_crt_register_native(0x00222222u, ctor_b);
    larush_crt_register_native(LARUSH_MAIN_VA, fake_main);

    uint32_t exit_code = larush_crt_run();
    const larush_crt_state *cs = larush_crt_get_state();

    CHECK(MEM32(0x00010200u) == 0x000000C0u,
          "cert clamp got 0x%08X", MEM32(0x00010200u));
    CHECK(cs->tls_slab_size == 0x44, "slab %u", cs->tls_slab_size);
    CHECK(MEM32(LARUSH_TLS_SLAB_GLOBAL) == 0x44,
          "global %u", MEM32(LARUSH_TLS_SLAB_GLOBAL));
    CHECK(cs->tls_index == -(0x44 / 4), "index %d", cs->tls_index);
    CHECK(MEM32(0x00020100u) == (uint32_t)cs->tls_index,
          "index cell 0x%08X", MEM32(0x00020100u));
    CHECK(cs->tls_slab_va != 0, "no slab");
    CHECK(cs->stack_commit == 0x00040000u, "stack %u", cs->stack_commit);
    CHECK(cs->heap_reserve == 0x00100000u && cs->heap_commit == 0x1000u,
          "heap %u/%u", cs->heap_reserve, cs->heap_commit);
    CHECK(cs->ctor_total == 2 && cs->ctor_native == 2 &&
          cs->ctor_pending == 0, "ctors %u/%u/%u",
          cs->ctor_total, cs->ctor_native, cs->ctor_pending);
    CHECK(s_ctor_a_ran && s_ctor_b_ran, "markers %d %d",
          s_ctor_a_ran, s_ctor_b_ran);
    CHECK(cs->reached_main_va == LARUSH_MAIN_VA, "main VA 0x%08X",
          cs->reached_main_va);
    CHECK(s_main_ran && cs->main_native && exit_code == 42,
          "main %d native %d exit %u", s_main_ran, cs->main_native,
          exit_code);

    xbox_MemoryLayoutShutdown();
    printf(s_fail ? "SELF-TEST FAILED\n" : "Self-test passed.\n");
    return s_fail;
}

/* ── retail mode ───────────────────────────────────────────── */

static int retail(const char *data_dir) {
    printf("═══════════════════════════════════════════════════════\n");
    printf("  L.A. Rush — CRT Entry Chain Diagnostic (retail)\n");
    printf("═══════════════════════════════════════════════════════\n");

    char path[1024];
    snprintf(path, sizeof(path), "%s/default.xbe", data_dir);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *xbe = malloc((size_t)sz);
    if (!xbe || fread(xbe, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "ERROR: XBE read failed\n");
        free(xbe); fclose(f); return 1;
    }
    fclose(f);

    if (!xbox_MemoryLayoutInit(xbe, (unsigned long)sz)) {
        free(xbe); return 1;
    }
    larush_kernel_init();

    /* Independent expectations, read straight from the loaded image
     * BEFORE the chain runs. */
    uint32_t tpl_lo = MEM32(LARUSH_TLS_DATA_START);
    uint32_t tpl_hi = MEM32(LARUSH_TLS_DATA_END);
    uint32_t zfill  = MEM32(LARUSH_TLS_ZEROFILL);
    uint32_t expect_slab = (((zfill - tpl_lo + tpl_hi) + 0xFu) & ~0xFu) + 4u;
    uint32_t expect_ctors = 0;
    for (uint32_t va = LARUSH_CTOR_TABLE_LO; va < LARUSH_CTOR_TABLE_HI; va += 4)
        if (MEM32(va) != 0 && MEM32(va) != 0xFFFFFFFFu) expect_ctors++;

    printf("\n── Running chain ──\n");
    larush_crt_run();
    const larush_crt_state *cs = larush_crt_get_state();

    printf("\n── Verification ──\n");
    CHECK(cs->tls_slab_size == expect_slab,
          "slab %u expect %u", cs->tls_slab_size, expect_slab);
    CHECK(MEM32(LARUSH_TLS_SLAB_GLOBAL) == expect_slab,
          "global %u", MEM32(LARUSH_TLS_SLAB_GLOBAL));
    CHECK((uint32_t)(-cs->tls_index) * 4u == expect_slab,
          "index %d", cs->tls_index);
    CHECK(cs->tls_slab_va != 0, "no TLS slab");
    CHECK(cs->ctor_total >= expect_ctors, "ctors %u expect >= %u",
          cs->ctor_total, expect_ctors);
    CHECK(cs->reached_main_va == LARUSH_MAIN_VA,
          "main VA 0x%08X", cs->reached_main_va);

    printf("\n── Recompilation frontier ──\n");
    printf("C++ static ctor table: %u live initializers, %u pending\n",
           expect_ctors, cs->ctor_pending);
    printf("First pending ctor VAs (next recompile targets):\n");
    int shown = 0;
    for (uint32_t va = LARUSH_CTOR_TABLE_LO;
         va < LARUSH_CTOR_TABLE_HI && shown < 10; va += 4) {
        uint32_t p = MEM32(va);
        if (p == 0 || p == 0xFFFFFFFFu) continue;
        printf("  [%2d] 0x%08X\n", shown, p);
        shown++;
    }
    printf("main: 0x%08X (%s)\n", LARUSH_MAIN_VA,
           cs->main_native ? "native" : "pending recompilation");

    larush_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe);
    printf(s_fail ? "\nDIAGNOSTIC FAILED\n" : "\nDiagnostic passed.\n");
    return s_fail;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--self-test") == 0)
        return self_test();
    return retail(argc > 1 ? argv[1] : "game_data/L.A.Rush.USA.XBOX-ZTM");
}
