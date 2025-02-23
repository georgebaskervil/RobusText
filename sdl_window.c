#include <stdbool.h>  // Added for bool, true, false
#include "sdl_window.h"
#include "text_renderer.h"
#include "debug.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>  // Add this for INT_MAX

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

void display_text_window(const char *font_path, int font_size) {
    debug_print(L"Entering display_text_window\n");  // Added debug log
    
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { /* error handling */ return; }
    if (TTF_Init() != 0) { /* error handling */ SDL_Quit(); return; }

    SDL_Window *window = SDL_CreateWindow("Unicode Editor",
                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         800, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_RaiseWindow(window);
    SDL_SetTextInputRect(&(SDL_Rect){0, 0, 800, 600});
    if (!window) { /* error handling */ TTF_Quit(); SDL_Quit(); return; }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) { /* error handling */ SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return; }

    TTF_Font *font = TTF_OpenFont(font_path, font_size);
    if (!font) {
        debug_print(L"[ERROR] Failed to open font: %hs\n", TTF_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return;
    }
    TTF_SetFontHinting(font, TTF_HINTING_NORMAL);

    const int margin = 20;
    int windowWidth = 800, windowHeight = 600;
    int maxTextWidth = windowWidth - (2 * margin);
    RenderData rd = {0};

    // Use a dynamic buffer for editable text. Start with empty text.
    char *editorText = strdup("");  // Changed from "" to " " so layout is built
    if (!editorText) { /* error handling */ return; }
    int cursorPos = 0;
    int selectionStart = -1, selectionEnd = -1;
    int mouseSelecting = 0;  // Only true when selecting with mouse

    // Start SDL text input.
    SDL_StartTextInput();

    // Initial render with line wrapping
    if (update_render_data(renderer, font, editorText, margin, maxTextWidth, &rd) != 0) {
        free(editorText);
        TTF_CloseFont(font); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
        TTF_Quit(); SDL_Quit();
        return;
    }

    int running = 1;
    SDL_Event event;

    int lastWidth = windowWidth;
    char *lastText = NULL;

    static struct {
        uint32_t last_render;
        uint32_t last_content_hash;
        int last_width;
        bool needs_update;
    } state = {0, 0, 0, true};

    while (running) {
        uint32_t frame_start = SDL_GetTicks();
        static int frame_count = 0;
        frame_count++;
        debug_print(L"[FRAME %d] ====================================\n", frame_count);
        debug_print(L"[FRAME %d] Window: %dx%d, Text len: %d\n", 
                    frame_count, windowWidth, windowHeight, (int)strlen(editorText));
        
        while (SDL_PollEvent(&event)) {
            debug_print(L"[FRAME %d] Event: %d\n", frame_count, event.type);
            
            if (event.type == SDL_QUIT) {
                running = 0;
            }
            else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    
                    windowWidth = event.window.data1;
                    windowHeight = event.window.data2;
                    
                    if (windowWidth != lastWidth) {
                        maxTextWidth = windowWidth - (2 * margin);
                        debug_print(L"Window width changed: %d -> %d\n", lastWidth, windowWidth);
                        update_render_data(renderer, font, editorText, margin, maxTextWidth, &rd);
                        lastWidth = windowWidth;
                    }
                }
            }
            else if (event.type == SDL_TEXTINPUT) {
                debug_print(L"[EVENT] SDL_TEXTINPUT received: \"%hs\"\n", event.text.text);
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
                debug_print(L"[EVENT] Updated editorText: \"%hs\"\n", editorText);
                update_render_data(renderer, font, editorText, margin, maxTextWidth, &rd);
            }
            else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_BACKSPACE) {
                    if (cursorPos > 0) {
                        int prevPos = cursorPos - 1;
                        while (prevPos > 0 && ((unsigned char)editorText[prevPos] & 0xC0) == 0x80) {
                            prevPos--;
                        }
                        int rem = cursorPos - prevPos;
                        if (rem > 0) {
                            int curLen = (int)strlen(editorText);
                            char *newText = malloc(curLen - rem + 1);
                            if (!newText) continue;
                            if (prevPos > 0) {
                                memcpy(newText, editorText, prevPos);
                            }
                            memcpy(newText + prevPos, editorText + cursorPos, curLen - cursorPos + 1);
                            free(editorText);
                            editorText = newText;
                            cursorPos = prevPos;
                            selectionStart = selectionEnd = -1;
                            mouseSelecting = 0;
                            update_render_data(renderer, font, editorText, margin, maxTextWidth, &rd);
                        }
                    }
                }
                else if (event.key.keysym.sym == SDLK_LEFT) {
                    if (cursorPos > 0) {
                        int pos = cursorPos - 1;
                        while (pos > 0 && ((unsigned char)editorText[pos] & 0xC0) == 0x80) { pos--; }
                        cursorPos = pos;
                    }
                }
                else if (event.key.keysym.sym == SDLK_RIGHT) {
                    int curLen = (int)strlen(editorText);
                    if (cursorPos < curLen) {
                        int pos = cursorPos + 1;
                        while (pos < curLen && ((unsigned char)editorText[pos] & 0xC0) == 0x80) { pos++; }
                        cursorPos = pos;
                    }
                }
                else if (event.key.keysym.sym == SDLK_ESCAPE) {
                    selectionStart = selectionEnd = -1;
                    mouseSelecting = 0;
                }
                else if (event.key.keysym.sym == SDLK_c && (event.key.keysym.mod & KMOD_GUI)) {
                    if (SDL_SetClipboardText(editorText) == 0)
                        printf("Text copied to clipboard.\n");
                    else
                        printf("Clipboard error: %s\n", SDL_GetError());
                }
                else if (event.key.keysym.sym == SDLK_v && (event.key.keysym.mod & KMOD_GUI)) {
                    char *clipboard_text = SDL_GetClipboardText();
                    if (clipboard_text) {
                        int pasteLen = (int)strlen(clipboard_text);
                        int curLen = (int)strlen(editorText);
                        char *newText = malloc(curLen + pasteLen + 1);
                        if (newText) {
                            memcpy(newText, editorText, cursorPos);
                            memcpy(newText + cursorPos, clipboard_text, pasteLen);
                            memcpy(newText + cursorPos + pasteLen, editorText + cursorPos, curLen - cursorPos + 1);
                            free(editorText);
                            editorText = newText;
                            cursorPos += pasteLen;
                            update_render_data(renderer, font, editorText, margin, maxTextWidth, &rd);
                        }
                        SDL_free(clipboard_text);
                    }
                }
                else if (event.key.keysym.sym == SDLK_s) {
                    if (selectionStart < 0) selectionStart = cursorPos;
                }
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                debug_print(L"Mouse down at (%d, %d)\n", event.button.x, event.button.y);
                if (SDL_PointInRect(&(SDL_Point){event.button.x, event.button.y}, &rd.textRect)) {
                    int relativeX = event.button.x - rd.textRect.x;
                    int nearestCluster = 0;
                    int minDist = INT_MAX;
                    if (rd.numClusters > 0) {
                        for (int i = 0; i < rd.numClusters; i++) {
                            if (i < rd.numGlyphs) {
                                int clusterX = rd.glyphOffsets[i];
                                int dist = abs(clusterX - relativeX);
                                if (dist < minDist) {
                                    minDist = dist;
                                    nearestCluster = i;
                                }
                            }
                        }
                        selectionStart = nearestCluster;
                        if (nearestCluster < rd.numClusters) {
                            cursorPos = rd.clusterByteIndices[nearestCluster];
                        }
                        mouseSelecting = 1;
                        debug_print(L"Selection started at cluster %d (byte offset %d)\n", nearestCluster, cursorPos);
                    }
                }
            }
            else if (event.type == SDL_MOUSEMOTION && mouseSelecting) {
                int relativeX = event.motion.x - rd.textRect.x;
                int nearestCluster = 0;
                int minDist = INT_MAX;
                for (int i = 0; i < rd.numClusters; i++) {
                    int clusterX = rd.glyphOffsets[i];
                    int dist = abs(clusterX - relativeX);
                    if (dist < minDist) {
                        minDist = dist;
                        nearestCluster = i;
                    }
                }
                selectionEnd = nearestCluster;
                cursorPos = rd.clusterByteIndices[nearestCluster];
                debug_print(L"Selection updated: %d to %d\n", selectionStart, selectionEnd);
            }
            else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                mouseSelecting = 0;
                if (selectionStart >= 0) {
                    selectionEnd = get_cluster_index_at_cursor(editorText, cursorPos, rd.clusterByteIndices, rd.numClusters);
                    debug_print(L"Selection ended at cluster %d\n", selectionEnd);
                }
            }
        } // End event poll

        debug_print(L"[FRAME %d] Update check - Last width: %d, Current width: %d\n", frame_count, lastWidth, windowWidth);

        if (lastText) {
            uint32_t content_hash = 0;
            for (const char *p = editorText; *p; p++) {
                content_hash = ((content_hash << 5) + content_hash) + *p;
            }
            if (content_hash != state.last_content_hash || windowWidth != state.last_width) {
                debug_print(L"[STATE] Content change detected (hash: %u -> %u)\n", state.last_content_hash, content_hash);
                state.needs_update = true;
                state.last_content_hash = content_hash;
                state.last_width = windowWidth;
            }
        }

        if (state.needs_update) {
            debug_print(L"[RENDER] Updating text layout\n");
            update_render_data(renderer, font, editorText, margin, maxTextWidth, &rd);
            state.needs_update = false;
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 22, 24, 32, 255);
        SDL_RenderClear(renderer);

        SDL_RenderCopy(renderer, rd.textTexture, NULL, &rd.textRect);

        if (mouseSelecting && selectionStart >= 0 && selectionEnd >= 0 && selectionStart != selectionEnd) {
            int startIdx = selectionStart < selectionEnd ? selectionStart : selectionEnd;
            int endIdx = selectionStart < selectionEnd ? selectionEnd : selectionStart;
            if (startIdx < rd.numClusters && endIdx < rd.numClusters) {
                SDL_Rect hl = {
                    rd.textRect.x + rd.glyphOffsets[startIdx],
                    rd.textRect.y,
                    rd.glyphOffsets[endIdx] - rd.glyphOffsets[startIdx] + rd.clusterRects[endIdx].w,
                    rd.textRect.h
                };
                debug_print(L"Highlight rect: x=%d, y=%d, w=%d, h=%d\n", hl.x, hl.y, hl.w, hl.h);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 200, 200, 180, 128);
                SDL_RenderFillRect(renderer, &hl);
            }
        }

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
        
        uint32_t frame_time = SDL_GetTicks() - frame_start;
        debug_print(L"[TIMING] Frame time: %dms\n", frame_time);
        
        if (frame_time < 16) {
            SDL_Delay(16 - frame_time);
        }
    } // End main loop

    SDL_StopTextInput();

    free(lastText);
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
