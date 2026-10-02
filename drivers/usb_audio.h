#ifndef USB_AUDIO_H
#define USB_AUDIO_H

/* 1 if a USB audio-class device was configured and one isochronous
 * OUT packet completed. */
int usb_audio_ready(void);
void usb_audio_init(void);

#endif
