// nat_stubs.h — native kernel stubs + thunk-table patching.

#ifndef NAT_STUBS_H
#define NAT_STUBS_H

#include <stdint.h>
#include "nat_arena.h"

/* Point the k9 VFS used by the file stubs at an open archive/dir. */
struct larush_k9;
void nat_stubs_set_k9(struct larush_k9 *k9);

/* Patch the 144 kernel thunk slots at xbe->thunk_va: each FUNC ordinal
 * gets the host address of its stdcall stub (or a generic abort stub of
 * the right arity for unimplemented ones); each DATA ordinal gets a
 * KDATA cell VA.  Returns the number of slots patched. */
uint32_t nat_thunks_patch(const nat_xbe *xbe);

/* Guest-heap bump allocator (ExAllocatePool / Mm* backing). */
uint32_t nat_heap_alloc(uint32_t size, uint32_t align);

/* Authoritative export name for logging. */
const char *nat_ordinal_name(uint32_t ord);

/* Guest heap window (for verification/asserts). */
#define NAT_HEAP_BASE 0x01000000u
#define NAT_HEAP_END  0x02000000u
#define NAT_KDATA_BASE 0x00F10000u

#endif /* NAT_STUBS_H */
