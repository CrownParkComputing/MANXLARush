// larush_k9_test.c — k9 archive diagnostic tool.
//
// Classifies L.A. Rush data files by magic (k9CP/k9SF/k9b, XPR packed
// resources, XACT banks, XMV video, shader packages), test-unpacks
// k9CP payloads, and — once the k9 entry-table format is decoded —
// will list and extract archive contents like FlatOut's BFS tool.
//
// Usage:
//   ./LARushK9Test <file.k9z>            single-file report
//   ./LARushK9Test <game_data_dir>       classify every file in the dir
//   ./LARushK9Test <archive> <out_dir> <path-in-archive>   (Stage B)

#include "larush_k9_vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* ── Magic classification for Midway k9 / Xbox data ─────────── */

typedef struct {
    const char *name;
    uint8_t     magic[4];
    uint8_t     magic_len;
} magic_entry;

static const magic_entry MAGICS[] = {
    {"k9 compressed payload (k9CP)", {'k','9','C','P'}, 4},
    {"k9 signature/filelist (k9SF)", {'k','9','S','F'}, 4},
    {"k9 binary blob (k9b)",         {'k','9','b',0},   4},
    {"Xbox Packed Resource XPR0",    {'X','P','R','0'}, 4},
    {"Xbox Packed Resource XPR1",    {'X','P','R','1'}, 4},
    {"Xbox Packed Resource XPR2",    {'X','P','R','2'}, 4},
    {"XACT wave bank (WBND)",        {'W','B','N','D'}, 4},
    {"XACT sound bank (SDBK)",       {'S','D','B','K'}, 4},
    {"XMV video",                    {'X','b','o','x'}, 4},
    {"RIFF (WAV/XWMA)",              {'R','I','F','F'}, 4},
    {"DDS texture",                  {'D','D','S',' '}, 4},
    {"XBE executable",               {'X','B','E','H'}, 4},
};

static const char *classify(const uint8_t *buf, size_t len) {
    if (len < 4) return "short file";
    for (size_t m = 0; m < sizeof(MAGICS)/sizeof(MAGICS[0]); m++) {
        if (memcmp(buf, MAGICS[m].magic, MAGICS[m].magic_len) == 0)
            return MAGICS[m].name;
    }
    if (buf[0] == 0x78 && (((uint32_t)buf[0] << 8) | buf[1]) % 31u == 0)
        return "bare zlib stream";
    return "unknown";
}

/* ── Single-file report ─────────────────────────────────────── */

static int report_file(const char *path, int verbose) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s\n", path); return 1; }
    fseeko(f, 0, SEEK_END);
    uint64_t fsize = (uint64_t)ftello(f);
    fseeko(f, 0, SEEK_SET);

    uint8_t head[64] = {0};
    size_t head_len = fread(head, 1, sizeof(head), f);

    const char *kind = classify(head, head_len);
    printf("  %-52s %10llu B  %s\n", path,
           (unsigned long long)fsize, kind);

    if (verbose) {
        printf("    First bytes:");
        for (size_t i = 0; i < head_len && i < 16; i++)
            printf(" %02X", head[i]);
        printf("\n");
    }

    /* k9CP: attempt a real unpack so the codec assumption gets tested
     * against retail bytes the moment they exist. */
    if (memcmp(head, "k9CP", 4) == 0) {
        uint8_t *blob = malloc((size_t)fsize);
        if (blob) {
            fseeko(f, 0, SEEK_SET);
            if (fread(blob, 1, (size_t)fsize, f) == fsize) {
                uint8_t *out = NULL; size_t out_len = 0;
                int rc = larush_k9cp_unpack(blob, (size_t)fsize,
                                            &out, &out_len);
                if (rc == K9_OK) {
                    printf("    k9CP unpack OK: %zu bytes (zlib confirmed), "
                           "payload magic %02X %02X %02X %02X\n",
                           out_len, out[0], out[1], out[2], out[3]);
                    free(out);
                } else if (rc == K9_ERR_UNKNOWN_CODEC) {
                    printf("    k9CP unpack: NO deflate stream found — "
                           "custom Midway LZ, needs the Stage B codec pass\n");
                } else {
                    printf("    k9CP unpack failed: error %d\n", rc);
                }
            }
            free(blob);
        }
    }

    fclose(f);
    return 0;
}

/* ── Directory scan ─────────────────────────────────────────── */

static int scan_dir(const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) { fprintf(stderr, "ERROR: cannot open dir %s\n", dir_path); return 1; }

    printf("── Classifying %s ──\n", dir_path);
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir_path, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_dir(path);       /* recurse into track/car subdirs */
            continue;
        }
        report_file(path, 0);
        n++;
    }
    closedir(d);
    if (n == 0)
        printf("  (no files)\n");
    return 0;
}

/* ── Main ───────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    const char *target = argc > 1 ? argv[1]
        : "game_data/L.A.Rush.USA.XBOX-ZTM";
    const char *extract_dir = argc > 2 ? argv[2] : NULL;
    const char *extract_one = argc > 3 ? argv[3] : NULL;

    printf("═══════════════════════════════════════════════════════\n");
    printf("  L.A. Rush — k9 Archive Diagnostic\n");
    printf("═══════════════════════════════════════════════════════\n");

    struct stat st;
    if (stat(target, &st) != 0) {
        fprintf(stderr,
                "No game data at %s.\n"
                "Supply the retail files under game_data/ to classify "
                "and unpack them.\n", target);
        return 1;
    }

    if (S_ISDIR(st.st_mode))
        return scan_dir(target);

    /* Single file: full report, then archive-layer probe. */
    report_file(target, 1);

    larush_k9 *k9 = larush_k9_open(target);
    if (k9) {
        uint32_t entries = larush_k9_entry_count(k9);
        printf("\nArchive layer: magic \"%s\", %u indexed entries\n",
               larush_k9_magic(k9), entries);
        for (uint32_t i = 0; i < entries; i++) {
            uint32_t off[3], size[3];
            larush_k9_entry_blocks(k9, i, off, size);
            printf("  [%3u] %-32s info %6u B  desc %6u B  payload %10u B"
                   "  @0x%08X\n",
                   i, larush_k9_entry_name(k9, i),
                   size[0], size[1], size[2], off[0]);
        }
        if (entries == 0)
            printf("(single file — no .dir/.res entry table)\n");

        if (extract_one) {
            const uint8_t *data; uint32_t len;
            if (larush_k9_find_by_path(k9, extract_one, &data, &len,
                                       NULL, NULL)) {
                char outpath[1024];
                const char *base = strrchr(extract_one, '/');
                base = base ? base + 1 : extract_one;
                snprintf(outpath, sizeof(outpath), "%s/%s",
                         extract_dir ? extract_dir : ".", base);
                FILE *out = fopen(outpath, "wb");
                if (out) {
                    fwrite(data, 1, len, out);
                    fclose(out);
                    printf("Wrote %u bytes to %s\n", len, outpath);
                }
            } else {
                printf("NOT FOUND: %s\n", extract_one);
            }
        }
        larush_k9_close(k9);
    }

    return 0;
}
