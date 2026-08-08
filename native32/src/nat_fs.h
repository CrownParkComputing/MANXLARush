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

/* Xbox KPCR field offsets (NT_TIB + KPCR head + embedded KPRCB). */
#define KPCR_EXCEPTIONLIST 0x00
#define KPCR_STACKBASE     0x04
#define KPCR_STACKLIMIT    0x08
#define KPCR_SELF          0x18
#define KPCR_SELFPCR       0x1C
#define KPCR_PRCB          0x20
#define KPCR_IRQL          0x24
#define KPCR_PRCBDATA      0x28   /* KPRCB; +0x00 = CurrentThread */
#define KPCR_SIZE          0x300

/* Initialize a KPCR at `kpcr_va` (self-pointers, exception list, stack
 * bounds, current-thread).  Does NOT load %fs. */
void nat_fs_init_kpcr(uint32_t kpcr_va, uint32_t stack_base,
                      uint32_t stack_limit, uint32_t current_thread_va);

/* Point %fs at the KPCR at `kpcr_va` for the calling thread.  Returns
 * the GDT selector, or 0 on failure. */
uint16_t nat_fs_load(uint32_t kpcr_va);

/* Init a KPCR and load %fs in one step (self-pointers only).  Returns
 * the selector, or 0 on failure. */
uint16_t nat_fs_install(uint32_t kpcr_va);

/* Read a dword through the current %fs (fs:[disp]). */
uint32_t nat_fs_read32(uint32_t disp);

#endif /* NAT_FS_H */
