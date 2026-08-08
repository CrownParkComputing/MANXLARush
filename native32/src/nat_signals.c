// nat_signals.c — fault handling for guest execution (minimal; the
// int3 probe infrastructure and watchdog are fleshed out in C1.f).
//
// Handles the privileged-instruction skips the guest CRT needs (cli/sti
// GPF in user mode) and turns any real fault into a classified register
// dump instead of a silent SIGSEGV.

#include "nat_signals.h"
#include "nat_arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

static const char *classify(uint32_t addr) {
    if (addr < NAT_ARENA_BASE) return "null/low page";
    if (addr >= NAT_ARENA_BASE && addr < NAT_ARENA_END) return "guest RAM";
    if (addr >= 0xFD000000u) return "GPU MMIO (expected C2 endpoint)";
    if (addr >= NAT_ARENA_END && addr < 0x60000000u) return "unmapped guest space";
    return "host space";
}

static void handler(int sig, siginfo_t *si, void *uctx) {
    ucontext_t *uc = (ucontext_t *)uctx;
    greg_t *r = uc->uc_mcontext.gregs;
    uint32_t eip = (uint32_t)r[REG_EIP];

    /* Skip privileged instructions that fault in user mode. */
    if (sig == SIGSEGV || sig == SIGILL) {
        const uint8_t *code = (const uint8_t *)(uintptr_t)eip;
        if (eip >= NAT_ARENA_BASE && eip < NAT_ARENA_END) {
            if (code[0] == 0xFA || code[0] == 0xFB) {   /* cli / sti */
                r[REG_EIP] = eip + 1; return;
            }
            if (code[0] == 0x0F && code[1] == 0x09) {   /* wbinvd */
                r[REG_EIP] = eip + 2; return;
            }
        }
    }

    uint32_t fault = (uint32_t)(uintptr_t)si->si_addr;
    int gpu = (fault >= 0xFD000000u);
    fprintf(stderr,
        "\n*** %s: %s at eip=0x%08X, addr=0x%08X (%s) ***\n",
        gpu ? "REACHED GPU (D3D8/dxvk boundary — Stage C3)" : "GUEST FAULT",
        strsignal(sig), eip, fault, classify(fault));
    fprintf(stderr,
        "    eax=%08X ebx=%08X ecx=%08X edx=%08X\n"
        "    esi=%08X edi=%08X ebp=%08X esp=%08X\n",
        (uint32_t)r[REG_EAX], (uint32_t)r[REG_EBX], (uint32_t)r[REG_ECX],
        (uint32_t)r[REG_EDX], (uint32_t)r[REG_ESI], (uint32_t)r[REG_EDI],
        (uint32_t)r[REG_EBP], (uint32_t)r[REG_ESP]);
    if (eip >= NAT_ARENA_BASE && eip < NAT_ARENA_END) {
        const uint8_t *c = (const uint8_t *)(uintptr_t)eip;
        fprintf(stderr, "    code:");
        for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", c[i]);
        fprintf(stderr, "\n");
    }
    fflush(stderr);
    _exit(gpu ? 0 : 1);   /* GPU MMIO = expected C1/C2 endpoint */
}

void nat_signals_install(void) {
    static char altstack[65536];   /* SIGSTKSZ is runtime in modern glibc */
    stack_t ss = { .ss_sp = altstack, .ss_size = sizeof altstack, .ss_flags = 0 };
    sigaltstack(&ss, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
}
