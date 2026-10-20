// What the agent needs of a C library that newlib's static libc and libm (linked in by
// presto/app/build.sh: the string functions, the maths) can't give an app without the
// firmware: newlib's printf family and atoi/atof reach for its reentrancy structure, its malloc
// (a float's digits) and its streams. An app can call nothing of the firmware's (app_api.h), so
// these are its own: a vsnprintf for the conversions the agent uses (d i u x X o c s p f e g %,
// with flags, width, precision and h/l/ll/z), fprintf going nowhere (the simulator's debug lines),
// no environment, and errno a plain int.
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *o; size_t n, k; } out_t;

static void put(out_t *w, char c)
{
    if (w->k + 1 < w->n) w->o[w->k] = c;
    w->k++;
}
static void pad(out_t *w, char c, int n)
{
    while (n-- > 0) put(w, c);
}

// a converted field: its sign/prefix, then (precision) zeros, then digits, padded to width
static void field(out_t *w, const char *pre, const char *digits, int len, int zeros, int width, int left, int zero_pad)
{
    int pl = (int) strlen(pre), total = pl + zeros + len;
    if (!left && !zero_pad) pad(w, ' ', width - total);
    for (int i = 0; i < pl; i++) put(w, pre[i]);
    if (!left && zero_pad) pad(w, '0', width - total);
    pad(w, '0', zeros);
    for (int i = 0; i < len; i++) put(w, digits[i]);
    if (left) pad(w, ' ', width - total);
}

static int utoa_rev(char *b, unsigned long long v, unsigned base, int upper)
{
    const char *d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do { b[n++] = d[v % base]; v /= base; } while (v);
    return n;
}

// v >= 0 with prec digits after the point (rounded), into b: its length
static int ftoa(char *b, double v, int prec, int alt)
{
    if (v != v) { memcpy(b, "nan", 3); return 3; }
    if (v > 1.8e19) { memcpy(b, "inf", 3); return 3; }      // (beyond what's printed here)
    if (prec > 17) prec = 17;
    double scale = 1;
    for (int i = 0; i < prec; i++) scale *= 10;
    unsigned long long ip = (unsigned long long) v;
    double frac = (v - (double) ip) * scale + 0.5;
    unsigned long long fp = (unsigned long long) frac;
    if (fp >= (unsigned long long) scale) { fp -= (unsigned long long) scale; ip++; }
    char r[24];
    int n = 0, k = utoa_rev(r, ip, 10, 0);
    while (k) b[n++] = r[--k];
    if (prec || alt) b[n++] = '.';
    k = utoa_rev(r, fp, 10, 0);
    for (int i = k; i < prec; i++) b[n++] = '0';
    if (prec) while (k) b[n++] = r[--k];
    return n;
}

// %e: d.ddde+xx
static int etoa(char *b, double v, int prec, int upper, int alt)
{
    int e = 0;
    if (v != 0 && v == v && v <= 1.7e308) {
        while (v >= 10) { v /= 10; e++; }
        while (v < 1) { v *= 10; e--; }
    }
    int n = ftoa(b, v, prec, alt);
    if (b[0] == '1' && b[1] == '0') {       // (rounded up to 10.0)
        n = ftoa(b, v / 10, prec, alt);
        e++;
    }
    b[n++] = upper ? 'E' : 'e';
    b[n++] = e < 0 ? '-' : '+';
    if (e < 0) e = -e;
    if (e >= 100) b[n++] = (char) ('0' + e / 100);
    b[n++] = (char) ('0' + e / 10 % 10);
    b[n++] = (char) ('0' + e % 10);
    return n;
}

int vsnprintf(char *o, size_t size, const char *f, va_list ap)
{
    out_t w = {o, size, 0};
    for (; *f; f++) {
        if (*f != '%') { put(&w, *f); continue; }
        int left = 0, plus = 0, space = 0, zero = 0, alt = 0, width = 0, prec = -1, lng = 0;
        for (;; f++) {
            if (f[1] == '-') left = 1;
            else if (f[1] == '+') plus = 1;
            else if (f[1] == ' ') space = 1;
            else if (f[1] == '0') zero = 1;
            else if (f[1] == '#') alt = 1;
            else break;
        }
        f++;
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } f++; }
        else while (*f >= '0' && *f <= '9') width = width * 10 + *f++ - '0';
        if (*f == '.') {
            f++;
            prec = 0;
            if (*f == '*') { prec = va_arg(ap, int); f++; }
            else while (*f >= '0' && *f <= '9') prec = prec * 10 + *f++ - '0';
        }
        for (;; f++) {
            if (*f == 'l') lng++;
            else if (*f == 'z' || *f == 'j' || *f == 't') lng = sizeof (size_t) > 4 ? 2 : 1;
            else if (*f != 'h') break;
        }
        char b[48], r[24];
        const char *pre = "";
        int n = 0;
        switch (*f) {
        case 'd': case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long) : lng ? va_arg(ap, long) : va_arg(ap, int);
            unsigned long long u = v < 0 ? 0ull - (unsigned long long) v : (unsigned long long) v;
            pre = v < 0 ? "-" : plus ? "+" : space ? " " : "";
            int k = prec == 0 && u == 0 ? 0 : utoa_rev(r, u, 10, 0);
            while (k) b[n++] = r[--k];
            field(&w, pre, b, n, prec > n ? prec - n : 0, width, left, zero && prec < 0);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': case 'p': {
            unsigned long long u;
            unsigned base = *f == 'u' ? 10 : *f == 'o' ? 8 : 16;
            if (*f == 'p') { u = (uintptr_t) va_arg(ap, void *); pre = "0x"; }
            else u = lng >= 2 ? va_arg(ap, unsigned long long) : lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            if (alt && u && base == 16) pre = *f == 'X' ? "0X" : "0x";
            int k = prec == 0 && u == 0 ? 0 : utoa_rev(r, u, base, *f == 'X');
            while (k) b[n++] = r[--k];
            field(&w, pre, b, n, prec > n ? prec - n : 0, width, left, zero && prec < 0);
            break;
        }
        case 'c':
            b[0] = (char) va_arg(ap, int);
            field(&w, "", b, 1, 0, width, left, 0);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0;
            while (s[len] && (prec < 0 || len < prec)) len++;
            field(&w, "", s, len, 0, width, left, 0);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double v = va_arg(ap, double);
            pre = v < 0 || (v == 0 && 1 / v < 0) ? "-" : plus ? "+" : space ? " " : "";
            if (v < 0) v = -v;
            if (prec < 0) prec = 6;
            if (*f == 'f' || *f == 'F') n = ftoa(b, v, prec, alt);
            else if (*f == 'e' || *f == 'E') n = etoa(b, v, prec, *f == 'E', alt);
            else {
                // %g: %e if the exponent is < -4 or >= the precision, else %f; trailing zeros gone
                if (prec == 0) prec = 1;
                int e = 0;
                double t = v;
                if (t != 0) { while (t >= 10) { t /= 10; e++; } while (t < 1) { t *= 10; e--; } }
                n = e < -4 || e >= prec ? etoa(b, v, prec - 1, *f == 'G', alt) : ftoa(b, v, prec - 1 - e, alt);
                if (!alt && memchr(b, '.', (size_t) n)) {
                    char *x = memchr(b, 'e', (size_t) n);
                    if (!x) x = memchr(b, 'E', (size_t) n);
                    int end = x ? (int) (x - b) : n, cut = end;
                    while (b[cut - 1] == '0') cut--;
                    if (b[cut - 1] == '.') cut--;
                    memmove(b + cut, b + end, (size_t) (n - end));
                    n -= end - cut;
                }
            }
            field(&w, pre, b, n, 0, width, left, zero);
            break;
        }
        case '%':
            put(&w, '%');
            break;
        default:        // (not a conversion: as it was)
            put(&w, '%');
            if (*f) put(&w, *f);
            else f--;
        }
    }
    if (size) o[w.k < size ? w.k : size - 1] = 0;
    return (int) w.k;
}

int snprintf(char *o, size_t size, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(o, size, f, ap);
    va_end(ap);
    return n;
}

int sprintf(char *o, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(o, (size_t) -1 / 2, f, ap);
    va_end(ap);
    return n;
}

// (the simulator's debug lines, to stderr: nowhere here)
int fprintf(FILE *s, const char *f, ...)
{
    (void) s, (void) f;
    return 0;
}

char *getenv(const char *name)
{
    (void) name;
    return NULL;
}

int atoi(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    int neg = *s == '-';
    if (*s == '-' || *s == '+') s++;
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0';
    return neg ? -v : v;
}

double atof(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    int neg = *s == '-';
    if (*s == '-' || *s == '+') s++;
    double v = 0, scale = 1;
    while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0';
    if (*s == '.')
        for (s++; *s >= '0' && *s <= '9'; s++) v += (*s - '0') * (scale /= 10);
    if (*s == 'e' || *s == 'E') {
        int e = atoi(s + 1);
        for (; e > 0; e--) v *= 10;
        for (; e < 0; e++) v /= 10;
    }
    return neg ? -v : v;
}

// errno, for newlib's libm: a plain int, not its reentrancy structure's
int *__errno(void)
{
    static int e;
    return &e;
}
