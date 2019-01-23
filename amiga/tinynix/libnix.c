/*
 * tinynix/libnix.c
 * 
 * Rewrites of bits of libnix that we need. These are not copied from libnix
 * or any other third-party software.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 */

#include "types.h"
#include <string.h>
#include <stdarg.h>

/* Division 32:16 -> 32q:16r */
#define do_div(x, y) ({                                             \
    uint32_t _x = (x), _y = (y), _q, _r;                            \
    asm volatile (                                                  \
        "swap %3; "                                                 \
        "move.w %3,%0; "                                            \
        "divu.w %4,%0; "  /* hi / div */                            \
        "move.w %0,%1; "  /* stash quotient-hi */                   \
        "swap %3; "                                                 \
        "move.w %3,%0; "  /* lo / div */                            \
        "divu.w %4,%0; "                                            \
        "swap %1; "                                                 \
        "move.w %0,%1; "  /* stash quotient-lo */                   \
        "eor.w %0,%0; "                                             \
        "swap %0; "                                                 \
        : "=&d" (_r), "=&d" (_q) : "0" (0), "d" (_x), "d" (_y));    \
   (x) = _q;                                                        \
   _r;                                                              \
})

static void do_putch(char **p, char *end, char c)
{
    if (*p < end)
        **p = c;
    (*p)++;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    unsigned int x, flags;
    int width;
    char c, *p = str, *end = p + size - 1, tmp[12], *q;

    while ((c = *format++) != '\0') {
        if (c != '%') {
            do_putch(&p, end, c);
            continue;
        }

        flags = width = 0;
#define BASE      (31u <<  0)
#define UPPER     ( 1u <<  8)
#define SIGN      ( 1u <<  9)
#define ALTERNATE ( 1u << 10)
#define ZEROPAD   ( 1u << 11)
#define CHAR      ( 1u << 12)
#define SHORT     ( 1u << 13)

    more:
        switch (c = *format++) {
        case '#':
            flags |= ALTERNATE;
            goto more;
        case '.':
        case '0':
            flags |= ZEROPAD;
            goto more;
        case '1'...'9':
            width = c-'0';
            while (((c = *format) >= '0') && (c <= '9')) {
                width = width*10 + c-'0';
                format++;
            }
            goto more;
        case 'h':
            if ((c = *format) == 'h') {
                flags |= CHAR;
                format++;
            } else {
                flags |= SHORT;
            }
            goto more;
        case 'o':
            flags |= 8;
            break;
        case 'd':
        case 'i':
            flags |= SIGN;
        case 'u':
            flags |= 10;
            break;
        case 'X':
            flags |= UPPER;
        case 'x':
        case 'p':
            flags |= 16;
            break;
        case 's':
            q = va_arg(ap, char *);
            while ((c = *q++) != '\0') {
                do_putch(&p, end, c);
                width--;
            }
            while (width-- > 0)
                do_putch(&p, end, ' ');
            continue;
        case 'c':
            c = va_arg(ap, unsigned int);
        default:
            do_putch(&p, end, c);
            continue;
        }

        x = va_arg(ap, unsigned int);

        if (flags & CHAR) {
            if (flags & SIGN)
                x = (char)x;
            else
                x = (unsigned char)x;
        } else if (flags & SHORT) {
            if (flags & SIGN)
                x = (short)x;
            else
                x = (unsigned short)x;
        }

        if ((flags & SIGN) && ((int)x < 0)) {
            if (flags & ZEROPAD) {
                do_putch(&p, end, '-');
                flags &= ~SIGN;
            }
            width--;
            x = -x;
        } else {
            flags &= ~SIGN;
        }

        if (flags & ALTERNATE) {
            if (((flags & BASE) == 8) || ((flags & BASE) == 16)) {
                do_putch(&p, end, '0');
                width--;
            }
            if ((flags & BASE) == 16) {
                do_putch(&p, end, 'x');
                width--;
            }
        }

        if (x == 0) {
            q = tmp;
            *q++ = '0';
        } else {
            for (q = tmp; x; q++)
                *q = ((flags & UPPER)
                      ? "0123456789ABCDEF"
                      : "0123456789abcdef") [do_div(x, flags&BASE)];
        }
        while (width-- > (q-tmp))
            do_putch(&p, end, (flags & ZEROPAD) ? '0' : ' ');
        if (flags & SIGN)
            do_putch(&p, end, '-');
        while (q != tmp)
            do_putch(&p, end, *--q);
    };

    if (p <= end)
        *p = '\0';

    return p - str;
}

int sprintf(char *str, const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = vsnprintf(str, 255, format, ap);
    va_end(ap);

    return n;
}

void *memset(void *s, int c, size_t n)
{
    char *p = s;
    while (n--)
        *p++ = c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    char *p = dest;
    const char *q = src;
    while (n--)
        *p++ = *q++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    char *p;
    const char *q;
    if (dest < src)
        return memcpy(dest, src, n);
    p = dest; p += n;
    q = src; q += n;
    while (n--)
        *--p = *--q;
    return dest;
}

char *strcpy(char *dest, const char *src)
{
    char *p = dest;
    while ((*p++ = *src++) != '\0')
        continue;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    char *p = dest;
    while (n-- && ((*p++ = *src++) != '\0'))
        continue;
    return dest;
}

char *strcat(char *dest, const char *src)
{
    char *p = dest;
    while (*p != '\0')
        p++;
    while ((*p++ = *src++) != '\0')
        continue;
    return p;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p != '\0')
        p++;
    return p - s;
}

int strcmp(const char *s1, const char *s2)
{
    return strncmp(s1, s2, ~0);
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n--) {
        int diff = *s1 - *s2;
        if (diff || !*s1)
            return diff;
        s1++; s2++;
    }
    return 0;
}

char *strlwr(char *s)
{
    unsigned char c, *p;
    for (p = (unsigned char *)s; (c = *p) != '\0'; p++) {
        if ((c >= 'A') && (c <= 'Z'))
            *p = c + 'a' - 'A';
    }
    return s;
}

char *strstr(const char *haystack, const char *needle)
{
    const char *p = haystack;
    int sz = strlen(needle);

    /* This could be made faster ;) */
    for (p = haystack; *p; p++) {
        if (!strncmp(p, needle, sz))
            return (char *)p;
    }

    return NULL;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
