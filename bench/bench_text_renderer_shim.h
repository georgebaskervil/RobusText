#ifndef BENCH_TEXT_RENDERER_SHIM_H
#define BENCH_TEXT_RENDERER_SHIM_H

#include <stddef.h>
#include <stdint.h>

// Minimal subset of RenderData used by the benchmark
typedef struct {
    int lazy_mode;
    int cluster_block_size;
    int cluster_cache_blocks;
    void *cluster_block_cache;
} RenderData;

int get_cluster_byte_offset(RenderData *rd, const char *text, int clusterIndex);
void invalidate_cluster_blocks_after(RenderData *rd, int clusterIndex);
void cleanup_render_data(RenderData *rd);

#endif // BENCH_TEXT_RENDERER_SHIM_H
