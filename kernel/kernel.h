#ifndef KERNEL_H
#define KERNEL_H

void worker_thread(void);

#ifdef AUTOTEST
int kernel_initrd_installed(void);
void run_early_selftests(void);
void run_autotests(void);
#endif

#endif
