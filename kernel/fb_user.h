#ifndef FB_USER_H
#define FB_USER_H

#include <stdint.h>

/* Shared kernel/user framebuffer request (SYS_FB). Roadmap P1.5. */

#define FBOP_INFO    0
#define FBOP_FILL    1
#define FBOP_TEXT    2
#define FBOP_CLEAR   3
#define FBOP_RESTORE 4
#define FBOP_BLIT    5  /* ABI v4: copy w×h 0x00RRGGBB pixels from src */

/* Largest blit accepted in one request (per side, pixels). */
#define FB_BLIT_MAX  4096

typedef struct {
    int32_t  op;
    int32_t  x, y, w, h;
    uint32_t color;   /* fill / clear bg / text fg */
    uint32_t color2;  /* text bg */
    char     text[48];
    /* INFO out: */
    uint32_t width, height, pitch;
    int32_t  ok;
    /* BLIT in (appended in v4; earlier fields keep their offsets): */
    const uint32_t *src;   /* user buffer, h rows of src_pitch pixels */
    int32_t  src_pitch;    /* row stride in pixels, >= w */
} fb_req_t;

#endif
