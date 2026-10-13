/* Sound effects for doomgeneric. Compiled against the engine headers.
 * Music is left quiet: this machine has PCM playback, not a MIDI synth.
 * Samples are 8-bit and get expanded to the HDA stream (48 kHz stereo). */
#include "i_sound.h"
#include "ulib.h"

int W_CheckNumForName(char *name);
int W_LumpLength(int lump);
void *W_CacheLumpNum(int lump, int tag);

#define CHAN_N 8
#define OUT_FRAMES 1024

typedef struct {
    const uint8_t *sample;
    int len;
    int pos16;
    int step16;
    int vol;
    int sep;
    int on;
} chan_t;

static chan_t chan[CHAN_N];
static int use_prefix;
static int16_t mix[OUT_FRAMES * 2];

int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

static snddevice_t sfx_devs[] = {
    SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS, SNDDEVICE_GENMIDI, SNDDEVICE_AWE32
};

static void copy_name(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static boolean sfx_init(boolean prefix) {
    use_prefix = prefix ? 1 : 0;
    for (int i = 0; i < CHAN_N; i++) chan[i].on = 0;
    for (int i = 0; i < OUT_FRAMES * 2; i++) mix[i] = 0;
    uplay(mix, OUT_FRAMES);
    return true;
}

static void sfx_shutdown(void) {}

static int sfx_lump(sfxinfo_t *sfx) {
    char name[16];
    if (use_prefix) {
        name[0] = 'd';
        name[1] = 's';
        copy_name(name + 2, sfx->name, 7);
    } else {
        copy_name(name, sfx->name, 9);
    }
    return W_CheckNumForName(name);
}

static void sfx_params(int channel, int vol, int sep) {
    if (channel < 0 || channel >= CHAN_N) return;
    chan[channel].vol = vol;
    chan[channel].sep = sep;
}

static int sfx_start(sfxinfo_t *sfx, int channel, int vol, int sep) {
    if (channel < 0 || channel >= CHAN_N) return -1;
    int lump = sfx->lumpnum;
    if (lump < 0) lump = sfx_lump(sfx);
    if (lump < 0) return -1;
    int bytes = W_LumpLength(lump);
    if (bytes <= 8) return -1;
    const uint8_t *raw = (const uint8_t *) W_CacheLumpNum(lump, 1);
    if (!raw) return -1;
    const uint8_t *sample = raw;
    int len = bytes;
    int rate = 11025;
    if (raw[0] == 3 && raw[1] == 0) {
        rate = raw[2] | (raw[3] << 8);
        len = raw[4] | (raw[5] << 8) | (raw[6] << 16) | (raw[7] << 24);
        if (rate < 4000 || rate > 48000) rate = 11025;
        if (len < 0 || len > bytes - 8) len = bytes - 8;
        sample = raw + 8;
    }
    chan_t *c = &chan[channel];
    c->sample = sample;
    c->len = len;
    c->pos16 = 0;
    c->step16 = (rate << 16) / 48000;
    if (c->step16 < 1) c->step16 = 1;
    c->vol = vol;
    c->sep = sep;
    c->on = 1;
    return channel;
}

static void sfx_stop(int channel) {
    if (channel < 0 || channel >= CHAN_N) return;
    chan[channel].on = 0;
}

static boolean sfx_playing(int channel) {
    if (channel < 0 || channel >= CHAN_N) return false;
    return chan[channel].on ? true : false;
}

static void sfx_cache(sfxinfo_t *sounds, int num_sounds) {
    (void) sounds;
    (void) num_sounds;
}

static void sfx_update(void) {
    for (int i = 0; i < OUT_FRAMES * 2; i++) mix[i] = 0;
    for (int ch = 0; ch < CHAN_N; ch++) {
        chan_t *c = &chan[ch];
        if (!c->on) continue;
        int lg = c->vol * (254 - c->sep);
        int rg = c->vol * c->sep;
        if (lg < 0) lg = 0;
        if (rg < 0) rg = 0;
        for (int i = 0; i < OUT_FRAMES; i++) {
            int idx = c->pos16 >> 16;
            if (idx >= c->len) { c->on = 0; break; }
            int s = ((int) c->sample[idx] - 128) << 8;
            c->pos16 += c->step16;
            int l = mix[i * 2] + s * lg / (127 * 254);
            int r = mix[i * 2 + 1] + s * rg / (127 * 254);
            if (l > 32767) l = 32767;
            if (l < -32768) l = -32768;
            if (r > 32767) r = 32767;
            if (r < -32768) r = -32768;
            mix[i * 2] = (int16_t) l;
            mix[i * 2 + 1] = (int16_t) r;
        }
    }
    uplay(mix, OUT_FRAMES);
}

sound_module_t DG_sound_module = {
    sfx_devs,
    (int) (sizeof sfx_devs / sizeof sfx_devs[0]),
    sfx_init,
    sfx_shutdown,
    sfx_lump,
    sfx_update,
    sfx_params,
    sfx_start,
    sfx_stop,
    sfx_playing,
    sfx_cache
};

static boolean mus_init(void) { return false; }
static void mus_shutdown(void) {}
static void mus_volume(int volume) { (void) volume; }
static void mus_pause(void) {}
static void mus_resume(void) {}
static void *mus_register(void *data, int len) { (void) data; (void) len; return 0; }
static void mus_unregister(void *handle) { (void) handle; }
static void mus_play(void *handle, boolean looping) { (void) handle; (void) looping; }
static void mus_stop(void) {}
static boolean mus_playing(void) { return false; }
static void mus_poll(void) {}

static snddevice_t mus_devs[] = { SNDDEVICE_SB, SNDDEVICE_GENMIDI, SNDDEVICE_GUS };

music_module_t DG_music_module = {
    mus_devs,
    (int) (sizeof mus_devs / sizeof mus_devs[0]),
    mus_init,
    mus_shutdown,
    mus_volume,
    mus_pause,
    mus_resume,
    mus_register,
    mus_unregister,
    mus_play,
    mus_stop,
    mus_playing,
    mus_poll
};
