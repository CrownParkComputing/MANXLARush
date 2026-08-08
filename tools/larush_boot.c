// larush_boot.c — L.A. Rush boot visualiser.
//
// Target pipeline (FlatOut milestone-5 equivalent): open a k9 archive,
// find a texture by path, unpack the k9CP payload, parse the XPR/DDS
// surface, DXT-decode, and render it in an SDL3 window.
//
// Until the k9 entry table is decoded there is nothing to load, so the
// visualiser renders a procedural L.A. Rush sunset-orange placeholder —
// which still proves the SDL3 window + texture upload path end to end.
//
// Usage: ./LARushBoot [game_data/L.A.Rush.USA.XBOX-ZTM] [--dump out.ppm]
//        --dump writes the decoded frame as binary PPM and exits without
//        opening a window (headless verification).

#include "larush_k9_vfs.h"

extern void larush_kernel_set_k9(larush_k9 *k9);

#include "dxt_decode.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

/* ── k9 texture package parsing ──────────────────────────────
 *
 * Each .dir/.res entry is a texture package of three blocks:
 *   block 0: info — 0x34-byte header, then {char name[16]; u32
 *            desc_byte_off} records naming descriptors
 *   block 1: Xbox D3DTexture descriptors, 20 bytes each:
 *            {u32 Common, u32 Data, u32 Lock, u32 Format, u32 Size}
 *            Common == 0x0004xxxx for textures; Data = payload offset;
 *            Format: fmt byte = (>>8)&0xFF (0x0C=DXT1, 0x0E=DXT3,
 *            0x0F=DXT5), USize = 1<<((>>20)&0xF), VSize = 1<<((>>24)&0xF)
 *   block 2: raw texel payload (DXT blocks are linear, not swizzled)
 */

#define XT_FMT_DXT1 0x0Cu
#define XT_FMT_DXT3 0x0Eu
#define XT_FMT_DXT5 0x0Fu

typedef struct {
    uint32_t width, height, four_cc;
    const uint8_t *pixels;
    uint32_t pixel_bytes;
    char name[20];
} k9_texture;

/* Pick the largest decodable DXT texture in entry `idx` of `k9`. */
static int k9_pick_texture(larush_k9 *k9, uint32_t idx, k9_texture *out) {
    uint32_t off[3], size[3];
    const uint8_t *entry; uint32_t entry_len;
    if (!larush_k9_entry_blocks(k9, idx, off, size)) return 0;
    if (!larush_k9_find_by_index(k9, idx, &entry, &entry_len)) return 0;

    const uint8_t *info    = entry + (off[0] - off[0]);
    const uint8_t *descs   = entry + (off[1] - off[0]);
    const uint8_t *payload = entry + (off[2] - off[0]);

    uint32_t desc_bytes = size[1];
    uint32_t best_area = 0;
    uint32_t best_off = 0;
    for (uint32_t d = 0; d + 20 <= desc_bytes; d += 20) {
        uint32_t common, data, format;
        memcpy(&common, descs + d + 0, 4);
        memcpy(&data,   descs + d + 4, 4);
        memcpy(&format, descs + d + 12, 4);
        if ((common & 0x00070000u) != 0x00040000u) continue;
        uint32_t fmt = (format >> 8) & 0xFFu;
        uint32_t w = 1u << ((format >> 20) & 0xFu);
        uint32_t h = 1u << ((format >> 24) & 0xFu);
        if (fmt != XT_FMT_DXT1 && fmt != XT_FMT_DXT3 && fmt != XT_FMT_DXT5)
            continue;
        if (w < 8 || h < 8 || w > 2048 || h > 2048) continue;
        size_t need = ((w + 3) / 4) * ((h + 3) / 4) *
                      (fmt == XT_FMT_DXT1 ? 8 : 16);
        if (data + need > size[2]) continue;
        if (w * h > best_area) {
            best_area = w * h;
            best_off = d;
            out->width = w;
            out->height = h;
            out->four_cc = fmt == XT_FMT_DXT1 ? FOURCC_DXT1 :
                           fmt == XT_FMT_DXT3 ? FOURCC_DXT3 : FOURCC_DXT5;
            out->pixels = payload + data;
            out->pixel_bytes = (uint32_t)need;
        }
    }
    if (!best_area) return 0;

    /* Resolve the winner's name from the info block. */
    snprintf(out->name, sizeof(out->name), "tex@+0x%X", best_off);
    for (uint32_t n = 0x34; n + 20 <= size[0]; n += 20) {
        uint32_t desc_off;
        memcpy(&desc_off, info + n + 16, 4);
        if (desc_off == best_off && info[n] >= 32 && info[n] < 127) {
            snprintf(out->name, sizeof(out->name), "%.16s",
                     (const char *)(info + n));
            break;
        }
    }
    return 1;
}

/* Procedural placeholder: LA sunset gradient with a road vanishing
 * toward the horizon — rendered so the SDL path is testable with no
 * game data. */
static uint32_t *make_placeholder(int w, int h) {
    uint32_t *px = malloc((size_t)w * h * 4);
    if (!px) return NULL;
    int horizon = h * 5 / 12;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            if (y < horizon) {
                /* Sky: deep orange fading to violet. */
                float t = (float)y / (float)horizon;
                r = (uint8_t)(70 + 185 * t);
                g = (uint8_t)(30 + 70 * t);
                b = (uint8_t)(90 - 40 * t);
            } else {
                /* Ground: dark asphalt with a lighter wedge road. */
                float depth = (float)(y - horizon) / (float)(h - horizon);
                int center = w / 2;
                int half = (int)(4 + depth * (float)w * 0.45f);
                if (x > center - half && x < center + half) {
                    uint8_t v = (uint8_t)(50 + 25 * depth);
                    r = g = b = v;
                    /* Dashed centre line. */
                    if (x > center - 2 && x < center + 2 && ((y / 8) % 2))
                        { r = 220; g = 170; b = 40; }
                } else {
                    r = (uint8_t)(28 + 10 * depth);
                    g = (uint8_t)(20 + 8 * depth);
                    b = (uint8_t)(24 + 6 * depth);
                }
            }
            px[y * w + x] = ((uint32_t)r << 24) | ((uint32_t)g << 16) |
                            ((uint32_t)b << 8) | 0xFFu;
        }
    }
    return px;
}

static int dump_ppm(const char *path, const uint32_t *px, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint8_t rgb[3] = { (uint8_t)(px[i] >> 24), (uint8_t)(px[i] >> 16),
                           (uint8_t)(px[i] >> 8) };
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0;
}

int main(int argc, char **argv) {
    const char *data_dir = "game_data/L.A.Rush.USA.XBOX-ZTM";
    const char *dump_path = NULL;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--dump") == 0 && a + 1 < argc)
            dump_path = argv[++a];
        else
            data_dir = argv[a];
    }

    k9_texture tex_info = {0};
    const char *loaded = NULL;
    uint32_t *decoded_pixels = NULL;
    int tex_w = 640, tex_h = 480;
    larush_k9 *k9 = NULL;

    /* Real pipeline: open a texture package pair and decode the
     * largest DXT surface.  EngineRes (root, uncompressed) is the
     * guaranteed-present fallback; frontend has the big UI art. */
    const char *PACKS[] = {
        "COMPRESSED_Frontend/frontend.dir.k9z",
        "EngineRes.dir",
        NULL
    };
    for (int p = 0; PACKS[p] && !loaded; p++) {
        char pack_path[1024];
        snprintf(pack_path, sizeof(pack_path), "%s/%s", data_dir, PACKS[p]);
        struct stat st;
        if (stat(pack_path, &st) != 0) continue;
        k9 = larush_k9_open(pack_path);
        if (!k9) continue;
        larush_kernel_set_k9(k9);

        /* Scan every entry, keep the largest decodable texture. */
        k9_texture best = {0};
        for (uint32_t i = 0; i < larush_k9_entry_count(k9); i++) {
            k9_texture t;
            if (k9_pick_texture(k9, i, &t) &&
                t.width * t.height > best.width * best.height)
                best = t;
        }
        if (best.width) {
            size_t dst_sz = dxt_decode_dst_size(best.width, best.height);
            decoded_pixels = (uint32_t *)malloc(dst_sz);
            if (decoded_pixels &&
                dxt_decode_image(best.pixels, best.pixel_bytes,
                                 decoded_pixels, best.width, best.height,
                                 best.four_cc)) {
                tex_info = best;
                tex_w = (int)best.width;
                tex_h = (int)best.height;
                loaded = PACKS[p];
                printf("Loaded texture \"%s\" %ux%u %s from %s\n",
                       best.name, best.width, best.height,
                       best.four_cc == FOURCC_DXT1 ? "DXT1" :
                       best.four_cc == FOURCC_DXT3 ? "DXT3" : "DXT5",
                       PACKS[p]);
            } else {
                free(decoded_pixels);
                decoded_pixels = NULL;
            }
        }
        if (!loaded) { larush_k9_close(k9); k9 = NULL; }
    }
    (void)tex_info;

    if (dump_path) {
        int ok = 0;
        if (decoded_pixels)
            ok = dump_ppm(dump_path, decoded_pixels, tex_w, tex_h);
        else {
            uint32_t *ph = make_placeholder(tex_w, tex_h);
            if (ph) { ok = dump_ppm(dump_path, ph, tex_w, tex_h); free(ph); }
        }
        printf("%s frame -> %s (%dx%d, %s)\n", ok ? "Dumped" : "FAILED to dump",
               dump_path, tex_w, tex_h, loaded ? loaded : "placeholder");
        free(decoded_pixels);
        if (k9) larush_k9_close(k9);
        return ok ? 0 : 1;
    }

    /* Init SDL3 */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        free(decoded_pixels);
        if (k9) larush_k9_close(k9);
        return 1;
    }

    int win_w = tex_w, win_h = tex_h;
    if (win_w > 1280) { win_h = win_h * 1280 / win_w; win_w = 1280; }
    if (win_h > 960)  { win_w = win_w * 960  / win_h; win_h = 960;  }

    SDL_Window *window = SDL_CreateWindow(
        "L.A. Rush — BOOTING", win_w, win_h, SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        SDL_Quit(); free(decoded_pixels);
        if (k9) larush_k9_close(k9);
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) renderer = SDL_CreateRenderer(window, "software");
    if (!renderer) {
        fprintf(stderr, "SDL renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window); SDL_Quit(); free(decoded_pixels);
        if (k9) larush_k9_close(k9);
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    SDL_Texture *tex = NULL;
    if (decoded_pixels && tex_w > 0 && tex_h > 0) {
        SDL_Surface *s = SDL_CreateSurfaceFrom(
            tex_w, tex_h, SDL_PIXELFORMAT_RGBA8888,
            decoded_pixels, tex_w * 4);
        if (s) {
            tex = SDL_CreateTextureFromSurface(renderer, s);
            SDL_DestroySurface(s);
        }
        free(decoded_pixels); decoded_pixels = NULL;
    }

    printf("\nL.A. Rush booting — %s.\n", loaded);
    printf("Close window or press key to exit.\n");
    int running = 1;
    Uint64 start = SDL_GetTicks();
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_KEY_DOWN)
                running = 0;

        SDL_SetRenderDrawColor(renderer, 24, 10, 30, 255);
        SDL_RenderClear(renderer);

        if (tex) {
            int rw, rh;
            SDL_GetCurrentRenderOutputSize(renderer, &rw, &rh);
            SDL_FRect d = {0, 0, (float)rw, (float)rh};
            float a = (float)tex_w / (float)tex_h;
            float wa = (float)rw / (float)rh;
            if (a > wa) { float hh = (float)rw / a; d.y = ((float)rh - hh)*0.5f; d.h = hh; }
            else        { float ww = (float)rh * a; d.x = ((float)rw - ww)*0.5f; d.w = ww; }
            SDL_RenderTexture(renderer, tex, NULL, &d);
        }

        /* Overlay bar */
        {
            int rw, rh;
            SDL_GetCurrentRenderOutputSize(renderer, &rw, &rh);
            (void)rh;
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
            SDL_FRect bar = {0, 0, (float)rw, 50};
            SDL_RenderFillRect(renderer, &bar);
        }

        SDL_RenderPresent(renderer);
        if (SDL_GetTicks() - start > 8000) running = 0;
        SDL_Delay(16);
    }

    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (k9) larush_k9_close(k9);
    printf("Done.\n");
    return 0;
}
