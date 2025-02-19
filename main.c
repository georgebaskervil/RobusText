#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <locale.h>
#include "unicode_processor.h"
#include "debug.h"
#include "sdl_window.h"

int main(void) {
    // Set locale for wide-character support.
    setlocale(LC_ALL, "");

    // --- Screen size and smooth scroll settings ---
    int screen_width = 80;
    int smooth_scroll_buffer = 5;

    // --- Unicode string ---
    const wchar_t *input = L"a\u0301b\u0300c\u0301\u0300de\u0301\u0300\u0301\u0300\u0301\u0300\u0301\u0300";

    // Process the Unicode string.
    process_unicode_string(input, screen_width, smooth_scroll_buffer);

    // Convert wide Unicode string to UTF-8.
    size_t utf8_len = wcstombs(NULL, input, 0) + 1;
    char *utf8_str = malloc(utf8_len);
    if (!utf8_str) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }
    wcstombs(utf8_str, input, utf8_len);

    // Display the text in a separate SDL window.
    display_text_window(utf8_str, "/Users/george/code/robustext-concept/Inter.ttf", 24);

    free(utf8_str);
    return 0;
}

