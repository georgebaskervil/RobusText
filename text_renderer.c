#include "text_renderer.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

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
    int idx = 0;
    for (int i = 0; i < numClusters; i++) {
        if (clusterByteIndices[i] <= byte_cursor)
            idx = i;
        else 
            break;
    }
    return idx;
}

int update_render_data(SDL_Renderer *renderer, TTF_Font *font,
                       const char *utf8_text, int margin, RenderData *rd) {
    if (!renderer || !font || !utf8_text || !rd)
        return -1;

    // Free previous resources
    if (rd->textTexture) { SDL_DestroyTexture(rd->textTexture); rd->textTexture = NULL; }
    if (rd->glyphRects) { free(rd->glyphRects); rd->glyphRects = NULL; }
    if (rd->glyphOffsets) { free(rd->glyphOffsets); rd->glyphOffsets = NULL; }
    if (rd->clusterRects) { free(rd->clusterRects); rd->clusterRects = NULL; }
    if (rd->glyphByteOffsets) { free(rd->glyphByteOffsets); rd->glyphByteOffsets = NULL; }
    if (rd->clusterByteIndices) { free(rd->clusterByteIndices); rd->clusterByteIndices = NULL; }

    // Render text surface
    SDL_Color textColor = {198, 194, 199, 255};
    SDL_Surface *textSurface = TTF_RenderUTF8_Blended(font, utf8_text, textColor);
    if (!textSurface) {
        rd->numGlyphs = 0; rd->numClusters = 0; rd->textW = 0; rd->textRect.w = 0;
        SDL_Surface *surf = SDL_CreateRGBSurface(0, 1, rd->textH, 32, 0,0,0,0);
        rd->textTexture = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_FreeSurface(surf);
        return 0;
    }
    rd->textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
    SDL_FreeSurface(textSurface);
    if (!rd->textTexture) {
        return -1;
    }
    SDL_QueryTexture(rd->textTexture, NULL, NULL, &rd->textW, &rd->textH);
    rd->textRect.x = margin;
    rd->textRect.y = margin;
    rd->textRect.w = rd->textW;
    rd->textRect.h = rd->textH;
    
    // Count and store glyph info.
    rd->numGlyphs = 0;
    const char *p = utf8_text;
    while (*p) {
        rd->numGlyphs++;
        p += utf8_char_length(p);
    }
    rd->glyphRects = malloc(rd->numGlyphs * sizeof(SDL_Rect));
    rd->glyphOffsets = malloc(rd->numGlyphs * sizeof(int));
    rd->glyphByteOffsets = malloc(rd->numGlyphs * sizeof(int));
    if (!rd->glyphRects || !rd->glyphOffsets || !rd->glyphByteOffsets)
        return -1;
    p = utf8_text;
    int currX = 0;
    int i = 0;
    while (*p && i < rd->numGlyphs) {
        rd->glyphByteOffsets[i] = p - utf8_text;
        int len = utf8_char_length(p);
        int charW = get_glyph_width(font, p, len);
        rd->glyphOffsets[i] = currX;
        rd->glyphRects[i].w = charW;
        rd->glyphRects[i].h = rd->textRect.h;
        currX += charW;
        i++;
        p += len;
    }
    rd->textW = currX;
    rd->textRect.w = rd->textW;
    for (i = 0; i < rd->numGlyphs; i++) {
        rd->glyphRects[i].x = rd->textRect.x + rd->glyphOffsets[i];
        rd->glyphRects[i].y = rd->textRect.y;
    }
    
    // Group glyphs into clusters.
    rd->clusterRects = malloc(rd->numGlyphs * sizeof(SDL_Rect));
    rd->clusterByteIndices = malloc(rd->numGlyphs * sizeof(int));
    if (!rd->clusterRects || !rd->clusterByteIndices)
        return -1;
    int numClusters = 0;
    int combiningCount = 0;
    // Allocate a temporary array to hold codepoints.
    wchar_t *codePoints = malloc(rd->numGlyphs * sizeof(wchar_t));
    if (!codePoints)
        return -1;
    p = utf8_text;
    for (i = 0; i < rd->numGlyphs; i++) {
        int len = utf8_char_length(p);
        wchar_t wc;
        mbtowc(&wc, p, len);
        codePoints[i] = wc;
        p += len;
    }
    for (i = 0; i < rd->numGlyphs; i++) {
        if (!(codePoints[i] >= 0x0300 && codePoints[i] <= 0x036F)) {
            rd->clusterRects[numClusters] = rd->glyphRects[i];
            rd->clusterByteIndices[numClusters] = rd->glyphByteOffsets[i];
            numClusters++;
            combiningCount = 0;
        } else {
            if (combiningCount < MAX_COMBINING_PER_CLUSTER && numClusters > 0) {
                rd->clusterRects[numClusters-1].w += rd->glyphRects[i].w;
                combiningCount++;
            }
        }
    }
    rd->numClusters = numClusters;
    free(codePoints);
    return 0;
}
