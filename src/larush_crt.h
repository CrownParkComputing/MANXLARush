// larush_crt.h — hand-recompiled XAPI/CRT entry chain (retail L.A. Rush).
//
// Chain recovered by tools/lar_disas.py (branch-following tracer):
//
//   entry 0x001B2594          certificate clamp, TLS slab sizing,
//                             CreateThread(main thread) and return
//   CreateThread 0x001B7557   PsCreateSystemThreadEx (ordinal 255);
//                             stack defaults to PeStackCommit [0x10130],
//                             TLS slab size taken from [0x00486834]
//   trampoline 0x001B74BF     copies the TLS template, zero-fills the
//                             tail, runs the routine, then
//                             PsTerminateSystemThread (ordinal 258)
//   mainCRTStartup 0x001B2520 kernel build patch probe (0x001B72D4),
//                             Xapi process init (0x001B6D5C), CRT
//                             __xi walk (0x001B7247), C++ static ctor
//                             walks (0x001B71EF), then
//                             main(0,0,0) = 0x00087860, exit 0x001B6CE6
//
// The shim runs the chain cooperatively on the host thread — no real
// thread is spawned.  Original x86 functions that are not yet
// hand-recompiled are counted as pending; recompiled ones are
// registered against their Xbox VA and dispatched natively.

#ifndef LARUSH_CRT_H
#define LARUSH_CRT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Retail image VAs ──────────────────────────────────────── */

#define LARUSH_ENTRY_VA        0x001B2594u
#define LARUSH_MAIN_THREAD_VA  0x001B2520u
#define LARUSH_MAIN_VA         0x00087860u

/* XBE header fields (header page is loaded at the image base) */
#define LARUSH_HDR_SIZEOF_HEADERS  0x00010108u
#define LARUSH_HDR_CERT_ADDR       0x00010118u
#define LARUSH_HDR_INIT_FLAGS      0x00010124u
#define LARUSH_HDR_PE_STACK_COMMIT 0x00010130u
#define LARUSH_HDR_PE_HEAP_RESERVE 0x00010134u
#define LARUSH_HDR_PE_HEAP_COMMIT  0x00010138u

/* XBE TLS directory pointers (.rdata) */
#define LARUSH_TLS_DATA_START  0x002A1FE4u
#define LARUSH_TLS_DATA_END    0x002A1FE8u
#define LARUSH_TLS_INDEX_PTR   0x002A1FECu
#define LARUSH_TLS_ZEROFILL    0x002A1FF4u
/* XAPI global holding the per-thread TLS slab size */
#define LARUSH_TLS_SLAB_GLOBAL 0x00486834u

/* CRT initializer tables (.data): [lo, hi) of function-pointer dwords;
 * 0 and -1 entries are skipped by the original walkers. */
#define LARUSH_XI_TABLE_LO     0x002D5DA0u   /* __xi (C init, 3 slots)  */
#define LARUSH_XI_TABLE_HI     0x002D5DACu
#define LARUSH_PREMAIN_LO      0x002D8088u   /* pre-main hooks, 6 slots */
#define LARUSH_PREMAIN_HI      0x002D80A0u
#define LARUSH_CTOR_TABLE_LO   0x002D5DB0u   /* C++ static constructors */
#define LARUSH_CTOR_TABLE_HI   0x002D8084u
#define LARUSH_PREMAIN_HOOK_PTR 0x002DDCF8u  /* called first if nonzero */

/* ── Observable chain state (diagnostics / tests) ──────────── */

typedef struct {
    uint32_t tls_slab_size;   /* value written to LARUSH_TLS_SLAB_GLOBAL */
    int32_t  tls_index;       /* value written through TLS_INDEX_PTR */
    uint32_t tls_slab_va;     /* shim-allocated slab (template copied) */
    uint32_t stack_commit;    /* thread stack size (PeStackCommit) */
    uint32_t heap_reserve;    /* XapiProcessHeap parameters */
    uint32_t heap_commit;
    uint32_t init_flags;
    uint32_t ctor_total;      /* live pointers across all three tables */
    uint32_t ctor_native;     /* dispatched to registered recompiles */
    uint32_t ctor_pending;    /* still awaiting hand-recompilation */
    uint32_t reached_main_va; /* LARUSH_MAIN_VA once dispatch got there */
    int      main_native;     /* 1 if a registered native main ran */
    uint32_t exit_code;       /* value passed to the exit path */
} larush_crt_state;

const larush_crt_state *larush_crt_get_state(void);

/* Register a hand-recompiled function for an Xbox VA.  The CRT walks
 * and the main dispatch call these instead of counting the VA as
 * pending.  Returns 0 when the registry is full. */
int larush_crt_register_native(uint32_t xbox_va, void (*fn)(void));

/* Dispatch a call from recompiled code.  If a native is registered it
 * runs with the given stack arguments staged at g_esp+4 (STACK_ARG
 * layout) and 1 is returned; otherwise the target is recorded in the
 * pending table (the recompile hit-list) and 0 is returned. */
int larush_crt_call(uint32_t va, const char *what);
int larush_crt_call_args(uint32_t va, const char *what,
                         const uint32_t *args, int n);
int larush_crt_pending_total(void);
int larush_crt_pending_get(int i, uint32_t *va, const char **what,
                           uint32_t *count);

/* Run the full chain: entry → thread trampoline → mainCRTStartup →
 * main dispatch.  Returns the exit code (g_eax of main when a native
 * main is registered, 0 otherwise). */
uint32_t larush_crt_run(void);

/* Hand-recompiled main (src/larush_game_main.c).  Registering wires
 * main (0x00087860) plus the CRT malloc it uses into the native
 * registry; the game loop then runs the configured frame count. */
void larush_game_main_register(void);
void larush_game_main_set_frames(uint32_t n);

/* Individual stages, exposed for tests. */
void larush_crt_entry(void);            /* 0x001B2594 */
void larush_crt_main_thread(void);      /* 0x001B74BF + 0x001B2520 */

#ifdef __cplusplus
}
#endif

#endif /* LARUSH_CRT_H */
