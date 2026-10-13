#ifndef HDA_H
#define HDA_H

#include <stdint.h>

/* Intel High Definition Audio. Follows the codec's pin configuration
 * (speaker, headphone, line out) back to a DAC, unmutes that path, and
 * enables EAPD on Realtek codecs. 1 if the playback DMA position moved. */
int hda_ready(void);
void hda_init(void);
/* Replace the looping 48 kHz stereo buffer. frames is capped at 1024. */
int hda_submit(const int16_t *stereo, int frames);

#endif
