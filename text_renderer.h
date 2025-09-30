#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include <SDL.h>
#include <SDL_ttf.h>

// RenderData holds precomputed text geometry.
typedef struct {
    SDL_Texture *textTexture;
    SDL_Rect textRect;
    SDL_Rect *glyphRects; // per-glyph rectangles
    int *glyphOffsets;    // relative x offsets per glyph
    int numGlyphs;
    SDL_Rect *clusterRects; // merged clusters for highlighting
    int numClusters;
    int textW;
    int textH;
    int *glyphByteOffsets;   // starting byte offset for each glyph
    int *clusterByteIndices; // starting byte offset for each cluster

    // Line wrapping data
    int *lineBreaks;  // Index of first cluster in each line
    int *lineWidths;  // Width of each line
    int numLines;     // Number of lines after wrapping
    int lineHeight;   // Height of each line
    int maxLineWidth; // Maximum allowed line width
    int baselineSkip; // Distance between baselines of successive lines
    int scrollY;      // Vertical scroll offset in pixels (for viewport culling)
    // Lazy layout / cache
    int lazy_mode;             // non-zero if using lazy per-viewport layout
    int cluster_block_size;    // clusters per block
    int cluster_cache_blocks;  // number of blocks to cache
    void *cluster_block_cache; // opaque pointer to block cache (allocated by implementation)
} RenderData;

// Add line wrapping parameter
int update_render_data(SDL_Renderer *renderer, TTF_Font *font, const char *utf8_text, int x_offset,
                       int y_offset, int maxWidth, RenderData *rd);
int get_glyph_index_at_cursor(const char *text, int byte_cursor);

// New lazy cluster accessor: returns the cluster index containing byte_cursor
// Uses RenderData's lazy blocks; prefers to be called when rd is available.
int get_cluster_index_at_cursor(const char *text, int byte_cursor, RenderData *rd);

// Return byte offset for a given cluster index; ensures the block containing
// the cluster is resident (lazy evaluation). Returns -1 on error.
int get_cluster_byte_offset(RenderData *rd, const char *text, int clusterIndex);
// Ensure the visible viewport texture is prepared in lazy mode. Returns 0 on success.
int prepare_visible_texture(SDL_Renderer *renderer, TTF_Font *font, const char *utf8_text,
                            int x_offset, int y_offset, int maxWidth, RenderData *rd, int viewportY,
                            int viewportHeight);

// Invalidate cache blocks after a cluster index (call after edits)
void invalidate_cluster_blocks_after(RenderData *rd, int clusterIndex);

// Cleanup function to free RenderData allocated memory
void cleanup_render_data(RenderData *rd);

#endif // TEXT_RENDERER_H
