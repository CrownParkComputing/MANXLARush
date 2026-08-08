// nat_thread.c — guest threads via native pthreads.
//
// PsCreateSystemThreadEx spawns a pthread that installs its own %fs/KPCR,
// switches to a guest stack, and enters the guest StartRoutine.  For the
// main game thread StartRoutine is the CRT trampoline 0x001B74BF, which
// copies the TLS template into [KTHREAD+0x28] and then calls
// mainCRTStartup (0x001B2520) — so all we must provide is the correct
// stack + TLS geometry and a valid KTHREAD/KPCR.

#include "nat_thread.h"
#include "nat_arena.h"
#include "nat_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* Per-thread guest resource region (bump-allocated top-down). */
#define TSTACK_BASE 0x03A00000u
#define TSTACK_END  0x03F80000u
static uint32_t s_tstack_next = TSTACK_END;

/* KTHREAD.TlsData lives at offset 0x28 (the trampoline reads it). */
#define KTHREAD_SIZE     0x120u
#define KTHREAD_TLSDATA  0x28u

static uint32_t tregion_alloc(uint32_t size, uint32_t align) {
    if (align < 16) align = 16;
    uint32_t top = (s_tstack_next - size) & ~(align - 1);
    if (top < TSTACK_BASE) { fprintf(stderr, "nat_thread: region OOM\n"); return 0; }
    s_tstack_next = top;
    return top;
}

/* ── boot / exit synchronisation ───────────────────────────── */
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_exit_cv = PTHREAD_COND_INITIALIZER;
static int s_exited = 0, s_exit_status = 0;

void nat_thread_signal_exit(int status) {
    pthread_mutex_lock(&s_lock);
    if (!s_exited) { s_exited = 1; s_exit_status = status; }
    pthread_cond_broadcast(&s_exit_cv);
    pthread_mutex_unlock(&s_lock);
}

/* ── guest thread trampoline ───────────────────────────────── */
typedef struct {
    uint32_t kpcr_va, kthread_va, stack_top, start_va, ctx1, ctx2;
} gthread;

/* Host return thunk: reached if the guest StartRoutine ever returns
 * normally (it usually exits via PsTerminateSystemThread instead). */
static void guest_returned(void) { nat_thread_signal_exit(0); pthread_exit(NULL); }

static void *guest_pthread(void *arg) {
    gthread *g = (gthread *)arg;
    nat_fs_load(g->kpcr_va);

    /* Build the initial guest frame: [esp]=return thunk, then ctx1,ctx2
     * (StartRoutine is stdcall and pops its own two args). */
    uint32_t esp = g->stack_top;
    esp -= 4; GMEM32(esp) = g->ctx2;
    esp -= 4; GMEM32(esp) = g->ctx1;
    esp -= 4; GMEM32(esp) = (uint32_t)(uintptr_t)guest_returned;

    __asm__ volatile("movl %0, %%esp\n\t"
                     "jmp  *%1"
                     :: "r"(esp), "r"(g->start_va) : "memory");
    return NULL; /* unreachable */
}

uint32_t __attribute__((stdcall))
nk_PsCreateSystemThreadEx(uint32_t handle_va, uint32_t extra, uint32_t kstack,
                          uint32_t tlssize, uint32_t tid_va, uint32_t ctx1,
                          uint32_t ctx2, uint32_t suspended, uint32_t dbgstack,
                          uint32_t start_va) {
    (void)extra; (void)dbgstack;

    uint32_t stack_sz = kstack ? kstack : 0x20000u;
    if (stack_sz < 0x20000u) stack_sz = 0x20000u;
    uint32_t tls_sz = tlssize ? tlssize : 0x200u;

    /* One contiguous region: [KPCR][KTHREAD][TLS][stack]. */
    uint32_t region = tregion_alloc(KPCR_SIZE + KTHREAD_SIZE + tls_sz + stack_sz,
                                    0x1000);
    if (!region) return 0xC000009Au; /* STATUS_INSUFFICIENT_RESOURCES */
    uint32_t kpcr_va    = region;
    uint32_t kthread_va = kpcr_va + KPCR_SIZE;
    uint32_t tls_base   = kthread_va + KTHREAD_SIZE;
    uint32_t stack_lo   = tls_base + tls_sz;
    uint32_t stack_top  = stack_lo + stack_sz;

    memset((void *)(uintptr_t)kthread_va, 0, KTHREAD_SIZE);
    GMEM32(kthread_va + KTHREAD_TLSDATA) = tls_base;    /* trampoline dest */
    nat_fs_init_kpcr(kpcr_va, stack_top, stack_lo, kthread_va);

    if (handle_va) GMEM32(handle_va) = 0xA7000000u | (region >> 12);
    if (tid_va)    GMEM32(tid_va)    = region >> 12;

    gthread *g = malloc(sizeof *g);
    *g = (gthread){ kpcr_va, kthread_va, stack_top, start_va, ctx1, ctx2 };

    if (suspended)
        fprintf(stderr, "nat_thread: CreateSuspended ignored (start 0x%08X)\n",
                start_va);

    pthread_t th;
    if (pthread_create(&th, NULL, guest_pthread, g) != 0) {
        free(g);
        return 0xC000009Au;
    }
    pthread_detach(th);
    fprintf(stderr, "nat_thread: spawned guest thread start=0x%08X "
            "stack=[0x%08X,0x%08X) tls=0x%08X\n",
            start_va, stack_lo, stack_top, tls_base);
    return 0; /* STATUS_SUCCESS */
}

uint32_t __attribute__((stdcall)) nk_PsTerminateSystemThread(uint32_t status) {
    fprintf(stderr, "nat_thread: PsTerminateSystemThread(0x%08X)\n", status);
    /* The main game thread terminating ends the boot; other threads just
     * exit.  For C1 we treat any terminate as the end of the run. */
    nat_thread_signal_exit((int)status);
    pthread_exit(NULL);
}

/* ── boot thread ───────────────────────────────────────────── */
int nat_thread_boot(uint32_t entry_va) {
    /* Boot-thread KPCR/KTHREAD at the top of the region. */
    uint32_t region = tregion_alloc(KPCR_SIZE + KTHREAD_SIZE, 0x1000);
    uint32_t kpcr_va = region, kthread_va = region + KPCR_SIZE;
    memset((void *)(uintptr_t)kthread_va, 0, KTHREAD_SIZE);
    /* The boot thread runs on the host stack; give KPCR plausible bounds. */
    nat_fs_init_kpcr(kpcr_va, 0x03FE0000u, 0x03A00000u, kthread_va);
    if (!nat_fs_load(kpcr_va)) return -1;

    fprintf(stderr, "nat_thread: boot -> entry 0x%08X\n", entry_va);
    ((void (__attribute__((stdcall)) *)(void))(uintptr_t)entry_va)();

    /* Entry has spawned the game thread and returned; wait for it. */
    pthread_mutex_lock(&s_lock);
    while (!s_exited)
        pthread_cond_wait(&s_exit_cv, &s_lock);
    int status = s_exit_status;
    pthread_mutex_unlock(&s_lock);
    return status;
}
