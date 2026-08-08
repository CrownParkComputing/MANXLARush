// larush_game_main.c — hand-recompiled main (0x00087860, retail).
//
// Faithful port of main's 205 instructions: every global write goes to
// the same Xbox VA through MEM32/MEM16/MEM8, register-passed arguments
// (this compiler uses eax/edx/ebx/edi as extra parameters) go through
// the g_* register globals, and every call is dispatched through the
// larush_crt native registry — unregistered targets land in the
// pending table, which is the per-frame recompile hit-list.
//
// Structure of the original:
//   init:  handler pair install (0x1F1880 with 0x0008031F twice),
//          MXCSR flush-to-zero, early init 0x000877E0, engine init
//          0x0017C930(0x407D48, 0x340B34), input-slot loop (5 × 0x104
//          descriptor stride), save-device probe on [0x308C34],
//          CRT mallocs building the 0x3DEB6C descriptor block,
//          0x00039D10(eax=0x3DEB6C)
//   loop:  0x00087B00..0x00087BBA — ~10 calls per frame, then
//          inc [0x340B70] (frame counter) and jmp back forever.
//
// The native port runs the loop a configurable number of frames.

#include "larush_crt.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern ptrdiff_t g_xbox_mem_offset;
extern uint32_t  g_eax, g_ebx, g_edx, g_edi;
extern uint32_t  larush_kernel_heap_alloc(uint32_t size, uint32_t align);

#define MEM32(addr) (*(volatile uint32_t *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))
#define MEM16(addr) (*(volatile uint16_t *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))
#define MEM8(addr)  (*(volatile uint8_t  *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))

#define STACK_ARG(n) MEM32(g_esp + 4 + (uint32_t)((n) * 4))
extern uint32_t g_esp;

static uint32_t s_max_frames = 3;   /* 0 = run until stop requested */
static int s_stop_requested = 0;

void larush_game_main_set_frames(uint32_t n) { s_max_frames = n; }
void larush_game_main_request_stop(void) { s_stop_requested = 1; }

/* ── native CRT malloc (0x001F20AB) ────────────────────────── */

static void crt_malloc_native(void) {
    g_eax = larush_kernel_heap_alloc(STACK_ARG(0), 16);
}

/* ── main ──────────────────────────────────────────────────── */

static void game_frame(void) {
    larush_crt_call(0x00087BC0u, "frame: game state machine step");
    larush_crt_call(0x00087E60u, "frame: pre-sim update");
    larush_crt_call(0x0011ED20u, "frame: input poll");
    g_edi = 0x00326F30u;
    larush_crt_call(0x000F3F20u, "frame: world update (edi=0x326F30)");
    larush_crt_call(0x00115B80u, "frame: physics step");
    larush_crt_call(0x000DA2B0u, "frame: camera/view update");

    /* esi = 0x000E8840() — nonzero while a session/race is live. */
    g_eax = 0;
    int have = larush_crt_call(0x000E8840u, "frame: session state query");
    uint32_t esi_live = have ? g_eax : 0;

    g_ebx = 0x003DEB04u;
    larush_crt_call(0x001674D0u, "frame: audio update (ebx=0x3DEB04)");
    if (esi_live)
        larush_crt_call(0x000FE430u, "frame: live-session tick");
    MEM32(0x003512CCu) = 0;
    larush_crt_call(0x000EA4A0u, "frame: render scene");
    if (esi_live) {
        if (MEM32(0x0036F974u) == 1)
            larush_crt_call(0x00101530u, "frame: replay/attract update");
    } else {
        /* Idle path: reset the camera-blend floats. */
        MEM32(0x0032CEE0u) = MEM32(0x002D49FCu);
        MEM32(0x0032D3A4u) = 0;
        MEM32(0x0032CEF4u) = 0;
        MEM32(0x0032CEDCu) = 0;
        MEM32(0x0032CEF0u) = 0;   /* xorps: 0.0f */
        MEM32(0x0032CEECu) = 0;
    }
    larush_crt_call(0x000E8E10u, "frame: present/swap");
    larush_crt_call(0x000D5CC0u, "frame: end-of-frame housekeeping");
    MEM32(0x00340B70u)++;         /* frame counter */
}

static void game_main_native(void) {
    /* [0x3846A8] = 30 — target frame-rate / tick divisor. */
    MEM32(0x003846A8u) = 0x1Eu;

    /* 0x001F1880(0x0008031F, 0x0008031F) — install the same routine as
     * both members of a handler pair (new/termination handlers). */
    {
        uint32_t a[2] = { 0x0008031Fu, 0x0008031Fu };
        larush_crt_call_args(0x001F1880u, "install handler pair", a, 2);
    }

    /* stmxcsr/or 0x6000/ldmxcsr — flush-to-zero + denormals-are-zero.
     * Host float semantics stay IEEE; noted, not replicated. */

    larush_crt_call(0x000877E0u, "early platform init");

    {
        uint32_t a[2] = { 0x00407D48u, 0x00340B34u };
        larush_crt_call_args(0x0017C930u, "engine init (676 insns)", a, 2);
    }

    MEM32(0x0031E614u) = MEM32(0x002D53E0u);   /* float default copy */
    MEM32(0x003846A8u) = 0x1Eu;
    MEM32(0x00407F98u) = 0xFFFFFFFFu;          /* four handle slots */
    MEM32(0x00407FC8u) = 0xFFFFFFFFu;
    MEM32(0x00407FF8u) = 0xFFFFFFFFu;
    MEM32(0x00408028u) = 0xFFFFFFFFu;

    larush_crt_call(0x00166990u, "device/session manager init");

    /* Input-slot loop: 5 slots, descriptor stride 0x104. */
    for (uint32_t slot = 0; slot < 5; slot++) {
        uint32_t esi = slot * 0x104u;
        uint32_t a[4] = { 0x0040E1D0u, 0, esi + 0x002C7FE8u,
                          esi + 0x002C7AD0u };
        larush_crt_call_args(0x0018A4F0u, "input slot init", a, 4);
        /* Original stores *(result) into a stack local per slot;
         * pending until 0x0018A4F0 is ported. */
    }

    larush_crt_call(0x000D5BF0u, "post-input init");
    larush_crt_call(0x00155DA0u, "profile subsystem init");

    /* Save-device probe on [0x308C34] (-2 = not probed yet). */
    {
        uint32_t idx = MEM32(0x00308C34u);
        if (idx == 0xFFFFFFFEu) {
            uint32_t a[2] = { 0x003DEBF0u, 0x00264000u };
            if (larush_crt_call_args(0x001684A0u,
                    "save-device mount probe", a, 2)) {
                uint32_t r = (g_eax == 0xFFFFFFFFu) ? 0xFFFFFFFEu : g_eax;
                MEM32(0x00308C34u) = r;
            }
        } else if ((int32_t)idx < 10 &&
                   MEM32(idx * 4u + 0x003DEC68u) != 0) {
            uint32_t desc = idx * 12u + 0x003DEBF0u;
            uint32_t p = MEM32(desc);
            if (p) {
                uint32_t a1[1] = { p };
                larush_crt_call_args(0x001B26A4u, "save slot free", a1, 1);
                uint32_t a2[3] = { 0, MEM32(desc + 8u), 0 };
                if (larush_crt_call_args(0x001B2655u,
                        "save slot valloc", a2, 3)) {
                    MEM32(desc) = g_eax;
                    MEM32(desc + 4u) = MEM32(desc + 8u);
                }
            }
        }
    }

    larush_crt_call(0x00087E10u, "video mode init");
    larush_crt_call(0x0011E900u, "renderer init");
    larush_crt_call(0x0008EC50u, "frontend init");

    MEM32(0x00340B70u) = 0;                    /* frame counter reset */
    g_eax = 0x002CBB20u;
    {
        uint32_t a[1] = { 0x00407D48u };
        larush_crt_call_args(0x0017DB10u,
            "registry object init (eax=0x2CBB20)", a, 1);
    }
    larush_crt_call(0x000A0880u, "loading-screen init");

    /* Content-container create: 0x00168A40(eax=0x2E0118, edx=0x3DEFC8,
     * 0xD800, 0, handle, 0, 0) then link descriptor 0x3268A0. */
    {
        MEM32(0x003268ACu) = 0;   /* original derives this from the
                                     probe above; keep the slot zeroed
                                     until 0x168A40 is ported */
        g_eax = 0x002E0118u;
        g_edx = 0x003DEFC8u;
        uint32_t a[5] = { 0xD800u, 0, 0, 0, 0 };
        if (larush_crt_call_args(0x00168A40u,
                "content container create", a, 5)) {
            uint32_t r = g_eax;
            MEM32(0x003268A8u) = r;
            if (r != 0xFFFFFFFFu) {
                MEM32(0x003268A4u) = 1;
                MEM32(r * 0xC8u + 0x003DF08Cu) = 0x003268A0u;
            }
        }
    }

    larush_crt_call(0x000DA220u, "hud init");

    g_eax = 0x002CBB0Cu;
    {
        uint32_t a[2] = { 0x003FD048u, 1 };
        uint32_t r = 0xFFFFFFFEu;
        if (larush_crt_call_args(0x0016A0A0u,
                "session registry init (eax=0x2CBB0C)", a, 2))
            r = (g_eax == 0xFFFFFFFFu) ? 0xFFFFFFFEu : g_eax;
        MEM32(0x003663C0u) = r;
    }

    /* Network/message descriptor block at 0x3DEB6C — the two CRT
     * mallocs run natively, so this structure is real. */
    MEM32(0x003DEB70u) = 0;
    MEM32(0x003DEB6Cu) = 0;
    MEM16(0x003DEB74u) = 0;
    MEM16(0x003DEB78u) = 0;
    MEM16(0x003DEB7Au) = 0;
    {
        uint32_t a1[1] = { 0x14u };
        larush_crt_call_args(0x001F20ABu, "malloc(0x14)", a1, 1);
        MEM32(0x003DEB70u) = g_eax;
        uint32_t a2[1] = { 0x114u };
        larush_crt_call_args(0x001F20ABu, "malloc(0x114)", a2, 1);
        MEM32(0x003DEB6Cu) = g_eax;
    }
    MEM16(0x003DEB74u) = 3;
    MEM16(0x003DEB7Cu) = 3;
    MEM16(0x003DEB76u) = 0;
    MEM16(0x003DEB7Eu) = 4;
    g_eax = 0x003DEB6Cu;
    larush_crt_call(0x00039D10u, "message queue init (eax=0x3DEB6C)");

    if (MEM8(0x0036F46Cu) == 1)
        MEM8(0x0036F46Cu) = 0;
    MEM32(0x00384C44u) = 0;

    /* ── the game loop (0x00087B00) ── */
    if (s_max_frames)
        fprintf(stderr, "larush_main: entering game loop for %u frames\n",
                s_max_frames);
    else
        fprintf(stderr, "larush_main: entering game loop (until stop)\n");
    s_stop_requested = 0;
    for (uint32_t f = 0; (!s_max_frames || f < s_max_frames) &&
                         !s_stop_requested; f++)
        game_frame();
    fprintf(stderr, "larush_main: game loop exited "
            "(counter [0x340B70] = %u)\n", MEM32(0x00340B70u));
    g_eax = 0;
}

void larush_game_main_register(void) {
    larush_crt_register_native(LARUSH_MAIN_VA, game_main_native);
    larush_crt_register_native(0x001F20ABu, crt_malloc_native);
}
