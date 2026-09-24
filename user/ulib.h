#ifndef ULIB_H
#define ULIB_H

#include <stdint.h>
#include "../kernel/fb_user.h"
#include "../kernel/event_user.h"

/* User-space wrappers around the kernel's int 0x80 system calls. A user program
 * links against these instead of the kernel; it never touches hardware directly
 * (that would fault and get the task terminated). */

/* Mirrors the kernel's fs_fileinfo_t (kernel/fs.h); used by ulistfiles. */
typedef struct {
    char     name[24];
    uint32_t size;
} ufileinfo_t;

/* Mirrors drivers/mouse.h mouse_state_t for SYS_MOUSEGET. */
typedef struct {
    int32_t  x;
    int32_t  y;
    uint32_t buttons; /* bit0=left, bit1=right, bit2=middle */
} umouse_t;

void     uprint(const char *s);
void     uprint_dec(uint32_t v);
char     ugetchar(void);                    /* blocks until a key is typed  */
char     utrychar(void);                    /* non-blocking; 0 if none      */
int      ureadline(char *buf, int max);     /* echoing line read -> length  */
int      uspawn(const char *path);          /* run an ELF concurrently -> pid */
int      uwait(int pid);                    /* block until task 'pid' exits */
int      urun(const char *path);            /* spawn + wait (foreground)    */
int      ulistfiles(ufileinfo_t *out, int max); /* -> number of files       */
int      upipe(void);                       /* create a pipe -> id or -1    */
int      upiperead(int id, void *buf, int max);  /* -> n>0, 0=EOF, -1=err   */
int      upipewrite(int id, const void *buf, int len); /* -> len or -1      */
int      upipeclose(int id);                /* end-of-stream -> 0 or -1     */
int      upipepoll(int id);                 /* bytes readable / 0 / -1 EOF  */
int      uspawnio(const char *path, int stdin_pipe, int stdout_pipe);
                                            /* spawn with console on pipes
                                               (-1 = inherit) -> pid / <0  */
int      umouse(umouse_t *out);             /* current pointer -> 0 / -1    */
int      uevent_wait(uevent_t *ev, uint32_t timeout_ticks);
                                            /* 1 event / 0 timeout / -1; the
                                               caller becomes the input
                                               listener (keys bypass ugetchar) */
int      ufb(fb_req_t *req);                /* framebuffer draw/info/restore */
int      ufbblit(int x, int y, int w, int h,
                 const uint32_t *src, int src_pitch_px); /* FBOP_BLIT      */
uint32_t *usurface(int w, int h);           /* w*h 0x00RRGGBB buffer or 0   */
int      usurface_free(uint32_t *s);        /* 0 / -1                       */
int      uatoi(const char *s);              /* decimal string -> int        */
int      ugetpid(void);
void     usleep(int ticks);
int      uwritefile(const char *name, const void *buf, int len);
int      ureadfile(const char *name, void *buf, int max);
int      udelete(const char *name);          /* remove file -> 0 / -1         */
void     udoze(void);                        /* blank display until wake      */
uint32_t uuptime(void);
int      ustrlen(const char *s);
void     uexit(void);

#endif
