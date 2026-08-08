# L.A. Rush — Native recompilation progress

**Updated:** 8 August 2026 (second pass)
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
- [x] Tracer fixed (Capstone operand-type constants were wrong — every
      call target was invisible; also now follows both arms of
      conditional branches and traces pushed code pointers as callback
      roots).  Full CRT chain recovered: entry `0x001B2594` →
      CreateThread `0x001B7557` (PsCreateSystemThreadEx, ordinal 255) →
      trampoline `0x001B74BF` (TLS attach, PsTerminateSystemThread 258)
      → mainCRTStartup `0x001B2520` → **main = `0x00087860`** →
      exit `0x001B6CE6` (launch-data page, HalReturnToFirmware 49)
- [x] Hand-recompiled CRT entry chain (`src/larush_crt.c`): certificate
      clamp, TLS slab sizing/attach (`[0x486834]`, index = size/−4),
      Xapi process init (PeHeapReserve/Commit, InitFlags), all three
      initializer-table walks, native-recompile registry, main dispatch.
      `LARushCRTTest` verifies every written value against an
      independent recomputation from the retail image; synthetic
      `--self-test` runs in ctest with no game data
- [x] Recompile `main` (`0x00087860`, 205 insns → `src/larush_game_main.c`):
      every global write faithful via MEM32/MEM16/MEM8, register-passed
      args via `g_eax/g_edx/g_ebx/g_edi`, CRT malloc (`0x001F20AB`)
      implemented natively.  **The game loop (`0x00087B00`) runs** — a
      configurable frame count with the frame counter live at
      `[0x340B70]`.  Unported callees are dispatched through
      `larush_crt_call`, which records the pending hit-list:
      **17 init functions + 11 per-frame functions** (printed by
      `LARushCRTTest` with call counts and roles)
- [x] **The recompiled game loop is on screen**: `LARushGameLoop`
      registers SDL-bridge natives at the loop's real render/present
      VAs (`0x000EA4A0`/`0x000E8E10`), so every displayed frame is
      driven by recompiled `main` — the on-screen counter is
      `MEM32(0x340B70)`.  Attract rotation cycles real frontend art
      (car-select atlas, L.A. street menu backgrounds) via the k9 VFS;
      `--dump` writes headless PPM snapshots
- [x] Colour fix: `dxt_decode.h` emits RGBA **byte order**
      (`0xAABBGGRR` words); the SDL/PPM consumers were treating words
      as `0xRRGGBBAA`, flooding red (alpha shown as red, green as
      blue…).  All consumers now use `SDL_PIXELFORMAT_ABGR8888` /
      byte-wise writes.  NOTE: `MANXFlatOut1/tools/flatout1_boot.c`
      uses `SDL_PIXELFORMAT_RGBA8888` with the same decoder — the same
      bug, worth fixing there
- [x] **k9 car mesh format cracked** (`COMPRESSED_Cars/cars.dir.k9z`,
      107 car entries): descriptor blocks mix 20-byte D3DTexture
      records with 12-byte `{0x00800001, offset, 0}` buffer records;
      vertex buffers are **stride 20, int16 x,y,z position (~mm) at
      offset 0** plus packed normal/uv/colour dwords.  The game-loop
      viewer's attract cycle alternates a garage state — the selected
      car (`--car NN`) rotating as a perspective point cloud — with
      frontend art.  Wheels sit at the model origin awaiting runtime
      placement matrices.  The garage renders **flat-shaded z-buffered
      triangles** from the strip-ordered vertices (over-long edges
      dropped as strip restarts) with vertex splats filling holes
- [ ] Exact car topology: mesh records in the info block are ~240 B
      (name, material, mesh id, index counts like 0xDDA); a u16 region
      at `info+0x30CC..0xC7F4` (~19K values, max = vtx_count−1) holds
      the index data, but neither plain strip nor plain list decode
      cleanly, and part of it is an ascending-first edge/adjacency
      table — needs a proper record-layout reverse
- [ ] Port main's callees off the hit-list — likely order: engine init
      `0x0017C930` (676 insns, gates most globals), then the per-frame
      eleven (state step `0x00087BC0`, world `0x000F3F20`, render
      `0x000EA4A0`, present `0x000E8E10`, …) toward real frames
      replacing the attract bridge
- [ ] Recompile the C++ static ctor table on demand (**2,228 live
      initializers** at `0x002D5DB0–0x002D8084`) as main's callees
      need them
- [ ] Full `.res` texture-package descriptor doc + per-mip extraction
- [ ] XACT (`.xsb`/`.xwb`) audio + XMV/WMA FMV via `MANXFramework::FMV`
- [ ] Give the 17 new kernel ordinals real semantics from call-site disasm

## Key files

| File | Purpose |
|---|---|
| `src/larush_crt.c/.h` | hand-recompiled CRT entry chain (entry → TLS → ctor walks → main dispatch) + native registry, arg-frame dispatch, pending hit-list |
| `src/larush_game_main.c` | hand-recompiled `main` 0x00087860: init sequence + the game loop |
| `tools/larush_game_loop.c` | the recompiled loop on screen: SDL bridge natives at render/present VAs |
| `tools/k9_texture.h` | shared k9 texture-package picker (boot + game loop) |
| `tools/larush_crt_test.c` | CRT chain diagnostic: retail cross-check + synthetic self-test |
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
ctest --test-dir build --output-on-failure   # 4 tests, no game data needed

./build/LARushD3DProbe --no-xbe   # Vulkan clear/readback smoke test
./build/LARushBoot                # SDL3 window, procedural placeholder
./build/LARushK9Test <file-or-dir>  # classify k9/XPR/XACT data

# Stage B (retail data):
./build/LARushFirstBoot game_data/L.A.Rush.USA.XBOX-ZTM
./build/LARushCRTTest   game_data/L.A.Rush.USA.XBOX-ZTM   # CRT chain cross-check
./build/LARushD3DProbe  game_data/L.A.Rush.USA.XBOX-ZTM
./build/LARushBoot --dump title.ppm    # headless real-art frame dump
python3 tools/lar_disas.py game_data/L.A.Rush.USA.XBOX-ZTM/default.xbe
```

## Open questions

1. ~~Entry direct code vs header-page thunk~~ — resolved: `0x001B2594`
   is direct CRT code; the full chain to `main = 0x00087860` is mapped.
2. ~~Synth/KDATA VA placement~~ — confirmed clear of the 19-section
   image (top `0x00497760`).
3. ~~k9CP codec~~ — confirmed plain zlib against retail `.k9z` archives.
4. Do DSOUND/XACTENG/XONLINE imply early audio/net ordinal traffic during
   boot (more stubs needed than FlatOut's first boot required)?
5. Which of the 2,228 static ctors gate `main`'s early calls?  Cluster
   them by section/VA range and recompile on demand as main's call
   graph is ported.
