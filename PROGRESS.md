# L.A. Rush — Native recompilation progress

**Updated:** 8 August 2026
**Primary milestone:** render retail L.A. Rush art via the shared D3D8→Vulkan
backend, loading assets from the k9 archives.

## Current status

Stage A **and** the first Stage B pass are complete against the retail
`L.A.Rush.USA.XBOX-ZTM` image.  The XBE boots to 144/144 kernel thunks
remapped, the k9 `.dir/.res` archive format is decoded and reads real
files, and the boot visualizer decodes and displays an actual 1024×512
DXT1 texture (the "PIMPED OUT" title art) straight out of `frontend.res`.
The entry point is confirmed direct CRT code at `0x001B2594`.

### Retail-image corrections to the recovered analysis
The recovered analysis had three wrong numbers, now fixed from the real
XBE header fields:
- **Entry point is `0x001B2594`** (header field 0x128 ^ retail key), not
  `0x00010184` — that value was the certificate address at 0x118.
- **Kernel thunk table is 144 entries at `0x002A1620`** (field 0x158 ^
  retail key), not 256 at `0x002BFF48`.
- Section count 19 and the k9 engine ID were correct.

## Verified XBE analysis (retail)

- Engine: Midway **k9**
- Base `0x00010000`, entry `0x001B2594` (direct code in `.text`),
  **19 sections**, image ends at `0x00497760`
- Kernel thunk table: **144 entries at VA `0x002A1620`** (`.rdata`),
  all remapped; 127/137 ordinals covered by the Burnout 3 stub set,
  17 new (17, 37, 65, 81, 87, 95, 167, 172, 186, 199, 202, 233, 252,
  270, 277, 289, 337 — several are data exports or already stubbed)
- Subsystem sections: XONLINE, XMV, XNET, D3D, D3DX, XGRPH, DSOUND,
  XACTENG, WMADEC, XPP, DOLBY
- **k9 archive format decoded**: `.dir`/`.res` pairs (optionally
  `k9CP`-wrapped as `.dir.k9z`/`.res.k9z`).  `.dir` = u32
  `(0x80000000|count)` + 0x38-byte records `{char name[0x20]; u32
  off1,size1, off2,size2, off3,size3}`; the three blocks (info /
  descriptors / payload) are contiguous and the last record's end
  equals the `.res` size.  Invariant-checked across cars, pcars,
  frontend, EngineRes.
- **k9CP codec confirmed zlib**: `78 DA` deflate stream at offset 12,
  header `{"k9CP", u32 usize, u32 csize}`.  No custom LZ.
- Texture packages: each `.res` entry's descriptor block holds Xbox
  `D3DTexture` structs (Common `0x0004xxxx`, Format byte 0x0C/0x0E/0x0F
  = DXT1/3/5, U/V size in nibbles 20/24); payload is linear DXT blocks.

## Milestone ladder (FlatOut 1 pattern)

### Stage A — no retail data required
- [x] XBE section probe with synthetic self-test (`LARushProbe --self-test`)
- [x] CMake restructure: `larush_port` static lib + diagnostic executables
      + ctest (FlatOut 1 shape)
- [x] Kernel shim fork: thunk table `0x002BFF48`×256, synthetic dispatch VAs
      at `0x00F00000`, KDATA exports at `0x00F10000` (both provisional until
      the retail section map is read), portable Burnout 3 stub set,
      k9-VFS-backed NtOpenFile/NtReadFile/NtClose plumbing
- [x] First-boot harness with synthetic-XBE `--self-test` (fabricated
      thunk table → asserts remap counts, KDATA VAs, synthetic VAs)
- [x] k9CP codec: magic probe, tolerant header, zlib-stream scan (the
      heuristic that cracked BFS), 16 MB safety cap, clean
      `K9_ERR_UNKNOWN_CODEC` seam for a custom-LZ fallback
- [x] k9 unit tests: CRC32 KAT, deflate round-trip, lying-size-hint,
      negative/truncation cases, archive-layer honesty checks
- [x] Diagnostic tools: `LARushK9Test` (classify k9/XPR/XACT/XMV magics,
      live-unpack k9CP), `LARushBoot` (SDL3 visualizer, procedural
      placeholder until archives decode), `LARushD3DProbe --no-xbe`
      (Vulkan init → Clear → Present → readback verified, exact color match)
- [x] `tools/lar_disas.py` Capstone tracer: dynamic code regions from
      section flags, dual entry hypothesis (direct code vs FlatOut-style
      init table)

### Stage B — retail image (in progress)
- [x] `fgui.py` inventory + section map; synth/KDATA VAs (`0x00F00000` /
      `0x00F10000`) confirmed clear of the 19-section image (top `0x00497760`)
- [x] Real first boot: 19/19 sections, **144/144 thunks** remapped, full
      ordinal enumeration; 17 new ordinals noted in `kernel_arg_bytes`
- [x] k9 format pass: `.dir/.res` layout decoded, zlib on `k9CP`
      confirmed, archive layer reads real entries (`larush_k9_open` on a
      `.dir.k9z` opens the pair transparently)
- [x] Texture-package parsing → `dxt_decode.h` (XPR unswizzle via
      `MANXFramework::XboxTexture` still pending for non-DXT surfaces)
- [x] Boot visualizer with real art: decodes the 1024×512 DXT1 title
      texture from `frontend.res` and displays it
- [x] Entry-path analysis: `lar_disas.py` confirms direct CRT entry at
      `0x001B2594` (not an init table)
- [ ] Full `.res` texture-package descriptor doc + per-mip extraction
- [ ] XACT (`.xsb`/`.xwb`) audio + XMV/WMA FMV via `MANXFramework::FMV`
- [ ] Hand-recompile the CRT entry chain from `0x001B2594` through the
      `g_eax`/`STACK_ARG` model toward a running game loop
- [ ] Give the 17 new kernel ordinals real semantics from call-site disasm

## Key files

| File | Purpose |
|---|---|
| `src/larush_kernel_shim.c` | Xbox kernel layer (thunk table 0x002BFF48×256, stubs, dispatch) |
| `src/larush_first_boot.c` | XBE loader + kernel init + ordinal diagnostic + synthetic self-test |
| `src/larush_k9_vfs.c/.h` | k9 archive VFS: k9CP zlib codec + archive-layer seam |
| `src/larush_game_native.c` | D3D8/Vulkan probe + dynamic XBE subsystem-section probe |
| `src/larush_probe.cpp` | original XBE header/section dumper (kept) |
| `tools/larush_k9_test.c` | k9/XPR/XACT classifier + k9CP live unpack |
| `tools/larush_boot.c` | SDL3 boot visualizer (placeholder → real art in Stage B) |
| `tools/lar_disas.py` | Capstone call-graph tracer (needs `python-capstone`) |
| `tools/dxt_decode.h` | DXT1/3/5 software decoder (verbatim from MANXFlatOut1) |
| `tools/fgui.py` | shared XBE/asset inventory (canonical k9-aware copy) |
| `tests/larush_k9_unit_test.c` | synthetic k9CP/CRC32/archive tests |

## Build and verification

```bash
cmake -S . -B build && cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure   # 3 tests, no game data needed

./build/LARushD3DProbe --no-xbe   # Vulkan clear/readback smoke test
./build/LARushBoot                # SDL3 window, procedural placeholder
./build/LARushK9Test <file-or-dir>  # classify k9/XPR/XACT data

# Stage B (once game data is supplied):
./build/LARushFirstBoot game_data/L.A.Rush.USA.XBOX-ZTM
./build/LARushD3DProbe  game_data/L.A.Rush.USA.XBOX-ZTM
python3 tools/lar_disas.py game_data/L.A.Rush.USA.XBOX-ZTM/default.xbe
```

## Open questions

1. Entry `0x00010184` is ~0x184 past the image base — direct CRT code, or
   header-page thunk?  `lar_disas.py` probes both hypotheses (B7).
2. `KERNEL_SYNTH_BASE 0x00F00000` / `KDATA_BASE 0x00F10000` must be
   confirmed against the real section map (B1).
3. k9CP codec: zlib assumption is validated only against synthetic data;
   `LARushK9Test` reports the verdict the moment a retail `.k9z` is seen.
4. Do DSOUND/XACTENG/XONLINE imply early audio/net ordinal traffic during
   boot (more stubs needed than FlatOut's first boot required)?
