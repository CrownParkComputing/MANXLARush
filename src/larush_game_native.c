// larush_game_native.c — D3D8/Vulkan render plumbing for L.A. Rush.
//
// Sets up MANXFramework's shared Vulkan D3D8 backend and probes the
// L.A. Rush XBE's XDK subsystem sections (D3D, D3DX, XGRPH, DSOUND,
// XACTENG, XONLINE, XNET, XMV, WMADEC) so their import surfaces can be
// catalogued for recompilation.
//
// Unlike the FlatOut 1 probe the section VAs are not hardcoded — the
// L.A. Rush analysis recorded section names but not addresses, so this
// walks the XBE section table and reports every subsystem it finds.
//
// Does NOT run unrecompiled game code.
//
// Usage:
//   ./LARushD3DProbe [game_data/L.A.Rush.USA.XBOX-ZTM]
//   ./LARushD3DProbe --no-xbe     Vulkan init/clear/readback only
//                                 (Stage A: runnable with no game data)

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── D3D8 / Vulkan backend ───────────────────────────────────
#include "d3d8_xbox.h"
#include "vulkan_d3d8.h"

// ── Kernel shim API ─────────────────────────────────────────
extern int      xbox_MemoryLayoutInit(const void *xbe, unsigned long sz);
extern void     xbox_MemoryLayoutShutdown(void);
extern void     larush_kernel_init(void);
extern void     larush_kernel_shutdown(void);
extern ptrdiff_t g_xbox_mem_offset;

#define MEM32(addr) (*(volatile uint32_t *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))
#define MEM16(addr) (*(volatile uint16_t *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))

#define LAR_THUNK_BASE  0x002A1620u
#define LAR_THUNK_COUNT 144u
#define LAR_KDATA_BASE  0x00F10000u
#define XBE_ENTRY_KEY   0xA8FC57ABu

/* XDK subsystem sections worth probing for import tables. */
static const char *const SUBSYSTEM_SECTIONS[] = {
    "D3D", "D3DX", "XGRPH", "DSOUND", "XACTENG",
    "XONLINE", "XNET", "XMV", "WMADEC", NULL
};

/* ═══════════════════════════════════════════════════════════════
 *  XBE section walk + subsystem probe
 * ═══════════════════════════════════════════════════════════════ */

static void probe_section(const char *name, uint32_t va, uint32_t vsize) {
    printf("\n── %s Section Probe (VA 0x%08X, %u bytes) ──\n",
           name, va, vsize);

    uint32_t end = va + vsize;
    int unresolved = 0, va_ptrs = 0;
    for (uint32_t p = va; p + 4 <= end; p += 4) {
        uint32_t val = MEM32(p);
        if (val == 0) continue;
        if (val & 0x80000000u) unresolved++;
        else if (val >= 0x00010000u && val < 0x01000000u) va_ptrs++;
    }
    printf("  Unresolved imports (bit 31 set): %d\n", unresolved);
    printf("  In-image VA pointers:            %d\n", va_ptrs);

    printf("  First 64 bytes:\n  ");
    for (int i = 0; i < 16; i++) {
        printf("%08X ", MEM32(va + (uint32_t)i * 4));
        if ((i + 1) % 8 == 0) printf("\n  ");
    }
    printf("\n");
}

static void probe_subsystems(const uint8_t *xbe, unsigned long xbe_size) {
    uint32_t base = *(const uint32_t *)(xbe + 0x104);
    uint32_t nsec = *(const uint32_t *)(xbe + 0x11C);
    uint32_t hdr  = *(const uint32_t *)(xbe + 0x120) - base;

    printf("\n── XBE Section Table (%u sections) ──\n", nsec);
    for (uint32_t i = 0; i < nsec; i++) {
        const uint32_t *s = (const uint32_t *)(xbe + hdr + i * 0x38);
        uint32_t va = s[1], vsize = s[2], raw = s[3], rsz = s[4];
        uint32_t name_va = s[5];
        const char *name = "?";
        if (name_va >= base && name_va - base < xbe_size)
            name = (const char *)(xbe + (name_va - base));
        printf("  [%2u] %-10s VA 0x%08X  vsize %8u  raw 0x%08X (%u)\n",
               i, name, va, vsize, raw, rsz);
    }

    /* Probe each known XDK subsystem section found in the table. */
    for (uint32_t i = 0; i < nsec; i++) {
        const uint32_t *s = (const uint32_t *)(xbe + hdr + i * 0x38);
        uint32_t va = s[1], vsize = s[2];
        uint32_t name_va = s[5];
        if (name_va < base || name_va - base >= xbe_size) continue;
        const char *name = (const char *)(xbe + (name_va - base));
        for (int k = 0; SUBSYSTEM_SECTIONS[k]; k++) {
            if (strcmp(name, SUBSYSTEM_SECTIONS[k]) == 0) {
                if (vsize > 4 * 1024 * 1024) vsize = 4 * 1024 * 1024;
                probe_section(name, va, vsize);
                break;
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Debug: dump kernel data exports
 * ═══════════════════════════════════════════════════════════════ */

static void dump_kernel_data(void) {
    printf("\n── Kernel Data Exports (0x%08X) ──\n", LAR_KDATA_BASE);
    printf("  HardwareInfo:      0x%08X\n", MEM32(LAR_KDATA_BASE + 0x000));
    printf("  KrnlVersion:       %u.%u.%u\n",
           (unsigned)MEM16(LAR_KDATA_BASE + 0x010),
           (unsigned)MEM16(LAR_KDATA_BASE + 0x012),
           (unsigned)MEM16(LAR_KDATA_BASE + 0x014));
    printf("  TickCount:         0x%08X\n", MEM32(LAR_KDATA_BASE + 0x020));
    printf("  LaunchDataPage:    0x%08X\n", MEM32(LAR_KDATA_BASE + 0x030));
}

/* ═══════════════════════════════════════════════════════════════
 *  D3D8 device init test — no game data required
 * ═══════════════════════════════════════════════════════════════ */

static int test_d3d8_init(void) {
    printf("\n── D3D8 Vulkan Device Init Test ──\n");

    if (!vulkan_d3d8_init(640, 480)) {
        printf("  FAILED: vulkan_d3d8_init returned 0\n");
        printf("  (This is expected on headless systems — skip if no GPU)\n");
        return 0;
    }

    IDirect3D8 *d3d8 = vulkan_d3d8_get_d3d8();
    IDirect3DDevice8 *dev = vulkan_d3d8_get_device();

    printf("  Vulkan D3D8 backend initialized\n");
    printf("  IDirect3D8:       %s\n", d3d8 ? "valid" : "NULL");
    printf("  IDirect3DDevice8: %s\n", dev  ? "valid" : "NULL");

    if (dev) {
        printf("  Testing Present() → readback...\n");
        dev->lpVtbl->BeginScene(dev);
        /* Clear to L.A. Rush sunset orange to prove the pipeline. */
        dev->lpVtbl->Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0xFFB0501C, 1.0f, 0);
        dev->lpVtbl->EndScene(dev);
        dev->lpVtbl->Present(dev, NULL, NULL, NULL, NULL);

        int w = 0, h = 0;
        const uint8_t *frame = vulkan_d3d8_present(&w, &h);
        if (frame && w > 0 && h > 0) {
            printf("  Readback: %dx%d RGBA8 — pipeline functional!\n", w, h);
            printf("  Top-left pixel: R=%u G=%u B=%u A=%u (expect 0xB0501C)\n",
                   frame[0], frame[1], frame[2], frame[3]);
        } else {
            printf("  Readback: returned NULL (pipeline not ready yet)\n");
        }
    }

    vulkan_d3d8_shutdown();
    return 1;
}

/* ═══════════════════════════════════════════════════════════════
 *  Main
 * ═══════════════════════════════════════════════════════════════ */

static int larush_game_native_probe(const char *game_data_path,
                                    int do_d3d8_test) {
    printf("═══════════════════════════════════════════════════════\n");
    printf("  L.A. Rush — D3D8/Vulkan Infrastructure Diagnostic\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("Data: %s\n", game_data_path);

    // 1. Load XBE
    char xbe_path[1024];
    snprintf(xbe_path, sizeof(xbe_path), "%s/default.xbe", game_data_path);
    FILE *f = fopen(xbe_path, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open %s\n", xbe_path);
        fprintf(stderr, "Run with --no-xbe for the GPU-only smoke test.\n");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *xbe_data = malloc((size_t)sz);
    if (!xbe_data || fread(xbe_data, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "ERROR: XBE read failed\n");
        free(xbe_data); fclose(f);
        return 1;
    }
    fclose(f);
    printf("Loaded %s (%ld bytes)\n", xbe_path, sz);

    {
        const uint8_t *xb = (const uint8_t *)xbe_data;
        uint32_t entry_raw = *(const uint32_t *)(xb + 0x128);
        printf("Entry: 0x%08X (raw=0x%08X)\n",
               entry_raw ^ XBE_ENTRY_KEY, entry_raw);
    }

    // 2. Init 64 MB Xbox memory
    if (!xbox_MemoryLayoutInit(xbe_data, (unsigned long)sz)) {
        fprintf(stderr, "ERROR: memory init failed\n");
        free(xbe_data);
        return 1;
    }
    printf("64 MB Xbox memory mapped\n");

    // 3. Init kernel thunks
    larush_kernel_init();

    // 4. Dump kernel data exports
    dump_kernel_data();

    // 5. Walk section table and probe every XDK subsystem present
    probe_subsystems((const uint8_t *)xbe_data, (unsigned long)sz);

    // 6. Test D3D8 Vulkan device
    if (do_d3d8_test)
        test_d3d8_init();

    // 7. Summary
    printf("\n── Summary ──\n");
    printf("  Kernel thunks: %u-entry table at VA 0x%08X\n",
           LAR_THUNK_COUNT, LAR_THUNK_BASE);
    printf("  Subsystems probed from the live section table above.\n");
    printf("  Next: k9 archive decode → XPR textures → entry-path analysis\n");

    // Cleanup
    larush_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe_data);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Diagnostic complete\n");
    printf("═══════════════════════════════════════════════════════\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--no-xbe") == 0) {
        printf("═══════════════════════════════════════════════════════\n");
        printf("  L.A. Rush — GPU smoke test (no game data)\n");
        printf("═══════════════════════════════════════════════════════\n");
        return test_d3d8_init() ? 0 : 1;
    }

    const char *data_dir = argc > 1 ? argv[1]
        : "game_data/L.A.Rush.USA.XBOX-ZTM";
    int do_d3d8 = !getenv("LAR_NO_GPU");
    return larush_game_native_probe(data_dir, do_d3d8);
}
