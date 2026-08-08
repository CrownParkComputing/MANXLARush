// larush_kernel_shim.c — Minimal Xbox kernel for L.A. Rush first boot.
//
// Based on flatout1_kernel_shim.c (itself forked from
// burnout3_kernel_shim.c) but parameterized for L.A. Rush's kernel
// thunk table at VA 0x002BFF48 with 256 entries.  The FlatOut init
// table walker and hand-recompiled stubs are stripped — whether L.A.
// Rush's entry point (0x00010184) is an init table or a direct CRT
// entry is still an open question for the entry-path analysis pass.
//
// File I/O stubs route through the k9 VFS (larush_kernel_set_k9);
// until the k9 entry-table format is decoded, lookups fail with
// STATUS_OBJECT_NAME_NOT_FOUND, which is the correct honest answer.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "larush_k9_vfs.h"

/* Xbox memory macros — inlined here because recomp_types.h pulls in
 * __forceinline and other MSVC constructs that fail under GCC. */
#define MEM32(addr) (*(volatile uint32_t *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))
#define MEM16(addr) (*(volatile uint16_t *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))
#define MEM8(addr)  (*(volatile uint8_t  *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

// ═══════════════════════════════════════════════════════════════
//  Xbox memory access (definitions)
// ═══════════════════════════════════════════════════════════════

ptrdiff_t g_xbox_mem_offset = 0;

// ═══════════════════════════════════════════════════════════════
//  CPU register stubs (referenced by recompiled code)
// ═══════════════════════════════════════════════════════════════

uint32_t g_esp = 0;
uint32_t g_eax = 0;
uint32_t g_ecx = 0;
uint32_t g_edx = 0;
uint32_t g_ebx = 0;
uint32_t g_esi = 0;
uint32_t g_edi = 0;
uint32_t g_ebp = 0;
uint32_t g_seh_ebp = 0;

/* ═══════════════════════════════════════════════════════════════
 *  Xbox memory layout — 64 MB unified RAM
 * ═══════════════════════════════════════════════════════════════ */

#define XBOX_TOTAL_RAM (64 * 1024 * 1024)

static void *s_xbox_memory = NULL;
static size_t s_xbox_memory_size = 0;

int xbox_MemoryLayoutInit(const void *xbe_data, unsigned long xbe_size) {
    if (s_xbox_memory) return 1;

#ifdef _WIN32
    s_xbox_memory = VirtualAlloc(NULL, XBOX_TOTAL_RAM,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    s_xbox_memory = mmap((void *)0x200000000ull, XBOX_TOTAL_RAM,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (s_xbox_memory != (void *)0x200000000ull) {
        if (s_xbox_memory != MAP_FAILED)
            munmap(s_xbox_memory, XBOX_TOTAL_RAM);
        s_xbox_memory = mmap((void *)0x200000000ull, XBOX_TOTAL_RAM,
            PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    }
#endif

    if (!s_xbox_memory || s_xbox_memory == MAP_FAILED) {
        fprintf(stderr, "larush_kernel: failed to allocate 64 MB Xbox RAM\n");
        return 0;
    }

    s_xbox_memory_size = XBOX_TOTAL_RAM;
    memset(s_xbox_memory, 0, XBOX_TOTAL_RAM);
    g_xbox_mem_offset = (ptrdiff_t)s_xbox_memory;

    // ── Load XBE sections ─────────────────────────────────
    const uint8_t *xb = (const uint8_t *)xbe_data;
    const uint32_t base = *(const uint32_t *)(xb + 0x104);
    const uint32_t nsec = *(const uint32_t *)(xb + 0x11C);
    const uint32_t hdr  = *(const uint32_t *)(xb + 0x120) - base;
    int loaded = 0;
    for (uint32_t i = 0; i < nsec; i++) {
        const uint32_t *s = (const uint32_t *)(xb + hdr + i * 0x38);
        const uint32_t va = s[1], raw = s[3], rsz = s[4];
        if ((unsigned long)raw + rsz > xbe_size) continue;
        if (va < XBOX_TOTAL_RAM && (unsigned long)va + rsz <= XBOX_TOTAL_RAM) {
            memcpy((uint8_t *)s_xbox_memory + va, xb + raw, rsz);
            loaded++;
        }
    }

    fprintf(stderr, "larush_kernel: 64 MB Xbox memory at %p (offset=%td, %d/%u sections)\n",
            s_xbox_memory, g_xbox_mem_offset, loaded, nsec);
    return 1;
}

void xbox_MemoryLayoutShutdown(void) {
    if (!s_xbox_memory) return;
#ifdef _WIN32
    VirtualFree(s_xbox_memory, 0, MEM_RELEASE);
#else
    munmap(s_xbox_memory, s_xbox_memory_size);
#endif
    s_xbox_memory = NULL;
    s_xbox_memory_size = 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  L.A. Rush kernel thunk table
 * ═══════════════════════════════════════════════════════════════ */

/* ── 0x002A1620 (.rdata), 144 entries — confirmed against the retail
 * XBE header: kernel-thunk field at 0x158 ^ retail key 0x5B6D40B6 =
 * 0x002A1620, and 144 consecutive 0x80000000|ordinal entries live
 * there.  (The earlier recovered analysis claimed 256 @ 0x002BFF48;
 * that was wrong.)  The retail image ends at 0x00497760, so the
 * synthetic dispatch block at 0x00F00000 and KDATA at 0x00F10000 sit
 * safely between the image top and XBOX_HEAP_START. ── */
#define KERNEL_THUNK_BASE  0x002A1620u
#define KERNEL_THUNK_COUNT 144u
#define KERNEL_SYNTH_BASE  0x00F00000u
#define KERNEL_SYNTH_END   (KERNEL_SYNTH_BASE + KERNEL_THUNK_COUNT * 4u)

static uint16_t s_kernel_ordinals[KERNEL_THUNK_COUNT];
static int s_kernel_dispatch_slot = -1;

// Forward declarations
static void kernel_data_init(void);
static uint32_t kernel_data_va_for_ordinal(uint32_t ordinal);

void larush_kernel_init(void) {
    uint32_t remapped = 0;
    memset(s_kernel_ordinals, 0, sizeof(s_kernel_ordinals));
    kernel_data_init();

    for (uint32_t i = 0; i < KERNEL_THUNK_COUNT; i++) {
        uint32_t entry = MEM32(KERNEL_THUNK_BASE + i * 4);
        if (entry & 0x80000000u) {
            uint32_t ordinal = entry & 0x7FFFFFFFu;
            uint32_t data_va = kernel_data_va_for_ordinal(ordinal);
            s_kernel_ordinals[i] = (uint16_t)ordinal;
            MEM32(KERNEL_THUNK_BASE + i * 4) =
                data_va ? data_va : KERNEL_SYNTH_BASE + i * 4;
            remapped++;
        }
    }
    fprintf(stderr, "larush_kernel: kernel init (%u/%u thunks remapped)\n",
            remapped, KERNEL_THUNK_COUNT);

    fprintf(stderr, "larush_kernel:   slot ordinals:");
    for (uint32_t j = 0; j < KERNEL_THUNK_COUNT; j++) {
        if (s_kernel_ordinals[j])
            fprintf(stderr, " %u:%u", j, s_kernel_ordinals[j]);
    }
    fprintf(stderr, "\n");
}

void larush_kernel_shutdown(void) {
    fprintf(stderr, "larush_kernel: shutdown\n");
}

/* Number of thunk slots currently remapped (test/diagnostic hook). */
uint32_t larush_kernel_remapped_count(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < KERNEL_THUNK_COUNT; i++)
        if (s_kernel_ordinals[i]) n++;
    return n;
}

/* Ordinal recorded at a thunk slot, 0 if none (test/diagnostic hook). */
uint32_t larush_kernel_slot_ordinal(uint32_t slot) {
    return slot < KERNEL_THUNK_COUNT ? s_kernel_ordinals[slot] : 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Heap allocator (bump, shared)
 * ═══════════════════════════════════════════════════════════════ */

#define XBOX_HEAP_START 0x01000000u
#define XBOX_HEAP_END   0x03F00000u

/* Emulated stack for init dispatch: grows DOWN from XBOX_STACK_BASE
 * toward XBOX_STACK_LIMIT, which sits just below the top of the heap
 * region so the two never collide. */
#define XBOX_STACK_BASE  0x03FE0000u
#define XBOX_STACK_LIMIT 0x03F00000u

static uint32_t s_heap_next = XBOX_HEAP_START;

static uint32_t xbox_heap_alloc(uint32_t size, uint32_t align) {
    if (align < 4) align = 4;
    if ((align & (align - 1)) != 0) align = 4096;
    uint32_t addr = (s_heap_next + align - 1) & ~(align - 1);
    if (addr >= XBOX_HEAP_END || size > XBOX_HEAP_END - addr) {
        fprintf(stderr, "larush_kernel: HEAP EXHAUSTED (need %u)\n", size);
        return 0;
    }
    s_heap_next = addr + size;
    memset((uint8_t *)g_xbox_mem_offset + addr, 0, size);
    return addr;
}

static void xbox_heap_free(uint32_t va) { (void)va; }

uint32_t larush_kernel_heap_alloc(uint32_t size, uint32_t align) {
    return xbox_heap_alloc(size, align);
}
void larush_kernel_heap_free(uint32_t va) { xbox_heap_free(va); }

/* ═══════════════════════════════════════════════════════════════
 *  k9 VFS integration (optional — set via larush_kernel_set_k9)
 * ═══════════════════════════════════════════════════════════════ */

#define K9_FILE_MAX 64

typedef struct {
    uint32_t      handle;
    const uint8_t *data;
    uint32_t      size;
    uint32_t      position;
    char          path[256];
} k9_file_entry;

static k9_file_entry s_k9_files[K9_FILE_MAX];
static uint32_t s_k9_file_next = 0xA9500001u;
static larush_k9 *s_k9 = NULL;

void larush_kernel_set_k9(larush_k9 *k9) {
    s_k9 = k9;
    if (k9) {
        memset(s_k9_files, 0, sizeof(s_k9_files));
        s_k9_file_next = 0xA9500001u;
    }
}

static k9_file_entry *k9_file_claim(const uint8_t *data, uint32_t size,
                                    const char *path) {
    for (int i = 0; i < K9_FILE_MAX; i++) {
        if (!s_k9_files[i].data) {
            s_k9_files[i].handle   = s_k9_file_next++;
            s_k9_files[i].data     = data;
            s_k9_files[i].size     = size;
            s_k9_files[i].position = 0;
            snprintf(s_k9_files[i].path,
                     sizeof(s_k9_files[i].path), "%.255s",
                     path ? path : "");
            return &s_k9_files[i];
        }
    }
    return NULL;
}

static k9_file_entry *k9_file_find(uint32_t handle) {
    for (int i = 0; i < K9_FILE_MAX; i++)
        if (s_k9_files[i].data && s_k9_files[i].handle == handle)
            return &s_k9_files[i];
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 *  Kernel stubs (same stubs as Burnout 3 / FlatOut 1 — portable)
 * ═══════════════════════════════════════════════════════════════ */

#define STACK_ARG(n) MEM32(g_esp + 4 + (uint32_t)((n) * 4))

static void stub_MmAllocateContiguousMemoryEx(void) {
    uint32_t size = STACK_ARG(0), align = STACK_ARG(3);
    if (align < 4096) align = 4096;
    uint32_t va = xbox_heap_alloc(size, align);
    g_eax = va;
}

static void stub_MmAllocateContiguousMemory(void) {
    g_eax = xbox_heap_alloc(STACK_ARG(0), 4096);
}

static void stub_ExAllocatePoolWithTag(void) {
    g_eax = xbox_heap_alloc(STACK_ARG(0), 16);
}

static void stub_ExFreePool(void) {
    xbox_heap_free(STACK_ARG(0));
    g_eax = 0;
}

static void stub_ExAllocatePool(void) {
    g_eax = xbox_heap_alloc(STACK_ARG(0), 16);
}

static void stub_ExQueryPoolBlockSize(void) {
    (void)STACK_ARG(0);
    g_eax = 0;
}

static void stub_NtDuplicateObject(void) {
    uint32_t target_va = STACK_ARG(1);
    if (target_va) MEM32(target_va) = STACK_ARG(0);
    g_eax = 0;
}

static void stub_RtlInitAnsiString(void) {
    uint32_t string_va = STACK_ARG(0);
    uint32_t source_va = STACK_ARG(1);
    size_t length = 0;
    if (source_va && source_va < XBOX_TOTAL_RAM) {
        const char *source = (const char *)((uintptr_t)g_xbox_mem_offset + source_va);
        length = strnlen(source, 0xFFFFu);
    }
    if (string_va && string_va + 8 <= XBOX_TOTAL_RAM) {
        MEM16(string_va + 0) = (uint16_t)length;
        MEM16(string_va + 2) = (uint16_t)(source_va ? length + 1 : 0);
        MEM32(string_va + 4) = source_va;
    }
    g_eax = 0;
}

static void stub_RtlInitializeCriticalSection(void) {
    uint32_t cs_va = STACK_ARG(0);
    if (cs_va && cs_va + 24 <= XBOX_TOTAL_RAM)
        memset((uint8_t *)g_xbox_mem_offset + cs_va, 0, 24);
    g_eax = 0;
}

static void stub_RtlCriticalSectionNoop(void) {
    (void)STACK_ARG(0);
    g_eax = 0;
}

/* ── File I/O stubs ──────────────────────────────────────── */

static void stub_NtClose(void) {
    uint32_t handle = STACK_ARG(0);
    for (int i = 0; i < K9_FILE_MAX; i++) {
        if (s_k9_files[i].handle == handle) {
            memset(&s_k9_files[i], 0, sizeof(s_k9_files[i]));
            break;
        }
    }
    g_eax = 0;
}

static void stub_NtQueryVolumeInformationFile(void) {
    uint32_t info_va   = STACK_ARG(2);
    uint32_t infoclass = STACK_ARG(4);
    switch (infoclass) {
    case 3:
        MEM32(info_va +  0) = 1048576;
        MEM32(info_va +  8) = 524288;
        MEM32(info_va + 16) = 8;
        MEM32(info_va + 20) = 512;
        g_eax = 0;
        break;
    default:
        g_eax = 0xC00000BBu;
        break;
    }
}

static void stub_NtOpenFile(void) {
    uint32_t handle_va  = STACK_ARG(0);
    /* Fallback: no k9 VFS wired — return fake handle (old behaviour). */
    if (!s_k9) {
        if (handle_va) MEM32(handle_va) = 0xDEAD0001u;
        g_eax = 0;
        return;
    }
    uint32_t attr_va    = STACK_ARG(2);
    uint32_t ios_va     = STACK_ARG(3);
    if (!attr_va) { g_eax = 0xC0000034u; return; }

    /* Extract Xbox path string from object attributes. */
    uint32_t ansi_va = attr_va < XBOX_TOTAL_RAM ? MEM32(attr_va + 4u) : 0;
    if (!ansi_va || ansi_va >= XBOX_TOTAL_RAM) { g_eax = 0xC0000034u; return; }
    uint32_t buf_va = ansi_va + 8 <= XBOX_TOTAL_RAM ? MEM32(ansi_va + 4u) : 0;
    if (!buf_va || buf_va >= XBOX_TOTAL_RAM) { g_eax = 0xC0000034u; return; }
    const char *xbox_path = (const char *)((uintptr_t)g_xbox_mem_offset + buf_va);
    size_t path_max = XBOX_TOTAL_RAM - (size_t)buf_va;
    size_t path_len = strnlen(xbox_path, path_max < 512 ? path_max : 512);
    if (path_len == 0 || path_len >= 512) { g_eax = 0xC0000034u; return; }

    /* Normalise to k9 path: strip drive letter / device prefix,
     * convert backslashes to forward slashes. */
    char k9_path[512];
    if (xbox_path[0] && xbox_path[1] == ':') xbox_path += 2;
    else if (strncmp(xbox_path, "/Device/", 8) == 0) {
        xbox_path = strchr(xbox_path + 8, '/');
        if (!xbox_path) { g_eax = 0xC0000034u; return; }
    }
    while (*xbox_path == '/' || *xbox_path == '\\') xbox_path++;
    snprintf(k9_path, sizeof(k9_path), "%s", xbox_path);
    for (char *p = k9_path; *p; p++) if (*p == '\\') *p = '/';

    /* Look up in the k9 VFS.  Fails with not-found until the entry
     * table is decoded (Stage B). */
    const uint8_t *data; uint32_t len;
    if (!larush_k9_find_by_path(s_k9, k9_path, &data, &len, NULL, NULL)) {
        g_eax = 0xC0000034u;
        return;
    }

    k9_file_entry *ent = k9_file_claim(data, len, k9_path);
    if (!ent) { g_eax = 0xC0000034u; return; }

    if (handle_va < XBOX_TOTAL_RAM) MEM32(handle_va) = ent->handle;
    if (ios_va && ios_va + 8 <= XBOX_TOTAL_RAM) {
        MEM32(ios_va + 0) = 0;
        MEM32(ios_va + 4) = len;
    }
    g_eax = 0;
}

/* NtCreateFile: same as NtOpenFile but may create new files.
 * For k9 (read-only archive), just open existing. */
static void stub_NtCreateFile(void) {
    stub_NtOpenFile();
}

/* NtReadFile: reads from a k9 file handle into an Xbox buffer. */
static void stub_NtReadFile(void) {
    uint32_t handle   = STACK_ARG(0);
    uint32_t ios_va   = STACK_ARG(4);
    uint32_t buf_va   = STACK_ARG(5);
    uint32_t length   = STACK_ARG(6);
    uint32_t off_va   = STACK_ARG(7);

    k9_file_entry *ent = k9_file_find(handle);
    if (!ent) { g_eax = 0xC0000008u; return; }

    /* Optional explicit offset. */
    if (off_va && off_va + 8 <= XBOX_TOTAL_RAM) {
        uint64_t off64 = (uint64_t)MEM32(off_va) |
                         ((uint64_t)MEM32(off_va + 4u) << 32);
        if (off64 < (uint64_t)ent->size) ent->position = (uint32_t)off64;
    }

    uint32_t avail = ent->size - ent->position;
    uint32_t to_read = length < avail ? length : avail;
    if (buf_va && buf_va + to_read <= XBOX_TOTAL_RAM && to_read > 0)
        memcpy((uint8_t *)g_xbox_mem_offset + buf_va,
               ent->data + ent->position, to_read);
    ent->position += to_read;

    if (ios_va && ios_va + 8 <= XBOX_TOTAL_RAM) {
        MEM32(ios_va + 0) = 0;
        MEM32(ios_va + 4) = to_read;
    }
    g_eax = (to_read == 0 && length > 0) ? 0xC0000011u : 0;
}

/* ── Kernel data exports ──────────────────────────────────── */

/* Provisional like KERNEL_SYNTH_BASE — must clear the retail section
 * map (Stage B1).  Sits just above the synthetic thunk block. */
#define KDATA_BASE 0x00F10000u

static uint32_t kernel_data_va_for_ordinal(uint32_t ordinal) {
    switch (ordinal) {
    case  17: return KDATA_BASE + 0x040;  // event obj type
    case  65: return KDATA_BASE + 0x070;  // io completion type
    case  71: return KDATA_BASE + 0x080;  // io device type
    case 156: return KDATA_BASE + 0x020;  // tick count
    case 164: return KDATA_BASE + 0x030;  // launch data page
    case 259: return KDATA_BASE + 0x040;  // thread obj type
    case 322: return KDATA_BASE + 0x000;  // hardware info
    case 323: return KDATA_BASE + 0x100;  // HD key
    case 324: return KDATA_BASE + 0x010;  // kernel version
    case 325: return KDATA_BASE + 0x110;  // signature key
    case 326:
    case 355: return KDATA_BASE + 0x120;  // LAN key
    case 327:
    case 356: return KDATA_BASE + 0x130;  // alt signature keys
    case 328: return KDATA_BASE + 0x060;  // XE image filename
    case 357: return KDATA_BASE + 0x300;  // XE public key
    default:  return 0;
    }
}

static void kernel_data_init(void) {
    memset((uint8_t *)g_xbox_mem_offset + KDATA_BASE, 0, 0x500);
    MEM32(KDATA_BASE + 0x000) = 0; MEM8(KDATA_BASE + 4) = 0xA1; MEM8(KDATA_BASE + 5) = 0xB1;
    MEM16(KDATA_BASE + 0x010) = 1; MEM16(KDATA_BASE + 0x012) = 0;
    MEM16(KDATA_BASE + 0x014) = 5849; MEM16(KDATA_BASE + 0x016) = 0;
    MEM32(KDATA_BASE + 0x020) = 1;  // tick count
    MEM32(KDATA_BASE + 0x030) = 0;  // launch data page
}

/* ── Dispatch table ───────────────────────────────────────── */

typedef void (*recomp_func_t)(void);

static int kernel_arg_bytes(uint32_t ordinal) {
    switch (ordinal) {
    case 1: case 8: case 23: case 42: case 126: case 127: case 129:
    case 160: case 161: case 238: case 250: case 358: case 360: return 0;
    case 4: case 15: case 24: case 40: case 49: case 69: case 97:
    case 99: case 100: case 124: case 128: case 137: case 139: case 142:
    case 151: case 165: case 171: case 173: case 179: case 180: case 181:
    case 187: case 195: case 258: case 277: case 291: case 294: case 301:
    case 302: case 345: case 359: return 4;
    case 16: case 41: case 44: case 46: case 83: case 113: case 143:
    case 168: case 169: case 170: case 176: case 198: case 203:
    case 225: case 228: case 253: case 289: case 304: case 305: case 339:
    case 353: return 8;
    case 74: case 84: case 107: case 119: case 145: case 153: case 175:
    case 177: case 178: case 182: case 197: case 215: case 222: case 234:
    case 246: case 256: case 260: case 269: case 279: case 308: case 335:
    case 336: case 338: case 340: case 344: case 346: case 349: case 354: return 12;
    case 2: case 85: case 149: case 189: case 193: case 217: case 312: return 16;
    case 86: case 98: case 150: case 159: case 166: case 184: case 211:
    case 218: case 226: case 247: case 347: return 20;
    case 3: case 47: return 24;
    case 109: return 28;
    case 62: case 190: case 207: case 210: return 36;
    case 158: case 219: case 236: return 32;
    case 67: case 196: case 200: case 255: return 40;
    /* Ordinals first seen in FlatOut 1's init table (value-only). */
    case 81: case 87: return 0;
    /* L.A. Rush ordinals beyond the Burnout 3 set (first boot scan:
     * 17/65 are data exports, 202/277/289 have stubs above).  Arg
     * byte counts TBD from disassembly of the call sites. */
    case 37: case 95: case 167: case 172: case 186: case 199:
    case 233: case 252: case 270: case 337: return 0;
    default: return 0;
    }
}

static recomp_func_t kernel_stub_for_ordinal(uint32_t ordinal) {
    switch (ordinal) {
    case 15:  return stub_ExAllocatePool;
    case 16:  return stub_ExAllocatePoolWithTag;
    case 24:  return stub_ExQueryPoolBlockSize;
    case 165: return stub_MmAllocateContiguousMemory;
    case 166: return stub_MmAllocateContiguousMemoryEx;
    case 171: return stub_ExFreePool;
    case 187: return stub_NtClose;
    case 190: return stub_NtCreateFile;
    case 197: return stub_NtDuplicateObject;
    case 202: return stub_NtOpenFile;
    case 210: return stub_NtReadFile;
    case 218: return stub_NtQueryVolumeInformationFile;
    case 277:
    case 294: return stub_RtlCriticalSectionNoop;
    case 289: return stub_RtlInitAnsiString;
    case 291: return stub_RtlInitializeCriticalSection;
    default:  return NULL;
    }
}

static void kernel_thunk_dispatch(void) {
    int slot = s_kernel_dispatch_slot;
    uint32_t ordinal;
    recomp_func_t stub;
    static unsigned call_count = 0;
    static bool warned[KERNEL_THUNK_COUNT];

    if (slot < 0 || slot >= (int)KERNEL_THUNK_COUNT) {
        g_eax = 0; g_esp += 4;
        return;
    }

    ordinal = s_kernel_ordinals[slot];
    stub = kernel_stub_for_ordinal(ordinal);
    call_count++;

    if (call_count <= 80) {
        fprintf(stderr, "  [LARUSH-KERN] #%u ordinal %u slot %d esp=0x%08X%s\n",
                call_count, ordinal, slot, g_esp, stub ? "" : " (STUBBED)");
    } else if (!stub && !warned[slot]) {
        warned[slot] = true;
        fprintf(stderr, "  [LARUSH-KERN] ordinal %u slot %d is a value-only stub\n",
                ordinal, slot);
    }

    if (stub) stub();
    else g_eax = 0;

    g_esp += 4u + (uint32_t)kernel_arg_bytes(ordinal);
}

recomp_func_t recomp_lookup_kernel(uint32_t xbox_va) {
    if (xbox_va < KERNEL_SYNTH_BASE || xbox_va >= KERNEL_SYNTH_END)
        return NULL;
    int slot = (int)((xbox_va - KERNEL_SYNTH_BASE) / 4);

    static uint32_t seen[32];
    static int nseen = 0;
    int i;
    for (i = 0; i < nseen; i++)
        if (seen[i] == (uint32_t)slot) break;
    if (i == nseen && nseen < 32) {
        seen[nseen++] = (uint32_t)slot;
        fprintf(stderr, "larush_kernel: kernel call slot %d (VA=%08X)\n",
                slot, xbox_va);
    }

    s_kernel_dispatch_slot = slot;
    return kernel_thunk_dispatch;
}
