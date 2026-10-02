#ifndef HDA_H
#define HDA_H

/* Intel High Definition Audio. Follows the codec's pin configuration
 * (speaker, headphone, line out) back to a DAC, unmutes that path, and
 * enables EAPD on Realtek codecs. 1 if the playback DMA position moved. */
int hda_ready(void);
void hda_init(void);

#endif
