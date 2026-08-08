// larush_crt.c — hand-recompiled XAPI/CRT entry chain.
//
// Each function mirrors its retail counterpart instruction-for-
// instruction where it touches Xbox memory (MEM32 / g_eax model, as in
// MANXFlatOut1); host-side concerns (threading, kernel patching) are
// replaced by the cooperative shim equivalents and noted inline.

#include "larush_crt.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern ptrdiff_t g_xbox_mem_offset;
extern uint32_t  g_eax;
extern uint32_t  larush_kernel_heap_alloc(uint32_t size, uint32_t align);

#define MEM32(addr) (*(volatile uint32_t *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))
#define MEM8(addr)  (*(volatile uint8_t  *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))

static larush_crt_state st;

const larush_crt_state *larush_crt_get_state(void) { return &st; }

/* ── Native recompile registry ─────────────────────────────── */

#define CRT_NATIVE_MAX 64

static struct { uint32_t va; void (*fn)(void); } s_native[CRT_NATIVE_MAX];
static int s_native_count = 0;

int larush_crt_register_native(uint32_t xbox_va, void (*fn)(void)) {
    if (s_native_count >= CRT_NATIVE_MAX) return 0;
    s_native[s_native_count].va = xbox_va;
    s_native[s_native_count].fn = fn;
    s_native_count++;
    return 1;
}

static void (*native_for(uint32_t va))(void) {
    for (int i = 0; i < s_native_count; i++)
        if (s_native[i].va == va) return s_native[i].fn;
    return NULL;
}

/* ── entry 0x001B2594 ──────────────────────────────────────── */

void larush_crt_entry(void) {
    memset(&st, 0, sizeof(st));

    /* Clamp the certificate size field to what actually fits between
     * the certificate and the end of the headers:
     *   ecx = [0x10118]; eax = [0x10108] - ecx + 0x10000;
     *   if (eax < [ecx]) [ecx] = eax; */
    uint32_t cert  = MEM32(LARUSH_HDR_CERT_ADDR);
    uint32_t limit = MEM32(LARUSH_HDR_SIZEOF_HEADERS) - cert + 0x10000u;
    if (limit < MEM32(cert))
        MEM32(cert) = limit;

    /* TLS slab size = template bytes + zero-fill, 16-aligned, +4 for
     * the self-pointer slot the trampoline writes:
     *   eax = [ZEROFILL] - [DATA_START] + [DATA_END];
     *   eax = ((eax + 0xF) & ~0xF) + 4;  [0x486834] = eax;
     *   [[TLS_INDEX_PTR]] = eax / -4;                            */
    uint32_t tls_size = MEM32(LARUSH_TLS_ZEROFILL)
                      - MEM32(LARUSH_TLS_DATA_START)
                      + MEM32(LARUSH_TLS_DATA_END);
    tls_size = ((tls_size + 0xFu) & ~0xFu) + 4u;
    MEM32(LARUSH_TLS_SLAB_GLOBAL) = tls_size;
    st.tls_slab_size = tls_size;
    st.tls_index = (int32_t)tls_size / -4;
    MEM32(MEM32(LARUSH_TLS_INDEX_PTR)) = (uint32_t)st.tls_index;

    /* CreateThread(0, 0, mainCRTStartup, 0, 0, 0) → 0x001B7557 →
     * PsCreateSystemThreadEx with stack = PeStackCommit and TLS slab
     * size from the global above.  The shim runs the thread
     * cooperatively — the trampoline below executes inline. */
    st.stack_commit = MEM32(LARUSH_HDR_PE_STACK_COMMIT);
    fprintf(stderr, "larush_crt: entry — tls slab %u B (index %d), "
            "main thread stack %u B\n",
            st.tls_slab_size, st.tls_index, st.stack_commit);
}

/* ── trampoline 0x001B74BF: per-thread TLS setup ───────────── */

static void crt_thread_tls_attach(void) {
    uint32_t tpl_lo = MEM32(LARUSH_TLS_DATA_START);
    uint32_t tpl_hi = MEM32(LARUSH_TLS_DATA_END);
    uint32_t zfill  = MEM32(LARUSH_TLS_ZEROFILL);
    uint32_t slab   = larush_kernel_heap_alloc(st.tls_slab_size, 16);
    if (!slab) {
        fprintf(stderr, "larush_crt: TLS slab alloc failed!\n");
        return;
    }
    for (uint32_t i = 0; i < tpl_hi - tpl_lo; i++)
        MEM8(slab + i) = MEM8(tpl_lo + i);
    for (uint32_t i = 0; i < zfill; i++)
        MEM8(slab + (tpl_hi - tpl_lo) + i) = 0;
    st.tls_slab_va = slab;
    fprintf(stderr, "larush_crt: TLS slab at 0x%08X (%u template + %u "
            "zero-fill)\n", slab, tpl_hi - tpl_lo, zfill);
}

/* ── 0x001B72D4: kernel build patch probe ──────────────────── */

static void crt_kernel_patch_probe(void) {
    /* The original reads the kernel version data export, matches builds
     * 3944 / 4034 / 4039 / 4816–4818, and run-patches a kernel routine
     * at 0x8003xxxx.  The native port runs no Microsoft kernel, so
     * there is nothing to patch — the probe is a no-op here. */
    fprintf(stderr, "larush_crt: kernel patch probe — native kernel, "
            "no patch applied\n");
}

/* ── 0x001B6D5C: Xapi process init ─────────────────────────── */

static void crt_init_process(void) {
    /* RtlCreateHeap(XapiProcessHeap, PeHeapReserve, PeHeapCommit) —
     * backed by the shim bump heap; the parameters are recorded so
     * later allocation stubs can honour them. */
    st.heap_reserve = MEM32(LARUSH_HDR_PE_HEAP_RESERVE);
    st.heap_commit  = MEM32(LARUSH_HDR_PE_HEAP_COMMIT);
    st.init_flags   = MEM32(LARUSH_HDR_INIT_FLAGS);
    /* InitFlags bit 3 (0x8) set → the original skips formatting and
     * mounting the utility drive; the k9 VFS stands in for all drive
     * access either way. */
    fprintf(stderr, "larush_crt: process init — heap reserve %u B, "
            "commit %u B, init flags 0x%02X%s\n",
            st.heap_reserve, st.heap_commit, st.init_flags,
            (st.init_flags & 8u) ? " (no utility drive)" : "");
}

/* ── 0x001B7247 / 0x001B71EF: initializer table walks ──────── */

static void crt_walk_table(const char *name, uint32_t lo, uint32_t hi) {
    uint32_t live = 0, native = 0;
    for (uint32_t va = lo; va < hi; va += 4) {
        uint32_t fn_va = MEM32(va);
        if (fn_va == 0 || fn_va == 0xFFFFFFFFu) continue;
        live++;
        void (*fn)(void) = native_for(fn_va);
        if (fn) { fn(); native++; }
    }
    st.ctor_total  += live;
    st.ctor_native += native;
    st.ctor_pending += live - native;
    fprintf(stderr, "larush_crt: %s [0x%08X,0x%08X): %u initializers, "
            "%u native, %u pending\n", name, lo, hi, live, native,
            live - native);
}

/* ── mainCRTStartup 0x001B2520 ─────────────────────────────── */

void larush_crt_main_thread(void) {
    crt_thread_tls_attach();          /* trampoline 0x001B74BF */
    crt_kernel_patch_probe();         /* 0x001B72D4 */
    crt_init_process();               /* 0x001B6D5C */

    crt_walk_table("__xi",     LARUSH_XI_TABLE_LO,   LARUSH_XI_TABLE_HI);
    /* 0x001B71EF: optional hook pointer first, then two ctor tables. */
    {
        uint32_t hook = MEM32(LARUSH_PREMAIN_HOOK_PTR);
        if (hook) {
            void (*fn)(void) = native_for(hook);
            if (fn) fn();
            else fprintf(stderr, "larush_crt: pre-main hook 0x%08X "
                         "pending recompilation\n", hook);
        }
    }
    crt_walk_table("pre-main", LARUSH_PREMAIN_LO,    LARUSH_PREMAIN_HI);
    crt_walk_table("C++ ctor", LARUSH_CTOR_TABLE_LO, LARUSH_CTOR_TABLE_HI);

    /* main(0, 0, 0) = 0x00087860 */
    st.reached_main_va = LARUSH_MAIN_VA;
    {
        void (*fn)(void) = native_for(LARUSH_MAIN_VA);
        if (fn) {
            fprintf(stderr, "larush_crt: dispatching native main "
                    "(0x%08X)\n", LARUSH_MAIN_VA);
            g_eax = 0;
            fn();
            st.main_native = 1;
            st.exit_code = g_eax;
        } else {
            fprintf(stderr, "larush_crt: main @ 0x%08X not yet "
                    "recompiled — chain verified up to main dispatch\n",
                    LARUSH_MAIN_VA);
            st.exit_code = 0;
        }
    }
    /* exit path 0x001B6CE6 builds a launch-data page and returns to
     * the dashboard — for the native port the chain simply returns. */
}

uint32_t larush_crt_run(void) {
    larush_crt_entry();
    larush_crt_main_thread();
    return st.exit_code;
}
