#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

#include "bench_text_renderer_shim.h"

// Generate a test string: 'e' followed by N combining acute accents (U+0301)
// UTF-8 for U+0301 is 0xCC 0x81 (2 bytes). Base 'e' = 0x65.

static char *make_combining_text(size_t combining_count, size_t *out_clusters)
{
    // clusters = 1 (base) + combining_count
    size_t clusters = 1 + combining_count;
    *out_clusters = clusters;
    // each combining mark 2 bytes, base 1 byte, plus null
    size_t buf_size = 1 + (combining_count * 2) + 1;
    char *buf = malloc(buf_size);
    if (!buf)
        return NULL;
    size_t pos = 0;
    buf[pos++] = 'e';
    for (size_t i = 0; i < combining_count; i++) {
        buf[pos++] = (char) 0xCC;
        buf[pos++] = (char) 0x81;
    }
    buf[pos] = '\0';
    return buf;
}

static double now_sec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    size_t combining = 1000000; // default: 1M combining marks
    if (argc > 1)
        combining = strtoull(argv[1], NULL, 10);
    size_t clusters = 0;
    char *text = make_combining_text(combining, &clusters);
    if (!text) {
        fprintf(stderr, "Failed to allocate test text\n");
        return 2;
    }

    printf("Generated text: base + %zu combining marks -> clusters=%zu, bytes=%zu\n", combining,
           clusters, strlen(text));

    RenderData rd;
    memset(&rd, 0, sizeof(rd));
    rd.lazy_mode = 1;
    rd.cluster_block_size = 1024;
    rd.cluster_cache_blocks = 32; // keep cache moderately large for benchmark
    rd.cluster_block_cache = NULL;

    // Warm-up: perform sequential accesses to populate a few blocks
    size_t probes = 10000;
    unsigned int seed = (unsigned int) time(NULL);
    double t0 = now_sec();
    for (size_t i = 0; i < probes; i++) {
        // random cluster within full range
        int idx = (int) (rand_r(&seed) % clusters);
        int off = get_cluster_byte_offset(&rd, text, idx);
        if (off < 0) {
            fprintf(stderr, "Error computing offset for idx %d\n", idx);
            break;
        }
    }
    double t1 = now_sec();

    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    printf("Warmup %zu probes in %.3fs, maxrss=%ld KB\n", probes, t1 - t0, ru.ru_maxrss);

    // Now do measured random-access benchmark with LRU behavior
    size_t measured_probes = 50000;
    t0 = now_sec();
    for (size_t i = 0; i < measured_probes; i++) {
        int idx = (int) (rand_r(&seed) % clusters);
        int off = get_cluster_byte_offset(&rd, text, idx);
        if (off < 0) {
            fprintf(stderr, "Error computing offset for idx %d\n", idx);
            break;
        }
    }
    t1 = now_sec();
    getrusage(RUSAGE_SELF, &ru);
    printf("Measured %zu probes in %.3fs (%.3f us/probe), maxrss=%ld KB\n", measured_probes,
           t1 - t0, (t1 - t0) * 1e6 / measured_probes, ru.ru_maxrss);

    // Clean up
    invalidate_cluster_blocks_after(&rd, 0);
    cleanup_render_data(&rd);
    free(text);
    return 0;
}
