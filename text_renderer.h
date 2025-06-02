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
} RenderData;

// Add line wrapping parameter
int update_render_data(SDL_Renderer *renderer, TTF_Font *font, const char *utf8_text, int x_offset,
                       int y_offset, int maxWidth, RenderData *rd);
int get_glyph_index_at_cursor(const char *text, int byte_cursor);
int get_cluster_index_at_cursor(const char *text, int byte_cursor, const int *clusterByteIndices,
                                int numClusters);

// Cleanup function to free RenderData allocated memory
void cleanup_render_data(RenderData *rd);

#endif // TEXT_RENDERER_H
