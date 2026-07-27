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
#define ATA_SR_DF  0x20

#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_FLUSH    0xE7
#define ATA_CMD_IDENTIFY 0xEC

static int disk_present = 0;

/* ~400ns settle delay: read the alternate status register a few times.
 * (Alt status does NOT clear a pending IRQ - that is intentional here.) */
static void ata_io_wait() {
    for (int i = 0; i < 4; i++) port_byte_in(ATA_ALT_STATUS);
}

/* Wait until BSY clears. Returns 0 on success, -1 on timeout. */
static int ata_wait_bsy() {
    int timeout = 1000000;
    while ((port_byte_in(ATA_STATUS) & ATA_SR_BSY) && timeout-- > 0) { }
    return timeout > 0 ? 0 : -1;
}

/* Wait until DRQ sets (data ready), BSY clear. Returns 0, or -1 on error/timeout.
 * VirtualBox is picky: check ERR/DF every poll, and don't spin forever. */
static int ata_wait_drq() {
    int timeout = 1000000;
    while (timeout-- > 0) {
        uint8_t status = port_byte_in(ATA_STATUS);
        if (status & (ATA_SR_ERR | ATA_SR_DF)) return -1;
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

/* Acknowledge a completed sector transfer. Reading the primary status port
 * clears a pending IRQ; VirtualBox's ATA emulation requires this between
 * sectors or the next command will hang / return garbage. */
static uint8_t ata_ack() {
    ata_io_wait();
    return port_byte_in(ATA_STATUS);
}

int init_ata() {
    /* Select master, issue IDENTIFY with LBA fields zeroed. */
    port_byte_out(ATA_DRIVE, 0xA0);
    ata_io_wait();
    port_byte_out(ATA_SECCOUNT, 0);
    port_byte_out(ATA_LBA_LO, 0);
    port_byte_out(ATA_LBA_MID, 0);
    port_byte_out(ATA_LBA_HI, 0);
    port_byte_out(ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_io_wait();

    uint8_t status = port_byte_in(ATA_STATUS);
    if (status == 0 || status == 0xFF) {
        disk_present = 0;
        return 0; /* no drive / floating bus */
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
    ata_ack();

    disk_present = 1;
    return 1;
}

/* Program the LBA registers for a SINGLE-sector transfer. Multi-sector PIO
 * commands are unreliable on VirtualBox (and some real hardware) when polled
 * without IRQs - the controller often never raises DRQ for sector 2+. Always
 * issue count=1 and loop in software instead. */
static void ata_setup_lba(uint32_t lba) {
    ata_wait_bsy();
    port_byte_out(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F)); /* master + LBA */
    port_byte_out(ATA_SECCOUNT, 1);
    port_byte_out(ATA_LBA_LO, (uint8_t) lba);
    port_byte_out(ATA_LBA_MID, (uint8_t) (lba >> 8));
    port_byte_out(ATA_LBA_HI, (uint8_t) (lba >> 16));
}

static int ata_read_one(uint32_t lba, uint8_t *buffer) {
    ata_setup_lba(lba);
    port_byte_out(ATA_COMMAND, ATA_CMD_READ);
    ata_io_wait();

    if (ata_wait_bsy() != 0) return -1;
    if (ata_wait_drq() != 0) return -1;

    for (int i = 0; i < 256; i++) {
        uint16_t data = port_word_in(ATA_DATA);
        buffer[i * 2]     = (uint8_t) (data & 0xFF);
        buffer[i * 2 + 1] = (uint8_t) (data >> 8);
    }

    uint8_t st = ata_ack();
    if (st & (ATA_SR_ERR | ATA_SR_DF)) return -1;
    return 0;
}

static int ata_write_one(uint32_t lba, const uint8_t *buffer) {
    ata_setup_lba(lba);
    port_byte_out(ATA_COMMAND, ATA_CMD_WRITE);
    ata_io_wait();

    if (ata_wait_bsy() != 0) return -1;
    if (ata_wait_drq() != 0) return -1;

    for (int i = 0; i < 256; i++) {
        uint16_t data = (uint16_t) (buffer[i * 2] | (buffer[i * 2 + 1] << 8));
        port_word_out(ATA_DATA, data);
    }

    /* After the last word, VirtualBox sets BSY while it commits the sector.
     * Wait it out and ack the IRQ before issuing the next command. */
    if (ata_wait_bsy() != 0) return -1;
    uint8_t st = ata_ack();
    if (st & (ATA_SR_ERR | ATA_SR_DF)) return -1;
    return 0;
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer) {
    if (!disk_present || count == 0) return -1;

    for (uint8_t s = 0; s < count; s++) {
        if (ata_read_one(lba + s, buffer + (uint32_t) s * 512) != 0)
            return -1;
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer) {
    if (!disk_present || count == 0) return -1;

    for (uint8_t s = 0; s < count; s++) {
        if (ata_write_one(lba + s, buffer + (uint32_t) s * 512) != 0)
            return -1;
    }

    /* Flush the drive's write cache so the data survives a reboot. Some
     * VirtualBox builds are sensitive to FLUSH without a prior BSY wait. */
    if (ata_wait_bsy() != 0) return -1;
    port_byte_out(ATA_COMMAND, ATA_CMD_FLUSH);
    ata_io_wait();
    if (ata_wait_bsy() != 0) return -1;
    uint8_t st = ata_ack();
    if (st & (ATA_SR_ERR | ATA_SR_DF)) return -1;
    return 0;
}
