#ifndef SYSCALL_H
#define SYSCALL_H

/* System call numbers (passed in eax). Arguments go in ebx, ecx, edx and the
 * return value comes back in eax. */
#define SYS_PRINT     0  /* ebx = char*                                   */
#define SYS_EXIT      1  /* returns to kernel                             */
#define SYS_GETPID    2  /* -> eax = current task id                      */
#define SYS_WRITEFILE 3  /* ebx = name, ecx = buf, edx = len  -> 0 / -1   */
#define SYS_READFILE  4  /* ebx = name, ecx = buf, edx = max  -> n / -1   */
#define SYS_LISTFILES 5  /* ebx = fs_fileinfo_t*, ecx = max   -> count    */
#define SYS_UPTIME    6  /* -> eax = timer ticks since boot               */
#define SYS_YIELD     7  /* give up the CPU                               */
#define SYS_SLEEP     8  /* ebx = ticks; block until they elapse          */

/* Registers the int 0x80 handler. */
void syscall_init();

#endif
