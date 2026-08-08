// larush_game_loop.c — the recompiled game loop, on screen.
//
// Runs the full recompiled chain (CRT entry → mainCRTStartup → native
// main) and bridges the game loop's own render and present calls to
// SDL: natives registered at the real VAs
//
//   0x000EA4A0  "render scene"  → pick the frontend texture the
//                                 current attract state calls for
//   0x000E8E10  "present/swap"  → blit + overlay + SDL present +
//                                 event pump (close window to stop)
//
// so every frame on screen is driven by the recompiled main's loop at
// 0x00087B00 — the frame counter you see is MEM32(0x340B70), written
// by recompiled code into Xbox memory.  Art is real retail frontend
// DXT textures via the k9 VFS; the attract rotation stands in for the
// unported scene renderer.
//
// Usage: ./LARushGameLoop [game_data/...] [--frames N] [--dump prefix]
//        --dump renders headless (no SDL) and writes PPM snapshots at
//        a few fixed frames.

#include "larush_crt.h"
#include "larush_k9_vfs.h"
#include "k9_texture.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int       xbox_MemoryLayoutInit(const void *xbe, unsigned long sz);
extern void      xbox_MemoryLayoutShutdown(void);
extern void      larush_kernel_init(void);
extern void      larush_kernel_shutdown(void);
extern void      larush_kernel_set_k9(larush_k9 *k9);
extern ptrdiff_t g_xbox_mem_offset;

#define MEM32(addr) (*(volatile uint32_t *)((uintptr_t)(addr) + (uintptr_t)g_xbox_mem_offset))

#define FB_W 640
#define FB_H 480
#define MAX_ART 16
#define FRAMES_PER_ART 90        /* attract rotation: 1.5 s at 60 Hz */

typedef struct {
    uint32_t *rgba;              /* owned, decoded */
    int w, h;
    char name[20];
} art_slot;

static art_slot s_art[MAX_ART];
static int s_art_count = 0;
static int s_current = 0;

/* ── CPU compositor (shared by SDL upload and --dump) ──────── */

static uint32_t s_fb[FB_W * FB_H];

/* dxt_decode.h emits RGBA byte order — 0xAABBGGRR words on x86
 * (SDL_PIXELFORMAT_ABGR8888).  The whole framebuffer stays in that
 * convention. */
#define ABGR(r, g, b, a) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
     ((uint32_t)(g) <<  8) | (uint32_t)(r))

static void compose_frame(void) {
    uint32_t frame = MEM32(0x00340B70u);
    for (int i = 0; i < FB_W * FB_H; i++)
        s_fb[i] = ABGR(0x18, 0x0A, 0x1E, 0xFF);

    if (s_art_count) {
        const art_slot *a = &s_art[s_current];
        /* Letterbox-fit nearest-neighbour blit. */
        int dw = FB_W, dh = FB_W * a->h / a->w;
        if (dh > FB_H) { dh = FB_H; dw = FB_H * a->w / a->h; }
        int x0 = (FB_W - dw) / 2, y0 = (FB_H - dh) / 2;
        for (int y = 0; y < dh; y++) {
            const uint32_t *src = a->rgba + (size_t)(y * a->h / dh) * a->w;
            uint32_t *dst = s_fb + (size_t)(y0 + y) * FB_W + x0;
            for (int x = 0; x < dw; x++)
                dst[x] = src[x * a->w / dw];
        }
    }

    /* Overlay: dark bar with a frame-counter progress strip — the
     * counter is read back from Xbox memory, where the recompiled
     * loop increments it. */
    for (int y = 0; y < 28; y++)
        for (int x = 0; x < FB_W; x++)
            s_fb[y * FB_W + x] = ABGR(0, 0, 0, 0xB4);
    int px = (int)((frame % FRAMES_PER_ART) * FB_W / FRAMES_PER_ART);
    for (int y = 20; y < 26; y++)
        for (int x = 0; x < px; x++)
            s_fb[y * FB_W + x] = ABGR(0xFF, 0xB4, 0x28, 0xFF);
}

static int write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint8_t rgb[3] = { (uint8_t)(s_fb[i]),
                           (uint8_t)(s_fb[i] >> 8),
                           (uint8_t)(s_fb[i] >> 16) };
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0;
}

/* ── render native (0x000EA4A0) ────────────────────────────── */

static void native_render_scene(void) {
    if (s_art_count)
        s_current = (int)(MEM32(0x00340B70u) / FRAMES_PER_ART)
                    % s_art_count;
}

/* ── present native (0x000E8E10): two backends ─────────────── */

static const char *s_dump_prefix = NULL;

static void native_present_dump(void) {
    uint32_t frame = MEM32(0x00340B70u);
    static const uint32_t snaps[] = { 0, 95, 185, 275, 365 };
    for (unsigned i = 0; i < sizeof(snaps) / sizeof(*snaps); i++) {
        if (frame == snaps[i]) {
            compose_frame();
            char path[1024];
            snprintf(path, sizeof(path), "%s_f%03u.ppm",
                     s_dump_prefix, frame);
            printf("frame %3u: art \"%s\" -> %s\n", frame,
                   s_art_count ? s_art[s_current].name : "-", path);
            write_ppm(path);
        }
    }
}

#ifndef LARUSH_NO_SDL
#include <SDL3/SDL.h>

static SDL_Window   *s_win = NULL;
static SDL_Renderer *s_ren = NULL;
static SDL_Texture  *s_tex = NULL;

static void native_present_sdl(void) {
    compose_frame();
    SDL_UpdateTexture(s_tex, NULL, s_fb, FB_W * 4);
    SDL_RenderClear(s_ren);
    SDL_RenderTexture(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);

    SDL_Event ev;
    while (SDL_PollEvent(&ev))
        if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_KEY_DOWN)
            larush_game_main_request_stop();
    SDL_Delay(16);
}
#endif

/* ── art loading ───────────────────────────────────────────── */

static void load_art(larush_k9 *k9) {
    for (uint32_t i = 0; i < larush_k9_entry_count(k9) &&
                         s_art_count < MAX_ART; i++) {
        k9_texture t;
        if (!k9_pick_texture(k9, i, &t)) continue;
        if (t.width * t.height < 256 * 256) continue;   /* big art only */
        size_t sz = dxt_decode_dst_size(t.width, t.height);
        uint32_t *rgba = (uint32_t *)malloc(sz);
        if (!rgba) continue;
        if (!dxt_decode_image(t.pixels, t.pixel_bytes, rgba,
                              t.width, t.height, t.four_cc)) {
            free(rgba);
            continue;
        }
        art_slot *a = &s_art[s_art_count++];
        a->rgba = rgba;
        a->w = (int)t.width;
        a->h = (int)t.height;
        snprintf(a->name, sizeof(a->name), "%s", t.name);
    }
    /* Largest art first — the attract rotation opens on the title. */
    for (int i = 1; i < s_art_count; i++)
        for (int j = i; j > 0 &&
             s_art[j].w * s_art[j].h > s_art[j-1].w * s_art[j-1].h; j--) {
            art_slot tmp = s_art[j];
            s_art[j] = s_art[j-1];
            s_art[j-1] = tmp;
        }
    printf("attract art: %d textures loaded\n", s_art_count);
}

int main(int argc, char **argv) {
    const char *data_dir = "game_data/L.A.Rush.USA.XBOX-ZTM";
    uint32_t frames = 0;                    /* 0 = until window close */
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--frames") == 0 && a + 1 < argc)
            frames = (uint32_t)strtoul(argv[++a], NULL, 0);
        else if (strcmp(argv[a], "--dump") == 0 && a + 1 < argc)
            s_dump_prefix = argv[++a];
        else
            data_dir = argv[a];
    }
    if (s_dump_prefix && !frames) frames = 400;

    /* XBE */
    char path[1024];
    snprintf(path, sizeof(path), "%s/default.xbe", data_dir);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *xbe = malloc((size_t)sz);
    if (!xbe || fread(xbe, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "XBE read failed\n");
        free(xbe); fclose(f); return 1;
    }
    fclose(f);
    if (!xbox_MemoryLayoutInit(xbe, (unsigned long)sz)) {
        free(xbe); return 1;
    }
    larush_kernel_init();

    /* Frontend art via the k9 VFS */
    larush_k9 *k9 = NULL;
    snprintf(path, sizeof(path),
             "%s/COMPRESSED_Frontend/frontend.dir.k9z", data_dir);
    k9 = larush_k9_open(path);
    if (k9) {
        larush_kernel_set_k9(k9);
        load_art(k9);
    } else {
        fprintf(stderr, "warning: no frontend archive — blank frames\n");
    }

    /* Wire the natives and run the chain. */
    larush_game_main_register();
    larush_crt_register_native(0x000EA4A0u, native_render_scene);
    if (s_dump_prefix) {
        larush_crt_register_native(0x000E8E10u, native_present_dump);
    } else {
#ifndef LARUSH_NO_SDL
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        s_win = SDL_CreateWindow("L.A. Rush — recompiled game loop",
                                 FB_W, FB_H, SDL_WINDOW_RESIZABLE);
        s_ren = s_win ? SDL_CreateRenderer(s_win, NULL) : NULL;
        s_tex = s_ren ? SDL_CreateTexture(s_ren,
                    SDL_PIXELFORMAT_ABGR8888,
                    SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H) : NULL;
        if (!s_tex) {
            fprintf(stderr, "SDL setup failed: %s\n", SDL_GetError());
            return 1;
        }
        larush_crt_register_native(0x000E8E10u, native_present_sdl);
#else
        fprintf(stderr, "built without SDL — use --dump\n");
        return 1;
#endif
    }
    larush_game_main_set_frames(frames);

    larush_crt_run();

    printf("game loop done: %u frames on the Xbox-memory counter, "
           "%d pending targets remain\n",
           MEM32(0x00340B70u), larush_crt_pending_total());

#ifndef LARUSH_NO_SDL
    if (s_tex) SDL_DestroyTexture(s_tex);
    if (s_ren) SDL_DestroyRenderer(s_ren);
    if (s_win) SDL_DestroyWindow(s_win);
    if (!s_dump_prefix) SDL_Quit();
#endif
    for (int i = 0; i < s_art_count; i++) free(s_art[i].rgba);
    if (k9) larush_k9_close(k9);
    larush_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe);
    return 0;
}
