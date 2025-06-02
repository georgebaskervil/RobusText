#include "unicode_processor.h"
#include "debug.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

// Check if a code point is a combining character U+0300 to U+036F
static bool is_combining(wchar_t cp)
{
    return (cp >= 0x0300 && cp <= 0x036F);
}

// Stub function to return the additional pixel spacing for a given combining character.
static int get_combining_spacing(wchar_t cp)
{
    if (cp == 0x0301) {
        return 2;
    }
    return 4;
}

void process_unicode_string(const wchar_t *input, int screen_width, int smooth_scroll_buffer)
{
    debug_print(L"Input string: %ls\n", input);

    // Backup the original Unicode string.
    wchar_t *backup = wcsdup(input);
    if (!backup) {
        fwprintf(stderr, L"Memory allocation failed\n");
        return;
    }
    debug_print(L"Backup string: %ls\n", backup);

    // Split the Unicode string into a list of characters.
    size_t len = wcslen(input);
    wchar_t *chars = malloc((len + 1) * sizeof(wchar_t));
    if (!chars) {
        fwprintf(stderr, L"Memory allocation failed\n");
        free(backup);
        return;
    }
    for (size_t i = 0; i < len; i++) {
        chars[i] = input[i];
    }
    chars[len] = L'\0';

    debug_print(L"Split characters: ");
    for (size_t i = 0; i < len; i++) {
        debug_print(L"%lc ", chars[i]);
    }
    debug_print(L"\n");

    // Prepare frequency map for combining characters (range: U+0300 to U+036F = 112 codepoints).
    int combiner_freq[112] = {0};

    // Process the string: group base characters with their combining marks.
    wprintf(L"Found groups:\n");
    for (size_t i = 0; i < len; i++) {
        if (!is_combining(chars[i])) {
            wprintf(L"Base: %lc", chars[i]);
            debug_print(L"Base character: %lc\n", chars[i]);
            size_t j = i + 1;
            while (j < len && is_combining(chars[j])) {
                wprintf(L" + %lc", chars[j]);
                debug_print(L"Combining character: %lc\n", chars[j]);
                combiner_freq[chars[j] - 0x0300]++;
                j++;
            }
            wprintf(L"\n");
            i = j - 1;
        }
    }

    // Calculate allowed counts for each unique combining character.
    wprintf(L"\nCombining character allowed counts:\n");
    for (int i = 0; i < 112; i++) {
        if (combiner_freq[i] > 0) {
            wchar_t combiner = 0x0300 + i;
            int spacing = get_combining_spacing(combiner);
            int allowed = smooth_scroll_buffer + (int) ceil(screen_width / (double) spacing);
            wprintf(L"Combiner U+%04X: count = %d, allowed = %d\n", (unsigned int) combiner,
                    combiner_freq[i], allowed);
            debug_print(L"Combiner U+%04X: count = %d, allowed = %d\n", (unsigned int) combiner,
                        combiner_freq[i], allowed);
        }
    }

    free(chars);
    free(backup);
}
