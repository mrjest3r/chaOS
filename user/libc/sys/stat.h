#ifndef CLIB_SYS_STAT_H
#define CLIB_SYS_STAT_H

typedef unsigned mode_t;

struct stat {
    long st_size;
    unsigned st_mode;
};

int stat(const char *path, struct stat *st);
int mkdir(const char *path, mode_t mode);

#endif
