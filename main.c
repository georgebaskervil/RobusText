#include "debug.h"
#include "sdl_window.h"
#include "unicode_processor.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
// Optional: mimalloc can be used as a drop-in replacement allocator.
// We'll set mimalloc runtime options here when available to bake-in optimizations.
#ifdef __has_include
#if __has_include(<mimalloc.h>)
#define MI_MALLOC_REDIRECT /* redirect malloc/free to mimalloc names */
#include <mimalloc.h>
#endif
#endif

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    /* If mimalloc is available at build/link time, configure runtime options
        early so they apply before any allocations. These mirror the
        environment-variable equivalents but are embedded in the binary.
        We wrap calls in a runtime check to avoid build failures when the
        header isn't present. */
#if defined(__has_include) && __has_include(<mimalloc.h>)
    /* Enable large OS pages (2/4MiB), eager arena commit, segment cache,
        immediate purge, decommit on purge, reserve huge OS pages, small
        eager commit delay and limit NUMA nodes to 1 for macOS. */
    mi_option_set(mi_option_allow_large_os_pages, 1);
    mi_option_set(mi_option_arena_eager_commit, 1);
    /* the segment cache option was renamed/deprecated in newer mimalloc
        versions; use the header-provided enum name to remain compatible */
    mi_option_set(mi_option_deprecated_segment_cache, 1);
    mi_option_set(mi_option_purge_delay, 0);
    mi_option_set(mi_option_purge_decommits, 1);
    mi_option_set(mi_option_reserve_huge_os_pages, 1);
    mi_option_set(mi_option_eager_commit_delay, 2);
    mi_option_set(mi_option_use_numa_nodes, 1);
    /* Optionally enable showing stats on exit for verification (commented
        out by default). Uncomment to print mimalloc stats when the process
        exits. */
    /* mi_option_set(mi_option_show_stats, 1); */
#if defined(__APPLE__)
/* runtime diagnostic: check whether the mimalloc symbol is present in
   the process. On macOS `dlsym("malloc")` may still point to libc's
   malloc even when our translation units are redirected to `mi_malloc`.
   So instead check for the presence of `mi_malloc` directly. */
/* delay including dlfcn.h until needed to avoid dependency when mimalloc isn't present */
#include <dlfcn.h>
    void *mi_malloc_sym = dlsym(RTLD_DEFAULT, "mi_malloc");
    if (mi_malloc_sym != NULL) {
        fprintf(stderr, "mimalloc symbol present: mi_malloc=%p\n", mi_malloc_sym);
    } else {
        fprintf(stderr, "mimalloc symbol NOT found via dlsym\n");
    }
#endif
#endif

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
