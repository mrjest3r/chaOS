#ifndef FRAME_USER_H
#define FRAME_USER_H

#include <stdint.h>

/* A graphical program publishes its pixels. The compositor places that
 * frame in a window and copies it into its own back buffer. */

#define FRAME_PUBLISH 0   /* ptr, w, h of the caller's buffer              */
#define FRAME_PLACE   1   /* pid, x, y where the frame should sit          */
#define FRAME_BLIT    2   /* pid's frame into ptr (pitch w, height h) at x,y */
#define FRAME_QUERY   3   /* caller's placement -> x, y, w, h              */
#define FRAME_KEYS    4   /* pid receives raw key events; -1 clears        */

typedef struct {
    uint32_t op;
    uint32_t ptr;
    int32_t  x, y, w, h;
    int32_t  pid;
    uint32_t flags;
} frame_req_t;

#endif
