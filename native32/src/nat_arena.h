// nat_arena.h — guest RAM arena for the native-execution harness.
//
// The retail XBE runs at its true virtual addresses, so guest VA == host
// VA and there is no bias: the arena is identity-mapped at the XBE base.
// (Contrast the 64-bit recompile path, which mmaps at 0x200000000 and
// carries a g_xbox_mem_offset.)

#ifndef NAT_ARENA_H
#define NAT_ARENA_H

#include <stdint.h>
#include <stddef.h>

/* Guest physical layout (see the Stage C plan). */
#define NAT_ARENA_BASE   0x00010000u   /* XBE base = mmap_min_addr */
#define NAT_ARENA_END    0x04000000u   /* 64 MB unified RAM top    */
#define NAT_ARENA_SIZE   (NAT_ARENA_END - NAT_ARENA_BASE)

/* Reserve and identity-map [BASE, END) as RWX.  Page 0 is left unmapped
 * so a null deref traps.  Returns 0 on success, -1 on failure (arena
 * window already occupied — reserve it before anything else maps). */
int nat_arena_reserve(void);

/* Direct guest-memory accessors (offset is 0 — VA is the host address). */
#define GMEM8(va)  (*(volatile uint8_t  *)(uintptr_t)(va))
#define GMEM16(va) (*(volatile uint16_t *)(uintptr_t)(va))
#define GMEM32(va) (*(volatile uint32_t *)(uintptr_t)(va))

#endif /* NAT_ARENA_H */
