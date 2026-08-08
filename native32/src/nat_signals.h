// nat_signals.h — guest fault handling.

#ifndef NAT_SIGNALS_H
#define NAT_SIGNALS_H

/* Install SIGSEGV/SIGILL/SIGBUS/SIGFPE handlers: skip privileged
 * instructions (cli/sti/wbinvd), classify + dump anything else. */
void nat_signals_install(void);

#endif /* NAT_SIGNALS_H */
