#include "ata.h"
#include "../cpu/ports.h"

/* Primary IDE channel I/O ports */
#define ATA_DATA       0x1F0
#define ATA_FEATURES   0x1F1
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_ALT_STATUS 0x3F6

/* Status register bits */
#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_FLUSH   0xE7
#define ATA_CMD_IDENTIFY 0xEC

static int disk_present = 0;

/* ~400ns settle delay: read the alternate status register a few times. */
static void ata_io_wait() {
    for (int i = 0; i < 4; i++) port_byte_in(ATA_ALT_STATUS);
}

/* Wait until BSY clears. Returns 0 on success, -1 on timeout. */
static int ata_wait_bsy() {
    int timeout = 1000000;
    while ((port_byte_in(ATA_STATUS) & ATA_SR_BSY) && timeout-- > 0) { }
    return timeout > 0 ? 0 : -1;
}

/* Wait until DRQ sets (data ready). Returns 0, or -1 on error/timeout. */
static int ata_wait_drq() {
    int timeout = 1000000;
    while (timeout-- > 0) {
        uint8_t status = port_byte_in(ATA_STATUS);
        if (status & ATA_SR_ERR) return -1;
        if (status & ATA_SR_DRQ) return 0;
    }
    return -1;
}

int init_ata() {
    /* Select master, issue IDENTIFY with LBA fields zeroed. */
    port_byte_out(ATA_DRIVE, 0xA0);
    port_byte_out(ATA_SECCOUNT, 0);
    port_byte_out(ATA_LBA_LO, 0);
    port_byte_out(ATA_LBA_MID, 0);
    port_byte_out(ATA_LBA_HI, 0);
    port_byte_out(ATA_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = port_byte_in(ATA_STATUS);
    if (status == 0) {
        disk_present = 0;
        return 0; /* no drive */
    }

    if (ata_wait_bsy() != 0) {
        disk_present = 0;
        return 0;
    }

    /* Non-ATA (e.g. ATAPI) devices set LBA_MID/HI; we only support plain ATA. */
    if (port_byte_in(ATA_LBA_MID) != 0 || port_byte_in(ATA_LBA_HI) != 0) {
        disk_present = 0;
        return 0;
    }

    if (ata_wait_drq() != 0) {
        disk_present = 0;
        return 0;
    }

    /* Consume the 256-word IDENTIFY block. */
    for (int i = 0; i < 256; i++) port_word_in(ATA_DATA);

    disk_present = 1;
    return 1;
}

static void ata_setup(uint32_t lba, uint8_t count) {
    ata_wait_bsy();
    port_byte_out(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F)); /* master + LBA */
    port_byte_out(ATA_SECCOUNT, count);
    port_byte_out(ATA_LBA_LO, (uint8_t) lba);
    port_byte_out(ATA_LBA_MID, (uint8_t) (lba >> 8));
    port_byte_out(ATA_LBA_HI, (uint8_t) (lba >> 16));
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer) {
    if (!disk_present || count == 0) return -1;

    ata_setup(lba, count);
    port_byte_out(ATA_COMMAND, ATA_CMD_READ);

    for (int s = 0; s < count; s++) {
        if (ata_wait_bsy() != 0) return -1;
        if (ata_wait_drq() != 0) return -1;
        for (int i = 0; i < 256; i++) {
            uint16_t data = port_word_in(ATA_DATA);
            buffer[s * 512 + i * 2]     = (uint8_t) (data & 0xFF);
            buffer[s * 512 + i * 2 + 1] = (uint8_t) (data >> 8);
        }
        ata_io_wait();
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer) {
    if (!disk_present || count == 0) return -1;

    ata_setup(lba, count);
    port_byte_out(ATA_COMMAND, ATA_CMD_WRITE);

    for (int s = 0; s < count; s++) {
        if (ata_wait_bsy() != 0) return -1;
        if (ata_wait_drq() != 0) return -1;
        for (int i = 0; i < 256; i++) {
            uint16_t data = (uint16_t) (buffer[s * 512 + i * 2]
                          | (buffer[s * 512 + i * 2 + 1] << 8));
            port_word_out(ATA_DATA, data);
        }
        ata_io_wait();
    }

    /* Flush the drive's write cache to persist the data. */
    port_byte_out(ATA_COMMAND, ATA_CMD_FLUSH);
    ata_wait_bsy();
    return 0;
}
