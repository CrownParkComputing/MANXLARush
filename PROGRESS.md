# L.A. Rush — Native recompilation progress

**Updated:** 7 August 2026
**Primary milestone:** render retail L.A. Rush art via the shared D3D8→Vulkan
backend, loading assets from the k9 archives.

## Current status

Stage A bring-up (no retail data) is complete: the kernel shim is
parameterized for L.A. Rush's 256-entry thunk table, the first-boot path is
verified against a synthetic XBE, the k9CP codec round-trips under the
zlib-first assumption, and the GPU smoke test clears/reads back through
`MANXFramework::XboxD3D8`.  Everything that needs retail bytes (real thunk
enumeration, the k9 entry-table format, XPR textures) is queued behind
supplying `game_data/L.A.Rush.USA.XBOX-ZTM/`.

## Recovered XBE analysis

- Engine: Midway **k9** (detected via D3DX + XONLINE + no localized strings)
- Retail XBE: base `0x00010000`, entry `0x00010184`, **19 sections**
- Kernel thunk table: **256 entries at VA `0x002BFF48`**
- Subsystem sections: XONLINE, XMV, XNET, D3D, D3DX, XGRPH, DSOUND,
  XACTENG, WMADEC
- Assets: k9-compressed clumps (`.k9z`, `k9CP`/`k9SF`/`k9b` magics),
  Xbox Packed Resources (XPR0/1/2), shader packages (`.pak`),
  XACT banks (`.xsb`), nav/car data

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

### Stage B — requires game_data/L.A.Rush.USA.XBOX-ZTM/
- [ ] `fgui.py` inventory vs recovered analysis; fix `KERNEL_SYNTH_BASE` /
      `KDATA_BASE` placement from the real 19-section map
- [ ] Real first boot: 19/19 sections, 256/256 thunks, full ordinal
      enumeration; add missing ordinals to `kernel_arg_bytes` / stub table
      (expect a bigger surface than FlatOut's given DSOUND/XACTENG/XONLINE)
- [ ] k9 format pass: entry-table layout, offset base, hash scheme;
      confirm/refute zlib on retail `k9CP`; fill in the archive layer
      (peek cache + lazy path hashtable per the BFS design)
- [ ] XPR0/1/2 parsing → `MANXFramework::XboxTexture` unswizzle →
      `dxt_decode.h`
- [ ] Boot visualizer with real art (k9 → XPR → decode → SDL)
- [ ] `LARushD3DProbe` with XBE + section-table subsystem probes
- [ ] Entry-path analysis at `0x00010184` (`lar_disas.py`), first
      hand-recompiled init function through the `g_eax`/`STACK_ARG` model

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
