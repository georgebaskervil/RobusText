#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <locale.h>
#include "unicode_processor.h"
#include "debug.h"
#include "sdl_window.h"

int main(void) {
    setlocale(LC_ALL, "");

    // Use a test string instead of an empty string for initial text.
    const char *initial_text = "";         // Revert to empty

    display_text_window(initial_text, "/Users/george/code/robustext-concept/Inter.ttf", 24);

    return 0;
}

