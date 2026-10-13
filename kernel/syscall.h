#ifndef SYSCALL_H
#define SYSCALL_H

/* System call numbers (passed in eax). Arguments go in ebx, ecx, edx and the
 * return value comes back in eax.
 * ABI version 11 — keep in sync with docs/agent/ABI.md; bump when changing. */
#define SYS_PRINT     0  /* ebx = char*                                   */
#define SYS_EXIT      1  /* returns to kernel                             */
#define SYS_GETPID    2  /* -> eax = current task id                      */
#define SYS_WRITEFILE 3  /* ebx = name, ecx = buf, edx = len  -> 0 / -1   */
#define SYS_READFILE  4  /* ebx = name, ecx = buf, edx = max  -> n / -1   */
#define SYS_LISTFILES 5  /* ebx = fs_fileinfo_t*, ecx = max   -> count    */
#define SYS_UPTIME    6  /* -> eax = timer ticks since boot               */
#define SYS_YIELD     7  /* give up the CPU                               */
#define SYS_SLEEP     8  /* ebx = ticks; block until they elapse          */
#define SYS_GETCHAR   9  /* block until a key is typed -> eax = char      */
#define SYS_READLINE  10 /* ebx = buf, ecx = max; echoing line read -> len */
#define SYS_SPAWN     11 /* ebx = ELF filename -> eax = new pid or <0     */
#define SYS_WAIT      12 /* ebx = pid; block until that task exits        */
#define SYS_PIPE      13 /* create a pipe -> eax = pipe id or -1          */
#define SYS_PIPEREAD  14 /* ebx = id, ecx = buf, edx = max -> n, 0=EOF    */
#define SYS_PIPEWRITE 15 /* ebx = id, ecx = buf, edx = len -> len / -1    */
#define SYS_PIPECLOSE 16 /* ebx = id; end-of-stream for readers           */
#define SYS_MOUSEGET  17 /* ebx = mouse_state_t*; fill x/y/buttons        */
#define SYS_FB        18 /* ebx = fb_req_t* (draw/info/restore)           */
#define SYS_TRYCHAR   19 /* non-blocking key -> eax = char or 0           */
#define SYS_DELETEFILE 20 /* ebx = name -> 0 / -1                          */
#define SYS_DOZE      21 /* blank display until key/mouse wake            */
#define SYS_SURFACE   22 /* ebx = w, ecx = h -> eax = pixel buffer or 0    */
#define SYS_SURFFREE  23 /* ebx = buffer from SYS_SURFACE -> 0 / -1        */
#define SYS_EVENT_WAIT 24 /* ebx = uevent_t*, ecx = timeout ticks (0 poll,
                             UEV_FOREVER block) -> 1 event / 0 timeout / -1 */
#define SYS_SPAWNIO   25 /* ebx = cmdline, ecx = stdin pipe, edx = stdout pipe
                            (-1 = inherit) -> pid / <0                      */
#define SYS_PIPEPOLL  26 /* ebx = id -> bytes readable, 0 empty, -1 EOF/bad  */
#define SYS_PROCS     27 /* ebx = uproc_t*, ecx = max -> count / -1          */
#define SYS_PSCTL     28 /* ebx = pid, ecx = PSOP_* -> 0 / -1                 */
#define SYS_POWER     29 /* ebx = PWOP_*; does not return on success          */
#define SYS_CLOCK     30 /* ebx = uclock_t* -> 0 / -1                         */
#define SYS_BRK       31 /* ebx = new break, 0 = query -> break / -1           */
#define SYS_KEYEVT    32 /* -> (pressed<<8)|scancode, or -1 if none            */
#define SYS_AUDIO     33 /* ebx = int16 stereo, ecx = frames -> 0 / -1        */
#define SYS_FRAME     34 /* ebx = frame_req_t* -> 0 / -1                      */

/* Registers the int 0x80 handler. */
void syscall_init();
/* SYS_FB body (kernel/syscall_fb.c). */
void syscall_fb(void *regs);

#endif
