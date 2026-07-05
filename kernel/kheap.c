#include "kheap.h"

/* A minimal first-fit heap with block splitting and forward coalescing.
 * Every allocation is preceded by a block_header_t. The list is kept in
 * address order (splitting only ever inserts the new block right after the
 * one being split), which makes coalescing a simple single pass. */

#define ALIGN8(x) (((x) + 7u) & ~7u)
#define MIN_SPLIT 8u /* don't split off blocks smaller than this */

typedef struct block_header {
    uint32_t size;             /* usable bytes available after this header */
    uint32_t free;             /* 1 = free, 0 = in use */
    struct block_header *next; /* next block in address order */
} block_header_t;

static block_header_t *heap_start = 0;

void init_kheap() {
    heap_start = (block_header_t *) KHEAP_START;
    heap_start->size = KHEAP_SIZE - sizeof(block_header_t);
    heap_start->free = 1;
    heap_start->next = 0;
}

/* Merge any run of adjacent free blocks into one. */
static void coalesce() {
    block_header_t *cur = heap_start;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += sizeof(block_header_t) + cur->next->size;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

void *malloc(uint32_t size) {
    if (size == 0 || heap_start == 0) return 0;
    size = ALIGN8(size);

    block_header_t *cur = heap_start;
    while (cur) {
        if (cur->free && cur->size >= size) {
            /* Split off the remainder if it's worth its own header. */
            if (cur->size >= size + sizeof(block_header_t) + MIN_SPLIT) {
                block_header_t *split =
                    (block_header_t *) ((uint8_t *) cur + sizeof(block_header_t) + size);
                split->size = cur->size - size - sizeof(block_header_t);
                split->free = 1;
                split->next = cur->next;

                cur->size = size;
                cur->next = split;
            }
            cur->free = 0;
            return (void *) ((uint8_t *) cur + sizeof(block_header_t));
        }
        cur = cur->next;
    }
    return 0; /* out of memory */
}

void free(void *ptr) {
    if (!ptr) return;
    block_header_t *b = (block_header_t *) ((uint8_t *) ptr - sizeof(block_header_t));
    b->free = 1;
    coalesce();
}

void kheap_stats(uint32_t *used, uint32_t *freebytes, uint32_t *blocks) {
    uint32_t u = 0, f = 0, n = 0;
    block_header_t *cur = heap_start;
    while (cur) {
        if (cur->free) f += cur->size;
        else u += cur->size;
        n++;
        cur = cur->next;
    }
    if (used) *used = u;
    if (freebytes) *freebytes = f;
    if (blocks) *blocks = n;
}
