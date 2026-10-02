#ifndef UHCI_H
#define UHCI_H

#include <stdint.h>

/* UHCI host controller. Control transfers only, plus a helper that
 * drops an isochronous TD at the head of one frame. */

int uhci_present(void);
void uhci_init(void);
/* Enable the first connected port. Returns the port index, or -1. */
int uhci_reset_port(void);

/* data_in = 1 copies `len` bytes into data. Returns 0 on success. */
int uhci_control(uint8_t addr, const uint8_t setup[8],
                 void *data, uint16_t len, int data_in);

/* Queue `len` bytes (max 64) of OUT isochronous data to `ep` for one frame.
 * Returns 0 if the controller retired the TD without a stall. */
int uhci_iso_out(uint8_t addr, uint8_t ep, const void *data, uint16_t len);

#endif
