// nat_fs.c — %fs / KPCR installation via set_thread_area.

#include "nat_fs.h"
#include "nat_arena.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <asm/ldt.h>

void nat_fs_init_kpcr(uint32_t kpcr_va, uint32_t stack_base,
                      uint32_t stack_limit, uint32_t current_thread_va) {
    GMEM32(kpcr_va + KPCR_EXCEPTIONLIST) = 0xFFFFFFFFu;
    GMEM32(kpcr_va + KPCR_STACKBASE)  = stack_base;
    GMEM32(kpcr_va + KPCR_STACKLIMIT) = stack_limit;
    GMEM32(kpcr_va + KPCR_SELF)    = kpcr_va;
    GMEM32(kpcr_va + KPCR_SELFPCR) = kpcr_va;
    GMEM32(kpcr_va + KPCR_PRCB)    = kpcr_va + KPCR_PRCBDATA;
    GMEM32(kpcr_va + KPCR_IRQL)    = 0;
    /* KPRCB.CurrentThread (fs:[0x28]) */
    GMEM32(kpcr_va + KPCR_PRCBDATA + 0x00) = current_thread_va;
}

uint16_t nat_fs_load(uint32_t kpcr_va) {
    struct user_desc d;
    memset(&d, 0, sizeof d);
    d.entry_number    = -1;            /* kernel picks a free GDT slot */
    d.base_addr       = kpcr_va;
    d.limit           = 0x00000FFF;
    d.seg_32bit       = 1;
    d.contents        = 0;            /* data, expand-up */
    d.read_exec_only  = 0;
    d.limit_in_pages  = 0;
    d.seg_not_present = 0;
    d.useable         = 1;

    if (syscall(SYS_set_thread_area, &d) != 0) {
        perror("nat_fs: set_thread_area");
        return 0;
    }

    uint16_t sel = (uint16_t)(d.entry_number * 8 + 3);  /* RPL 3 */
    __asm__ volatile("movw %0, %%fs" :: "r"(sel));

    /* Verify the selector actually resolves to our KPCR. */
    if (nat_fs_read32(KPCR_SELF) != kpcr_va) {
        fprintf(stderr, "nat_fs: fs:[0x18] readback mismatch\n");
        return 0;
    }
    return sel;
}

uint16_t nat_fs_install(uint32_t kpcr_va) {
    nat_fs_init_kpcr(kpcr_va, 0, 0, 0);
    return nat_fs_load(kpcr_va);
}

uint32_t nat_fs_read32(uint32_t disp) {
    uint32_t v;
    __asm__ volatile("movl %%fs:(%1), %0" : "=r"(v) : "r"(disp));
    return v;
}
