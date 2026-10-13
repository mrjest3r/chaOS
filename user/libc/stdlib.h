#ifndef CLIB_STDLIB_H
#define CLIB_STDLIB_H

#include <stddef.h>

void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void free(void *p);
void exit(int code);
void abort(void);
int atoi(const char *s);
long atol(const char *s);
double atof(const char *s);
int abs(int x);
int system(const char *cmd);

#endif
