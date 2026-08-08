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

#include <math.h>
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
static int s_show_car = 0;       /* garage state active this frame */

/* ── car mesh (k9 cars archive) ────────────────────────────────
 *
 * Car entry descriptor blocks mix 20-byte D3DTexture records with
 * 12-byte {Common=0x00800001, DataOffset, Lock=0} vertex/index buffer
 * records.  Vertex buffers are stride-20: int16 x,y,z position in
 * ~millimetres at offset 0 (verified: coherent buffers reassemble a
 * ~2.9 m car silhouette), then packed normal/uv/colour dwords.
 * Buffers whose positions aren't coherent are index/aux data — the
 * 98 %-inlier filter drops them. */

typedef struct { float x, y, z; } car_vtx;

static car_vtx *s_car = NULL;
static uint32_t s_car_count = 0;
static char s_car_name[24] = "";
/* Strip boundaries: each buffer is one non-indexed triangle strip
 * (verified: median consecutive-vertex distance ~77 mm on a 2.9 m
 * model).  s_strip[i]..s_strip[i+1] indexes into s_car. */
static uint32_t s_strip[300];
static int s_strip_count = 0;

/* Exact topology (when the info-block chunk table decodes):
 * triangles as global-index triples into s_car. */
static uint32_t *s_tris = NULL;
static uint32_t s_tri_count = 0;

static void load_car(larush_k9 *k9, const char *want) {
    uint32_t idx = (uint32_t)-1;
    for (uint32_t i = 0; i < larush_k9_entry_count(k9); i++) {
        const char *nm = larush_k9_entry_name(k9, i);
        if (nm && strcmp(nm, want) == 0) { idx = i; break; }
    }
    if (idx == (uint32_t)-1) {
        fprintf(stderr, "car \"%s\" not in archive\n", want);
        return;
    }
    uint32_t off[3], size[3];
    const uint8_t *entry; uint32_t entry_len;
    if (!larush_k9_entry_blocks(k9, idx, off, size)) return;
    if (!larush_k9_find_by_index(k9, idx, &entry, &entry_len)) return;
    const uint8_t *desc    = entry + (off[1] - off[0]);
    const uint8_t *payload = entry + (off[2] - off[0]);

    const uint8_t *inf = entry;               /* block 0 */

    /* Collect buffer start offsets from the 12-byte records, keeping
     * DESCRIPTOR order — the info-block chunk table maps 1:1 onto it. */
    uint32_t dorder[256], sorted[257];
    int nvb = 0;
    uint32_t p = 0;
    while (p + 12 <= size[1] && nvb < 255) {
        uint32_t common, data, lock;
        memcpy(&common, desc + p + 0, 4);
        memcpy(&data,   desc + p + 4, 4);
        memcpy(&lock,   desc + p + 8, 4);
        if (common == 0x00800001u && lock == 0 && data < size[2]) {
            dorder[nvb++] = data;
            p += 12;
        } else if ((common & 0x00070000u) == 0x00040000u) {
            p += 20;
        } else {
            p += 4;
        }
    }
    memcpy(sorted, dorder, sizeof(uint32_t) * (size_t)nvb);
    for (int i = 1; i < nvb; i++)
        for (int j = i; j > 0 && sorted[j] < sorted[j-1]; j--) {
            uint32_t t = sorted[j]; sorted[j] = sorted[j-1];
            sorted[j-1] = t;
        }
    sorted[nvb] = size[2];

    car_vtx *vts = malloc(sizeof(car_vtx) * (size[2] / 20 + 1));
    uint32_t *vbase = malloc(sizeof(uint32_t) * (size_t)(nvb + 1));
    uint32_t *vcnt  = malloc(sizeof(uint32_t) * (size_t)(nvb + 1));
    if (!vts || !vbase || !vcnt) { free(vts); free(vbase); free(vcnt); return; }
    uint32_t n = 0;
    for (int b = 0; b < nvb; b++) {
        uint32_t bo = dorder[b], bs = 0;
        for (int k = 0; k < nvb; k++)
            if (sorted[k] == bo) { bs = sorted[k+1] - bo; break; }
        vbase[b] = n;
        vcnt[b] = (bs % 20) ? 0 : bs / 20;
        uint32_t bn = vcnt[b], inlier = 0;
        for (uint32_t i = 0; i < bn; i++) {
            int16_t v[3];
            memcpy(v, payload + bo + i * 20, 6);
            if (v[0] > -4000 && v[0] < 4000 && v[1] > -4000 &&
                v[1] < 4000 && v[2] > -4000 && v[2] < 4000)
                inlier++;
            vts[n].x = (float)v[0] / 1000.0f;
            vts[n].y = (float)v[1] / 1000.0f;
            vts[n].z = (float)v[2] / 1000.0f;
            n++;
        }
        /* Coherent buffers also feed the heuristic-strip fallback. */
        if (bn >= 3 && inlier * 100 >= bn * 98 && s_strip_count < 298) {
            s_strip[s_strip_count++] = vbase[b];
            s_strip[s_strip_count] = n;
        }
    }
    s_car = vts;
    s_car_count = n;

    /* ── Exact topology from the info-block chunk table ────────
     *
     * info: u32 header (total index count at +0x2C), then 48-byte
     * mesh records, then a table of 40-byte chunk descriptors
     *   { u32 cum_index_start; u32 f2; u16 nidx; u16 nvtx;
     *     f32 bbox[6]; u32 part_id; }
     * one per vertex buffer in descriptor order, immediately followed
     * by the u16 index pool.  Indices are a triangle STRIP with
     * degenerate stitching, local to the chunk's own buffer. */
    uint32_t total_idx = 0;
    if (size[0] >= 0x34) memcpy(&total_idx, inf + 0x2C, 4);
    uint32_t last = 0;
    for (uint32_t q = 0x34; total_idx && q + 40 <= size[0]; q += 4) {
        uint32_t cum, prt; uint16_t ni, nv;
        memcpy(&cum, inf + q, 4);
        memcpy(&ni,  inf + q + 8, 2);
        memcpy(&nv,  inf + q + 10, 2);
        memcpy(&prt, inf + q + 36, 4);
        if (ni && cum + ni == total_idx && cum < total_idx && prt < 200)
            last = q;                          /* keep the LAST match */
    }
    if (last) {
        /* Walk backward, validating the cumulative chain. */
        uint32_t first = last;
        while (first >= 0x34 + 40) {
            uint32_t cum_prev, cum_here; uint16_t ni_prev;
            memcpy(&cum_prev, inf + first - 40, 4);
            memcpy(&ni_prev,  inf + first - 40 + 8, 2);
            memcpy(&cum_here, inf + first, 4);
            if (cum_prev + ni_prev != cum_here) break;
            first -= 40;
        }
        int nchunk = (int)((last - first) / 40) + 1;
        uint32_t idx0 = last + 40;             /* index pool start */
        if (idx0 + 2u * total_idx <= size[0]) {
            s_tris = malloc(sizeof(uint32_t) * 3u * total_idx);
            uint32_t nt = 0;
            for (int c = 0; c < nchunk && c < nvb && s_tris; c++) {
                uint32_t cum; uint16_t ni, nv;
                memcpy(&cum, inf + first + (uint32_t)c * 40, 4);
                memcpy(&ni,  inf + first + (uint32_t)c * 40 + 8, 2);
                memcpy(&nv,  inf + first + (uint32_t)c * 40 + 10, 2);
                if (nv > vcnt[c]) continue;    /* mapping mismatch */
                const uint8_t *ix = inf + idx0 + 2u * cum;
                for (uint32_t i = 2; i < ni; i++) {
                    uint16_t a, b2, c2;
                    memcpy(&a,  ix + 2u * (i - 2), 2);
                    memcpy(&b2, ix + 2u * (i - 1), 2);
                    memcpy(&c2, ix + 2u * i, 2);
                    if (a == b2 || b2 == c2 || a == c2) continue;
                    if (a >= vcnt[c] || b2 >= vcnt[c] || c2 >= vcnt[c])
                        continue;
                    s_tris[nt * 3 + 0] = vbase[c] + a;
                    s_tris[nt * 3 + 1] = vbase[c] + b2;
                    s_tris[nt * 3 + 2] = vbase[c] + c2;
                    nt++;
                }
            }
            s_tri_count = nt;
        }
    }
    free(vbase); free(vcnt);
    snprintf(s_car_name, sizeof(s_car_name), "car %s", want);
    printf("garage: car \"%s\", %u vertices, %d buffers, "
           "%u exact triangles%s\n", want, n, nvb, s_tri_count,
           s_tri_count ? "" : " (heuristic-strip fallback)");
}

/* ── CPU compositor (shared by SDL upload and --dump) ──────── */

static uint32_t s_fb[FB_W * FB_H];

/* dxt_decode.h emits RGBA byte order — 0xAABBGGRR words on x86
 * (SDL_PIXELFORMAT_ABGR8888).  The whole framebuffer stays in that
 * convention. */
#define ABGR(r, g, b, a) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
     ((uint32_t)(g) <<  8) | (uint32_t)(r))

/* Rotating flat-shaded render of the car mesh: each buffer is one
 * non-indexed triangle strip; strip-restart artifacts show up as
 * triangles with an over-long model-space edge and are skipped. */

static float s_zbuf[FB_W * FB_H];
static float s_vx[16384], s_vy[16384], s_vz[16384];   /* view space */

static void fill_tri(int i0, int i1, int i2, uint32_t color) {
    float x0 = s_vx[i0], y0 = s_vy[i0], z0 = s_vz[i0];
    float x1 = s_vx[i1], y1 = s_vy[i1], z1 = s_vz[i1];
    float x2 = s_vx[i2], y2 = s_vy[i2], z2 = s_vz[i2];
    int minx = (int)fminf(fminf(x0, x1), x2), maxx = (int)fmaxf(fmaxf(x0, x1), x2);
    int miny = (int)fminf(fminf(y0, y1), y2), maxy = (int)fmaxf(fmaxf(y0, y1), y2);
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= FB_W) maxx = FB_W - 1;
    if (maxy >= FB_H) maxy = FB_H - 1;
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area == 0.0f) return;
    float inv = 1.0f / area;
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            float w0 = ((x1 - (float)x) * (y2 - (float)y) -
                        (x2 - (float)x) * (y1 - (float)y)) * inv;
            float w1 = ((x2 - (float)x) * (y0 - (float)y) -
                        (x0 - (float)x) * (y2 - (float)y)) * inv;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * z0 + w1 * z1 + w2 * z2;
            int o = y * FB_W + x;
            if (z < s_zbuf[o]) { s_zbuf[o] = z; s_fb[o] = color; }
        }
}

static void draw_car(uint32_t frame) {
    if (s_car_count > 16384) return;
    float ang = (float)frame * 0.025f;
    float ca = cosf(ang), sa = sinf(ang);
    const float CAMD = 4.2f;

    for (int i = 0; i < FB_W * FB_H; i++) s_zbuf[i] = 1e9f;

    for (uint32_t i = 0; i < s_car_count; i++) {
        float rx = s_car[i].x * ca - s_car[i].z * sa;
        float rz = s_car[i].x * sa + s_car[i].z * ca;
        float cz = rz + CAMD;
        if (cz < 0.5f) cz = 0.5f;
        s_vx[i] = (float)(FB_W / 2) + rx * 620.0f / cz;
        s_vy[i] = (float)(FB_H / 2 + 30) - (s_car[i].y + 0.10f) * 620.0f / cz;
        s_vz[i] = cz;
    }

    /* Ground plane wash first (under the car). */
    int gy = FB_H / 2 + 30 + (int)(0.32f * 620.0f / CAMD);
    for (int y = gy; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++)
            s_fb[y * FB_W + x] = ABGR(0x22, 0x14, 0x20, 0xFF);

    /* Two-sided flat shading, light from upper-front-left. */
    const float lx = -0.45f, ly = 0.78f, lz = -0.43f;

    if (s_tri_count) {
        /* Exact topology from the info-block chunk table. */
        for (uint32_t t = 0; t < s_tri_count; t++) {
            uint32_t i0 = s_tris[t * 3], i1 = s_tris[t * 3 + 1],
                     i2 = s_tris[t * 3 + 2];
            car_vtx a = s_car[i0], b = s_car[i1], c = s_car[i2];
            float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
            float e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
            float nx = e1y * e2z - e1z * e2y;
            float ny = e1z * e2x - e1x * e2z;
            float nz = e1x * e2y - e1y * e2x;
            float nl = sqrtf(nx * nx + ny * ny + nz * nz);
            if (nl < 1e-9f) continue;
            float rnx = nx * ca - nz * sa;
            float rnz = nx * sa + nz * ca;
            float lit = (rnx * lx + ny * ly + rnz * lz) / nl;
            if (lit < 0) lit = -lit;             /* two-sided */
            float d = 0.25f + 0.75f * lit;
            fill_tri((int)i0, (int)i1, (int)i2,
                     ABGR((uint8_t)(235 * d), (uint8_t)(90 * d),
                          (uint8_t)(45 * d), 0xFF));
        }
        return;
    }

    /* Fallback: heuristic strips over the raw vertex order. */
    for (int st = 0; st < s_strip_count; st++) {
        for (uint32_t i = s_strip[st]; i + 2 < s_strip[st + 1]; i++) {
            car_vtx a = s_car[i], b = s_car[i + 1], c = s_car[i + 2];
            float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
            float e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
            /* Skip strip-restart bridges (over-long edges). */
            const float MAXE = 0.04f;        /* (0.2 m)^2 */
            float e3x = c.x - b.x, e3y = c.y - b.y, e3z = c.z - b.z;
            if (e1x * e1x + e1y * e1y + e1z * e1z > MAXE) continue;
            if (e2x * e2x + e2y * e2y + e2z * e2z > MAXE) continue;
            if (e3x * e3x + e3y * e3y + e3z * e3z > MAXE) continue;
            float nx = e1y * e2z - e1z * e2y;
            float ny = e1z * e2x - e1x * e2z;
            float nz = e1x * e2y - e1y * e2x;
            float nl = sqrtf(nx * nx + ny * ny + nz * nz);
            if (nl < 1e-9f) continue;
            float rnx = nx * ca - nz * sa;
            float rnz = nx * sa + nz * ca;
            float lit = (rnx * lx + ny * ly + rnz * lz) / nl;
            if (lit < 0) lit = -lit;
            float d = 0.25f + 0.75f * lit;
            fill_tri((int)i, (int)i + 1, (int)i + 2,
                     ABGR((uint8_t)(235 * d), (uint8_t)(90 * d),
                          (uint8_t)(45 * d), 0xFF));
        }
    }
}

static void compose_frame(void) {
    uint32_t frame = MEM32(0x00340B70u);
    for (int i = 0; i < FB_W * FB_H; i++)
        s_fb[i] = ABGR(0x18, 0x0A, 0x1E, 0xFF);

    if (s_show_car && s_car_count) {
        draw_car(frame);
    } else if (s_art_count) {
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
    uint32_t frame = MEM32(0x00340B70u);
    /* Attract cycle: 180 frames in the garage (rotating car), then
     * 180 frames of frontend art, repeating. */
    uint32_t cycle = frame % 360u;
    s_show_car = s_car_count && cycle < 180u;
    if (s_art_count)
        s_current = (int)(frame / FRAMES_PER_ART) % s_art_count;
}

/* ── present native (0x000E8E10): two backends ─────────────── */

static const char *s_dump_prefix = NULL;

static void native_present_dump(void) {
    uint32_t frame = MEM32(0x00340B70u);
    static const uint32_t snaps[] = { 30, 80, 130, 200, 275, 365 };
    for (unsigned i = 0; i < sizeof(snaps) / sizeof(*snaps); i++) {
        if (frame == snaps[i]) {
            compose_frame();
            char path[1024];
            snprintf(path, sizeof(path), "%s_f%03u.ppm",
                     s_dump_prefix, frame);
            printf("frame %3u: %s -> %s\n", frame,
                   s_show_car ? s_car_name :
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
    const char *car_name = "01";
    uint32_t frames = 0;                    /* 0 = until window close */
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--frames") == 0 && a + 1 < argc)
            frames = (uint32_t)strtoul(argv[++a], NULL, 0);
        else if (strcmp(argv[a], "--dump") == 0 && a + 1 < argc)
            s_dump_prefix = argv[++a];
        else if (strcmp(argv[a], "--car") == 0 && a + 1 < argc)
            car_name = argv[++a];
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

    /* Car mesh for the garage state */
    larush_k9 *cars = NULL;
    snprintf(path, sizeof(path),
             "%s/COMPRESSED_Cars/cars.dir.k9z", data_dir);
    cars = larush_k9_open(path);
    if (cars)
        load_car(cars, car_name);

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
    free(s_car);
    if (cars) larush_k9_close(cars);
    if (k9) larush_k9_close(k9);
    larush_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe);
    return 0;
}
