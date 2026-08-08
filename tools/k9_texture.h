// k9_texture.h — pick decodable DXT surfaces out of k9 texture
// packages (shared by the boot visualizer and the game-loop bridge).
//
// Each .dir/.res entry is a texture package of three blocks:
//   block 0: info — 0x34-byte header, then {char name[16]; u32
//            desc_byte_off} records naming descriptors
//   block 1: Xbox D3DTexture descriptors, 20 bytes each:
//            {u32 Common, u32 Data, u32 Lock, u32 Format, u32 Size}
//            Common == 0x0004xxxx for textures; Data = payload offset;
//            Format: fmt byte = (>>8)&0xFF (0x0C=DXT1, 0x0E=DXT3,
//            0x0F=DXT5), USize = 1<<((>>20)&0xF), VSize = 1<<((>>24)&0xF)
//   block 2: raw texel payload (DXT blocks are linear, not swizzled)

#ifndef LARUSH_K9_TEXTURE_H
#define LARUSH_K9_TEXTURE_H

#include "larush_k9_vfs.h"
#include "dxt_decode.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

#endif /* LARUSH_K9_TEXTURE_H */
