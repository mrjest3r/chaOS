#include "pipe.h"

typedef enum {
    P_FREE = 0,  /* slot unused                                   */
    P_OPEN,      /* readable and writable                         */
    P_CLOSED     /* writer hung up; readable until drained (EOF)  */
} pipe_state_t;

typedef struct {
    pipe_state_t state;
    uint8_t buf[PIPE_BUF];
    /* Free-running indexes; count = head - tail. Volatile because reader and
     * writer are different tasks interleaved by the preemptive scheduler. */
    volatile uint32_t head; /* next write position */
    volatile uint32_t tail; /* next read position  */
} pipe_t;

static pipe_t pipes[MAX_PIPES];

static uint32_t pipe_count(pipe_t *p) {
    return p->head - p->tail;
}

int pipe_create() {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (pipes[i].state == P_FREE) {
            pipes[i].head = 0;
            pipes[i].tail = 0;
            pipes[i].state = P_OPEN;
            return i;
        }
    }
    return -1;
}

int pipe_write(int id, const uint8_t *buf, int len) {
    if (id < 0 || id >= MAX_PIPES || len < 0) return -1;
    pipe_t *p = &pipes[id];
    if (p->state != P_OPEN) return -1;

    int written = 0;
    while (written < len) {
        /* Full: sleep until the reader catches up. Interrupts stay enabled so
         * the timer keeps scheduling the reading task. */
        while (pipe_count(p) == PIPE_BUF) {
            if (p->state != P_OPEN) return -1;
            asm volatile("sti; hlt");
        }
        p->buf[p->head % PIPE_BUF] = buf[written++];
        p->head++;
    }
    return written;
}

int pipe_read(int id, uint8_t *buf, int max) {
    if (id < 0 || id >= MAX_PIPES || max <= 0) return -1;
    pipe_t *p = &pipes[id];
    if (p->state == P_FREE) return -1;

    /* Empty: block until data arrives or the writer closes. */
    while (pipe_count(p) == 0) {
        if (p->state == P_CLOSED) {
            p->state = P_FREE; /* EOF observed; recycle the slot */
            return 0;
        }
        asm volatile("sti; hlt");
    }

    int n = 0;
    while (n < max && pipe_count(p) > 0) {
        buf[n++] = p->buf[p->tail % PIPE_BUF];
        p->tail++;
    }
    return n;
}

int pipe_avail(int id) {
    if (id < 0 || id >= MAX_PIPES) return -1;
    pipe_t *p = &pipes[id];
    if (p->state == P_FREE) return -1;
    uint32_t n = pipe_count(p);
    if (n > 0) return (int) n;
    return p->state == P_CLOSED ? -1 : 0;
}

int pipe_close(int id) {
    if (id < 0 || id >= MAX_PIPES || pipes[id].state != P_OPEN) return -1;
    /* Keep buffered data readable; the slot is freed when a reader hits EOF. */
    pipes[id].state = P_CLOSED;
    return 0;
}
