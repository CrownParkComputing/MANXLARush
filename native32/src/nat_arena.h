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

/* Loaded XBE description. */
typedef struct {
    uint32_t base;          /* image base (== NAT_ARENA_BASE)        */
    uint32_t entry;         /* decoded entry VA (0x001B2594 retail)  */
    uint32_t size_of_image; /* image span from base                  */
    uint32_t thunk_va;      /* kernel thunk table VA (0x002A1620)    */
    uint32_t xor_key;       /* the entry/thunk XOR key that resolved */
    uint32_t nsections;
} nat_xbe;

/* Load default.xbe from `data_dir` into the (already reserved) arena at
 * true VAs: copies the header page to `base`, then each section's raw
 * bytes to its VA, zero-filling vsize>rsize tails.  Decodes the entry
 * point and kernel-thunk VA from the header.  Returns 0 on success and
 * fills *out; -1 on any error. */
int nat_load_xbe(const char *data_dir, nat_xbe *out);

/* Direct guest-memory accessors (offset is 0 — VA is the host address). */
#define GMEM8(va)  (*(volatile uint8_t  *)(uintptr_t)(va))
#define GMEM16(va) (*(volatile uint16_t *)(uintptr_t)(va))
#define GMEM32(va) (*(volatile uint32_t *)(uintptr_t)(va))

#endif /* NAT_ARENA_H */
