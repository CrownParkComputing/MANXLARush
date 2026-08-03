# MANX L.A. Rush

Early static-recompilation analysis for the original Xbox release of
L.A. Rush. The recovered work identifies the Midway k9 engine, parses the XBE
layout and inventories the proprietary asset containers. This is not a
playable port yet.

The compiled probe depends on
[MANXFramework](https://github.com/RetroRecompilations/MANXFramework), including
its shared Xbox D3D8-to-Vulkan backend. It has no dependency on MANX Arcade or
another game repository.

No game files are included. Put a legally obtained extracted copy under
`game_data/L.A.Rush.USA.XBOX-ZTM/` or pass `default.xbe` explicitly.

## Build and test

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/LARushProbe game_data/L.A.Rush.USA.XBOX-ZTM/default.xbe
```

`tools/fgui.py` performs the deeper asset inventory and writes its report next
to the supplied game data, where Git ignores it.
