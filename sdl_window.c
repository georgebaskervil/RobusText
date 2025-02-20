#include "sdl_window.h"
#include "text_renderer.h"
#include "debug.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COMBINING_PER_CLUSTER 5  // limit combining marks per cluster

// Helper: determine the byte length of next UTF-8 character.
static int utf8_char_length(const char *s) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return 1;
    else if ((c >> 5) == 0x6) return 2;
    else if ((c >> 4) == 0xE) return 3;
    else if ((c >> 3) == 0x1E) return 4;
    return 1;
}

void display_text_window(const char *utf8_text, const char *font_path, int font_size) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { /* error handling */ return; }
    if (TTF_Init() != 0) { /* error handling */ SDL_Quit(); return; }

    SDL_Window *window = SDL_CreateWindow("Unicode Editor",
                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         800, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) { /* error handling */ TTF_Quit(); SDL_Quit(); return; }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) { /* error handling */ SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return; }

    TTF_Font *font = TTF_OpenFont(font_path, font_size);
    if (!font) { /* error handling */ SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return; }
    TTF_SetFontHinting(font, TTF_HINTING_NONE);

    const int margin = 20;
    RenderData rd = {0};

    // Use a dynamic buffer for editable text.
    char *editorText = strdup(utf8_text);
    if (!editorText) { /* error handling */ return; }
    int cursorPos = (int)strlen(editorText);
    // Selection variables.
    int selectionStart = -1, selectionEnd = -1;
    int selecting = 0;

    // Start SDL text input.
    SDL_StartTextInput();

    // Initial render.
    if (update_render_data(renderer, font, editorText, margin, &rd) != 0) {
        // error handling
        free(editorText);
        TTF_CloseFont(font); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
        TTF_Quit(); SDL_Quit();
        return;
    }

    int running = 1;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
            else if (event.type == SDL_TEXTINPUT) {
                // Insert new text at the cursor position.
                int insertLen = (int)strlen(event.text.text);
                int curLen = (int)strlen(editorText);
                char *newText = malloc(curLen + insertLen + 1);
                if (!newText) continue;
                memcpy(newText, editorText, cursorPos);
                memcpy(newText + cursorPos, event.text.text, insertLen);
                memcpy(newText + cursorPos + insertLen, editorText + cursorPos, curLen - cursorPos + 1);
                free(editorText);
                editorText = newText;
                cursorPos += insertLen;
                update_render_data(renderer, font, editorText, margin, &rd);
            }
            else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_BACKSPACE) {
                    if (cursorPos > 0) {
                        int curLen = (int)strlen(editorText);
                        int prevPos = cursorPos - 1;
                        // Find start of previous UTF-8 character.
                        while (prevPos > 0 && ((unsigned char)editorText[prevPos] & 0xC0) == 0x80) { prevPos--; }
                        int rem = cursorPos - prevPos;
                        char *newText = malloc(curLen - rem + 1);
                        if (!newText) continue;
                        memcpy(newText, editorText, prevPos);
                        memcpy(newText + prevPos, editorText + cursorPos, curLen - cursorPos + 1);
                        free(editorText);
                        editorText = newText;
                        cursorPos = prevPos;
                        update_render_data(renderer, font, editorText, margin, &rd);
                    }
                }
                else if (event.key.keysym.sym == SDLK_LEFT) {
                    // Move cursor left.
                    if (cursorPos > 0) {
                        int pos = cursorPos - 1;
                        while (pos > 0 && ((unsigned char)editorText[pos] & 0xC0) == 0x80) { pos--; }
                        cursorPos = pos;
                    }
                }
                else if (event.key.keysym.sym == SDLK_RIGHT) {
                    // Move cursor right.
                    int curLen = (int)strlen(editorText);
                    if (cursorPos < curLen) {
                        int pos = cursorPos + 1;
                        while (pos < curLen && ((unsigned char)editorText[pos] & 0xC0) == 0x80) { pos++; }
                        cursorPos = pos;
                    }
                }
                else if (event.key.keysym.sym == SDLK_ESCAPE) {
                    // Clear selection.
                    selectionStart = selectionEnd = -1;
                    selecting = 0;
                }
                // Allow clipboard copy as before.
                else if (event.key.keysym.sym == SDLK_c && (event.key.keysym.mod & KMOD_GUI)) {
                    if (SDL_SetClipboardText(editorText) == 0)
                        printf("Text copied to clipboard.\n");
                    else
                        printf("Clipboard error: %s\n", SDL_GetError());
                }
                // Start selection with Shift+Left/Right.
                else if (event.key.keysym.sym == SDLK_s) {
                    // For demonstration, toggle selection start at current cursor.
                    if (selectionStart < 0) selectionStart = cursorPos;
                }
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                // Set selection start based on mouse click.
                if (SDL_PointInRect(&(SDL_Point){event.button.x, event.button.y}, &rd.textRect)) {
                    int glyphIdx = get_glyph_index_at_cursor(editorText, cursorPos);
                    selectionStart = glyphIdx;
                    selecting = 1;
                }
            }
            else if (event.type == SDL_MOUSEMOTION && selecting) {
                // For simplicity update selectionEnd based on current cursorPos.
                selectionEnd = get_glyph_index_at_cursor(editorText, cursorPos);
            }
            else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                selecting = 0;
                selectionEnd = get_glyph_index_at_cursor(editorText, cursorPos);
            }
        } // End event poll

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 22, 24, 32, 255);
        SDL_RenderClear(renderer);

        SDL_RenderCopy(renderer, rd.textTexture, NULL, &rd.textRect);

        // Draw selection highlight if selection exists.
        if (selectionStart >= 0 && selectionEnd >= 0 && selectionStart != selectionEnd) {
            int startIdx = selectionStart < selectionEnd ? selectionStart : selectionEnd;
            int endIdx = selectionStart < selectionEnd ? selectionEnd : selectionStart;
            SDL_Rect hl = { rd.textRect.x + (startIdx < rd.numGlyphs ? rd.glyphOffsets[startIdx] : rd.textW),
                            rd.textRect.y,
                            (endIdx < rd.numGlyphs ? rd.glyphOffsets[endIdx] : rd.textW) - (startIdx < rd.numGlyphs ? rd.glyphOffsets[startIdx] : rd.textW),
                            rd.textRect.h };
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 200, 200, 180, 128);
            SDL_RenderFillRect(renderer, &hl);
        }

        // Draw a cursor (a simple vertical line) at the insertion point.
        int clusterIdx = get_cluster_index_at_cursor(editorText, cursorPos, rd.clusterByteIndices, rd.numClusters);
        int cursorX = rd.textRect.x;
        if (clusterIdx < rd.numClusters)
            cursorX += rd.glyphOffsets[clusterIdx];
        else
            cursorX += rd.textW;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(renderer, cursorX, rd.textRect.y, cursorX, rd.textRect.y + rd.textRect.h);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    } // End main loop

    SDL_StopTextInput();

    // Cleanup:
    free(rd.clusterByteIndices);
    free(rd.glyphByteOffsets);
    free(rd.clusterRects);
    free(rd.glyphRects);
    free(rd.glyphOffsets);
    SDL_DestroyTexture(rd.textTexture);
    free(editorText);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}