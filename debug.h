#ifndef DEBUG_H
#define DEBUG_H

#include <wchar.h>
#include <stdio.h>
#include <stdarg.h>

// The central debug logging variable.
extern int debug_logging;

// Inline function to print debug messages only when debugging is enabled.
static inline void debug_print(const wchar_t *format, ...) {
    if (!debug_logging) return;
    va_list args;
    va_start(args, format);
    vfwprintf(stderr, format, args);
    va_end(args);
}

#endif // DEBUG_H