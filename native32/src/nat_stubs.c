// nat_stubs.c — native (in-process) Xbox kernel stubs.
//
// Unlike the 64-bit recompile shim (which unpacks an emulated stack via
// STACK_ARG and a g_xbox_mem_offset bias), these run as real i386
// __stdcall functions: the guest's `call [thunk_slot]` lands here with
// arguments on its own stack and NTSTATUS returned in eax.  Guest memory
// is identity-mapped, so GMEM32(va) is a direct access.
//
// The thunk patcher installs a real stub for each implemented ordinal, a
// KDATA cell VA for each data export, and an arity-correct abort stub for
// everything else (so a first unimplemented hit stops loudly with the
// guest caller VA, rather than corrupting the stack via a wrong ret n).

#include "nat_stubs.h"
#include "nat_arena.h"
#include "nat_thread.h"
#include "larush_k9_vfs.h"
#include "xkernel_ordinals.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define XSTDCALL __attribute__((stdcall))
#define XFASTCALL __attribute__((fastcall))

/* Ordinal metadata (shared table). */
static const uint8_t s_argbytes[XK_ORDINAL_MAX + 1] = {
#define X(o, n, cc, ab, k) [o] = (uint8_t)(ab),
    XK_ORDINALS(X)
#undef X
};
static const uint8_t s_kind[XK_ORDINAL_MAX + 1] = {
#define X(o, n, cc, ab, k) [o] = (uint8_t)(k),
    XK_ORDINALS(X)
#undef X
};
static const uint8_t s_cc[XK_ORDINAL_MAX + 1] = {
#define X(o, n, cc, ab, k) [o] = (uint8_t)(cc),
    XK_ORDINALS(X)
#undef X
};
static const char *const s_name[XK_ORDINAL_MAX + 1] = {
#define X(o, n, cc, ab, k) [o] = #n,
    XK_ORDINALS(X)
#undef X
};

/* ── Guest heap (bump) ─────────────────────────────────────── */
#define HEAP_BASE 0x01000000u
#define HEAP_END  0x02000000u
static uint32_t s_heap_next = HEAP_BASE;

uint32_t nat_heap_alloc(uint32_t size, uint32_t align) {
    if (align < 16) align = 16;
    uint32_t va = (s_heap_next + align - 1) & ~(align - 1);
    if ((uint64_t)va + size > HEAP_END) {
        fprintf(stderr, "nat_stubs: guest heap exhausted (%u B)\n", size);
        return 0;
    }
    s_heap_next = va + size;
    return va;
}

/* ── KDATA cells for data exports ──────────────────────────── */
#define KDATA_BASE 0x00F10000u
static uint32_t kdata_va(uint32_t ord) { return KDATA_BASE + ord * 16u; }

/* ── k9 file handle table ──────────────────────────────────── */
#define K9_FILE_MAX 64
typedef struct {
    uint32_t handle; const uint8_t *data; uint32_t size, position;
} k9_file;
static k9_file s_files[K9_FILE_MAX];
static uint32_t s_file_next = 0xA9500001u;
static larush_k9 *s_k9 = NULL;

void nat_stubs_set_k9(larush_k9 *k9) {
    s_k9 = k9;
    memset(s_files, 0, sizeof s_files);
    s_file_next = 0xA9500001u;
}
static k9_file *file_claim(const uint8_t *d, uint32_t sz) {
    for (int i = 0; i < K9_FILE_MAX; i++)
        if (!s_files[i].data) {
            s_files[i] = (k9_file){ s_file_next++, d, sz, 0 };
            return &s_files[i];
        }
    return NULL;
}
static k9_file *file_find(uint32_t h) {
    for (int i = 0; i < K9_FILE_MAX; i++)
        if (s_files[i].data && s_files[i].handle == h) return &s_files[i];
    return NULL;
}

/* ── NTSTATUS ──────────────────────────────────────────────── */
#define STATUS_SUCCESS            0x00000000u
#define STATUS_NO_SUCH_FILE       0xC000000Fu
#define STATUS_OBJECT_NAME_INVALID 0xC0000033u
#define STATUS_OBJECT_NAME_NOT_FOUND 0xC0000034u
#define STATUS_INVALID_HANDLE     0xC0000008u
#define STATUS_END_OF_FILE        0xC0000011u

/* ── Memory / pool ─────────────────────────────────────────── */
XSTDCALL static uint32_t nk_ExAllocatePool(uint32_t size) {
    return nat_heap_alloc(size, 16);
}
XSTDCALL static uint32_t nk_ExAllocatePoolWithTag(uint32_t size, uint32_t tag) {
    (void)tag; return nat_heap_alloc(size, 16);
}
XSTDCALL static uint32_t nk_ExFreePool(uint32_t va) { (void)va; return 0; }
XSTDCALL static uint32_t nk_MmAllocateContiguousMemory(uint32_t size) {
    return nat_heap_alloc(size, 4096);
}
XSTDCALL static uint32_t nk_MmAllocateContiguousMemoryEx(
        uint32_t size, uint32_t lo, uint32_t hi, uint32_t align, uint32_t prot) {
    (void)lo; (void)hi; (void)prot;
    return nat_heap_alloc(size, align < 4096 ? 4096 : align);
}

/* ── Rtl string / critical section ─────────────────────────── */
XSTDCALL static uint32_t nk_RtlInitAnsiString(uint32_t str_va, uint32_t src_va) {
    uint32_t len = 0;
    if (src_va && src_va < NAT_ARENA_END) {
        const char *s = (const char *)(uintptr_t)src_va;
        len = (uint32_t)strnlen(s, 0xFFFF);
    }
    if (str_va && str_va + 8 <= NAT_ARENA_END) {
        GMEM16(str_va + 0) = (uint16_t)len;
        GMEM16(str_va + 2) = (uint16_t)(src_va ? len + 1 : 0);
        GMEM32(str_va + 4) = src_va;
    }
    return 0;
}
XSTDCALL static uint32_t nk_RtlInitCS(uint32_t cs_va) {
    if (cs_va && cs_va + 28 <= NAT_ARENA_END)
        memset((void *)(uintptr_t)cs_va, 0, 28);
    return 0;
}
/* Critical sections (Enter 277 / Leave 294) as noops.  Safe while only
 * one guest thread runs (the boot-to-GPU path), but 294 alone is called
 * from 271 sites: once multiple guest threads contend (C3+), these must
 * become real pthread_mutex ops keyed by the CS VA, or shared-state
 * races corrupt guest data.  TODO before multi-threaded gameplay. */
XSTDCALL static uint32_t nk_CS_noop(uint32_t cs_va) { (void)cs_va; return 0; }

/* ── Fastcall IRQL ops (ecx/edx args, hot: 131+ sites) ─────── */
/* KfRaiseIrql(NewIrql) returns the old IRQL; KfLowerIrql(NewIrql) void.
 * We run at a fixed IRQL, so raise returns 0 and lower is a noop. */
XFASTCALL static uint32_t nk_KfRaiseIrql(uint32_t new_irql) { (void)new_irql; return 0; }
XFASTCALL static uint32_t nk_KfLowerIrql(uint32_t new_irql) { (void)new_irql; return 0; }

/* ── File I/O over the k9 VFS ──────────────────────────────── */
XSTDCALL static uint32_t nk_NtOpenFile(uint32_t handle_va, uint32_t access,
        uint32_t attr_va, uint32_t ios_va, uint32_t share, uint32_t opts) {
    (void)access; (void)share; (void)opts;
    if (!s_k9) { if (handle_va) GMEM32(handle_va) = 0xDEAD0001u; return 0; }
    if (!attr_va || attr_va >= NAT_ARENA_END) return STATUS_OBJECT_NAME_INVALID;
    uint32_t ansi_va = GMEM32(attr_va + 4u);
    if (!ansi_va || ansi_va >= NAT_ARENA_END) return STATUS_OBJECT_NAME_INVALID;
    uint32_t buf_va = GMEM32(ansi_va + 4u);
    if (!buf_va || buf_va >= NAT_ARENA_END) return STATUS_OBJECT_NAME_INVALID;

    const char *xp = (const char *)(uintptr_t)buf_va;
    size_t maxlen = NAT_ARENA_END - buf_va;
    size_t plen = strnlen(xp, maxlen < 512 ? maxlen : 512);
    if (plen == 0 || plen >= 512) return STATUS_OBJECT_NAME_INVALID;

    char k9p[512];
    if (xp[0] && xp[1] == ':') xp += 2;
    else if (strncmp(xp, "/Device/", 8) == 0) {
        xp = strchr(xp + 8, '/');
        if (!xp) return STATUS_OBJECT_NAME_INVALID;
    }
    while (*xp == '/' || *xp == '\\') xp++;
    snprintf(k9p, sizeof k9p, "%s", xp);
    for (char *p = k9p; *p; p++) if (*p == '\\') *p = '/';

    const uint8_t *data; uint32_t len;
    if (!larush_k9_find_by_path(s_k9, k9p, &data, &len, NULL, NULL))
        return STATUS_OBJECT_NAME_NOT_FOUND;
    k9_file *ent = file_claim(data, len);
    if (!ent) return STATUS_NO_SUCH_FILE;

    if (handle_va < NAT_ARENA_END) GMEM32(handle_va) = ent->handle;
    if (ios_va && ios_va + 8 <= NAT_ARENA_END) {
        GMEM32(ios_va) = 0; GMEM32(ios_va + 4) = len;
    }
    return STATUS_SUCCESS;
}
XSTDCALL static uint32_t nk_NtCreateFile(uint32_t a, uint32_t b, uint32_t c,
        uint32_t d, uint32_t e, uint32_t f, uint32_t g, uint32_t h,
        uint32_t i, uint32_t j) {
    (void)e; (void)f; (void)g; (void)h; (void)i; (void)j;
    return nk_NtOpenFile(a, b, c, d, 0, 0);
}
XSTDCALL static uint32_t nk_NtReadFile(uint32_t handle, uint32_t event,
        uint32_t apc, uint32_t apcctx, uint32_t ios_va, uint32_t buf_va,
        uint32_t length, uint32_t off_va) {
    (void)event; (void)apc; (void)apcctx;
    k9_file *ent = file_find(handle);
    if (!ent) return STATUS_INVALID_HANDLE;
    if (off_va && off_va + 8 <= NAT_ARENA_END) {
        uint64_t o = (uint64_t)GMEM32(off_va) | ((uint64_t)GMEM32(off_va + 4) << 32);
        if (o < ent->size) ent->position = (uint32_t)o;
    }
    uint32_t avail = ent->size - ent->position;
    uint32_t n = length < avail ? length : avail;
    if (buf_va && buf_va + n <= NAT_ARENA_END && n)
        memcpy((void *)(uintptr_t)buf_va, ent->data + ent->position, n);
    ent->position += n;
    if (ios_va && ios_va + 8 <= NAT_ARENA_END) {
        GMEM32(ios_va) = 0; GMEM32(ios_va + 4) = n;
    }
    return (n == 0 && length) ? STATUS_END_OF_FILE : STATUS_SUCCESS;
}
XSTDCALL static uint32_t nk_NtClose(uint32_t handle) {
    for (int i = 0; i < K9_FILE_MAX; i++)
        if (s_files[i].handle == handle) { memset(&s_files[i], 0, sizeof s_files[i]); break; }
    return STATUS_SUCCESS;
}

/* ── Virtual memory (page-bump allocator) ──────────────────── */
#define VM_BASE 0x02000000u
#define VM_END  0x03A00000u
static uint32_t s_vm_next = VM_BASE;

XSTDCALL static uint32_t nk_NtAllocateVirtualMemory(uint32_t base_va,
        uint32_t zbits, uint32_t size_va, uint32_t type, uint32_t protect) {
    (void)zbits; (void)type; (void)protect;
    if (!size_va) return 0xC000000Du;
    uint32_t size = GMEM32(size_va);
    uint32_t want = base_va ? GMEM32(base_va) : 0;
    uint32_t got;
    if (want) {
        got = want & ~0xFFFu;                 /* honor requested base */
    } else {
        got = (s_vm_next + 0xFFFu) & ~0xFFFu;
        s_vm_next = got + ((size + 0xFFFu) & ~0xFFFu);
        if (s_vm_next > VM_END) return 0xC000009Au;
    }
    if (base_va) GMEM32(base_va) = got;
    if (size_va) GMEM32(size_va) = (size + 0xFFFu) & ~0xFFFu;
    return STATUS_SUCCESS;
}
XSTDCALL static uint32_t nk_NtFreeVirtualMemory(uint32_t base_va,
        uint32_t size_va, uint32_t type) {
    (void)base_va; (void)size_va; (void)type;
    return STATUS_SUCCESS;
}

/* ── Debug print ───────────────────────────────────────────── */
/* cdecl varargs: caller cleans up, so a plain cdecl function is safe. */
static uint32_t nk_DbgPrint(uint32_t fmt_va, ...) {
    if (fmt_va && fmt_va < NAT_ARENA_END)
        fprintf(stderr, "  [DbgPrint] %.200s", (const char *)(uintptr_t)fmt_va);
    return 0;
}

/* ── Generic arity-correct abort stubs ─────────────────────── */
/* One per stdcall arg-count 0..10 so `ret n` balances the guest stack
 * even for ordinals we haven't implemented yet.  Reports the ordinal and
 * the guest call site, then exits (C1 policy: loud on first hit). */
/* Slot VA -> ordinal, recorded at patch time so a miss can name itself. */
#define THUNK_SLOTS 144
static uint32_t s_slot_va[THUNK_SLOTS];
static uint32_t s_slot_ord[THUNK_SLOTS];
static int s_slot_count = 0;

static uint32_t ordinal_at_callsite(uint32_t retaddr) {
    /* The guest call is `FF 15 <abs32>` (call dword ptr [slot]); the
     * return address points just past it, so the slot VA is at -4. */
    if (retaddr < NAT_ARENA_BASE + 6 || retaddr >= NAT_ARENA_END) return 0;
    const uint8_t *p = (const uint8_t *)(uintptr_t)(retaddr - 6);
    if (p[0] != 0xFF || p[1] != 0x15) return 0;
    uint32_t slot; memcpy(&slot, p + 2, 4);
    for (int i = 0; i < s_slot_count; i++)
        if (s_slot_va[i] == slot) return s_slot_ord[i];
    return 0;
}

/* Default policy for unimplemented ordinals: log the first hit of each,
 * then return 0 (STATUS_SUCCESS / NULL).  This walks the whole boot
 * kernel-call sequence in one run; a bogus 0 that gets used as a pointer
 * faults later and the signal dumper names the site.  Ordinals that
 * clearly need real return values get real stubs above. */
static uint8_t s_logged[512];
static int s_miss_total = 0;

static void stub_miss(int nargs, void *retaddr) {
    uint32_t ra = (uint32_t)(uintptr_t)retaddr;
    uint32_t ord = ordinal_at_callsite(ra);
    if (ord < 512 && !s_logged[ord]) {
        s_logged[ord] = 1;
        s_miss_total++;
        fprintf(stderr, "  [stub] %s (ord %u, %d args) -> 0  [from 0x%08X]\n",
                ord ? nat_ordinal_name(ord) : "?", ord, nargs, ra);
    }
}
#define ABORT_STUB(N, ...) \
    XSTDCALL static uint32_t nk_abort_##N(__VA_ARGS__) { \
        stub_miss(N, __builtin_return_address(0)); return 0; }
ABORT_STUB(0, void)
ABORT_STUB(1, uint32_t a)
ABORT_STUB(2, uint32_t a, uint32_t b)
ABORT_STUB(3, uint32_t a, uint32_t b, uint32_t c)
ABORT_STUB(4, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
ABORT_STUB(5, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
ABORT_STUB(6, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
ABORT_STUB(7, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g)
ABORT_STUB(8, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g, uint32_t h)
ABORT_STUB(9, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g, uint32_t h, uint32_t i)
ABORT_STUB(10, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g, uint32_t h, uint32_t i, uint32_t j)

/* Fastcall abort stubs: first two dwords come in ecx/edx, so only args
 * beyond 8 bytes are on the stack and callee-cleaned.  A stdcall abort
 * stub in a fastcall slot would `ret` the wrong count and corrupt the
 * caller's stack (KfLowerIrql alone is called from 131 sites). */
#define FABORT_STUB(N, ...) \
    XFASTCALL static uint32_t nkf_abort_##N(__VA_ARGS__) { \
        stub_miss(N, __builtin_return_address(0)); return 0; }
FABORT_STUB(0, void)
FABORT_STUB(1, uint32_t a)
FABORT_STUB(2, uint32_t a, uint32_t b)
FABORT_STUB(3, uint32_t a, uint32_t b, uint32_t c)
FABORT_STUB(4, uint32_t a, uint32_t b, uint32_t c, uint32_t d)

static void *abort_stub_for(int argbytes, int cc) {
    int nargs = argbytes / 4;
    if (cc == XK_FASTCALL) {
        switch (nargs) {
        case 0: case 1: case 2: return (void *)nkf_abort_2;  /* all in regs */
        case 3: return (void *)nkf_abort_3;
        default: return (void *)nkf_abort_4;
        }
    }
    switch (nargs) {
    case 0: return (void *)nk_abort_0;   case 1: return (void *)nk_abort_1;
    case 2: return (void *)nk_abort_2;   case 3: return (void *)nk_abort_3;
    case 4: return (void *)nk_abort_4;   case 5: return (void *)nk_abort_5;
    case 6: return (void *)nk_abort_6;   case 7: return (void *)nk_abort_7;
    case 8: return (void *)nk_abort_8;   case 9: return (void *)nk_abort_9;
    default: return (void *)nk_abort_10;
    }
}

/* ── Implemented-ordinal → stub map ────────────────────────── */
static void *impl_stub_for(uint32_t ord) {
    switch (ord) {
    case 14:  return (void *)nk_ExAllocatePool;
    case 15:  return (void *)nk_ExAllocatePoolWithTag;
    case 17:  return (void *)nk_ExFreePool;
    case 165: return (void *)nk_MmAllocateContiguousMemory;
    case 166: return (void *)nk_MmAllocateContiguousMemoryEx;
    case 187: return (void *)nk_NtClose;
    case 190: return (void *)nk_NtCreateFile;
    case 202: return (void *)nk_NtOpenFile;
    case 219: return (void *)nk_NtReadFile;
    case 277: case 294: return (void *)nk_CS_noop;
    case 289: return (void *)nk_RtlInitAnsiString;
    case 291: return (void *)nk_RtlInitCS;
    case 184: return (void *)nk_NtAllocateVirtualMemory;
    case 199: return (void *)nk_NtFreeVirtualMemory;
    case 8:   return (void *)nk_DbgPrint;
    case 255: return (void *)nk_PsCreateSystemThreadEx;
    case 258: return (void *)nk_PsTerminateSystemThread;
    case 160: return (void *)nk_KfRaiseIrql;   /* fastcall */
    case 161: return (void *)nk_KfLowerIrql;   /* fastcall */
    default:  return NULL;
    }
}

uint32_t nat_thunks_patch(const nat_xbe *xbe) {
    uint32_t n = 0;
    s_slot_count = 0;
    for (uint32_t i = 0; i < 144; i++) {
        uint32_t slot = xbe->thunk_va + i * 4u;
        uint32_t val = GMEM32(slot);
        if (!(val & 0x80000000u)) continue;   /* not an ordinal entry */
        uint32_t ord = val & 0x7FFFFFFFu;
        if (s_slot_count < THUNK_SLOTS) {
            s_slot_va[s_slot_count]  = slot;
            s_slot_ord[s_slot_count] = ord;
            s_slot_count++;
        }

        if (ord <= XK_ORDINAL_MAX && s_kind[ord] == XK_DATA) {
            GMEM32(slot) = kdata_va(ord);      /* data export cell */
            n++;
            continue;
        }
        void *fn = impl_stub_for(ord);
        if (!fn) {
            int ab = ord <= XK_ORDINAL_MAX ? s_argbytes[ord] : 0;
            int cc = ord <= XK_ORDINAL_MAX ? s_cc[ord] : XK_STDCALL;
            fn = abort_stub_for(ab, cc);
        }
        GMEM32(slot) = (uint32_t)(uintptr_t)fn;
        n++;
    }
    return n;
}

const char *nat_ordinal_name(uint32_t ord) {
    return (ord <= XK_ORDINAL_MAX && s_name[ord]) ? s_name[ord] : "?";
}
