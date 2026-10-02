#include "uhci.h"
#include "pci.h"
#include "serial.h"
#include "../cpu/ports.h"
#include "../cpu/vmm.h"
#include "../cpu/paging.h"
#include "../cpu/timer.h"
#include "../libc/mem.h"

/* QEMU `-usb` is a PIIX UHCI controller (I/O BAR). Isochronous TDs sit at
 * the head of a frame; async control uses one queue head after them. */

#define TD_ACTIVE 0x00800000u
#define TD_STALL  0x00400000u
#define TD_CERR   (3u << 27)
#define PID_SETUP 0x2Du
#define PID_OUT   0xE1u
#define PID_IN    0x69u

struct uhci_td {
    uint32_t link;
    uint32_t cs;
    uint32_t token;
    uint32_t buffer;
};

struct uhci_qh {
    uint32_t head;
    uint32_t elem;
};

static uint16_t io;
static int present;
static uint32_t *frames;
static struct uhci_qh *qh;
static struct uhci_td *tds;
static uint8_t *setup_buf;
static uint8_t *data_buf;

static uint16_t inw(uint16_t port) { return port_word_in(port); }
static void outw(uint16_t port, uint16_t v) { port_word_out(port, v); }
static void outl(uint16_t port, uint32_t v) { port_dword_out(port, v); }

static void spin(uint32_t n) {
    uint32_t start = timer_ticks();
    while (timer_ticks() - start < n) { }
}

static uint32_t token(uint8_t pid, uint8_t addr, uint8_t ep,
                      int toggle, uint16_t len) {
    uint32_t maxlen = len ? (uint32_t) (len - 1) : 0x7FFu;
    return (uint32_t) pid | ((uint32_t) addr << 8) | ((uint32_t) ep << 15) |
           ((uint32_t) toggle << 19) | (maxlen << 21);
}

static int wait_td(struct uhci_td *td) {
    for (int i = 0; i < 2000000; i++) {
        if (td->cs & TD_ACTIVE) continue;
        if (td->cs & TD_STALL) return -1;
        return 0;
    }
    return -1;
}

int uhci_present(void) { return present; }

void uhci_init(void) {
    present = 0;
    uint8_t dev, fn;
    uint32_t bar = 0;
    if (pci_find_class(0x0C, 0x03, 0x00, &dev, &fn, &bar) != 0) {
        serial_write("[usb] no uhci controller\n");
        return;
    }
    /* PIIX UHCI puts the I/O base in BAR4, not BAR0. */
    for (int n = 0; n < 6 && (bar & 1u) == 0; n++)
        bar = pci_config_read32(0, dev, fn, (uint8_t) (0x10 + n * 4));
    if ((bar & 1u) == 0) {
        serial_write("[usb] uhci has no I/O BAR\n");
        return;
    }
    io = (uint16_t) (bar & ~3u);
    pci_enable(dev, fn, 0, 1, 1);

    uint32_t page = frame_alloc();
    uint32_t page2 = frame_alloc();
    if (!page || !page2) return;
    memory_set((uint8_t *) page, 0, PAGE_SIZE);
    memory_set((uint8_t *) page2, 0, PAGE_SIZE);
    frames = (uint32_t *) page;
    qh = (struct uhci_qh *) page2;
    tds = (struct uhci_td *) (page2 + 0x40);
    setup_buf = (uint8_t *) (page2 + 0x200);
    data_buf = (uint8_t *) (page2 + 0x220);

    qh->head = 1;
    qh->elem = 1;
    for (int i = 0; i < 1024; i++)
        frames[i] = ((uint32_t) qh) | 2u; /* QH select */

    outw(io + 0x00, 0x0002); /* host reset */
    spin(2);
    outw(io + 0x00, 0);
    outl(io + 0x08, page); /* frame list base */
    outw(io + 0x06, 0);    /* frame number */
    outw(io + 0x00, 0x0001); /* run */
    present = 1;
    serial_write("[usb] uhci running\n");
}

static int port_up(void) {
    for (int p = 0; p < 2; p++) {
        uint16_t port = (uint16_t) (io + 0x10 + p * 2);
        uint16_t sc = inw(port);
        if ((sc & 1u) == 0) continue;
        outw(port, (uint16_t) (sc | 0x0200u)); /* reset */
        spin(3);
        sc = inw(port);
        outw(port, (uint16_t) (sc & ~0x0200u));
        spin(1);
        sc = inw(port);
        outw(port, (uint16_t) ((sc & ~0x000Au) | 0x0004u)); /* enable, clear changes */
        spin(1);
        if (inw(port) & 0x0004u) return p;
    }
    return -1;
}

int uhci_control(uint8_t addr, const uint8_t setup[8],
                 void *data, uint16_t len, int data_in) {
    if (!present) return -1;
    for (int i = 0; i < 8; i++) setup_buf[i] = setup[i];
    if (!data_in && data && len)
        memory_copy((uint8_t *) data, data_buf, len);

    struct uhci_td *setup_td = &tds[0];
    struct uhci_td *status_td = &tds[1];
    uint32_t status_phys = (uint32_t) status_td;
    /* Full-speed default packet is 8 bytes. Chain that many so a longer
     * descriptor still completes. */
    int ntd = 0;
    uint16_t left = len;
    uint16_t off = 0;
    int toggle = 1;
    while (left && ntd < 16) {
        uint16_t chunk = left > 8 ? 8 : left;
        struct uhci_td *td = &tds[2 + ntd];
        td->link = status_phys;
        td->cs = TD_CERR | TD_ACTIVE;
        td->token = token(data_in ? PID_IN : PID_OUT, addr, 0, toggle, chunk);
        td->buffer = (uint32_t) data_buf + off;
        toggle ^= 1;
        off = (uint16_t) (off + chunk);
        left = (uint16_t) (left - chunk);
        ntd++;
    }
    for (int i = 0; i < ntd; i++)
        tds[2 + i].link = (i + 1 < ntd) ? (uint32_t) &tds[3 + i] : status_phys;

    setup_td->link = ntd ? (uint32_t) &tds[2] : status_phys;
    setup_td->cs = TD_CERR | TD_ACTIVE;
    setup_td->token = token(PID_SETUP, addr, 0, 0, 8);
    setup_td->buffer = (uint32_t) setup_buf;

    status_td->link = 1;
    status_td->cs = TD_CERR | TD_ACTIVE;
    status_td->token = token(data_in ? PID_OUT : PID_IN, addr, 0, 1, 0);
    status_td->buffer = 0;

    qh->elem = (uint32_t) setup_td;
    if (wait_td(setup_td) != 0) return -1;
    for (int i = ntd - 1; i >= 0; i--) {
        if (wait_td(&tds[2 + i]) != 0) return -1;
    }
    if (wait_td(status_td) != 0) return -1;
    if (data_in && data && len)
        memory_copy(data_buf, (uint8_t *) data, len);
    return 0;
}

int uhci_iso_out(uint8_t addr, uint8_t ep, const void *data, uint16_t len) {
    if (!present || len == 0 || len > 64) return -1;
    memory_copy((uint8_t *) data, data_buf, len);
    struct uhci_td *td = &tds[3];
    td->link = ((uint32_t) qh) | 2u;
    td->cs = TD_CERR | TD_ACTIVE | (1u << 25); /* isochronous */
    td->token = token(PID_OUT, addr, ep, 0, len);
    td->buffer = (uint32_t) data_buf;
    uint16_t frame = (uint16_t) ((inw(io + 0x06) + 2) & 1023);
    frames[frame] = (uint32_t) td; /* TD, not QH */
    if (wait_td(td) != 0) return -1;
    frames[frame] = ((uint32_t) qh) | 2u;
    return 0;
}

int uhci_reset_port(void) {
    return port_up();
}
