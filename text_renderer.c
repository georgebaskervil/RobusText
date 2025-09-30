#include "text_renderer.h"
#include "debug.h"
#include <SDL.h>
#include <SDL_ttf.h>
#ifndef __EMSCRIPTEN__
#include <execinfo.h>
#else
/* Emscripten doesn't provide execinfo.h; provide minimal stubs used by the
    project so code that calls backtrace/backtrace_symbols still links. These
    stubs won't produce useful backtraces in wasm, but satisfy compilation. */
static int backtrace(void **buffer, int size)
{
    (void) buffer;
    (void) size;
    return 0;
}
static char **backtrace_symbols(void *const *buffer, int size)
{
    (void) buffer;
    (void) size;
    return NULL;
}
#endif
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // Add this for STDERR_FILENO
#include <wchar.h>

// Forward declarations for functions defined later in this file
static int utf8_char_length(const char *s);
int get_cluster_index_from_array(const char *text, int byte_cursor, const int *clusterByteIndices,
                                 int numClusters);

// Maximum number of combining marks allowed per base character.
#define MAX_COMBINING_PER_CLUSTER 5
// Lazy block size: number of clusters per block for cached cluster byte indices
#define CLUSTER_BLOCK_SIZE 1024
// Number of blocks to keep cached at once
#define CLUSTER_CACHE_BLOCKS 8

// Block cache entry
typedef struct {
    int block_index; // which block (0..)
    int *offsets;    // array of size CLUSTER_BLOCK_SIZE for cluster byte offsets
    bool valid;
    uint64_t last_used; // LRU timestamp
} ClusterBlock;

typedef struct {
    int block_size;
    int num_blocks_cached;
    ClusterBlock *blocks;   // array of size num_blocks_cached
    uint64_t usage_counter; // monotonic counter for LRU
} ClusterBlockCache;

// Ensure rd has block cache storage allocated
static void ensure_block_cache(RenderData *rd)
{
    if (!rd)
        return;
    if (!rd->cluster_block_cache) {
        ClusterBlockCache *cache = malloc(sizeof(ClusterBlockCache));
        cache->block_size =
            rd->cluster_block_size > 0 ? rd->cluster_block_size : CLUSTER_BLOCK_SIZE;
        cache->num_blocks_cached =
            rd->cluster_cache_blocks > 0 ? rd->cluster_cache_blocks : CLUSTER_CACHE_BLOCKS;
        cache->blocks = calloc(cache->num_blocks_cached, sizeof(ClusterBlock));
        cache->usage_counter = 1;
        rd->cluster_block_cache = cache;
    }
}

// Find existing block or allocate/evict one using LRU. Returns pointer to block.
static ClusterBlock *get_or_create_block(ClusterBlockCache *cache, int block_idx, const char *text)
{
    if (!cache)
        return NULL;
    // Search for existing block
    for (int i = 0; i < cache->num_blocks_cached; i++) {
        ClusterBlock *b = &cache->blocks[i];
        if (b->valid && b->block_index == block_idx) {
            b->last_used = ++cache->usage_counter;
            return b;
        }
    }

    // Find an unused slot first
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
    // Evict if necessary
    if (dest->offsets) {
        free(dest->offsets);
        dest->offsets = NULL;
    }
    dest->offsets = NULL;
    dest->block_index = block_idx;
    dest->valid = true;
    dest->last_used = ++cache->usage_counter;
    return dest;
}

// Fetch byte offset for a clusterIndex by computing which block it's in and
// computing offsets within that block lazily. For simplicity implement a
// straightforward on-demand scan from start-of-block to fill offsets.
int get_cluster_byte_offset(RenderData *rd, const char *text, int clusterIndex)
{
    if (!rd || !text || clusterIndex < 0)
        return -1;

    // If the full clusterByteIndices array exists (legacy), use it directly
    if (rd->clusterByteIndices) {
        if (clusterIndex < rd->numClusters)
            return rd->clusterByteIndices[clusterIndex];
        return -1;
    }

    // If lazy-mode and a block cache exists, try to use it
    if (rd->lazy_mode) {
        ensure_block_cache(rd);
        ClusterBlockCache *cache = (ClusterBlockCache *) rd->cluster_block_cache;
        int bs = cache->block_size;
        int block_idx = clusterIndex / bs;
        int within = clusterIndex % bs;

        // Find if block is cached
        for (int i = 0; i < cache->num_blocks_cached; i++) {
            ClusterBlock *b = &cache->blocks[i];
            if (b->valid && b->block_index == block_idx) {
                if (within < bs && b->offsets && b->offsets[within] >= 0)
                    return b->offsets[within];
                break;
            }
        }

        // Not cached: populate a block (evict oldest)
        ClusterBlock *dest = get_or_create_block(cache, block_idx, text);
        if (!dest)
            return -1;
        dest->offsets = malloc(bs * sizeof(int));
        if (!dest->offsets)
            return -1;
        for (int k = 0; k < bs; k++)
            dest->offsets[k] = -1;

        // Scan forward from beginning of block to fill offsets
        int target_start_cluster = block_idx * bs;
        int pos = 0;
        int cidx = 0;
        // Fast-forward by scanning until target_start_cluster
        while (text[pos] && cidx < target_start_cluster) {
            pos += utf8_char_length(text + pos);
            cidx++;
        }
        // Fill offsets for this block
        for (int k = 0; k < bs; k++) {
            if (!text[pos]) {
                dest->offsets[k] = -1;
            } else {
                dest->offsets[k] = pos;
                pos += utf8_char_length(text + pos);
                cidx++;
            }
        }

        // Return requested offset if within block
        if (within < bs) {
            int off = dest->offsets[within];
            if (off >= 0)
                return off;
        }

        // As fallback, scan from block end until we reach clusterIndex
        pos = dest->offsets[bs - 1] >= 0 ? dest->offsets[bs - 1] : pos;
        int idx = block_idx * bs + bs - 1;
        while (text[pos] && idx < clusterIndex) {
            pos += utf8_char_length(text + pos);
            idx++;
        }
        if (idx == clusterIndex)
            return pos;
        return -1;
    }

    // Fallback: if no clusters known (shouldn't happen) scan from start
    int pos = 0;
    int idx = 0;
    while (text[pos] && idx <= clusterIndex) {
        if (idx == clusterIndex)
            return pos;
        int len = utf8_char_length(text + pos);
        pos += len;
        idx++;
    }
    if (idx == clusterIndex)
        return pos;
    return -1;
}

// Invalidate cached blocks after a given cluster index (used after edits)
static void invalidate_blocks_after(RenderData *rd, int clusterIndex)
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
            free(b->offsets);
            b->offsets = NULL;
            b->valid = false;
        }
    }
}

void invalidate_cluster_blocks_after(RenderData *rd, int clusterIndex)
{
    invalidate_blocks_after(rd, clusterIndex);
}

// Prepare a texture containing only the visible lines (lazy rendering). This
// renders line-by-line into a surface then converts to a texture.
int prepare_visible_texture(SDL_Renderer *renderer, TTF_Font *font, const char *utf8_text,
                            int x_offset, int y_offset, int maxWidth, RenderData *rd, int viewportY,
                            int viewportHeight)
{
    if (!renderer || !font || !utf8_text || !rd)
        return -1;
    // Create an empty surface sized to viewportWidth x viewportHeight
    SDL_Surface *surface = SDL_CreateRGBSurface(0, rd->maxLineWidth ? rd->maxLineWidth : maxWidth,
                                                viewportHeight, 32, 0, 0, 0, 0);
    if (!surface)
        return -1;

    // Clear background
    SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 22, 24, 32));

    // Determine first visible cluster/line from viewportY using lineHeight
    int line_h = TTF_FontLineSkip(font);
    int first_line = viewportY / line_h;
    int last_line = (viewportY + viewportHeight) / line_h + 1;

    // Scan text to render lines in [first_line, last_line]
    int cur_line = 0;
    int pos = 0;
    int text_len = strlen(utf8_text);
    while (pos < text_len && cur_line <= last_line) {
        // Extract next logical line (until \n or end)
        int line_start = pos;
        while (pos < text_len && utf8_text[pos] != '\n')
            pos++;
        int line_end = pos; // points at '\n' or end

        if (cur_line >= first_line && cur_line <= last_line) {
            int chunk_len = line_end - line_start;
            char *linebuf = malloc(chunk_len + 1);
            if (linebuf) {
                memcpy(linebuf, utf8_text + line_start, chunk_len);
                linebuf[chunk_len] = '\0';

                SDL_Color textColor = {198, 194, 199, 255};
                SDL_Surface *textSurf =
                    TTF_RenderUTF8_Blended_Wrapped(font, linebuf, textColor, maxWidth);
                if (textSurf) {
                    SDL_Rect dst = {0, (cur_line - first_line) * line_h, textSurf->w, textSurf->h};
                    SDL_BlitSurface(textSurf, NULL, surface, &dst);
                    SDL_FreeSurface(textSurf);
                }
                free(linebuf);
            }
        }

        if (pos < text_len && utf8_text[pos] == '\n')
            pos++;
        cur_line++;
    }

    // Create texture from surface and store in rd
    if (rd->textTexture) {
        SDL_DestroyTexture(rd->textTexture);
        rd->textTexture = NULL;
    }
    rd->textTexture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!rd->textTexture) {
        SDL_FreeSurface(surface);
        return -1;
    }
    // Update rd->textRect to describe viewport-sized texture
    rd->textRect.x = x_offset;
    rd->textRect.y = y_offset;
    rd->textRect.w = surface->w;
    rd->textRect.h = surface->h;

    SDL_FreeSurface(surface);
    return 0;
}

// New cluster index accessor that uses RenderData (placeholder lazy behavior).
int get_cluster_index_at_cursor(const char *text, int byte_cursor, RenderData *rd)
{
    // If legacy full index exists, use old behavior
    if (rd && rd->clusterByteIndices) {
        return get_cluster_index_from_array(text, byte_cursor, rd->clusterByteIndices,
                                            rd->numClusters);
    }

    // Otherwise fallback to scanning — but do not allocate large arrays here
    if (!text)
        return 0;
    if (byte_cursor > 0 && (size_t) byte_cursor == strlen(text)) {
        return rd ? rd->numClusters : 0;
    }

    int pos = 0;
    int cluster = 0;
    while (text[pos]) {
        int next = pos + utf8_char_length(text + pos);
        if (byte_cursor >= pos && byte_cursor < next)
            return cluster;
        pos = next;
        cluster++;
    }

    // Cursor at end
    return cluster;
}

// Helper: determine the byte length of next UTF-8 character.
static int utf8_char_length(const char *s)
{
    unsigned char c = (unsigned char) s[0];
    if (c < 0x80)
        return 1;
    else if ((c >> 5) == 0x6)
        return 2;
    else if ((c >> 4) == 0xE)
        return 3;
    else if ((c >> 3) == 0x1E)
        return 4;
    return 1;
}

int get_glyph_width(TTF_Font *font, const char *utf8_seq, int len)
{
    wchar_t wc;
    mbtowc(&wc, utf8_seq, len);
    int minx, maxx, miny, maxy, advance;
    if (TTF_GlyphMetrics(font, (Uint16) wc, &minx, &maxx, &miny, &maxy, &advance) == 0)
        return advance;
    int w, h;
    TTF_SizeUTF8(font, utf8_seq, &w, &h);
    return w;
}

int get_glyph_index_at_cursor(const char *text, int byte_cursor)
{
    int idx = 0, pos = 0;
    while (text[pos] && pos < byte_cursor) {
        int len = utf8_char_length(text + pos);
        pos += len;
        idx++;
    }
    return idx;
}

int get_cluster_index_from_array(const char *text, int byte_cursor, const int *clusterByteIndices,
                                 int numClusters)
{
    // Handle empty text or invalid input
    if (!text || numClusters == 0) {
        debug_print(L"Empty text or no clusters, returning 0\n");
        return 0;
    }
    // If cursor is at the very end of the text
    if (byte_cursor > 0 && (size_t) byte_cursor == strlen(text)) {
        debug_print(L"Cursor at end of text, returning numClusters\n");
        return numClusters; // Indicate position after the last cluster
    }

    debug_print(L"Finding cluster for byte_cursor: %d\n", byte_cursor);

    // Find the exact cluster that contains the cursor
    for (int i = 0; i < numClusters; i++) {
        int nextOffset = (i + 1 < numClusters) ? clusterByteIndices[i + 1] : strlen(text);

        if (byte_cursor >= clusterByteIndices[i] && byte_cursor < nextOffset) {
            debug_print(L"Found cursor in cluster %d\n", i);
            return i;
        }
    }

    // Fallback if byte_cursor is 0 (start of text) and not caught by loop (e.g. empty string was
    // handled)
    if (byte_cursor == 0) {
        debug_print(L"Cursor at beginning of text (byte_cursor 0)\n");
        return 0;
    }

    // This case should ideally not be reached if logic above is complete.
    // It implies cursor is beyond text but not caught by the specific end-of-text check,
    // or some other unhandled scenario. Defaulting to last cluster or numClusters might be
    // context-dependent.
    debug_print(
        L"Cursor position %d not resolved cleanly, returning numClusters -1 (last cluster)\n",
        byte_cursor);
    return numClusters - 1;
}

int update_render_data(SDL_Renderer *renderer, TTF_Font *font, const char *utf8_text, int x_offset,
                       int y_offset, int maxWidth, RenderData *rd)
{
    static uint32_t last_update_time = 0;
    static uint32_t update_count = 0;
    update_count++;
    uint32_t current_time = SDL_GetTicks();

    // Throttling and state tracking.
    static uint32_t update_hash = 0;
    uint32_t new_hash = 0;
    for (const char *p = utf8_text; *p; p++) {
        new_hash = ((new_hash << 5) + new_hash) + *p;
    }
    new_hash ^= maxWidth;
    debug_print(L"[UPDATE %d] Computed hash: %u (prev: %u)\n", update_count, new_hash, update_hash);

    if (current_time - last_update_time < 16 && new_hash == update_hash) {
        debug_print(L"[THROTTLE] Skipping redundant update (hash: %u)\n", new_hash);
        return 0;
    }
    update_hash = new_hash;
    last_update_time = current_time;

    // Log text length and content.
    size_t text_len = strlen(utf8_text);
    debug_print(L"[UPDATE %d] Received text of length %u: \"%hs\"\n", update_count,
                (unsigned) text_len, utf8_text);
    // Check if text is empty
    if (!utf8_text || !*utf8_text) {
        debug_print(L"[UPDATE %d] Text is empty. Cleaning up and skipping layout.\n", update_count);

        // Clean up any existing allocations
        if (rd->textTexture) {
            SDL_DestroyTexture(rd->textTexture);
            rd->textTexture = NULL;
        }
        if (rd->glyphOffsets) {
            free(rd->glyphOffsets);
            rd->glyphOffsets = NULL;
        }
        if (rd->clusterByteIndices) {
            free(rd->clusterByteIndices);
            rd->clusterByteIndices = NULL;
        }
        if (rd->glyphRects) {
            free(rd->glyphRects);
            rd->glyphRects = NULL;
        }
        if (rd->clusterRects) {
            free(rd->clusterRects);
            rd->clusterRects = NULL;
        }
        if (rd->lineBreaks) {
            free(rd->lineBreaks);
            rd->lineBreaks = NULL;
        }
        if (rd->lineWidths) {
            free(rd->lineWidths);
            rd->lineWidths = NULL;
        }
        if (rd->glyphByteOffsets) {
            free(rd->glyphByteOffsets);
            rd->glyphByteOffsets = NULL;
        }

        rd->numGlyphs = 0;
        rd->numClusters = 0;
        rd->numLines = 0;
        rd->textW = 0;
        rd->textH = TTF_FontHeight(font);
        rd->textRect.x = x_offset;
        rd->textRect.y = y_offset;
        rd->textRect.w = 0;
        rd->textRect.h = rd->textH;
        debug_print(L"[UPDATE %d] Skipping texture creation due to empty text.\n", update_count);
        return 0;
    }

    // Create the text surface.
    SDL_Color textColor = {198, 194, 199, 255};
    SDL_Surface *textSurface = TTF_RenderUTF8_Blended_Wrapped(font, utf8_text, textColor, maxWidth);
    if (!textSurface) {
        debug_print(L"[ERROR] Failed to create text surface: %s\n", TTF_GetError());
        return -1;
    }

    // Right after surface creation, update maxLineWidth for wrapping logic
    rd->maxLineWidth = maxWidth;

    debug_print(L"[UPDATE %d] Surface created - W: %d, H: %d (Font height: %d)\n", update_count,
                textSurface->w, textSurface->h, TTF_FontHeight(font));

    // Right after confirming textSurface is valid but before creating texture:
    rd->textW = textSurface->w;
    rd->textH = textSurface->h;
    rd->textRect.x = x_offset;
    rd->textRect.y = y_offset;
    rd->textRect.w = textSurface->w;
    rd->textRect.h = textSurface->h;
    // Initialize scroll position to top when layout changes
    rd->scrollY = 0;
    // Heuristic: enable lazy mode if surface or text is very large
    rd->lazy_mode = 0;
    rd->cluster_block_size = CLUSTER_BLOCK_SIZE;
    rd->cluster_cache_blocks = CLUSTER_CACHE_BLOCKS;
    if (rd->textH > 16384 || strlen(utf8_text) > 100000) {
        rd->lazy_mode = 1;
        // Free full arrays to avoid huge memory usage; cache will be used lazily
        if (rd->clusterByteIndices) {
            free(rd->clusterByteIndices);
            rd->clusterByteIndices = NULL;
        }
        if (rd->glyphOffsets) {
            free(rd->glyphOffsets);
            rd->glyphOffsets = NULL;
        }
        if (rd->glyphRects) {
            free(rd->glyphRects);
            rd->glyphRects = NULL;
        }
        if (rd->clusterRects) {
            free(rd->clusterRects);
            rd->clusterRects = NULL;
        }
    }
    debug_print(L"[UPDATE %d] Updated textRect to (%d, %d, %d, %d)\n", update_count, rd->textRect.x,
                rd->textRect.y, rd->textRect.w, rd->textRect.h);

    // (Optionally, log the raw surface data here.)

    // Debug: log that we are about to compute glyph metrics.
    debug_print(L"[UPDATE %d] Starting layout computations...\n", update_count);

    // Compute glyph and cluster layout (legacy/full mode only)
    int utf8_len = strlen(utf8_text);

    // Count UTF-8 characters first
    int char_count = 0;
    int pos = 0;
    while (pos < utf8_len) {
        int char_len = utf8_char_length(utf8_text + pos);
        pos += char_len;
        char_count++;
    }

    // If lazy_mode is enabled, avoid allocating large arrays and avoid creating
    // a full texture. We will render only visible portions on demand.
    if (rd->lazy_mode) {
        rd->numGlyphs = char_count;
        rd->numClusters = char_count;
        // Free the big surface to avoid memory blowup
        SDL_FreeSurface(textSurface);
        // Ensure block cache exists (lazy allocations will populate on demand)
        ensure_block_cache(rd);
        debug_print(L"[UPDATE %d] Lazy mode enabled - skipping full layout (chars=%d)\n",
                    update_count, char_count);
        return 0;
    }

    // Free old allocations if they exist
    if (rd->textTexture) {
        SDL_DestroyTexture(rd->textTexture);
        rd->textTexture = NULL;
    }
    if (rd->glyphOffsets) {
        free(rd->glyphOffsets);
        rd->glyphOffsets = NULL;
    }
    if (rd->clusterByteIndices) {
        free(rd->clusterByteIndices);
        rd->clusterByteIndices = NULL;
    }
    if (rd->glyphRects) {
        free(rd->glyphRects);
        rd->glyphRects = NULL;
    }
    if (rd->clusterRects) {
        free(rd->clusterRects);
        rd->clusterRects = NULL;
    }
    if (rd->glyphByteOffsets) {
        free(rd->glyphByteOffsets);
        rd->glyphByteOffsets = NULL;
    }
    if (rd->lineBreaks) {
        free(rd->lineBreaks);
        rd->lineBreaks = NULL;
    }
    if (rd->lineWidths) {
        free(rd->lineWidths);
        rd->lineWidths = NULL;
    }

    rd->glyphOffsets = malloc(char_count * sizeof(int));
    rd->clusterByteIndices = malloc(char_count * sizeof(int));
    rd->glyphRects = malloc(char_count * sizeof(SDL_Rect));
    rd->clusterRects = malloc(char_count * sizeof(SDL_Rect));

    if (!rd->glyphOffsets || !rd->clusterByteIndices || !rd->glyphRects || !rd->clusterRects) {
        debug_print(L"[ERROR] Failed to allocate glyph/cluster arrays\n");
        return -1;
    }

    // Compute positions for each character
    pos = 0;
    int current_x = 0;
    int font_height = TTF_FontHeight(font);

    for (int i = 0; i < char_count; i++) {
        int char_len = utf8_char_length(utf8_text + pos);

        // Store byte index for this cluster
        rd->clusterByteIndices[i] = pos;

        // Store x offset for this glyph
        rd->glyphOffsets[i] = current_x;

        // Get character width
        char temp_char[5] = {0};
        memcpy(temp_char, utf8_text + pos, char_len);

        int char_width, char_height;
        if (TTF_SizeUTF8(font, temp_char, &char_width, &char_height) == 0) {
            // Create glyph and cluster rectangles
            rd->glyphRects[i] = (SDL_Rect){current_x, 0, char_width, font_height};
            rd->clusterRects[i] = (SDL_Rect){current_x, 0, char_width, font_height};
            current_x += char_width;
        } else {
            // Fallback for problematic characters
            rd->glyphRects[i] = (SDL_Rect){current_x, 0, 10, font_height};
            rd->clusterRects[i] = (SDL_Rect){current_x, 0, 10, font_height};
            current_x += 10;
        }

        pos += char_len;
    }

    debug_print(L"[UPDATE %d] Intermediate: computed glyph count = %d\n", update_count,
                rd->numGlyphs);
    debug_print(L"[UPDATE %d] Intermediate: computed cluster count = %d\n", update_count,
                rd->numClusters);

    // ...existing code...

    debug_print(
        L"[UPDATE %d] Final layout - TextW: %d, TextH: %d, NumGlyphs: %d, NumClusters: %d\n",
        update_count, rd->textW, rd->textH, rd->numGlyphs, rd->numClusters);

    // Create texture from the surface.
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, textSurface);
    if (!texture) {
        debug_print(L"[ERROR] Failed to create texture: %s\n", SDL_GetError());
        SDL_FreeSurface(textSurface);
        return -1;
    }

    // Verify texture dimensions are reasonable
    int tex_w, tex_h;
    if (SDL_QueryTexture(texture, NULL, NULL, &tex_w, &tex_h) == 0) {
        if (tex_w <= 0 || tex_h <= 0) {
            debug_print(L"[ERROR] Invalid texture dimensions: %d x %d\n", tex_w, tex_h);
            SDL_DestroyTexture(texture);
            SDL_FreeSurface(textSurface);
            return -1;
        }
        debug_print(L"[UPDATE %d] Created texture: %d x %d\n", update_count, tex_w, tex_h);
    }

    rd->textTexture = texture;
    SDL_FreeSurface(textSurface);

    return 0;
}

void cleanup_render_data(RenderData *rd)
{
    if (rd->textTexture) {
        SDL_DestroyTexture(rd->textTexture);
        rd->textTexture = NULL;
    }
    if (rd->glyphOffsets) {
        free(rd->glyphOffsets);
        rd->glyphOffsets = NULL;
    }
    if (rd->clusterByteIndices) {
        free(rd->clusterByteIndices);
        rd->clusterByteIndices = NULL;
    }
    if (rd->glyphRects) {
        free(rd->glyphRects);
        rd->glyphRects = NULL;
    }
    if (rd->clusterRects) {
        free(rd->clusterRects);
        rd->clusterRects = NULL;
    }
    rd->numGlyphs = 0;
    rd->numClusters = 0;

    // Free cluster block cache if present
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
