#include "text_renderer.h"
#include "debug.h"
#include <execinfo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // Add this for STDERR_FILENO
#include <wchar.h>

// Maximum number of combining marks allowed per base character.
#define MAX_COMBINING_PER_CLUSTER 5

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

int get_cluster_index_at_cursor(const char *text, int byte_cursor, const int *clusterByteIndices,
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
    if (!utf8_text || !*utf8_text) {
        debug_print(L"[UPDATE %d] Text is empty. Skipping layout.\n", update_count);
        rd->numGlyphs = 0;
        rd->numClusters = 0;
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
    debug_print(L"[UPDATE %d] Updated textRect to (%d, %d, %d, %d)\n", update_count, rd->textRect.x,
                rd->textRect.y, rd->textRect.w, rd->textRect.h);

    // (Optionally, log the raw surface data here.)

    // Debug: log that we are about to compute glyph metrics.
    debug_print(L"[UPDATE %d] Starting layout computations...\n", update_count);

    // Compute glyph and cluster layout
    int utf8_len = strlen(utf8_text);

    // Count UTF-8 characters first
    int char_count = 0;
    int pos = 0;
    while (pos < utf8_len) {
        int char_len = utf8_char_length(utf8_text + pos);
        pos += char_len;
        char_count++;
    }

    // Allocate arrays for glyph and cluster data
    rd->numGlyphs = char_count;
    rd->numClusters = char_count; // For simplicity, each character is its own cluster

    // Free old allocations if they exist
    if (rd->textTexture) {
        SDL_DestroyTexture(rd->textTexture);
        rd->textTexture = NULL;
    }
    if (rd->glyphOffsets)
        free(rd->glyphOffsets);
    if (rd->clusterByteIndices)
        free(rd->clusterByteIndices);
    if (rd->glyphRects)
        free(rd->glyphRects);
    if (rd->clusterRects)
        free(rd->clusterRects);

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
}
