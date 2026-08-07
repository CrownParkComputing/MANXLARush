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
// Usage: ./LARushBoot [game_data/L.A.Rush.USA.XBOX-ZTM]

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

typedef struct {
    uint32_t width, height, four_cc, pitch;
    const uint8_t *pixels;
    uint32_t pixel_bytes;
} dds_info;

/* Parse DDS header only — returns 0 on failure.  (XPR parsing joins
 * in Stage B once real packed resources are available.) */
static int dds_parse(const uint8_t *data, uint32_t len, dds_info *out) {
    if (len < 128 || memcmp(data, "DDS ", 4) != 0) return 0;
    uint32_t h = *(const uint32_t *)(data + 12);
    uint32_t w = *(const uint32_t *)(data + 16);
    uint32_t p  = *(const uint32_t *)(data + 20);
    uint32_t pf = *(const uint32_t *)(data + 80);
    uint32_t fc = *(const uint32_t *)(data + 84);
    if (!w || !h || w > 4096 || h > 4096) return 0;
    out->width  = w;
    out->height = h;
    out->four_cc = (pf & 0x4) ? fc : 0;
    out->pitch = p;
    out->pixels      = data + 128;
    out->pixel_bytes  = len > 128 ? (uint32_t)(len - 128) : 0;
    return 1;
}

/* Find the first .k9z file in the data dir, if any. */
static int find_first_k9z(const char *dir_path, char *out, size_t out_sz) {
    DIR *d = opendir(dir_path);
    if (!d) return 0;
    struct dirent *de;
    int found = 0;
    while (!found && (de = readdir(d)) != NULL) {
        size_t n = strlen(de->d_name);
        if (n > 4 && strcmp(de->d_name + n - 4, ".k9z") == 0) {
            snprintf(out, out_sz, "%s/%s", dir_path, de->d_name);
            found = 1;
        }
    }
    closedir(d);
    return found;
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

int main(int argc, char **argv) {
    const char *data_dir = argc > 1 ? argv[1]
        : "game_data/L.A.Rush.USA.XBOX-ZTM";

    dds_info dds = {0};
    const char *loaded = NULL;
    uint32_t *decoded_pixels = NULL;
    int tex_w = 640, tex_h = 480;
    larush_k9 *k9 = NULL;

    /* Try the real pipeline first: k9 archive → path lookup → DDS. */
    char k9_path[1024];
    struct stat st;
    if (stat(data_dir, &st) == 0 && S_ISDIR(st.st_mode) &&
        find_first_k9z(data_dir, k9_path, sizeof(k9_path))) {
        k9 = larush_k9_open(k9_path);
        if (k9) {
            larush_kernel_set_k9(k9);
            printf("k9: opened %s (%u indexed entries)\n",
                   k9_path, larush_k9_entry_count(k9));

            /* Candidate texture paths — refined in Stage B once the
             * real directory layout is known. */
            static const char *TEX_PATHS[] = {
                "frontend/loading.xpr",
                "textures/loading.dds",
                NULL
            };
            for (int i = 0; TEX_PATHS[i]; i++) {
                const uint8_t *data; uint32_t len;
                if (!larush_k9_find_by_path(k9, TEX_PATHS[i],
                                            &data, &len, NULL, NULL))
                    continue;
                if (!dds_parse(data, len, &dds)) continue;
                if (dds.four_cc) {
                    size_t dst_sz = dxt_decode_dst_size(dds.width, dds.height);
                    decoded_pixels = (uint32_t *)malloc(dst_sz);
                    if (decoded_pixels &&
                        dxt_decode_image(dds.pixels, dds.pixel_bytes,
                                         decoded_pixels, dds.width,
                                         dds.height, dds.four_cc)) {
                        loaded = TEX_PATHS[i];
                        tex_w = (int)dds.width; tex_h = (int)dds.height;
                    } else {
                        free(decoded_pixels); decoded_pixels = NULL;
                    }
                }
                if (loaded) break;
            }
        }
    }

    if (!loaded) {
        printf("No k9 texture available (entry table not yet decoded"
               "%s) — rendering placeholder.\n",
               k9 ? "" : ", no game data");
        decoded_pixels = make_placeholder(tex_w, tex_h);
        loaded = "procedural placeholder";
    } else {
        printf("Loaded via path: %s  %ux%u\n", loaded, dds.width, dds.height);
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
