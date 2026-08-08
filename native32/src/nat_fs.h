// nat_fs.h — %fs / Xbox KPCR setup for the current thread.
//
// Xbox game code reads thread state through fs:[...] (NT_TIB + KPCR).
// Linux i386 glibc uses %gs for its own TLS, leaving %fs free; we point
// %fs at a per-thread KPCR allocated inside the guest arena (so guest
// pointer arithmetic on it stays in guest RAM).
//
// The full KPCR/KPRCB layout and per-thread allocation land in C1.e;
// C1.a provides just enough to prove the mechanism in the smoke test.

#ifndef NAT_FS_H
#define NAT_FS_H

#include <stdint.h>

/* Point %fs at a KPCR located at guest VA `kpcr_va`, writing the
 * self-pointer fields so fs:[0x18]/fs:[0x1C] read back as kpcr_va.
 * Returns the GDT selector loaded into %fs, or 0 on failure. */
uint16_t nat_fs_install(uint32_t kpcr_va);

/* Read a dword through the current %fs (fs:[disp]). */
uint32_t nat_fs_read32(uint32_t disp);

#endif /* NAT_FS_H */
