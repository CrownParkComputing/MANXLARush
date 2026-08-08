// nat_arena.c — guest RAM reservation.  The XBE loader (section copy,
// header page) is added in Stage C1.c; this unit currently provides just
// the identity-mapped reservation the smoke test asserts.

#include "nat_arena.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

int nat_arena_reserve(void) {
    /* MAP_FIXED_NOREPLACE fails (rather than clobbering) if anything
     * already occupies the window — the harness must own [BASE, END)
     * before any library maps into it. */
    void *want = (void *)(uintptr_t)NAT_ARENA_BASE;
    void *got = mmap(want, NAT_ARENA_SIZE,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                     -1, 0);
    if (got != want) {
        fprintf(stderr, "nat_arena: reserve at %p failed (got %p, errno=%d %s)\n",
                want, got, errno, strerror(errno));
        return -1;
    }
    return 0;
}
