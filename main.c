#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <locale.h>
#include <string.h>
#include "unicode_processor.h"
#include "debug.h"
#include "sdl_window.h"

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            debug_logging = 1;
            printf("Debug mode enabled\n");
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("RobusText Editor - Feature Complete Text Editor\n");
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --debug, -d    Enable debug output\n");
            printf("  --help, -h     Show this help message\n");
            return 0;
        }
    }

    // Removed initial text functionality:
    display_text_window("./Inter.ttf", 24);

    return 0;
}

