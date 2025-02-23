#include "text_renderer.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include "debug.h"
#include <execinfo.h>
#include <unistd.h>  // Add this for STDERR_FILENO

// Maximum number of combining marks allowed per base character.
#define MAX_COMBINING_PER_CLUSTER 5

// Helper: determine the byte length of next UTF-8 character.
static int utf8_char_length(const char *s) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return 1;
    else if ((c >> 5) == 0x6) return 2;
    else if ((c >> 4) == 0xE) return 3;
    else if ((c >> 3) == 0x1E) return 4;
    return 1;
}

int get_glyph_width(TTF_Font *font, const char *utf8_seq, int len) {
    wchar_t wc;
    mbtowc(&wc, utf8_seq, len);
    int minx, maxx, miny, maxy, advance;
    if (TTF_GlyphMetrics(font, (Uint16)wc, &minx, &maxx, &miny, &maxy, &advance) == 0)
        return advance;
    int w, h;
    TTF_SizeUTF8(font, utf8_seq, &w, &h);
    return w;
}

int get_glyph_index_at_cursor(const char *text, int byte_cursor) {
    int idx = 0, pos = 0;
    while (text[pos] && pos < byte_cursor) {
        int len = utf8_char_length(text + pos);
        pos += len;
        idx++;
    }
    return idx;
}

int get_cluster_index_at_cursor(const char *text, int byte_cursor, const int *clusterByteIndices, int numClusters) {
    // Handle empty text or invalid input
    if (!text || !*text || numClusters == 0) {
        debug_print(L"Empty text or no clusters, returning 0\n");
        return 0;
    }

    debug_print(L"Finding cluster for byte_cursor: %d\n", byte_cursor);

    // Find the exact cluster that contains the cursor
    for (int i = 0; i < numClusters; i++) {
        int nextOffset = (i + 1 < numClusters) ? 
                        clusterByteIndices[i + 1] : strlen(text);
        
        if (byte_cursor >= clusterByteIndices[i] && byte_cursor < nextOffset) {
            debug_print(L"Found cursor in cluster %d\n", i);
            return i;
        }
    }

    // If cursor is beyond text, return last cluster
    return numClusters - 1;
}

int update_render_data(SDL_Renderer *renderer, TTF_Font *font,
                       const char *utf8_text, int margin, int maxWidth, RenderData *rd) {
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
    debug_print(L"[UPDATE %d] Received text of length %u: \"%hs\"\n", update_count, (unsigned)text_len, utf8_text);
    if (!utf8_text || !*utf8_text) {
        debug_print(L"[UPDATE %d] Text is empty. Skipping layout.\n", update_count);
        rd->numGlyphs = 0;
        rd->numClusters = 0;
        rd->textW = 0;
        rd->textH = TTF_FontHeight(font);
        rd->textRect.x = margin;
        rd->textRect.y = margin;
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
    debug_print(L"[UPDATE %d] Surface created - W: %d, H: %d (Font height: %d)\n",
                update_count, textSurface->w, textSurface->h, TTF_FontHeight(font));

    // Right after confirming textSurface is valid but before creating texture:
    rd->textW = textSurface->w;
    rd->textH = textSurface->h;
    rd->textRect.x = margin;
    rd->textRect.y = margin;
    rd->textRect.w = textSurface->w;
    rd->textRect.h = textSurface->h;
    debug_print(L"[UPDATE %d] Updated textRect to (%d, %d, %d, %d)\n",
                update_count, rd->textRect.x, rd->textRect.y, 
                rd->textRect.w, rd->textRect.h);

    // (Optionally, log the raw surface data here.) 

    // Debug: log that we are about to compute glyph metrics.
    debug_print(L"[UPDATE %d] Starting layout computations...\n", update_count);
    
    // ...existing code that computes glyph metrics and groups clusters...
    // For debugging, simulate logging intermediate layout results:
    // (In your actual layout code, log the computed rd->numGlyphs, rd->numClusters, and the resulting geometry)
    debug_print(L"[UPDATE %d] Intermediate: computed glyph count = %d\n", update_count, rd->numGlyphs);
    debug_print(L"[UPDATE %d] Intermediate: computed cluster count = %d\n", update_count, rd->numClusters);
    
    // ...existing code...
    
    debug_print(L"[UPDATE %d] Final layout - TextW: %d, TextH: %d, NumGlyphs: %d, NumClusters: %d\n",
                update_count, rd->textW, rd->textH, rd->numGlyphs, rd->numClusters);
    
    // Create texture from the surface.
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, textSurface);
    if (!texture) {
         debug_print(L"[ERROR] Failed to create texture: %s\n", SDL_GetError());
         SDL_FreeSurface(textSurface);
         return -1;
    }
    rd->textTexture = texture;
    SDL_FreeSurface(textSurface);
    
    return 0;
}
