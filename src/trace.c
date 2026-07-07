#include "nano1g/trace.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void n1g_log(n1g_state_t *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    if (s && s->trace) {
        va_list ap2;
        va_start(ap2, fmt);
        vfprintf(s->trace, fmt, ap2);
        fputc('\n', s->trace);
        va_end(ap2);
    }
    va_end(ap);
}

void n1g_die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

uint32_t n1g_parse_u32(const char *text, const char *label) {
    char *end = NULL;
    unsigned long v = strtoul(text, &end, 0);
    if (!text[0] || (end && *end)) {
        n1g_die("invalid %s: %s", label, text);
    }
    return (uint32_t)v;
}

uint64_t n1g_parse_u64(const char *text, const char *label) {
    char *end = NULL;
    unsigned long long v = strtoull(text, &end, 0);
    if (!text[0] || (end && *end)) {
        n1g_die("invalid %s: %s", label, text);
    }
    return (uint64_t)v;
}
