#include "atapi.h"
#include "../cpu/ports.h"
#include "../drivers/serial.h"

/* Primary 0x1F0 / 0x3F6, secondary 0x170 / 0x376.
 * A CD is usually the secondary master when a hard disk is the primary. */

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

static uint16_t io_base;
static uint16_t io_alt;
static uint8_t drive_sel;
static int present;

static void wait_io(void) {
    for (int i = 0; i < 4; i++) port_byte_in(io_alt);
}

static int wait_bsy(void) {
    for (int i = 0; i < 1000000; i++) {
        if (!(port_byte_in(io_base + 7) & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int wait_drq(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = port_byte_in(io_base + 7);
        if (s & ATA_SR_ERR) return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

static int probe(uint16_t base, uint16_t alt, uint8_t sel) {
    io_base = base;
    io_alt = alt;
    drive_sel = sel;
    port_byte_out(io_alt, 0x00);
    port_byte_out(io_base + 6, sel);
    wait_io();
    if (wait_bsy() != 0) return 0;
    /* The signature is valid once the drive is selected, before the command. */
    uint8_t mid = port_byte_in(io_base + 4);
    uint8_t hi = port_byte_in(io_base + 5);
    if (mid != 0x14 || hi != 0xEB) return 0;
    port_byte_out(io_base + 7, 0xA1); /* IDENTIFY PACKET, drains the identify data */
    wait_io();
    if (wait_bsy() != 0 || wait_drq() != 0) return 0;
    for (int i = 0; i < 256; i++) port_word_in(io_base);
    port_byte_in(io_base + 7);
    return 1;
}

int atapi_init(void) {
    present = 0;
    if (probe(0x170, 0x376, 0xA0) || probe(0x170, 0x376, 0xB0) ||
        probe(0x1F0, 0x3F6, 0xB0) || probe(0x1F0, 0x3F6, 0xA0)) {
        present = 1;
        serial_write("[cd] ATAPI drive ready\n");
        return 1;
    }
    serial_write("[cd] no ATAPI drive\n");
    return 0;
}

int atapi_read(uint32_t lba, uint8_t *buf) {
    if (!present || !buf) return -1;
    port_byte_out(io_alt, 0x00);
    if (wait_bsy() != 0) return -1;
    port_byte_out(io_base + 6, drive_sel);
    wait_io();
    port_byte_out(io_base + 1, 0);          /* PIO */
    port_byte_out(io_base + 2, 0);
    port_byte_out(io_base + 3, 0);
    port_byte_out(io_base + 4, (uint8_t) (2048 & 0xFF));
    port_byte_out(io_base + 5, (uint8_t) (2048 >> 8));
    port_byte_out(io_base + 7, 0xA0);       /* PACKET */
    if (wait_drq() != 0) return -1;

    uint8_t pkt[12];
    for (int i = 0; i < 12; i++) pkt[i] = 0;
    pkt[0] = 0xA8;                          /* READ(12) */
    pkt[2] = (uint8_t) (lba >> 24);
    pkt[3] = (uint8_t) (lba >> 16);
    pkt[4] = (uint8_t) (lba >> 8);
    pkt[5] = (uint8_t) lba;
    pkt[9] = 1;                             /* one sector */
    for (int i = 0; i < 6; i++) {
        uint16_t w = (uint16_t) pkt[i * 2] | ((uint16_t) pkt[i * 2 + 1] << 8);
        port_word_out(io_base, w);
    }
    if (wait_drq() != 0) return -1;
    uint16_t *out = (uint16_t *) buf;
    for (int i = 0; i < 1024; i++) out[i] = port_word_in(io_base);
    port_byte_in(io_base + 7);
    return 0;
}
