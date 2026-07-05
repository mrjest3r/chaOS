#ifndef ULIB_H
#define ULIB_H

#include <stdint.h>

/* User-space wrappers around the kernel's int 0x80 system calls. A user program
 * links against these instead of the kernel; it never touches hardware directly
 * (that would fault and get the task terminated). */

void     uprint(const char *s);
void     uprint_dec(uint32_t v);
int      ugetpid(void);
void     usleep(int ticks);
int      uwritefile(const char *name, const void *buf, int len);
int      ureadfile(const char *name, void *buf, int max);
uint32_t uuptime(void);
int      ustrlen(const char *s);
void     uexit(void);

#endif
