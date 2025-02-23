#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <locale.h>
#include "unicode_processor.h"
#include "debug.h"
#include "sdl_window.h"

int main(void) {
    setlocale(LC_ALL, "");

    // Removed initial text functionality:
    display_text_window("./Inter.ttf", 24);

    return 0;
}

