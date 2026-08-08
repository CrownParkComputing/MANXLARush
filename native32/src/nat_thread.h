// nat_thread.h — guest threads (PsCreateSystemThreadEx → pthreads).

#ifndef NAT_THREAD_H
#define NAT_THREAD_H

#include <stdint.h>

/* Install %fs/KPCR for the boot thread, then call the guest entry point
 * (0x001B2594) as stdcall(void).  The entry spawns the main game thread
 * via PsCreateSystemThreadEx and returns; this blocks until the game
 * thread exits (or the watchdog/fault path terminates the process).
 * Returns the game thread's exit status. */
int nat_thread_boot(uint32_t entry_va);

/* The two Ps* stubs, registered into the thunk map by nat_stubs.c. */
uint32_t __attribute__((stdcall))
nk_PsCreateSystemThreadEx(uint32_t handle_va, uint32_t extra, uint32_t kstack,
                          uint32_t tlssize, uint32_t tid_va, uint32_t ctx1,
                          uint32_t ctx2, uint32_t suspended, uint32_t dbgstack,
                          uint32_t start_va);
uint32_t __attribute__((stdcall)) nk_PsTerminateSystemThread(uint32_t status);

/* Signal the boot waiter that the game reached a terminal point. */
void nat_thread_signal_exit(int status);

#endif /* NAT_THREAD_H */
