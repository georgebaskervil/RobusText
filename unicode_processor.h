#ifndef UNICODE_PROCESSOR_H
#define UNICODE_PROCESSOR_H

#include <wchar.h>

// Processes the input Unicode string by grouping base characters
// with combining marks and calculates allowed counts.
// screen_width: the available width;
// smooth_scroll_buffer: extra spacing to accommodate smooth scrolling.
void process_unicode_string(const wchar_t *input, int screen_width, int smooth_scroll_buffer);

#endif // UNICODE_PROCESSOR_H