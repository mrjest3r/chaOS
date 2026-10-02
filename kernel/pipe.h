#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

/* Byte-stream pipes for inter-process communication (roadmap P1.1).
 *
 * A pipe is a fixed-size kernel ring buffer identified by a small integer id.
 * Processes agree on ids out of band (typically the parent passes them to the
 * child on its command line). Semantics:
 *
 *   - pipe_write blocks while the buffer is full.
 *   - pipe_read blocks while the buffer is empty and the pipe is open, and
 *     returns at least 1 byte otherwise (up to 'max').
 *   - pipe_close marks end-of-stream; a reader that drains a closed pipe gets
 *     0 (EOF), which also frees the slot for reuse.
 *
 * Blocking happens with interrupts enabled, so the scheduler keeps running
 * the other side of the conversation. Single-core, so no further locking. */

#define MAX_PIPES 16
#define PIPE_BUF  1024

int pipe_create();                                /* -> id or -1            */
int pipe_write(int id, const uint8_t *buf, int len); /* -> len or -1        */
int pipe_read(int id, uint8_t *buf, int max);     /* -> n>0, 0=EOF, -1=err  */
int pipe_close(int id);                           /* -> 0 or -1             */
/* Non-blocking readiness: bytes readable (>0), 0 = empty and still open,
 * -1 = invalid / free, or closed and drained (a pipe_read now returns EOF). */
int pipe_avail(int id);

#endif
