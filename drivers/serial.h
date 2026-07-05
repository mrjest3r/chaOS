#ifndef SERIAL_H
#define SERIAL_H

/* Initializes COM1 for 38400 baud, 8N1. Returns 0 on success, 1 if the
 * loopback self-test fails (no serial hardware). */
int init_serial();

void serial_write_char(char c);
void serial_write(const char *s);

#endif
