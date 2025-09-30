#include "debug.h"
#include "sdl_window.h"
#include "unicode_processor.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

int main(int argc, char *argv[])
{
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

    // Allow optional command-line args: [font_path] [initial_file]
    const char *font_path = "./SpaceMono-Regular.ttf";
    const char *initial_file = NULL;
    // Skip over any flags parsed earlier; find remaining non-flag args
    int nonflag_idx = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-')
            continue;
        if (nonflag_idx == 0) {
            // If there is only one non-flag arg, treat it as initial_file later
            nonflag_idx = i;
        } else if (nonflag_idx == 1) {
            // second non-flag arg
            nonflag_idx = i;
        }
    }
    if (nonflag_idx > 0) {
        // If there are two non-flag args, the first is font and the second is file
        // Simpler: if argc >= 3 and argv[1] is not a flag, treat argv[1]=font, argv[2]=file
        if (argc >= 3 && argv[1][0] != '-') {
            font_path = argv[1];
            initial_file = argv[2];
        } else if (argc >= 2 && argv[1][0] != '-') {
            initial_file = argv[1];
        }
    }

    display_text_window(font_path, 28, initial_file);

    return 0;
}
// Test comment
