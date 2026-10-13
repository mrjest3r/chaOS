#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "strings.h"
#include "ctype.h"
#include "math.h"
#include "errno.h"
#include "sys/stat.h"
#include "../ulib.h"

int errno;

/* ---- memory / strings --------------------------------------------------- */

void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *dd = (uint8_t *) d;
    const uint8_t *ss = (const uint8_t *) s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i];
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    uint8_t *dd = (uint8_t *) d;
    const uint8_t *ss = (const uint8_t *) s;
    if (dd < ss) {
        for (size_t i = 0; i < n; i++) dd[i] = ss[i];
    } else {
        for (size_t i = n; i > 0; i--) dd[i - 1] = ss[i - 1];
    }
    return d;
}

void *memset(void *d, int c, size_t n) {
    uint8_t *dd = (uint8_t *) d;
    for (size_t i = 0; i < n; i++) dd[i] = (uint8_t) c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *aa = (const uint8_t *) a;
    const uint8_t *bb = (const uint8_t *) b;
    for (size_t i = 0; i < n; i++) {
        if (aa[i] != bb[i]) return (int) aa[i] - (int) bb[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char) *a - (unsigned char) *b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (unsigned char) a[i] - (unsigned char) b[i];
    }
    return 0;
}

char *strcpy(char *d, const char *s) {
    char *p = d;
    while ((*p++ = *s++)) {}
    return d;
}

char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strcat(char *d, const char *s) {
    char *p = d;
    while (*p) p++;
    while ((*p++ = *s++)) {}
    return d;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char) c) return (char *) s;
        s++;
    }
    return c == 0 ? (char *) s : 0;
}

char *strrchr(const char *s, int c) {
    const char *hit = 0;
    while (*s) {
        if (*s == (char) c) hit = s;
        s++;
    }
    if (c == 0) return (char *) s;
    return (char *) hit;
}

char *strstr(const char *s, const char *n) {
    if (!*n) return (char *) s;
    for (; *s; s++) {
        size_t i = 0;
        while (n[i] && s[i] == n[i]) i++;
        if (!n[i]) return (char *) s;
    }
    return 0;
}

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = (char *) malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static int fold(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

int strcasecmp(const char *a, const char *b) {
    while (*a && fold(*a) == fold(*b)) { a++; b++; }
    return fold((unsigned char) *a) - fold((unsigned char) *b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = fold((unsigned char) a[i]);
        int cb = fold((unsigned char) b[i]);
        if (ca != cb || !a[i]) return ca - cb;
    }
    return 0;
}

int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }
int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }

/* ---- heap --------------------------------------------------------------- */

void *malloc(size_t n) { return umalloc((uint32_t) n); }
void *calloc(size_t n, size_t sz) { return ucalloc((uint32_t) n, (uint32_t) sz); }
void *realloc(void *p, size_t n) { return urealloc(p, (uint32_t) n); }
void free(void *p) { ufree(p); }

void exit(int code) { (void) code; uexit(); }
void abort(void) { uexit(); }

int atoi(const char *s) { return uatoi(s); }

long atol(const char *s) { return (long) uatoi(s); }

double atof(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    double v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10.0 + (double) (*s - '0'); s++; }
    if (*s == '.') {
        s++;
        double place = 0.1;
        while (*s >= '0' && *s <= '9') {
            v += (double) (*s - '0') * place;
            place *= 0.1;
            s++;
        }
    }
    return neg ? -v : v;
}

int abs(int x) { return x < 0 ? -x : x; }

double fabs(double x) { return x < 0 ? -x : x; }

int system(const char *cmd) { (void) cmd; errno = EINVAL; return -1; }

/* ---- formatted print ---------------------------------------------------- */

static int emit_char(char *out, size_t cap, int pos, char c) {
    if (out && cap > 0 && (size_t) pos < cap - 1) out[pos] = c;
    return pos + 1;
}

static int emit_str(char *out, size_t cap, int pos, const char *s, int width, int zero, int prec) {
    if (!s) s = "(null)";
    int len = 0;
    while (s[len] && (prec < 0 || len < prec)) len++;
    int pad = width > len ? width - len : 0;
    char pc = zero ? '0' : ' ';
    for (int i = 0; i < pad; i++) pos = emit_char(out, cap, pos, pc);
    for (int i = 0; i < len; i++) pos = emit_char(out, cap, pos, s[i]);
    return pos;
}

static int emit_uint(char *out, size_t cap, int pos, unsigned long v, int base,
                     int upper, int width, int zero, int sign, int prec) {
    char tmp[40];
    const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    if (v == 0 && prec != 0) tmp[n++] = '0';
    while (v) {
        tmp[n++] = digs[v % (unsigned) base];
        v /= (unsigned) base;
    }
    if (prec > n) {
        while (n < prec && n < (int) sizeof tmp) tmp[n++] = '0';
    }
    if (sign) tmp[n++] = '-';
    int pad = width > n ? width - n : 0;
    char pc = zero ? '0' : ' ';
    if (sign && zero) {
        pos = emit_char(out, cap, pos, '-');
        n--;
        for (int i = 0; i < pad; i++) pos = emit_char(out, cap, pos, '0');
        while (n > 0) pos = emit_char(out, cap, pos, tmp[--n]);
        return pos;
    }
    for (int i = 0; i < pad; i++) pos = emit_char(out, cap, pos, pc);
    while (n > 0) pos = emit_char(out, cap, pos, tmp[--n]);
    return pos;
}

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap) {
    int pos = 0;
    while (*fmt) {
        if (*fmt != '%') {
            pos = emit_char(s, n, pos, *fmt++);
            continue;
        }
        fmt++;
        int zero = 0, width = 0, prec = -1;
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; }
        char spec = *fmt ? *fmt++ : 0;
        if (spec == '%') { pos = emit_char(s, n, pos, '%'); continue; }
        if (spec == 'c') { pos = emit_char(s, n, pos, (char) va_arg(ap, int)); continue; }
        if (spec == 's') { pos = emit_str(s, n, pos, va_arg(ap, const char *), width, 0, prec); continue; }
        if (spec == 'd' || spec == 'i') {
            long v = is_long ? va_arg(ap, long) : va_arg(ap, int);
            unsigned long u;
            int sign = 0;
            if (v < 0) { sign = 1; u = (unsigned long) (-v); }
            else u = (unsigned long) v;
            pos = emit_uint(s, n, pos, u, 10, 0, width, zero, sign, prec);
            continue;
        }
        if (spec == 'u' || spec == 'x' || spec == 'X' || spec == 'p') {
            unsigned long u = spec == 'p' ? (unsigned long) va_arg(ap, void *)
                            : is_long ? va_arg(ap, unsigned long)
                                      : va_arg(ap, unsigned int);
            int base = (spec == 'u') ? 10 : 16;
            if (spec == 'p') pos = emit_str(s, n, pos, "0x", 0, 0, -1);
            pos = emit_uint(s, n, pos, u, base, spec == 'X', width, zero, 0, prec);
            continue;
        }
        pos = emit_char(s, n, pos, spec);
    }
    if (s && n > 0) {
        size_t end = (size_t) pos < n - 1 ? (size_t) pos : n - 1;
        s[end] = 0;
    }
    return pos;
}

int snprintf(char *s, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, 65536, fmt, ap);
    va_end(ap);
    return r;
}

/* ---- files -------------------------------------------------------------- */

struct FILE {
    uint8_t *data;
    int size;
    int cap;
    int pos;
    int eof;
    int err;
    int writable;
    int console;
    char name[24];
};

static FILE std_in;
static FILE std_out = { .console = 1 };
static FILE std_err = { .console = 1 };
FILE *stdin = &std_in;
FILE *stdout = &std_out;
FILE *stderr = &std_err;

static int file_size(const char *name) {
    static ufileinfo_t *infos;
    if (!infos)
        infos = (ufileinfo_t *) umalloc(UFS_MAX_FILES * (uint32_t) sizeof(ufileinfo_t));
    if (!infos) return -1;
    int n = ulistfiles(infos, UFS_MAX_FILES);
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) {
        if (strcmp(infos[i].name, name) == 0) return (int) infos[i].size;
    }
    return -1;
}

static void copy_name(char *dst, const char *src) {
    int i = 0;
    while (src[i] && i < 23) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int ensure_loaded(FILE *f) {
    if (!f || f->writable || f->console || f->data || f->size <= 0) return 0;
    uint8_t *data = (uint8_t *) malloc((size_t) f->size);
    if (!data) { f->err = 1; errno = ENOMEM; return -1; }
    int n = ureadfile(f->name, data, f->size);
    if (n < 0) { free(data); f->err = 1; errno = ENOENT; return -1; }
    f->data = data;
    f->size = n;
    f->cap = n;
    return 0;
}

FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode || !path[0]) { errno = EINVAL; return 0; }
    int write = mode[0] == 'w' || mode[0] == 'a';
    int append = mode[0] == 'a';
    FILE *f = (FILE *) malloc(sizeof(FILE));
    if (!f) { errno = ENOMEM; return 0; }
    memset(f, 0, sizeof(FILE));
    copy_name(f->name, path);
    f->writable = write;
    if (!write) {
        /* Remember the length only. The bytes are read on the first seek
         * or read, so an existence check does not pull a whole WAD in. */
        int sz = file_size(path);
        if (sz < 0) { free(f); errno = ENOENT; return 0; }
        f->size = sz;
        f->cap = sz;
        return f;
    }
    if (append) {
        int sz = file_size(path);
        if (sz > 0) {
            f->data = (uint8_t *) malloc((size_t) sz);
            if (!f->data) { free(f); errno = ENOMEM; return 0; }
            int n = ureadfile(path, f->data, sz);
            if (n > 0) { f->size = n; f->cap = n; f->pos = n; }
        }
    }
    return f;
}

static int flush_write(FILE *f) {
    if (!f || !f->writable || f->console) return 0;
    if (uwritefile(f->name, f->data, f->size) < 0) { f->err = 1; errno = EINVAL; return -1; }
    return 0;
}

int fclose(FILE *f) {
    if (!f || f->console || f == stdin) return 0;
    int rc = 0;
    if (f->writable) rc = flush_write(f);
    free(f->data);
    free(f);
    return rc;
}

int fflush(FILE *f) {
    if (!f || f->console) return 0;
    if (f->writable) return flush_write(f);
    return 0;
}

size_t fread(void *p, size_t sz, size_t n, FILE *f) {
    if (!f || !p || sz == 0 || n == 0) return 0;
    if (f->console) return 0;
    if (ensure_loaded(f) < 0) return 0;
    size_t want = sz * n;
    int left = f->size - f->pos;
    if (left <= 0) { f->eof = 1; return 0; }
    if ((size_t) left < want) {
        want = (size_t) left;
        f->eof = 1;
    }
    memcpy(p, f->data + f->pos, want);
    f->pos += (int) want;
    return want / sz;
}

size_t fwrite(const void *p, size_t sz, size_t n, FILE *f) {
    if (!f || !p || sz == 0 || n == 0) return 0;
    size_t want = sz * n;
    if (f->console) {
        char buf[256];
        size_t i = 0;
        const char *s = (const char *) p;
        while (i < want) {
            size_t chunk = want - i;
            if (chunk > sizeof(buf) - 1) chunk = sizeof(buf) - 1;
            memcpy(buf, s + i, chunk);
            buf[chunk] = 0;
            uprint(buf);
            i += chunk;
        }
        return n;
    }
    if (!f->writable) { f->err = 1; return 0; }
    int need = f->pos + (int) want;
    if (need > f->cap) {
        int cap = f->cap > 0 ? f->cap : 64;
        while (cap < need) cap *= 2;
        uint8_t *grown = (uint8_t *) realloc(f->data, (size_t) cap);
        if (!grown) { f->err = 1; errno = ENOMEM; return 0; }
        f->data = grown;
        f->cap = cap;
    }
    memcpy(f->data + f->pos, p, want);
    f->pos += (int) want;
    if (f->pos > f->size) f->size = f->pos;
    return n;
}

int fseek(FILE *f, long off, int whence) {
    if (!f || f->console) return -1;
    if (ensure_loaded(f) < 0) return -1;
    long base = 0;
    if (whence == SEEK_CUR) base = f->pos;
    else if (whence == SEEK_END) base = f->size;
    else if (whence != SEEK_SET) { errno = EINVAL; return -1; }
    long next = base + off;
    if (next < 0) next = 0;
    if (next > f->size) next = f->size;
    f->pos = (int) next;
    f->eof = 0;
    return 0;
}

long ftell(FILE *f) {
    if (!f || f->console) return -1;
    if (ensure_loaded(f) < 0) return -1;
    return f->pos;
}

int feof(FILE *f) { return f && f->eof; }
int ferror(FILE *f) { return f && f->err; }

int remove(const char *path) {
    if (!path) { errno = EINVAL; return -1; }
    if (udelete(path) < 0) { errno = ENOENT; return -1; }
    return 0;
}

int rename(const char *a, const char *b) {
    FILE *in = fopen(a, "rb");
    if (!in) return -1;
    FILE *out = fopen(b, "wb");
    if (!out) { fclose(in); return -1; }
    uint8_t buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    int rc = fclose(out);
    fclose(in);
    if (rc < 0) return -1;
    return remove(a);
}

int puts(const char *s) {
    uprint(s ? s : "(null)");
    uprint("\n");
    return 0;
}

int putchar(int c) {
    char b[2] = { (char) c, 0 };
    uprint(b);
    return c;
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (!f) f = stdout;
    size_t put = (size_t) n;
    if (put > sizeof(buf) - 1) put = sizeof(buf) - 1;
    fwrite(buf, 1, put, f);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int assigned = 0;
    if (!str || !fmt) { va_end(ap); return 0; }
    while (*fmt) {
        if (*fmt == ' ') {
            while (*str == ' ' || *str == '\t') str++;
            fmt++;
            continue;
        }
        if (*fmt != '%') {
            if (*str != *fmt) break;
            str++;
            fmt++;
            continue;
        }
        fmt++;
        char spec = *fmt ? *fmt++ : 0;
        while (*str == ' ' || *str == '\t') str++;
        if (spec == 'd' || spec == 'i' || spec == 'u' || spec == 'x' || spec == 'o') {
            int *out = va_arg(ap, int *);
            int sign = 1;
            if (*str == '-') { sign = -1; str++; }
            else if (*str == '+') str++;
            int base = 10;
            if (spec == 'x') base = 16;
            if (spec == 'o') base = 8;
            if (spec == 'i' && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
                base = 16;
                str += 2;
            }
            int v = 0, any = 0;
            for (;;) {
                int d = -1;
                if (*str >= '0' && *str <= '9') d = *str - '0';
                else if (*str >= 'a' && *str <= 'f') d = *str - 'a' + 10;
                else if (*str >= 'A' && *str <= 'F') d = *str - 'A' + 10;
                if (d < 0 || d >= base) break;
                v = v * base + d;
                any = 1;
                str++;
            }
            if (!any) break;
            *out = sign < 0 ? -v : v;
            assigned++;
            continue;
        }
        if (spec == 's') {
            char *out = va_arg(ap, char *);
            if (!*str) break;
            while (*str && *str != ' ' && *str != '\t') *out++ = *str++;
            *out = 0;
            assigned++;
            continue;
        }
        break;
    }
    va_end(ap);
    return assigned;
}

int stat(const char *path, struct stat *st) {
    int sz = file_size(path);
    if (sz < 0 || !st) { errno = ENOENT; return -1; }
    st->st_size = sz;
    st->st_mode = 0;
    return 0;
}

int mkdir(const char *path, mode_t mode) {
    (void) mode;
    if (!path || !path[0]) { errno = EINVAL; return -1; }
    char name[24];
    int i = 0;
    while (path[i] && i < 22) { name[i] = path[i]; i++; }
    if (path[i]) { errno = EINVAL; return -1; }
    if (i == 0 || name[i - 1] != '/') name[i++] = '/';
    name[i] = 0;
    if (file_size(name) >= 0) return 0;
    if (uwritefile(name, "", 0) < 0) { errno = EINVAL; return -1; }
    return 0;
}
