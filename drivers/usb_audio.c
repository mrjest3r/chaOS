#include "usb_audio.h"
#include "uhci.h"
#include "serial.h"
#include "../libc/mem.h"

/* USB audio class 1. QEMU: -usb -device usb-audio.
 * Walks the configuration for a streaming interface and sends one
 * isochronous OUT packet of a square wave. */

static int ready;

static int ctrl(uint8_t addr, uint8_t type, uint8_t req,
                uint16_t value, uint16_t index, void *data, uint16_t len, int in) {
    uint8_t setup[8];
    setup[0] = type;
    setup[1] = req;
    setup[2] = (uint8_t) value;
    setup[3] = (uint8_t) (value >> 8);
    setup[4] = (uint8_t) index;
    setup[5] = (uint8_t) (index >> 8);
    setup[6] = (uint8_t) len;
    setup[7] = (uint8_t) (len >> 8);
    return uhci_control(addr, setup, data, len, in);
}

int usb_audio_ready(void) { return ready; }

void usb_audio_init(void) {
    ready = 0;
    uhci_init();
    if (!uhci_present()) return;
    if (uhci_reset_port() < 0) {
        serial_write("[usb] no device on uhci\n");
        return;
    }

    uint8_t dev[18];
    memory_set(dev, 0, sizeof(dev));
    if (ctrl(0, 0x80, 6, 0x0100, 0, dev, 8, 1) != 0) {
        serial_write("[usb] setup failed\n");
        return;
    }
    /* SET_ADDRESS 2 */
    if (ctrl(0, 0x00, 5, 2, 0, 0, 0, 0) != 0) {
        serial_write("[usb] set address failed\n");
        return;
    }
    uint8_t cfg[128];
    memory_set(cfg, 0, sizeof(cfg));
    if (ctrl(2, 0x80, 6, 0x0200, 0, cfg, 120, 1) != 0) return;
    uint16_t total = (uint16_t) (cfg[2] | (cfg[3] << 8));
    if (total < 9) total = 9;
    if (total > 120) total = 120;

    int audio = 0;
    uint8_t ep = 0;
    uint8_t iface = 0;
    uint8_t alt = 0;
    for (uint16_t i = 0; i + 1 < total; ) {
        uint8_t len = cfg[i];
        uint8_t typ = cfg[i + 1];
        if (len < 2 || i + len > total) break;
        if (typ == 4 && len >= 9 && cfg[i + 5] == 1 && cfg[i + 6] == 2) {
            /* Audio streaming interface. */
            audio = 1;
            iface = cfg[i + 2];
            alt = cfg[i + 3];
        }
        if (audio && typ == 5 && len >= 7) {
            uint8_t addr = cfg[i + 2];
            uint8_t attr = cfg[i + 3];
            if ((attr & 3) == 1 && (addr & 0x80) == 0) {
                ep = (uint8_t) (addr & 0x0F);
                break;
            }
        }
        i = (uint16_t) (i + len);
    }
    if (!audio || !ep) {
        serial_write("[usb] no audio streaming endpoint\n");
        return;
    }
    if (ctrl(2, 0x00, 9, 1, 0, 0, 0, 0) != 0) return; /* SET_CONFIGURATION 1 */
    if (ctrl(2, 0x01, 11, alt, iface, 0, 0, 0) != 0) return; /* SET_INTERFACE */

    int16_t tone[16];
    for (int i = 0; i < 16; i++) tone[i] = (i & 4) ? 8000 : -8000;
    if (uhci_iso_out(2, ep, tone, 32) != 0) {
        serial_write("[usb] isochronous packet failed\n");
        return;
    }
    ready = 1;
    serial_write("[usb] audio stream packet sent\n");
}
