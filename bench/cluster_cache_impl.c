#include "bench_text_renderer_shim.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int block_index;
    int *offsets;
    int valid;
    uint64_t last_used;
} ClusterBlock;

typedef struct {
    int block_size;
    int num_blocks_cached;
    ClusterBlock *blocks;
    uint64_t usage_counter;
} ClusterBlockCache;

static void ensure_block_cache(RenderData *rd)
{
    if (!rd)
        return;
    if (!rd->cluster_block_cache) {
        ClusterBlockCache *cache = malloc(sizeof(ClusterBlockCache));
        cache->block_size = rd->cluster_block_size > 0 ? rd->cluster_block_size : 1024;
        cache->num_blocks_cached = rd->cluster_cache_blocks > 0 ? rd->cluster_cache_blocks : 8;
        cache->blocks = calloc(cache->num_blocks_cached, sizeof(ClusterBlock));
        cache->usage_counter = 1;
        rd->cluster_block_cache = cache;
    }
}

static ClusterBlock *get_or_create_block(ClusterBlockCache *cache, int block_idx)
{
    if (!cache)
        return NULL;
    for (int i = 0; i < cache->num_blocks_cached; i++) {
        ClusterBlock *b = &cache->blocks[i];
        if (b->valid && b->block_index == block_idx) {
            b->last_used = ++cache->usage_counter;
            return b;
        }
    }
    int lru_index = -1;
    uint64_t lru_value = (uint64_t) -1;
    for (int i = 0; i < cache->num_blocks_cached; i++) {
        ClusterBlock *b = &cache->blocks[i];
        if (!b->valid) {
            lru_index = i;
            break;
        }
        if (b->last_used < lru_value) {
            lru_value = b->last_used;
            lru_index = i;
        }
    }
    if (lru_index < 0)
        return NULL;
    ClusterBlock *dest = &cache->blocks[lru_index];
    if (dest->offsets) {
        free(dest->offsets);
        dest->offsets = NULL;
    }
    dest->offsets = NULL;
    dest->block_index = block_idx;
    dest->valid = 1;
    dest->last_used = ++cache->usage_counter;
    return dest;
}

int get_cluster_byte_offset(RenderData *rd, const char *text, int clusterIndex)
{
    if (!rd || !text || clusterIndex < 0)
        return -1;
    if (rd->cluster_block_cache == (void *) -1)
        return -1; // guard
    if (rd->cluster_block_cache == NULL)
        ensure_block_cache(rd);
    ClusterBlockCache *cache = (ClusterBlockCache *) rd->cluster_block_cache;
    int bs = cache->block_size;
    int block_idx = clusterIndex / bs;
    int within = clusterIndex % bs;
    // search existing
    for (int i = 0; i < cache->num_blocks_cached; i++) {
        ClusterBlock *b = &cache->blocks[i];
        if (b->valid && b->block_index == block_idx) {
            if (within < bs && b->offsets && b->offsets[within] >= 0)
                return b->offsets[within];
            break;
        }
    }
    ClusterBlock *dest = get_or_create_block(cache, block_idx);
    if (!dest)
        return -1;
    dest->offsets = malloc(bs * sizeof(int));
    if (!dest->offsets)
        return -1;
    for (int k = 0; k < bs; k++)
        dest->offsets[k] = -1;
    // scan to block start
    int target_start_cluster = block_idx * bs;
    int pos = 0;
    int cidx = 0;
    while (text[pos] && cidx < target_start_cluster) {
        unsigned char c = (unsigned char) text[pos];
        int len = 1;
        if (c >= 0x80) {
            if ((c >> 5) == 0x6)
                len = 2;
            else if ((c >> 4) == 0xE)
                len = 3;
            else if ((c >> 3) == 0x1E)
                len = 4;
        }
        pos += len;
        cidx++;
    }
    for (int k = 0; k < bs; k++) {
        if (!text[pos])
            dest->offsets[k] = -1;
        else {
            dest->offsets[k] = pos;
            unsigned char c = (unsigned char) text[pos];
            int len = 1;
            if (c >= 0x80) {
                if ((c >> 5) == 0x6)
                    len = 2;
                else if ((c >> 4) == 0xE)
                    len = 3;
                else if ((c >> 3) == 0x1E)
                    len = 4;
            }
            pos += len;
            cidx++;
        }
    }
    if (within < bs) {
        int off = dest->offsets[within];
        if (off >= 0)
            return off;
    }
    pos = dest->offsets[bs - 1] >= 0 ? dest->offsets[bs - 1] : pos;
    int idx = block_idx * bs + bs - 1;
    while (text[pos] && idx < clusterIndex) {
        unsigned char c = (unsigned char) text[pos];
        int len = 1;
        if (c >= 0x80) {
            if ((c >> 5) == 0x6)
                len = 2;
            else if ((c >> 4) == 0xE)
                len = 3;
            else if ((c >> 3) == 0x1E)
                len = 4;
        }
        pos += len;
        idx++;
    }
    if (idx == clusterIndex)
        return pos;
    return -1;
}

void invalidate_cluster_blocks_after(RenderData *rd, int clusterIndex)
{
    if (!rd || !rd->cluster_block_cache)
        return;
    ClusterBlockCache *cache = (ClusterBlockCache *) rd->cluster_block_cache;
    int bs = cache->block_size;
    int cutoff = clusterIndex / bs;
    for (int i = 0; i < cache->num_blocks_cached; i++) {
        ClusterBlock *b = &cache->blocks[i];
        if (!b->valid)
            continue;
        if (b->block_index >= cutoff) {
            if (b->offsets)
                free(b->offsets);
            b->offsets = NULL;
            b->valid = 0;
        }
    }
}

void cleanup_render_data(RenderData *rd)
{
    if (!rd)
        return;
    if (rd->cluster_block_cache) {
        ClusterBlockCache *cache = (ClusterBlockCache *) rd->cluster_block_cache;
        if (cache->blocks) {
            for (int i = 0; i < cache->num_blocks_cached; i++) {
                if (cache->blocks[i].offsets)
                    free(cache->blocks[i].offsets);
            }
            free(cache->blocks);
        }
        free(cache);
        rd->cluster_block_cache = NULL;
    }
}
