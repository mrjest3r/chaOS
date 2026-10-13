#include "hda.h"
#include "pci.h"
#include "serial.h"
#include "../cpu/vmm.h"
#include "../cpu/paging.h"
#include "../libc/mem.h"
#include "../libc/string.h"

/* Intel HDA, polled. The codec path follows pin configuration (speaker,
 * headphone, line out) through the connection list to a DAC, then unmutes
 * amplifiers and enables EAPD. QEMU's hda-duplex exercises that walk;
 * a Realtek part logs vendor 10ec. DMA buffers are identity-mapped frames. */

#define HDA_VIRT 0xD0000000u
#define VERB_GET_PARAM 0xF00
#define VERB_GET_CONN  0xF02
#define VERB_GET_PINCFG 0xF1C
#define VERB_SET_FORMAT 0x200
#define VERB_SET_AMP 0x300
#define VERB_SET_SEL 0x701
#define VERB_SET_EAPD 0x70C
#define VERB_SET_STREAM 0x706
#define VERB_SET_PIN 0x707
#define VERB_SET_POWER 0x705

#define WCAP_IN_AMP  (1u << 1)
#define WCAP_OUT_AMP (1u << 2)
#define WCAP_AMP_OVR (1u << 3)
#define WCAP_CONN    (1u << 8)
#define WCAP_POWER   (1u << 10)
#define PINCAP_HP    (1u << 3)
#define PINCAP_OUT   (1u << 4)
#define PINCAP_EAPD  (1u << 16)

static volatile uint8_t *regs;
static int ready;

static uint32_t rd32(uint32_t off) {
    return *(volatile uint32_t *)(regs + off);
}
static uint16_t rd16(uint32_t off) {
    return *(volatile uint16_t *)(regs + off);
}
static uint8_t rd8(uint32_t off) {
    return *(volatile uint8_t *)(regs + off);
}
static void wr32(uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(regs + off) = v;
}
static void wr16(uint32_t off, uint16_t v) {
    *(volatile uint16_t *)(regs + off) = v;
}
static void wr8(uint32_t off, uint8_t v) {
    *(volatile uint8_t *)(regs + off) = v;
}

static int map_bar(uint32_t phys) {
    phys &= ~0xFu;
    for (uint32_t i = 0; i < 4; i++) {
        if (vmm_map((uint32_t) kernel_directory, HDA_VIRT + i * PAGE_SIZE,
                    phys + i * PAGE_SIZE, PAGE_PRESENT | PAGE_RW) != 0)
            return -1;
    }
    vmm_share_pde((int) (HDA_VIRT >> 22));
    load_page_directory(kernel_directory);
    regs = (volatile uint8_t *) HDA_VIRT;
    return 0;
}

static int wait_bit(uint32_t off, uint32_t mask, int set) {
    for (int i = 0; i < 1000000; i++) {
        uint32_t v = rd32(off);
        int on = (v & mask) != 0;
        if (on == set) return 0;
    }
    return -1;
}

static volatile uint32_t *corb;
static volatile uint32_t *rirb;
static uint32_t *bdl;
static int16_t *pcm;
static uint32_t corb_phys, rirb_phys, bdl_phys, pcm_phys;

static int alloc_dma(void) {
    corb_phys = frame_alloc();
    rirb_phys = frame_alloc();
    bdl_phys = frame_alloc();
    pcm_phys = frame_alloc();
    if (!corb_phys || !rirb_phys || !bdl_phys || !pcm_phys) return -1;
    corb = (volatile uint32_t *) corb_phys;
    rirb = (volatile uint32_t *) rirb_phys;
    bdl = (uint32_t *) bdl_phys;
    pcm = (int16_t *) pcm_phys;
    memory_set((uint8_t *) corb, 0, PAGE_SIZE);
    memory_set((uint8_t *) rirb, 0, PAGE_SIZE);
    memory_set((uint8_t *) bdl, 0, PAGE_SIZE);
    return 0;
}

static uint32_t verb(uint32_t cad, uint32_t nid, uint32_t cmd, uint32_t parm) {
    return (cad << 28) | (nid << 20) | (cmd << 8) | (parm & 0xFFFFu);
}

static int corb_send(uint32_t cad, uint32_t nid, uint32_t cmd, uint32_t parm,
                     uint32_t *resp) {
    /* Immediate command port. QEMU's intel-hda answers here without the
     * CORB DMA engine, which stays quiet until a stream is programmed. */
    for (int i = 0; i < 100000; i++)
        if ((rd16(0x68) & 1u) == 0) break;
    wr32(0x60, verb(cad, nid, cmd, parm));
    wr16(0x68, 0x1); /* set busy: send */
    for (int i = 0; i < 1000000; i++) {
        uint16_t st = rd16(0x68);
        if (st & 1u) continue;
        if (st & 2u) {
            *resp = rd32(0x64);
            return 0;
        }
        return -1;
    }
    return -1;
}

static int reset_controller(void) {
    wr32(0x08, 0);
    if (wait_bit(0x08, 1, 0) != 0) return -1;
    wr32(0x08, 1);
    if (wait_bit(0x08, 1, 1) != 0) return -1;
    for (int i = 0; i < 100000; i++)
        if (rd16(0x0E) != 0) return 0;
    return -1;
}

static int start_corb(void) {
    wr8(0x4C, 0);
    wr8(0x5C, 0);
    wr8(0x4E, 0x02); /* 256 entries */
    wr8(0x5E, 0x02);
    wr32(0x40, corb_phys);
    wr32(0x44, 0);
    wr16(0x48, 0);
    wr16(0x4A, 0x8000); /* reset read pointer */
    for (int i = 0; i < 100000; i++)
        if (rd16(0x4A) & 0x8000) break;
    wr16(0x4A, 0);
    wr32(0x50, rirb_phys);
    wr32(0x54, 0);
    wr16(0x58, 0x8000);
    wr8(0x4C, 0x02); /* run */
    wr8(0x5C, 0x02);
    return 0;
}

static uint32_t codec_vendor;

static int send(uint32_t cad, uint32_t nid, uint32_t cmd, uint32_t parm, uint32_t *out) {
    uint32_t r = 0;
    if (corb_send(cad, nid, cmd, parm, &r) != 0) return -1;
    if (out) *out = r;
    return 0;
}

static uint32_t widget_type(uint32_t cap) { return (cap >> 20) & 0xFu; }

static uint32_t amp_gain(uint32_t cad, uint32_t nid, uint32_t afg, int output) {
    uint32_t cap = 0;
    uint32_t wcap = 0;
    if (send(cad, nid, VERB_GET_PARAM, 0x09, &wcap) != 0) return 0x7F;
    int present = output ? (wcap & WCAP_OUT_AMP) : (wcap & WCAP_IN_AMP);
    if (!present) return 0;
    uint32_t who = (wcap & WCAP_AMP_OVR) ? nid : afg;
    if (send(cad, who, VERB_GET_PARAM, output ? 0x12 : 0x0D, &cap) != 0) return 0x7F;
    uint32_t steps = (cap >> 8) & 0x7Fu;
    uint32_t off = cap & 0x7Fu;
    uint32_t g = off + steps;
    if (g > 0x7F) g = 0x7F;
    return g;
}

static void unmute(uint32_t cad, uint32_t nid, uint32_t afg, int output, uint32_t index) {
    uint32_t g = amp_gain(cad, nid, afg, output);
    uint32_t wcap = 0;
    send(cad, nid, VERB_GET_PARAM, 0x09, &wcap);
    if (output && !(wcap & WCAP_OUT_AMP)) return;
    if (!output && !(wcap & WCAP_IN_AMP)) return;
    /* Output bit 15, input bit 14, left/right 13/12, index in 11:8, gain low. */
    uint32_t payload = (output ? 0xB000u : 0x7000u) | ((index & 0xFu) << 8) | g;
    uint32_t dummy = 0;
    send(cad, nid, VERB_SET_AMP, payload, &dummy);
}

static uint32_t conn_count(uint32_t cad, uint32_t nid) {
    uint32_t n = 0;
    if (send(cad, nid, VERB_GET_PARAM, 0x0E, &n) != 0) return 0;
    return n & 0x7Fu;
}

static uint32_t conn_at(uint32_t cad, uint32_t nid, uint32_t index, int longform) {
    uint32_t r = 0;
    uint32_t per = longform ? 2u : 4u;
    uint32_t width = longform ? 16u : 8u;
    if (send(cad, nid, VERB_GET_CONN, (index / per) * per, &r) != 0) return 0;
    return (r >> ((index % per) * width)) & (longform ? 0xFFFFu : 0xFFu);
}

/* path[0] is the pin, path[path_n-1] is the DAC. sel[i] is which connection
 * of path[i] leads toward the DAC. */
static uint32_t path_nid[8];
static uint32_t path_sel[8];
static int path_n;
static uint8_t walked[64];

static int reach_dac(uint32_t cad, uint32_t nid, int depth) {
    if (depth >= 8 || nid == 0 || nid >= 64 || walked[nid]) return 0;
    walked[nid] = 1;
    uint32_t cap = 0;
    if (send(cad, nid, VERB_GET_PARAM, 0x09, &cap) != 0) return 0;
    if (widget_type(cap) == 0) {
        path_nid[depth] = nid;
        path_sel[depth] = 0;
        path_n = depth + 1;
        return 1;
    }
    if (!(cap & WCAP_CONN)) return 0;
    uint32_t len_raw = 0;
    if (send(cad, nid, VERB_GET_PARAM, 0x0E, &len_raw) != 0) return 0;
    uint32_t n = len_raw & 0x7Fu;
    int longform = (len_raw & 0x80u) != 0;
    if (n > 16) n = 16;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t child = conn_at(cad, nid, i, longform);
        if (reach_dac(cad, child, depth + 1)) {
            path_nid[depth] = nid;
            path_sel[depth] = i;
            return 1;
        }
    }
    return 0;
}

static void power_up(uint32_t cad, uint32_t nid) {
    uint32_t cap = 0, dummy = 0;
    if (send(cad, nid, VERB_GET_PARAM, 0x09, &cap) != 0) return;
    if (cap & WCAP_POWER) send(cad, nid, VERB_SET_POWER, 0, &dummy);
}

/* Speaker, then headphone, then line out. Connectivity 1 means the pin
 * is not wired, which real codecs use for unused jacks. */
static int pin_score(uint32_t cfg) {
    if (((cfg >> 30) & 3u) == 1u) return -1;
    switch ((cfg >> 20) & 0xFu) {
    case 1: return 3; /* speaker */
    case 2: return 2; /* headphone */
    case 0: return 1; /* line out */
    default: return 0;
    }
}

static int arm_output(uint32_t cad, uint32_t afg, uint32_t pin) {
    for (int i = 0; i < 64; i++) walked[i] = 0;
    path_n = 0;
    if (!reach_dac(cad, pin, 0)) return -1;
    uint32_t dac = path_nid[path_n - 1];
    uint32_t dummy = 0;
    for (int i = 0; i < path_n; i++) {
        uint32_t nid = path_nid[i];
        power_up(cad, nid);
        uint32_t cap = 0;
        send(cad, nid, VERB_GET_PARAM, 0x09, &cap);
        if ((cap & WCAP_CONN) && conn_count(cad, nid) > 1)
            send(cad, nid, VERB_SET_SEL, path_sel[i], &dummy);
        unmute(cad, nid, afg, 0, path_sel[i]);
        unmute(cad, nid, afg, 1, 0);
    }
    uint32_t pincap = 0, cfg = 0;
    send(cad, pin, VERB_GET_PARAM, 0x0C, &pincap);
    send(cad, pin, VERB_GET_PINCFG, 0, &cfg);
    uint32_t ctl = 0x40; /* output enable */
    if ((pincap & PINCAP_HP) || ((cfg >> 20) & 0xFu) == 2) ctl |= 0x80;
    send(cad, pin, VERB_SET_PIN, ctl, &dummy);
    if ((pincap & PINCAP_EAPD) || (codec_vendor >> 16) == 0x10ECu)
        send(cad, pin, VERB_SET_EAPD, 0x02, &dummy);
    if (send(cad, dac, VERB_SET_STREAM, 0x10, &dummy) != 0) return -1;
    return (int) dac;
}

static int play(uint32_t cad, uint32_t dac) {
    /* 48 kHz, 16-bit, stereo. A short square wave. */
    for (int i = 0; i < 1024; i++) {
        int16_t s = (i & 64) ? 8000 : -8000;
        pcm[i] = s;
    }
    uint32_t bytes = 1024u * 2u;
    bdl[0] = pcm_phys;
    bdl[1] = 0;
    bdl[2] = bytes;
    bdl[3] = 1; /* interrupt on completion */

    uint16_t gcap = rd16(0x00);
    uint32_t iss = (gcap >> 8) & 0xFu;
    uint32_t base = 0x80u + iss * 0x20u;

    wr8(base + 0x00, 1); /* stream reset */
    for (int i = 0; i < 100000; i++)
        if (rd8(base + 0x00) & 1) break;
    wr8(base + 0x00, 0);
    for (int i = 0; i < 100000; i++)
        if ((rd8(base + 0x00) & 1) == 0) break;

    wr32(base + 0x18, bdl_phys);
    wr32(base + 0x1C, 0);
    wr32(base + 0x08, bytes);
    wr16(base + 0x0C, 0); /* last valid index */
    wr16(base + 0x12, 0x0011);
    uint32_t fmt = 0;
    if (corb_send(cad, dac, VERB_SET_FORMAT, 0x0011, &fmt) != 0) return -1;
    wr32(base + 0x00, (1u << 20) | 2u); /* stream tag 1, run */

    uint32_t lpib = rd32(base + 0x04);
    for (int i = 0; i < 3000000; i++) {
        if (rd32(base + 0x04) != lpib) return 0;
    }
    return -1;
}

int hda_ready(void) { return ready; }

int hda_submit(const int16_t *stereo, int frames) {
    if (!ready || !pcm || !stereo || frames <= 0) return -1;
    if (frames > 1024) frames = 1024;
    for (int i = 0; i < frames * 2; i++) pcm[i] = stereo[i];
    for (int i = frames * 2; i < 2048; i++) pcm[i] = 0;
    return 0;
}

void hda_init(void) {
    ready = 0;
    uint8_t dev, fn;
    uint32_t bar = 0;
    if (pci_find_class(0x04, 0x03, 0x00, &dev, &fn, &bar) != 0) {
        serial_write("[hda] no controller\n");
        return;
    }
    if (bar & 1u) {
        serial_write("[hda] BAR is I/O\n");
        return;
    }
    pci_enable(dev, fn, 1, 0, 1);
    if (map_bar(bar) != 0 || alloc_dma() != 0) {
        serial_write("[hda] map failed\n");
        return;
    }
    if (reset_controller() != 0) {
        serial_write("[hda] reset failed\n");
        return;
    }
    start_corb();

    uint16_t state = rd16(0x0E);
    uint32_t cad = 0;
    int found = 0;
    for (uint32_t i = 0; i < 15; i++) {
        if (state & (1u << i)) { cad = i; found = 1; break; }
    }
    if (!found) {
        serial_write("[hda] no codec\n");
        return;
    }

    uint32_t vendor = 0;
    if (corb_send(cad, 0, VERB_GET_PARAM, 0x00, &vendor) != 0) {
        serial_write("[hda] verb timeout\n");
        return;
    }
    codec_vendor = vendor;
    if (vendor == 0 || vendor == 0xFFFFFFFFu) {
        char b[16];
        int_to_ascii(rd16(0x00), b);
        serial_write("[hda] codec silent gcap ");
        serial_write(b);
        serial_write("\n");
        return;
    }
    uint32_t nodes = 0;
    if (corb_send(cad, 0, VERB_GET_PARAM, 0x04, &nodes) != 0) return;
    uint32_t start = (nodes >> 16) & 0xFFu;
    uint32_t count = nodes & 0xFFu;
    uint32_t afg = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t kind = 0;
        if (corb_send(cad, start + i, VERB_GET_PARAM, 0x05, &kind) != 0) continue;
        if ((kind & 0x7Fu) == 1) { afg = start + i; break; }
    }
    if (!afg) afg = 1;
    {
        uint32_t dummy = 0;
        corb_send(cad, afg, VERB_SET_POWER, 0x00, &dummy);
    }
    if (corb_send(cad, afg, VERB_GET_PARAM, 0x04, &nodes) != 0) return;
    start = (nodes >> 16) & 0xFFu;
    count = nodes & 0xFFu;

    int best = -1;
    uint32_t best_pin = 0;
    int fallback = -1;
    uint32_t fallback_pin = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t cap = 0, pincap = 0, cfg = 0;
        uint32_t nid = start + i;
        if (send(cad, nid, VERB_GET_PARAM, 0x09, &cap) != 0) continue;
        if (widget_type(cap) != 4) continue;
        if (send(cad, nid, VERB_GET_PARAM, 0x0C, &pincap) != 0) continue;
        if (!(pincap & PINCAP_OUT)) continue;
        if (fallback < 0) { fallback = 0; fallback_pin = nid; }
        if (send(cad, nid, VERB_GET_PINCFG, 0, &cfg) != 0) continue;
        int score = pin_score(cfg);
        if (score > best) { best = score; best_pin = nid; }
    }
    if (best < 0) best_pin = fallback_pin;
    if (!best_pin) {
        serial_write("[hda] no output pin\n");
        return;
    }
    int dac = arm_output(cad, afg, best_pin);
    if (dac < 0) {
        serial_write("[hda] no path from pin to dac\n");
        return;
    }
    if (play(cad, (uint32_t) dac) != 0) {
        serial_write("[hda] stream did not move\n");
        return;
    }
    ready = 1;
    serial_write("[hda] codec ");
    {
        char hex[9];
        uint32_t v = codec_vendor;
        for (int i = 7; i >= 0; i--) {
            uint32_t n = v & 0xFu;
            hex[i] = (char) (n < 10 ? '0' + n : 'a' + n - 10);
            v >>= 4;
        }
        hex[8] = '\0';
        serial_write(hex);
    }
    serial_write(" stream running\n");
}
