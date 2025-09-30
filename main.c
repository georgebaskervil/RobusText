#include "debug.h"
#include "sdl_window.h"
#include "unicode_processor.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
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

#ifdef __EMSCRIPTEN__
    EM_ASM({ console.log('[EMSCRIPTEN] main() start'); });
#endif

    /* If mimalloc is available at build/link time, configure runtime options
        early so they apply before any allocations. These mirror the
        environment-variable equivalents but are embedded in the binary.
        We wrap calls in a runtime check to avoid build failures when the
        header isn't present. */
#if defined(__has_include) && __has_include(<mimalloc.h>)
    /* Only configure mimalloc at runtime on native builds; Emscripten
       builds should not attempt to link or call mimalloc runtime APIs. */
#ifndef __EMSCRIPTEN__
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
   the process. On platforms like Emscripten dlsym isn't available in the
   same way, so skip this check there. */
/* delay including dlfcn.h until needed to avoid dependency when mimalloc isn't present */
#include <dlfcn.h>
    void *mi_malloc_sym = dlsym(RTLD_DEFAULT, "mi_malloc");
    if (mi_malloc_sym != NULL) {
        fprintf(stderr, "mimalloc symbol present: mi_malloc=%p\n", mi_malloc_sym);
    } else {
        fprintf(stderr, "mimalloc symbol NOT found via dlsym\n");
    }
#endif
#else
#ifdef __EMSCRIPTEN__
    debug_print(L"mimalloc runtime configuration skipped on Emscripten\n");
#else
    fprintf(stderr, "mimalloc runtime configuration skipped on Emscripten\n");
#endif
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
    const char *font_path = "./Inter_18pt-Regular.ttf";
    const char *initial_file = NULL;
    // Collect up to two non-flag arguments (flags like --debug may appear anywhere)
    const char *nonflags[2] = {NULL, NULL};
    int nf = 0;
    for (int i = 1; i < argc && nf < 2; i++) {
        if (argv[i][0] == '-')
            continue;
        nonflags[nf++] = argv[i];
    }
    if (nf == 2) {
        // first non-flag arg is font, second is initial file
        font_path = nonflags[0];
        initial_file = nonflags[1];
    } else if (nf == 1) {
        // single non-flag arg -> treat as initial file
        initial_file = nonflags[0];
    }

    display_text_window(font_path, 28, initial_file);

    return 0;
}
// Test comment
