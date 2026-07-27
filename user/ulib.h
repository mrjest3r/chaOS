#ifndef ULIB_H
#define ULIB_H

#include <stdint.h>

/* User-space wrappers around the kernel's int 0x80 system calls. A user program
 * links against these instead of the kernel; it never touches hardware directly
 * (that would fault and get the task terminated). */

/* Mirrors the kernel's fs_fileinfo_t (kernel/fs.h); used by ulistfiles. */
typedef struct {
    char     name[24];
    uint32_t size;
} ufileinfo_t;

void     uprint(const char *s);
void     uprint_dec(uint32_t v);
char     ugetchar(void);                    /* blocks until a key is typed  */
int      ureadline(char *buf, int max);     /* echoing line read -> length  */
int      uspawn(const char *path);          /* run an ELF concurrently -> pid */
int      uwait(int pid);                    /* block until task 'pid' exits */
int      urun(const char *path);            /* spawn + wait (foreground)    */
int      ulistfiles(ufileinfo_t *out, int max); /* -> number of files       */
int      ugetpid(void);
void     usleep(int ticks);
int      uwritefile(const char *name, const void *buf, int len);
int      ureadfile(const char *name, void *buf, int max);
uint32_t uuptime(void);
int      ustrlen(const char *s);
void     uexit(void);

#endif
