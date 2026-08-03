# L.A. Rush recompilation start

Recovered analysis confirms the original Xbox executable uses Midway's k9
engine. The retail XBE has a base address of `0x00010000`, an entry value of
`0x00010184`, 19 sections, and a 256-entry kernel thunk table at `0x002BFF48`.

Identified subsystems include XONLINE, XMV, XNET, D3D, D3DX, XGRPH, DSOUND,
XACTENG and WMADEC. Assets include k9-compressed clumps, Xbox packed resources,
shader packages, XACT banks and navigation/car data.

Current milestones:

- source-only XBE section probe with a synthetic-data unit test;
- reusable asset/XBE inventory tool;
- shared D3D8-to-Vulkan backend supplied by MANXFramework;
- strict exclusion of extracted retail data.

Next work is a k9 decompressor, kernel-thunk mapping, function discovery and
static translation of the first native entry path.
