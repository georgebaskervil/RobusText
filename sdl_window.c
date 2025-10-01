#include "sdl_window.h"
#include "auto_save.h"
#include "debug.h"
#include "dialog.h"
#include "file_operations.h"
#include "line_numbers.h"
#include "search_system.h"
#include "status_bar.h"
#include "text_renderer.h"
#include "undo_system.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <ctype.h>
#include <limits.h> // Add this for INT_MAX
#include <pthread.h>
#include <stdbool.h> // Added for bool, true, false
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COMBINING_PER_CLUSTER 5 // limit combining marks per cluster

// Continuous resize support structures
typedef struct {
    SDL_Event event;
    bool valid;
} EventMessage;

typedef struct {
    EventMessage messages[256];
    int read_pos;
    int write_pos;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool should_exit;
} EventQueue;

// Global state for continuous resize
static EventQueue g_event_queue = {0};
static pthread_t g_render_thread;
static bool g_continuous_resize_active = false;

// Rendering context shared between threads
typedef struct {
    SDL_Renderer *renderer;
    TTF_Font *font;
    TTF_Font *status_font;
    SDL_Window *window;
    char **editorText;
    int *cursorPos;
    int *selectionStart;
    int *selectionEnd;
    int *windowWidth;
    int *windowHeight;
    bool *running;
    DocumentState *document;
    UndoSystem *undo;
    SearchState *search;
    StatusBar *status_bar;
    LineNumbers *line_numbers;
    AutoSave *auto_save;
    RenderData *rd;
    bool *search_mode;
    char *search_buffer;
    int *search_buffer_pos;
    int *mouseSelecting;
    int margin;
    int *maxTextWidth;
    int *text_area_height;
    int *text_area_x;
    int *text_area_y;
} RenderContext;

static RenderContext g_render_context = {0};
/* Flag to indicate the render context is ready for the emscripten callback. */
static volatile int g_emscripten_ready = 0;

// Forward declarations
static void render_frame(RenderContext *ctx);

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Minimal Emscripten main loop callback: poll events and render one frame.
static void emscripten_frame_callback(void *arg)
{
    if (!g_emscripten_ready)
        return;
    RenderContext *ctx = (RenderContext *) arg;
    SDL_Event event;

    /* Removed noisy per-frame logs - uncomment to debug frame callback */
    // debug_print(L"[EMSCRIPTEN] frame callback invoked\n");
    // printf("[EMSCRIPTEN] frame callback invoked\n");

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            if (ctx->document && ctx->document->is_modified) {
                debug_print(L"Warning: Closing with unsaved changes\n");
            }
            *ctx->running = false;
            emscripten_cancel_main_loop();
            return;
        }

        if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                                              event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
            int new_w = event.window.data1;
            int new_h = event.window.data2;
            if (ctx->windowWidth)
                *ctx->windowWidth = new_w;
            if (ctx->windowHeight)
                *ctx->windowHeight = new_h;
            SDL_SetTextInputRect(&(SDL_Rect){0, 0, new_w, new_h});
        }

        /* Log and HANDLE keyboard events */
        if (event.type == SDL_TEXTINPUT) {
            printf("[SDL] TEXTINPUT: %s\n", event.text.text);
#ifdef __EMSCRIPTEN__
            EM_ASM({ console.log('[SDL] TEXTINPUT:', UTF8ToString($0)); }, event.text.text);
            
            // Actually handle the text input - insert it into the editor
            if (ctx->editorText && ctx->cursorPos) {
                char *text = *ctx->editorText;
                int cursorPos = *ctx->cursorPos;
                
                // Record undo action
                if (ctx->undo) {
                    record_insert_action(ctx->undo, cursorPos, event.text.text, cursorPos,
                                       cursorPos + strlen(event.text.text));
                }
                
                // Invalidate cache if using lazy mode
                if (ctx->rd && ctx->rd->lazy_mode) {
                    int cluster_at_cursor = get_cluster_index_at_cursor(text, cursorPos, ctx->rd);
                    invalidate_cluster_blocks_after(ctx->rd, cluster_at_cursor);
                }
                
                // Insert the text
                int textLen = strlen(text);
                int insertLen = strlen(event.text.text);
                char *newText = malloc(textLen + insertLen + 1);
                if (newText) {
                    memcpy(newText, text, cursorPos);
                    memcpy(newText + cursorPos, event.text.text, insertLen);
                    memcpy(newText + cursorPos + insertLen, text + cursorPos, textLen - cursorPos + 1);
                    free(text);
                    *ctx->editorText = newText;
                    *ctx->cursorPos = cursorPos + insertLen;
                    
                    // Mark document as modified
                    if (ctx->document) {
                        mark_document_modified(ctx->document, true);
                    }
                    
                    // Update render data to reflect the new text
                    if (ctx->renderer && ctx->font && ctx->text_area_x && ctx->text_area_y && ctx->maxTextWidth) {
                        update_render_data(ctx->renderer, ctx->font, *ctx->editorText,
                                         *ctx->text_area_x, *ctx->text_area_y, *ctx->maxTextWidth, ctx->rd);
                        if (ctx->rd->lazy_mode && ctx->text_area_height) {
                            printf("[DEBUG] Calling prepare_visible_texture: text_area_height=%d scrollY=%d\n",
                                   *ctx->text_area_height, ctx->rd->scrollY);
                            prepare_visible_texture(ctx->renderer, ctx->font, *ctx->editorText,
                                                  *ctx->text_area_x, *ctx->text_area_y, *ctx->maxTextWidth,
                                                  ctx->rd, ctx->rd->scrollY, *ctx->text_area_height);
                        }
                    }
                    
                    printf("[SDL] Text inserted, new cursor pos: %d\n", *ctx->cursorPos);
                }
            }
#endif
        } else if (event.type == SDL_KEYDOWN) {
            printf("[SDL] KEYDOWN: sym=%d scancode=%d\n", event.key.keysym.sym, event.key.keysym.scancode);
#ifdef __EMSCRIPTEN__
            EM_ASM({ console.log('[SDL] KEYDOWN: sym=', $0); }, event.key.keysym.sym);
            
            // Handle special keys (backspace, arrows, etc.)
            if (ctx->editorText && ctx->cursorPos) {
                char *text = *ctx->editorText;
                int cursorPos = *ctx->cursorPos;
                int textLen = strlen(text);
                bool needsUpdate = false;
                
                // Backspace
                if (event.key.keysym.sym == SDLK_BACKSPACE && cursorPos > 0) {
                    // Record undo
                    if (ctx->undo) {
                        char deleted_char[2] = {text[cursorPos - 1], '\0'};
                        record_delete_action(ctx->undo, cursorPos - 1, deleted_char, cursorPos, cursorPos - 1);
                    }
                    
                    // Delete character before cursor
                    memmove(text + cursorPos - 1, text + cursorPos, textLen - cursorPos + 1);
                    *ctx->cursorPos = cursorPos - 1;
                    needsUpdate = true;
                    
                    if (ctx->document) mark_document_modified(ctx->document, true);
                    printf("[SDL] Backspace: new cursor pos %d\n", *ctx->cursorPos);
                }
                // Delete key
                else if (event.key.keysym.sym == SDLK_DELETE && cursorPos < textLen) {
                    if (ctx->undo) {
                        char deleted_char[2] = {text[cursorPos], '\0'};
                        record_delete_action(ctx->undo, cursorPos, deleted_char, cursorPos, cursorPos);
                    }
                    
                    memmove(text + cursorPos, text + cursorPos + 1, textLen - cursorPos);
                    needsUpdate = true;
                    
                    if (ctx->document) mark_document_modified(ctx->document, true);
                }
                // Arrow keys
                else if (event.key.keysym.sym == SDLK_LEFT && cursorPos > 0) {
                    *ctx->cursorPos = cursorPos - 1;
                }
                else if (event.key.keysym.sym == SDLK_RIGHT && cursorPos < textLen) {
                    *ctx->cursorPos = cursorPos + 1;
                }
                else if (event.key.keysym.sym == SDLK_UP) {
                    // Find start of current line
                    int line_start = cursorPos;
                    while (line_start > 0 && text[line_start - 1] != '\n') line_start--;
                    
                    if (line_start > 0) {
                        // Find start of previous line
                        int prev_line_start = line_start - 1;
                        while (prev_line_start > 0 && text[prev_line_start - 1] != '\n') prev_line_start--;
                        
                        int col = cursorPos - line_start;
                        int prev_line_len = line_start - 1 - prev_line_start;
                        *ctx->cursorPos = prev_line_start + (col < prev_line_len ? col : prev_line_len);
                    }
                }
                else if (event.key.keysym.sym == SDLK_DOWN) {
                    // Find start and end of current line
                    int line_start = cursorPos;
                    while (line_start > 0 && text[line_start - 1] != '\n') line_start--;
                    
                    int line_end = cursorPos;
                    while (line_end < textLen && text[line_end] != '\n') line_end++;
                    
                    if (line_end < textLen) {
                        int next_line_start = line_end + 1;
                        int next_line_end = next_line_start;
                        while (next_line_end < textLen && text[next_line_end] != '\n') next_line_end++;
                        
                        int col = cursorPos - line_start;
                        int next_line_len = next_line_end - next_line_start;
                        *ctx->cursorPos = next_line_start + (col < next_line_len ? col : next_line_len);
                    }
                }
                // Enter/Return
                else if (event.key.keysym.sym == SDLK_RETURN) {
                    if (ctx->undo) {
                        record_insert_action(ctx->undo, cursorPos, "\n", cursorPos, cursorPos + 1);
                    }
                    
                    char *newText = malloc(textLen + 2);
                    if (newText) {
                        memcpy(newText, text, cursorPos);
                        newText[cursorPos] = '\n';
                        memcpy(newText + cursorPos + 1, text + cursorPos, textLen - cursorPos + 1);
                        free(text);
                        *ctx->editorText = newText;
                        *ctx->cursorPos = cursorPos + 1;
                        needsUpdate = true;
                        
                        if (ctx->document) mark_document_modified(ctx->document, true);
                    }
                }
                
                // Update render if needed
                if (needsUpdate && ctx->renderer && ctx->font && ctx->text_area_x && 
                    ctx->text_area_y && ctx->maxTextWidth) {
                    update_render_data(ctx->renderer, ctx->font, *ctx->editorText,
                                     *ctx->text_area_x, *ctx->text_area_y, *ctx->maxTextWidth, ctx->rd);
                    if (ctx->rd->lazy_mode && ctx->text_area_height) {
                        prepare_visible_texture(ctx->renderer, ctx->font, *ctx->editorText,
                                              *ctx->text_area_x, *ctx->text_area_y, *ctx->maxTextWidth,
                                              ctx->rd, ctx->rd->scrollY, *ctx->text_area_height);
                    }
                }
            }
#endif
        }
    }

    render_frame(ctx);
}
#endif

// Event queue functions
static void init_event_queue(EventQueue *queue)
{
    memset(queue, 0, sizeof(EventQueue));
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

static void cleanup_event_queue(EventQueue *queue)
{
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

static void push_event(EventQueue *queue, const SDL_Event *event)
{
    pthread_mutex_lock(&queue->mutex);

    int next_write = (queue->write_pos + 1) % 256;
    if (next_write != queue->read_pos) {
        queue->messages[queue->write_pos].event = *event;
        queue->messages[queue->write_pos].valid = true;
        queue->write_pos = next_write;
        pthread_cond_signal(&queue->cond);
    }

    pthread_mutex_unlock(&queue->mutex);
}

static bool pop_event(EventQueue *queue, SDL_Event *event)
{
    pthread_mutex_lock(&queue->mutex);

    while (queue->read_pos == queue->write_pos && !queue->should_exit) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }

    if (queue->should_exit) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }

    if (queue->read_pos != queue->write_pos) {
        *event = queue->messages[queue->read_pos].event;
        queue->read_pos = (queue->read_pos + 1) % 256;
        pthread_mutex_unlock(&queue->mutex);
        return true;
    }

    pthread_mutex_unlock(&queue->mutex);
    return false;
}

static void shutdown_event_queue(EventQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    queue->should_exit = true;
    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

// SDL Event Watch callback - called immediately when events are received
static int event_watch_callback(void *userdata, SDL_Event *event)
{
    (void) userdata;

    if (!g_continuous_resize_active) {
        return 0;
    }

    // Push all events to our queue for the render thread
    push_event(&g_event_queue, event);

    // Return 0 to allow normal event processing
    return 0;
}

// Render thread function
static void *render_thread_func(void *arg)
{
    RenderContext *ctx = (RenderContext *) arg;
    SDL_Event event;

    debug_print(L"Render thread started\n");

    while (*ctx->running) {
        // Process events from the queue
        bool events_processed = false;
        while (pop_event(&g_event_queue, &event)) {
            events_processed = true;

            if (event.type == SDL_QUIT) {
                *ctx->running = false;
                break;
            } else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    // Handle resize immediately in render thread
                    *ctx->windowWidth = event.window.data1;
                    *ctx->windowHeight = event.window.data2;

                    debug_print(L"Render thread handling resize: %dx%d\n", *ctx->windowWidth,
                                *ctx->windowHeight);

                    // Update SDL Text Input Rect for the new window size
                    SDL_SetTextInputRect(&(SDL_Rect){0, 0, *ctx->windowWidth, *ctx->windowHeight});

                    *ctx->text_area_height = *ctx->windowHeight - ctx->status_bar->height;

                    int line_numbers_width = get_line_numbers_width(ctx->line_numbers);
                    *ctx->maxTextWidth = *ctx->windowWidth - (2 * ctx->margin) - line_numbers_width;
                    *ctx->text_area_x = line_numbers_width + ctx->margin;
                    *ctx->text_area_y = ctx->margin;

                    resize_line_numbers(ctx->line_numbers, *ctx->windowHeight);
                    ctx->status_bar->rect.y = *ctx->windowHeight - ctx->status_bar->height;

                    SDL_RenderSetLogicalSize(ctx->renderer, *ctx->windowWidth, *ctx->windowHeight);

                    ctx->status_bar->needs_update = true;
                }
            } else if (event.type == SDL_MOUSEWHEEL) {
                // Handle wheel events in render thread as well (continuous resize mode)
                int line_h = TTF_FontLineSkip(ctx->font);
                ctx->rd->scrollY -= event.wheel.y * line_h * 3;
                if (ctx->rd->scrollY < 0)
                    ctx->rd->scrollY = 0;
                if (ctx->rd->textH > 0 &&
                    ctx->rd->scrollY > ctx->rd->textH - *ctx->text_area_height)
                    ctx->rd->scrollY = ctx->rd->textH - *ctx->text_area_height;
                if (ctx->rd->lazy_mode)
                    prepare_visible_texture(ctx->renderer, ctx->font, *ctx->editorText,
                                            *ctx->text_area_x, *ctx->text_area_y,
                                            *ctx->maxTextWidth, ctx->rd, ctx->rd->scrollY,
                                            *ctx->text_area_height);
            }
        }

        // Always render frame to maintain smooth updates during resize
        render_frame(ctx);

        // Cap frame rate - shorter delay when processing events for responsiveness
        SDL_Delay(events_processed ? 8 : 16); // Higher FPS when actively resizing
    }

    debug_print(L"Render thread exiting\n");
    return NULL;
}

// Render a single frame
static void render_frame(RenderContext *ctx)
{
    /* Removed noisy per-frame logs - uncomment to debug rendering */
    // EM_ASM({ console.log('[EMSCRIPTEN] render_frame start'); });
    // printf("[EMSCRIPTEN] render_frame start\n");

    SDL_Renderer *renderer = ctx->renderer;
    TTF_Font *font = ctx->font;
    RenderData *rd = ctx->rd;
    int cursorPos = *ctx->cursorPos;
    int selectionStart = *ctx->selectionStart;
    int selectionEnd = *ctx->selectionEnd;
    char *editorText = *ctx->editorText;
    SearchState *search = ctx->search;

    // Update content hash for change detection
    static uint32_t last_content_hash = 0;
    static int last_width = 0;
    static int last_cursor_pos = -1;
    static int last_selection_start = -1;
    static int last_selection_end = -1;
    static bool needs_update = true;

    uint32_t content_hash = 0;
    for (const char *p = editorText; *p; p++) {
        content_hash = ((content_hash << 5) + content_hash) + *p;
    }

    // Check for any changes that require re-rendering
    bool content_changed = (content_hash != last_content_hash);
    bool layout_changed = (*ctx->windowWidth != last_width);
    bool cursor_changed = (*ctx->cursorPos != last_cursor_pos);
    bool selection_changed =
        (*ctx->selectionStart != last_selection_start || *ctx->selectionEnd != last_selection_end);

    if (content_changed || layout_changed || cursor_changed || selection_changed ||
        ctx->status_bar->needs_update || needs_update) {
        /* Removed noisy render logs - uncomment to debug rendering paths */
        // EM_ASM({ console.log('[EMSCRIPTEN] about to update_render_data'); });
        // printf("[EMSCRIPTEN] about to update_render_data\n");

        if (content_changed || layout_changed) {
            update_render_data(renderer, font, editorText, *ctx->text_area_x, *ctx->text_area_y,
                               *ctx->maxTextWidth, rd);
            /* Removed noisy render logs */
            // EM_ASM({ console.log('[EMSCRIPTEN] update_render_data returned'); });
            // printf("[EMSCRIPTEN] update_render_data returned\n");

            if (rd->lazy_mode) {
                prepare_visible_texture(renderer, font, editorText, *ctx->text_area_x,
                                        *ctx->text_area_y, *ctx->maxTextWidth, rd, rd->scrollY,
                                        *ctx->text_area_height);
            }
        }

        // Update tracking variables
        last_content_hash = content_hash;
        last_width = *ctx->windowWidth;
        last_cursor_pos = *ctx->cursorPos;
        last_selection_start = *ctx->selectionStart;
        last_selection_end = *ctx->selectionEnd;
        needs_update = false;
    }

    // Update status bar
    update_status_bar(ctx->status_bar, renderer, ctx->document, search, cursorPos, editorText,
                      *ctx->windowWidth);

    // Update line numbers
    int font_height = TTF_FontLineSkip(font);
    int line_numbers_area_height = *ctx->windowHeight - ctx->status_bar->height;
    int visible_lines = line_numbers_area_height / font_height;
    update_line_numbers(ctx->line_numbers, renderer, editorText, 1, visible_lines);
    ctx->line_numbers->rect.y = 0;

    // Clear screen
    /* Removed noisy render logs */
    // EM_ASM({ console.log('[EMSCRIPTEN] clearing screen'); });
    // printf("[EMSCRIPTEN] clearing screen\n");

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 22, 24, 32, 255);
    SDL_RenderClear(renderer);

    // Calculate cursor line early so we can ensure it is visible (adjust scrollY)
    int cursor_font_height = TTF_FontLineSkip(font);
    int cursor_line = 0;
    int line_start_pos = 0;
    for (int i = 0; i < cursorPos && editorText[i]; i++) {
        if (editorText[i] == '\n') {
            cursor_line++;
            line_start_pos = i + 1;
        }
    }

    // Ensure scrollY is within valid bounds
    if (rd->scrollY < 0)
        rd->scrollY = 0;
    if (rd->textH > 0 && *ctx->text_area_height > 0) {
        int max_scroll = rd->textH - *ctx->text_area_height;
        if (max_scroll < 0)
            max_scroll = 0; // Text fits in viewport, no scrolling needed
        if (rd->scrollY > max_scroll)
            rd->scrollY = max_scroll;
    }

    // Adjust scroll to make cursor visible (simple policy: if cursor above or below viewport, snap)
    int cursorY_for_scroll = rd->textRect.y + (cursor_line * cursor_font_height);
    int viewTop = rd->textRect.y;
    int viewBottom = rd->textRect.y + *ctx->text_area_height - cursor_font_height;
    if (cursorY_for_scroll < viewTop) {
        rd->scrollY = 0; // scroll to top
    } else if (cursorY_for_scroll > viewBottom) {
        // Move scroll so cursor line is visible at bottom
        int desired =
            cursorY_for_scroll - rd->textRect.y - (*ctx->text_area_height - cursor_font_height);
        if (desired < 0)
            desired = 0;
        rd->scrollY = desired;
    }

    // Render text using floating-point positioning for macOS-like precision
    if (rd && rd->textTexture && rd->textRect.w > 0 && rd->textRect.h > 0 && rd->textRect.x >= 0 &&
        rd->textRect.y >= 0) {
        /* Removed noisy render logs */
        // EM_ASM({ console.log('[EMSCRIPTEN] about to render texture'); });
        // printf("[EMSCRIPTEN] about to render texture\n");

        // Source rect selects the visible portion of the texture based on scrollY
        // Use the actual texture height, not viewport height, to avoid stretching
        int src_h = rd->textRect.h < *ctx->text_area_height ? rd->textRect.h : *ctx->text_area_height;
        SDL_Rect src = {0, rd->scrollY, rd->textRect.w, src_h};
        SDL_FRect dst = {(float) rd->textRect.x, (float) rd->textRect.y, (float) rd->textRect.w,
                         (float) src_h};
#ifdef __EMSCRIPTEN__
        // Debug: log render dimensions to diagnose stretching
        static int log_count = 0;
        if (log_count++ < 3) {
            printf("[DEBUG] Render: src={%d,%d,%d,%d} dst={%.0f,%.0f,%.0f,%.0f} tex={%d,%d}\n",
                   src.x, src.y, src.w, src.h, dst.x, dst.y, dst.w, dst.h, rd->textRect.w, rd->textRect.h);
        }
#endif
        SDL_RenderCopyF(renderer, rd->textTexture, &src, &dst);
    }

    // Render selection highlight
    if (selectionStart >= 0 && selectionEnd >= 0 && selectionStart != selectionEnd) {
        /* Removed noisy selection render logs */
        // EM_ASM({ console.log('[EMSCRIPTEN] rendering selection start=%d end=%d', $0, $1); }, selectionStart, selectionEnd);
        // printf("[EMSCRIPTEN] rendering selection start=%d end=%d\n", selectionStart, selectionEnd);

        int startIdx = selectionStart < selectionEnd ? selectionStart : selectionEnd;
        int endIdx = selectionStart < selectionEnd ? selectionEnd : selectionStart;
        if (startIdx < rd->numClusters && endIdx < rd->numClusters) {
            int start_x = 0, end_x = 0;
            if (rd->glyphOffsets && startIdx < rd->numGlyphs)
                start_x = rd->glyphOffsets[startIdx];
            else if (rd->clusterRects)
                start_x = rd->clusterRects[startIdx].x;
            if (rd->glyphOffsets && endIdx < rd->numGlyphs)
                end_x = rd->glyphOffsets[endIdx];
            else if (rd->clusterRects)
                end_x = rd->clusterRects[endIdx].x + rd->clusterRects[endIdx].w;

            SDL_Rect hl = {rd->textRect.x + start_x, rd->textRect.y, end_x - start_x,
                           rd->textRect.h};
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 200, 200, 180, 128);
            SDL_RenderFillRect(renderer, &hl);
        }
    }

    // Render search highlights
    if (search->is_active && has_matches(search)) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < search->num_matches; i++) {
            int match_pos = search->match_positions[i];
            int cluster_idx = get_cluster_index_at_cursor(editorText, match_pos, rd);

            if (cluster_idx < rd->numClusters) {
                SDL_Rect search_hl = {rd->textRect.x + rd->glyphOffsets[cluster_idx],
                                      rd->textRect.y,
                                      search->match_lengths[i] * 8, // Approximate width
                                      rd->textRect.h};

                if (i == search->current_match) {
                    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 100); // Yellow for current match
                } else {
                    SDL_SetRenderDrawColor(renderer, 255, 200, 0, 80); // Orange for other matches
                }
                SDL_RenderFillRect(renderer, &search_hl);
            }
        }
    }

    // Render cursor (cursor_line and line_start_pos were computed earlier)
    // Calculate cursor position on the current line
    int cursor_pos_in_line = cursorPos - line_start_pos;

    // Get the width of text from line start to cursor
    char temp_line[1024] = {0};
    int copy_len = cursor_pos_in_line;
    if (copy_len > 1023)
        copy_len = 1023;
    memcpy(temp_line, editorText + line_start_pos, copy_len);

    int cursorX = rd->textRect.x;
    if (strlen(temp_line) > 0) {
        int text_width;
        TTF_SizeUTF8(font, temp_line, &text_width, NULL);
        cursorX += text_width;
    }

    int cursorY = rd->textRect.y + (cursor_line * cursor_font_height);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDrawLine(renderer, cursorX, cursorY, cursorX, cursorY + cursor_font_height);

    // Render line numbers
    ctx->line_numbers->rect.y = 0; // Line numbers go all the way to the top
    render_line_numbers(ctx->line_numbers, renderer);

    // Render status bar
    render_status_bar(ctx->status_bar, renderer);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_RenderPresent(renderer);
}

// Move cursor to previous word
static int move_cursor_word_left(const char *text, int cursor_pos)
{
    if (cursor_pos <= 0)
        return 0;

    int pos = cursor_pos - 1;

    // Skip whitespace
    while (pos > 0 && isspace(text[pos])) {
        pos--;
    }

    // Skip non-whitespace
    while (pos > 0 && !isspace(text[pos])) {
        pos--;
    }

    // If we stopped on whitespace, move forward one
    if (pos > 0 && isspace(text[pos])) {
        pos++;
    }

    return pos;
}

// Move cursor to next word
static int move_cursor_word_right(const char *text, int cursor_pos)
{
    int text_len = strlen(text);
    if (cursor_pos >= text_len)
        return text_len;

    int pos = cursor_pos;

    // Skip non-whitespace
    while (pos < text_len && !isspace(text[pos])) {
        pos++;
    }

    // Skip whitespace
    while (pos < text_len && isspace(text[pos])) {
        pos++;
    }

    return pos;
}

// Move cursor to beginning of line
static int move_cursor_line_start(const char *text, int cursor_pos)
{
    int pos = cursor_pos;
    while (pos > 0 && text[pos - 1] != '\n') {
        pos--;
    }
    return pos;
}

// Move cursor to end of line
static int move_cursor_line_end(const char *text, int cursor_pos)
{
    int text_len = strlen(text);
    int pos = cursor_pos;
    while (pos < text_len && text[pos] != '\n') {
        pos++;
    }
    return pos;
}

// Delete selected text and return new cursor position
static int delete_selection(char **text, int selection_start, int selection_end,
                            const int *cluster_byte_indices, int num_clusters)
{
    if (selection_start < 0 || selection_end < 0 || selection_start == selection_end) {
        return -1; // No selection
    }

    int start_idx = selection_start < selection_end ? selection_start : selection_end;
    int end_idx = selection_start < selection_end ? selection_end : selection_start;

    if (start_idx >= num_clusters || end_idx >= num_clusters) {
        return -1;
    }

    int start_byte = cluster_byte_indices[start_idx];
    int end_byte = (end_idx + 1 < num_clusters) ? cluster_byte_indices[end_idx + 1] : strlen(*text);

    int text_len = strlen(*text);
    char *new_text = malloc(text_len - (end_byte - start_byte) + 1);
    if (!new_text)
        return -1;

    memcpy(new_text, *text, start_byte);
    memcpy(new_text + start_byte, *text + end_byte, text_len - end_byte + 1);

    free(*text);
    *text = new_text;

    return start_byte;
}

// Lazy deletion helper that works with RenderData (does not require a full cluster
// byte indices array to be present). Returns new cursor byte offset or -1.
static int delete_selection_lazy(char **text, int selection_start, int selection_end,
                                 RenderData *rd)
{
    if (!rd || !text)
        return -1;
    if (selection_start < 0 || selection_end < 0 || selection_start == selection_end) {
        return -1; // No selection
    }

    int start_idx = selection_start < selection_end ? selection_start : selection_end;
    int end_idx = selection_start < selection_end ? selection_end : selection_start;

    int start_byte = get_cluster_byte_offset(rd, *text, start_idx);
    int end_byte = get_cluster_byte_offset(rd, *text, end_idx + 1);
    if (start_byte < 0)
        return -1;
    if (end_byte < 0)
        end_byte = strlen(*text);

    int text_len = strlen(*text);
    char *new_text = malloc(text_len - (end_byte - start_byte) + 1);
    if (!new_text)
        return -1;

    memcpy(new_text, *text, start_byte);
    memcpy(new_text + start_byte, *text + end_byte, text_len - end_byte + 1);

    free(*text);
    *text = new_text;

    return start_byte;
}

// Simple file picker (basic implementation)
static char *simple_file_picker(bool is_save)
{
    // Use the new dialog system for consistency
    return get_file_dialog(is_save);
}

void display_text_window(const char *font_path, int font_size, const char *initial_file)
{
    debug_print(L"Entering display_text_window\n");
#ifdef __EMSCRIPTEN__
    EM_ASM({ console.log('[EMSCRIPTEN] display_text_window() entry'); });
#endif

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { /* error handling */
        return;
    }
#ifdef __EMSCRIPTEN__
    {
        int sdl_init_ret = 0; /* SDL_Init already called - assume success path */
        EM_ASM({ console.log('[EMSCRIPTEN] SDL_Init returned (assumed 0)'); });
        printf("[EMSCRIPTEN] SDL_Init called\n");
        fflush(stdout);
    }
#endif

#ifdef __EMSCRIPTEN__
    /* Register a safe stub main loop early so that runtime calls like
       eglSwapInterval/_emscripten_set_main_loop_timing (invoked by SDL
       during renderer/window setup) do not fail. The callback is guarded
       by g_emscripten_ready and will return until the context is ready. */
    emscripten_set_main_loop_arg(emscripten_frame_callback, &g_render_context, 0, 0);
#endif

    // Enable macOS-like text rendering with stem darkening
    setenv("FREETYPE_PROPERTIES", "autofitter:no-stem-darkening=0 cff:no-stem-darkening=0", 1);

    if (TTF_Init() != 0) { /* error handling */
        SDL_Quit();
        return;
    }
#ifdef __EMSCRIPTEN__
    EM_ASM({ console.log('[EMSCRIPTEN] TTF_Init completed'); });
#endif

    SDL_Window *window =
        SDL_CreateWindow("RobusText Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 900,
                         700, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_RaiseWindow(window);
    SDL_SetTextInputRect(&(SDL_Rect){0, 0, 900, 700});
    if (!window) { /* error handling */
        TTF_Quit();
        SDL_Quit();
        return;
    }
#ifdef __EMSCRIPTEN__
    EM_ASM({ console.log('[EMSCRIPTEN] SDL_CreateWindow succeeded'); });
    /* Make canvas focusable, attach key listeners for diagnosis, and ensure
       clicking focuses it so keyboard events are delivered to SDL. */
    EM_ASM({
        var canvas = Module['canvas'] || document.querySelector('canvas');
        if (canvas) {
            canvas.setAttribute('tabindex', '0');
            canvas.focus();
            console.log('[EMSCRIPTEN] canvas focused for keyboard events');
            
            // Attach click listener to refocus on click
            canvas.addEventListener('click', function() { 
                canvas.focus(); 
                console.log('[EMSCRIPTEN] canvas clicked and focused'); 
            });
            canvas.addEventListener('focus', function() { 
                console.log('[EMSCRIPTEN] canvas focus event'); 
            });
        } else {
            console.log('[EMSCRIPTEN] canvas not found');
        }
        
        // REMOVED window-level key listeners that were blocking SDL
        // SDL needs to receive events on the canvas, not have them
        // intercepted at the window level
    });
#endif
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) { /* error handling */
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return;
    }
#ifdef __EMSCRIPTEN__
    EM_ASM({ console.log('[EMSCRIPTEN] SDL_CreateRenderer succeeded'); });
#endif
    // Set logical rendering size to window dimensions to handle high-DPI scaling
    SDL_RenderSetLogicalSize(renderer, 900, 700);

    TTF_Font *font = TTF_OpenFont(font_path, font_size);
    TTF_Font *status_font = TTF_OpenFont(font_path, font_size - 4); // Smaller font for status
    if (!font || !status_font) {
#ifdef __EMSCRIPTEN__
        EM_ASM({ console.log('[EMSCRIPTEN] Failed to open fonts'); });
#endif
    } else {
#ifdef __EMSCRIPTEN__
        EM_ASM({ console.log('[EMSCRIPTEN] Fonts loaded'); });
#endif
    }
    if (!font || !status_font) {
        debug_print(L"[ERROR] Failed to open font: %hs\n", TTF_GetError());
        if (font)
            TTF_CloseFont(font);
        if (status_font)
            TTF_CloseFont(status_font);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return;
    }
    // Apply macOS-like text rendering: disable hinting for natural glyph shapes
    TTF_SetFontHinting(font, TTF_HINTING_NONE);
    TTF_SetFontHinting(status_font, TTF_HINTING_NONE);

    // Initialize dialog context for GUI dialogs
    set_dialog_context(renderer, font, window);

    const int margin = 20;
    int windowWidth = 900, windowHeight = 700;

    // Initialize all systems first to get line numbers width
    DocumentState document;
    init_document_state(&document);

    UndoSystem undo;
    init_undo_system(&undo, 100); // Keep 100 undo actions

    SearchState search;
    init_search_state(&search);

    StatusBar status_bar;
    init_status_bar(&status_bar, renderer, status_font, windowWidth, windowHeight);

    LineNumbers line_numbers;
    init_line_numbers(&line_numbers, renderer, font, windowHeight);

    AutoSave auto_save;
    init_auto_save(&auto_save, 30000); // Auto-save every 30 seconds

    // Calculate text area dimensions accounting for line numbers
    int line_numbers_width = get_line_numbers_width(&line_numbers);
    int maxTextWidth = windowWidth - (2 * margin) - line_numbers_width;
    int text_area_height = windowHeight - status_bar.height;
    int text_area_x = line_numbers_width + margin; // Start text after line numbers
    int text_area_y = margin;                      // Added to align text with line numbers

    RenderData rd = {0};

    // Use a dynamic buffer for editable text. Start with empty text or load initial file
    char *editorText = NULL;
    if (initial_file) {
        char *content = NULL;
        if (open_file((char *) initial_file, &content)) {
            editorText = content;
            set_document_filename(&document, strdup(initial_file));
            mark_document_modified(&document, false);
        } else {
            // Fall back to empty text if open failed
            editorText = strdup("");
        }
    } else {
        editorText = strdup("");
    }
    if (!editorText) { /* error handling */
        return;
    }
    int cursorPos = 0;
    int selectionStart = -1, selectionEnd = -1;
    int mouseSelecting = 0;
    bool search_mode = false;
    char search_buffer[256] = {0};
    int search_buffer_pos = 0;

    // Start SDL text input.
#ifdef __EMSCRIPTEN__
    /* Under Emscripten, explicitly set SDL hints to ensure keyboard events
       are captured from the canvas element. */
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
#endif
    SDL_StartTextInput();

#ifdef __EMSCRIPTEN__
    /* Verify canvas focus and keyboard setup */
    EM_ASM({
        var canvas = Module['canvas'] || document.querySelector('canvas');
        if (canvas) {
            canvas.focus();
            console.log('[EMSCRIPTEN] canvas focused, activeElement is:', document.activeElement === canvas);
            console.log('[EMSCRIPTEN] SDL keyboard hint set and text input started');
        } else {
            console.log('[EMSCRIPTEN] ERROR: canvas not found');
        }
    });
#endif

    // Initial render with line wrapping
#ifdef __EMSCRIPTEN__
    printf("[DEBUG] Before update_render_data: text_area_height=%d windowHeight=%d\n", 
           text_area_height, windowHeight);
#endif
    if (update_render_data(renderer, font, editorText, text_area_x, text_area_y, maxTextWidth,
                           &rd) != 0) {
        free(editorText);
        cleanup_document_state(&document);
        cleanup_undo_system(&undo);
        cleanup_search_state(&search);
        cleanup_status_bar(&status_bar);
        cleanup_line_numbers(&line_numbers);
        TTF_CloseFont(font);
        TTF_CloseFont(status_font);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return;
    }

    bool running = true;
    SDL_Event event;
    int lastWidth = windowWidth;
    int lastHeight = windowHeight; // ADDED: To track last processed window height
    char *lastText = NULL;

    static struct {
        uint32_t last_render;
        uint32_t last_content_hash;
        int last_width;
        bool needs_update;
    } state = {0, 0, 0, true};

    // Update window title
    char window_title[512];
    snprintf(window_title, sizeof(window_title), "%s - RobusText Editor",
             document.filename ? document.filename : "Untitled");
    SDL_SetWindowTitle(window, window_title);

    // Initialize continuous resize system
    init_event_queue(&g_event_queue);

    // Set up render context with all necessary pointers
#ifdef __EMSCRIPTEN__
    /* Allocate a single heap container for all state the emscripten callback
       will need after this function returns. This avoids multiple separate
       mallocs and reduces the chance of some pointers being NULL. */
    struct EmsHeap {
        RenderData rd;
        DocumentState document;
        UndoSystem undo;
        SearchState search;
        StatusBar status_bar;
        LineNumbers line_numbers;
        AutoSave auto_save;
        char *editorText;
        int cursorPos;
        int selectionStart;
        int selectionEnd;
        int windowWidth;
        int windowHeight;
        bool running;
        int search_buffer_pos;
        int mouseSelecting;
        int maxTextWidth;
        int text_area_height;
        int text_area_x;
        int text_area_y;
        char search_buffer[256];
    } *heap = malloc(sizeof(struct EmsHeap));

    if (heap) {
        /* Copy locals into the heap container (shallow copies where appropriate). */
        *heap = (struct EmsHeap){0};
        heap->rd = rd;
        heap->document = document;
        heap->undo = undo;
        heap->search = search;
        heap->status_bar = status_bar;
        heap->line_numbers = line_numbers;
        heap->auto_save = auto_save;
        heap->editorText = editorText;
        heap->cursorPos = cursorPos;
        heap->selectionStart = selectionStart;
        heap->selectionEnd = selectionEnd;
        heap->windowWidth = windowWidth;
        heap->windowHeight = windowHeight;
        heap->running = running;
        heap->search_buffer_pos = search_buffer_pos;
        heap->mouseSelecting = mouseSelecting;
        heap->maxTextWidth = maxTextWidth;
        heap->text_area_height = text_area_height;
        heap->text_area_x = text_area_x;
        heap->text_area_y = text_area_y;
        memcpy(heap->search_buffer, search_buffer, sizeof(heap->search_buffer));

        g_render_context.renderer = renderer;
        g_render_context.font = font;
        g_render_context.status_font = status_font;
        g_render_context.window = window;
        g_render_context.editorText = &heap->editorText;
        g_render_context.cursorPos = &heap->cursorPos;
        g_render_context.selectionStart = &heap->selectionStart;
        g_render_context.selectionEnd = &heap->selectionEnd;
        g_render_context.windowWidth = &heap->windowWidth;
        g_render_context.windowHeight = &heap->windowHeight;
        g_render_context.running = &heap->running;
        g_render_context.document = &heap->document;
        g_render_context.undo = &heap->undo;
        g_render_context.search = &heap->search;
        g_render_context.status_bar = &heap->status_bar;
        g_render_context.line_numbers = &heap->line_numbers;
        g_render_context.auto_save = &heap->auto_save;
        g_render_context.rd = &heap->rd;
        g_render_context.search_mode = &search_mode; /* small transient flag ok */
        g_render_context.search_buffer = heap->search_buffer;
        g_render_context.search_buffer_pos = &heap->search_buffer_pos;
        g_render_context.mouseSelecting = &heap->mouseSelecting;
        g_render_context.margin = margin;
        g_render_context.maxTextWidth = &heap->maxTextWidth;
        g_render_context.text_area_height = &heap->text_area_height;
        g_render_context.text_area_x = &heap->text_area_x;
        g_render_context.text_area_y = &heap->text_area_y;
    } else {
        /* Allocation failed; fall back to using locals (will likely be invalid
           after return) but proceed to avoid blocking — worst case the app
           aborts and we can diagnose. */
        g_render_context.renderer = renderer;
        g_render_context.font = font;
        g_render_context.status_font = status_font;
        g_render_context.window = window;
        g_render_context.editorText = &editorText;
        g_render_context.cursorPos = &cursorPos;
        g_render_context.selectionStart = &selectionStart;
        g_render_context.selectionEnd = &selectionEnd;
        g_render_context.windowWidth = &windowWidth;
        g_render_context.windowHeight = &windowHeight;
        g_render_context.running = &running;
        g_render_context.document = &document;
        g_render_context.undo = &undo;
        g_render_context.search = &search;
        g_render_context.status_bar = &status_bar;
        g_render_context.line_numbers = &line_numbers;
        g_render_context.auto_save = &auto_save;
        g_render_context.rd = &rd;
        g_render_context.search_mode = &search_mode;
        g_render_context.search_buffer = search_buffer;
        g_render_context.search_buffer_pos = &search_buffer_pos;
        g_render_context.mouseSelecting = &mouseSelecting;
        g_render_context.margin = margin;
        g_render_context.maxTextWidth = &maxTextWidth;
        g_render_context.text_area_height = &text_area_height;
        g_render_context.text_area_x = &text_area_x;
        g_render_context.text_area_y = &text_area_y;
    }
#else
    g_render_context.renderer = renderer;
    g_render_context.font = font;
    g_render_context.status_font = status_font;
    g_render_context.window = window;
    g_render_context.editorText = &editorText;
    g_render_context.cursorPos = &cursorPos;
    g_render_context.selectionStart = &selectionStart;
    g_render_context.selectionEnd = &selectionEnd;
    g_render_context.windowWidth = &windowWidth;
    g_render_context.windowHeight = &windowHeight;
    g_render_context.running = &running;
    g_render_context.document = &document;
    g_render_context.undo = &undo;
    g_render_context.search = &search;
    g_render_context.status_bar = &status_bar;
    g_render_context.line_numbers = &line_numbers;
    g_render_context.auto_save = &auto_save;
    g_render_context.rd = &rd;
    g_render_context.search_mode = &search_mode;
    g_render_context.search_buffer = search_buffer;
    g_render_context.search_buffer_pos = &search_buffer_pos;
    g_render_context.mouseSelecting = &mouseSelecting;
    g_render_context.margin = margin;
    g_render_context.maxTextWidth = &maxTextWidth;
    g_render_context.text_area_height = &text_area_height;
    g_render_context.text_area_x = &text_area_x;
    g_render_context.text_area_y = &text_area_y;
#endif

#ifdef __EMSCRIPTEN__
    /* Mark the render context ready so the previously-registered
       emscripten_frame_callback will begin to run. The actual main loop
       was registered early after SDL_Init to satisfy runtime timing
       calls; do not re-register here (that causes an assertion). */
    g_emscripten_ready = 1;
    debug_print(L"[EMSCRIPTEN] g_emscripten_ready set -> callback will run\n");
#ifdef __EMSCRIPTEN__
    printf("[EMSCRIPTEN] g_emscripten_ready set -> callback will run\n");
#endif
#endif

    // Start render thread (native only). Under Emscripten the browser-driven
    // main loop will handle rendering so we must not create threads or enter
    // the blocking while loop below.
#ifndef __EMSCRIPTEN__
    g_continuous_resize_active = true;
    if (pthread_create(&g_render_thread, NULL, render_thread_func, &g_render_context) != 0) {
        debug_print(L"Failed to create render thread\n");
        g_continuous_resize_active = false;
    } else {
        // Add event watch for continuous resize
        SDL_AddEventWatch(event_watch_callback, NULL);
        debug_print(L"Continuous resize system initialized\n");
    }
#else
    g_continuous_resize_active = false;
#endif

    // If continuous resize failed, fall back to regular mode
    bool use_continuous_resize = g_continuous_resize_active;

#ifdef __EMSCRIPTEN__
    /* Under Emscripten we must not block the main thread. The emscripten
       main loop was registered earlier and will drive rendering and event
       polling. Return now so the browser can schedule the callback. */
    debug_print(L"[EMSCRIPTEN] returning from display_text_window to browser main loop\n");
#ifdef __EMSCRIPTEN__
    printf("[EMSCRIPTEN] returning from display_text_window to browser main loop\n");
#endif
    return;
#endif

    while (running) {
        uint32_t frame_start = SDL_GetTicks();

        if (use_continuous_resize) {
            // In continuous resize mode, use SDL_WaitEvent to pump events
            // The render thread handles rendering and resize events
            if (SDL_WaitEvent(&event)) {
                // Process quit events specially
                if (event.type == SDL_QUIT) {
                    if (document.is_modified) {
                        debug_print(L"Warning: Closing with unsaved changes\n");
                    }
                    running = false;
                    continue;
                }

                // Skip resize events - let render thread handle them via event watch callback
                if (event.type == SDL_WINDOWEVENT &&
                    (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                     event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                    // Update main thread's knowledge of window size for other event processing
                    windowWidth = event.window.data1;
                    windowHeight = event.window.data2;
                    text_area_height = windowHeight - status_bar.height;
                    continue;
                }

                // Process all other events normally (text input, keyboard, mouse, etc.)
                goto handle_normal_event;
            }
            // In continuous resize mode, skip the normal rendering - render thread handles it
            continue;
        } else {
            // Regular mode - process events normally
            while (SDL_PollEvent(&event)) {
            handle_normal_event:
                // Mouse wheel: scroll viewport
                if (event.type == SDL_MOUSEWHEEL) {
                    int line_h = TTF_FontLineSkip(font);
                    rd.scrollY -= event.wheel.y * line_h * 3;
                    if (rd.scrollY < 0)
                        rd.scrollY = 0;
                    if (rd.textH > 0 && rd.scrollY > rd.textH - text_area_height)
                        rd.scrollY = rd.textH - text_area_height;
                    if (rd.lazy_mode)
                        prepare_visible_texture(renderer, font, editorText, text_area_x,
                                                text_area_y, maxTextWidth, &rd, rd.scrollY,
                                                text_area_height);
                    continue;
                }

                if (event.type == SDL_QUIT) {
                    if (document.is_modified) {
                        debug_print(L"Warning: Closing with unsaved changes\n");
                    }
                    running = false;
                } else if (event.type == SDL_WINDOWEVENT) {
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        int new_event_width = event.window.data1;
                        int new_event_height = event.window.data2;

                        // Check if dimensions actually changed compared to the last processed state
                        if (new_event_width != lastWidth || new_event_height != lastHeight) {
                            windowWidth = new_event_width;
                            windowHeight = new_event_height;

                            // Update SDL Text Input Rect for the new window size
                            SDL_SetTextInputRect(&(SDL_Rect){0, 0, windowWidth, windowHeight});

                            text_area_height = windowHeight - status_bar.height;

                            int line_numbers_width = get_line_numbers_width(&line_numbers);
                            maxTextWidth = windowWidth - (2 * margin) - line_numbers_width;
                            text_area_x = line_numbers_width + margin;
                            text_area_y = margin; // Added to align text with line numbers

                            resize_line_numbers(&line_numbers, windowHeight);
                            status_bar.rect.y = windowHeight - status_bar.height;

                            SDL_RenderSetLogicalSize(renderer, windowWidth, windowHeight);
                            update_render_data(renderer, font, editorText, text_area_x, text_area_y,
                                               maxTextWidth, &rd);

                            lastWidth = windowWidth;   // Update last known width
                            lastHeight = windowHeight; // Update last known height

                            status_bar.needs_update =
                                true; // Moved inside: update status bar if dimensions changed
                        }

                    /* Debug key/text events to verify keyboard input arrives */
                    if (event.type == SDL_TEXTINPUT) {
                #ifdef __EMSCRIPTEN__
                        EM_ASM({ console.log('[EMSCRIPTEN] SDL_TEXTINPUT ->', UTF8ToString($0)); }, event.text.text);
                #endif
                    } else if (event.type == SDL_KEYDOWN) {
                #ifdef __EMSCRIPTEN__
                        EM_ASM({ console.log('[EMSCRIPTEN] SDL_KEYDOWN sym=', $0, 'mod=', $1); }, event.key.keysym.sym, event.key.keysym.mod);
                #endif
                    }
                    }
                } else if (event.type == SDL_TEXTINPUT && !search_mode) {
                    // Record undo action before modification
                    record_insert_action(&undo, cursorPos, event.text.text, cursorPos,
                                         cursorPos + strlen(event.text.text));

                    // Invalidate cached blocks after insertion point (conservative for affected
                    // blocks)
                    if (rd.lazy_mode) {
                        int cluster_at_cursor =
                            get_cluster_index_at_cursor(editorText, cursorPos, &rd);
                        invalidate_cluster_blocks_after(&rd, cluster_at_cursor);
                    }

                    // Delete selection if any
                    if (selectionStart >= 0 && selectionEnd >= 0 &&
                        selectionStart != selectionEnd) {
                        int new_cursor =
                            delete_selection_lazy(&editorText, selectionStart, selectionEnd, &rd);
                        if (new_cursor >= 0) {
                            cursorPos = new_cursor;
                            // Invalidate cached blocks starting at deletion start
                            if (rd.lazy_mode) {
                                int startIdx =
                                    selectionStart < selectionEnd ? selectionStart : selectionEnd;
                                invalidate_cluster_blocks_after(&rd, startIdx);
                            }
                            selectionStart = selectionEnd = -1;
                        }
                    }

                    int insertLen = (int) strlen(event.text.text);
                    int curLen = (int) strlen(editorText);
                    char *newText = malloc(curLen + insertLen + 1);
                    if (!newText)
                        continue;
                    memcpy(newText, editorText, cursorPos);
                    memcpy(newText + cursorPos, event.text.text, insertLen);
                    memcpy(newText + cursorPos + insertLen, editorText + cursorPos,
                           curLen - cursorPos + 1);
                    free(editorText);
                    editorText = newText;
                    cursorPos += insertLen;

                    mark_document_modified(&document, true);
                    update_render_data(renderer, font, editorText, text_area_x, text_area_y,
                                       maxTextWidth, &rd);
                    status_bar.needs_update = true;
                } else if (event.type == SDL_TEXTINPUT && search_mode) {
                    // Handle search input (in replace mode, this is still the search term)
                    int input_len = strlen(event.text.text);
                    if (search_buffer_pos + input_len < (int) (sizeof(search_buffer) - 1)) {
                        memcpy(search_buffer + search_buffer_pos, event.text.text, input_len);
                        search_buffer_pos += input_len;
                        search_buffer[search_buffer_pos] = '\0';

                        // Set search term and perform search
                        perform_search(&search, editorText, search_buffer);
                        if (search.replace_mode) {
                            // For now, use the search term as replace term too (will be enhanced
                            // later)
                            set_replace_term(&search, search_buffer);
                        }
                        if (has_matches(&search)) {
                            cursorPos = get_current_match_position(&search);
                            selectionStart = search.current_match;
                            selectionEnd = search.current_match;
                        }
                        status_bar.needs_update = true;
                    }
                } else if (event.type == SDL_KEYDOWN) {
                    SDL_Keymod mod = event.key.keysym.mod;
                    SDL_Keycode key = event.key.keysym.sym;

                    if (search_mode) {
                        if (key == SDLK_ESCAPE) {
                            search_mode = false;
                            clear_search(&search);
                            selectionStart = selectionEnd = -1;
                            status_bar.needs_update = true;
                        } else if (key == SDLK_RETURN) {
                            if (search.replace_mode && has_matches(&search)) {
                                // In replace mode, Enter performs replace on current match
                                if (search.replace_term) {
                                    char *new_text = replace_current_match(&search, editorText);
                                    if (new_text) {
                                        record_delete_action(
                                            &undo, get_current_match_position(&search),
                                            editorText + get_current_match_position(&search),
                                            cursorPos, cursorPos);
                                        free(editorText);
                                        editorText = new_text;
                                        mark_document_modified(&document, true);
                                        // Re-search to update positions
                                        perform_search(&search, editorText, search_buffer);
                                        update_render_data(renderer, font, editorText, text_area_x,
                                                           text_area_y, maxTextWidth, &rd);
                                    }
                                }
                            } else if (has_matches(&search)) {
                                // Regular search mode, Enter finds next
                                find_next(&search);
                                cursorPos = get_current_match_position(&search);
                                selectionStart = search.current_match;
                                selectionEnd = search.current_match;
                            }
                            status_bar.needs_update = true;
                        } else if (key == SDLK_BACKSPACE && search_buffer_pos > 0) {
                            search_buffer_pos--;
                            search_buffer[search_buffer_pos] = '\0';
                            perform_search(&search, editorText, search_buffer);
                            status_bar.needs_update = true;
                        }
                        continue; // Skip other key handling in search mode
                    }

                    // File operations
                    if (key == SDLK_n && (mod & KMOD_GUI)) {
                        // New file
                        bool proceed = true;
                        if (document.is_modified) {
                            DialogResult result = show_save_confirmation_dialog(document.filename);
                            if (result == DIALOG_YES) {
                                // Save first
                                if (document.filename) {
                                    if (!save_file(document.filename, editorText)) {
                                        show_error_dialog("Save Error", "Failed to save file");
                                        proceed = false;
                                    }
                                } else {
                                    // Use Save As dialog for new files
                                    char *save_as_filename = get_file_dialog(true);
                                    if (save_as_filename) {
                                        if (save_file(save_as_filename, editorText)) {
                                            set_document_filename(&document, save_as_filename);
                                            mark_document_modified(&document, false);
                                        } else {
                                            show_error_dialog("Save Error", "Failed to save file");
                                            proceed = false;
                                        }
                                        free(save_as_filename);
                                    } else {
                                        proceed = false;
                                    }
                                }
                            } else if (result == DIALOG_CANCEL) {
                                proceed = false;
                            }
                            // DIALOG_NO means proceed without saving
                        }

                        if (proceed) {
                            free(editorText);
                            editorText = strdup("");
                            cursorPos = 0;
                            selectionStart = selectionEnd = -1;
                            cleanup_document_state(&document);
                            init_document_state(&document);
                            cleanup_undo_system(&undo);
                            init_undo_system(&undo, 100);
                            update_render_data(renderer, font, editorText, text_area_x, text_area_y,
                                               maxTextWidth, &rd);
                            status_bar.needs_update = true;
                        }
                    } else if (key == SDLK_o && (mod & KMOD_GUI)) {
                        // Open file (simplified - in real app use file dialog)
                        bool proceed = true;
                        if (document.is_modified) {
                            DialogResult result = show_save_confirmation_dialog(document.filename);
                            if (result == DIALOG_YES) {
                                // Save first
                                if (document.filename) {
                                    if (!save_file(document.filename, editorText)) {
                                        show_error_dialog("Save Error", "Failed to save file");
                                        proceed = false;
                                    }
                                } else {
                                    // Use Save As dialog for new files
                                    char *save_as_filename = get_file_dialog(true);
                                    if (save_as_filename) {
                                        if (save_file(save_as_filename, editorText)) {
                                            set_document_filename(&document, save_as_filename);
                                            mark_document_modified(&document, false);
                                        } else {
                                            show_error_dialog("Save Error", "Failed to save file");
                                            proceed = false;
                                        }
                                        free(save_as_filename);
                                    } else {
                                        proceed = false;
                                    }
                                }
                            } else if (result == DIALOG_CANCEL) {
                                proceed = false;
                            }
                        }

                        if (proceed) {
                            char *filename = simple_file_picker(false);
                            if (filename) {
                                char *content = NULL;
                                if (open_file(filename, &content)) {
                                    free(editorText);
                                    editorText = content;
                                    cursorPos = 0;
                                    selectionStart = selectionEnd = -1;
                                    set_document_filename(&document, filename);
                                    mark_document_modified(&document, false);
                                    cleanup_undo_system(&undo);
                                    init_undo_system(&undo, 100);
                                    update_render_data(renderer, font, editorText, text_area_x,
                                                       text_area_y, maxTextWidth, &rd);
                                    if (rd.lazy_mode)
                                        invalidate_cluster_blocks_after(&rd, 0);

                                    // Update window title
                                    snprintf(window_title, sizeof(window_title),
                                             "%s - RobusText Editor", document.filename);
                                    SDL_SetWindowTitle(window, window_title);
                                }
                                free(filename);
                            }
                        }
                        status_bar.needs_update = true;
                    } else if (key == SDLK_s && (mod & KMOD_GUI)) {
                        // Save file
                        if (document.is_new_file) {
                            // Save As
                            char *filename = simple_file_picker(true);
                            if (filename) {
                                if (save_file(filename, editorText)) {
                                    set_document_filename(&document, filename);
                                    mark_document_modified(&document, false);

                                    // Update window title
                                    snprintf(window_title, sizeof(window_title),
                                             "%s - RobusText Editor", document.filename);
                                    SDL_SetWindowTitle(window, window_title);
                                }
                                free(filename);
                            }
                        } else {
                            // Save existing file
                            if (save_file(document.filepath, editorText)) {
                                mark_document_modified(&document, false);
                            }
                        }
                        status_bar.needs_update = true;
                    }
                    // Undo/Redo
                    else if (key == SDLK_z && (mod & KMOD_GUI) && !(mod & KMOD_SHIFT)) {
                        // Undo
                        if (perform_undo(&undo, &editorText, &cursorPos)) {
                            mark_document_modified(&document, true);
                            selectionStart = selectionEnd = -1;
                            update_render_data(renderer, font, editorText, text_area_x, text_area_y,
                                               maxTextWidth, &rd);
                            status_bar.needs_update = true;
                        }
                    } else if ((key == SDLK_z && (mod & KMOD_GUI) && (mod & KMOD_SHIFT)) ||
                               (key == SDLK_y && (mod & KMOD_GUI))) {
                        // Redo
                        if (perform_redo(&undo, &editorText, &cursorPos)) {
                            mark_document_modified(&document, true);
                            selectionStart = selectionEnd = -1;
                            update_render_data(renderer, font, editorText, text_area_x, text_area_y,
                                               maxTextWidth, &rd);
                            status_bar.needs_update = true;
                        }
                    }
                    // Search
                    else if (key == SDLK_f && (mod & KMOD_GUI)) {
                        search_mode = true;
                        search.replace_mode = false; // Regular search mode
                        search_buffer[0] = '\0';
                        search_buffer_pos = 0;
                        clear_search(&search);
                        status_bar.needs_update = true;
                    }
                    // Replace (Cmd+H)
                    else if (key == SDLK_h && (mod & KMOD_GUI)) {
                        search_mode = true;
                        search.replace_mode = true; // Replace mode
                        search_buffer[0] = '\0';
                        search_buffer_pos = 0;
                        clear_search(&search);
                        status_bar.needs_update = true;
                    }
                    // Toggle Line Numbers (Cmd+L)
                    else if (key == SDLK_l && (mod & KMOD_GUI)) {
                        toggle_line_numbers(&line_numbers);
                        // Recalculate text area dimensions
                        int line_numbers_width = get_line_numbers_width(&line_numbers);
                        maxTextWidth = windowWidth - (2 * margin) - line_numbers_width;
                        text_area_x = line_numbers_width + margin;
                        update_render_data(renderer, font, editorText, text_area_x, text_area_y,
                                           maxTextWidth, &rd);
                        status_bar.needs_update = true;
                    }
                    // Toggle Auto-save (Cmd+Shift+S)
                    else if (key == SDLK_s && (mod & KMOD_GUI) && (mod & KMOD_SHIFT)) {
                        set_auto_save_enabled(&auto_save, !auto_save.enabled);
                        debug_print(L"Auto-save %s\n", auto_save.enabled ? "enabled" : "disabled");
                        status_bar.needs_update = true;
                    }
                    // Save As (Cmd+Shift+A)
                    else if (key == SDLK_a && (mod & KMOD_GUI) && (mod & KMOD_SHIFT)) {
                        char *save_as_filename = get_file_dialog(true);
                        if (save_as_filename) {
                            if (save_file(save_as_filename, editorText)) {
                                set_document_filename(&document, save_as_filename);
                                mark_document_modified(&document, false);

                                // Update window title
                                snprintf(window_title, sizeof(window_title),
                                         "%s - RobusText Editor", document.filename);
                                SDL_SetWindowTitle(window, window_title);
                                status_bar.needs_update = true;
                            } else {
                                show_error_dialog("Save Error", "Failed to save file");
                            }
                            free(save_as_filename);
                        }
                    }
                    // Select All
                    else if (key == SDLK_a && (mod & KMOD_GUI) && !(mod & KMOD_SHIFT)) {
                        selectionStart = 0;
                        selectionEnd = rd.numClusters - 1;
                        status_bar.needs_update = true;
                    }
                    // Cut
                    else if (key == SDLK_x && (mod & KMOD_GUI)) {
                        if (selectionStart >= 0 && selectionEnd >= 0 &&
                            selectionStart != selectionEnd) {
                            // Copy to clipboard first
                            int startIdx =
                                selectionStart < selectionEnd ? selectionStart : selectionEnd;
                            int endIdx =
                                selectionStart < selectionEnd ? selectionEnd : selectionStart;

                            if (startIdx < rd.numClusters && endIdx < rd.numClusters) {
                                int startByte = get_cluster_byte_offset(&rd, editorText, startIdx);
                                int endByte = get_cluster_byte_offset(&rd, editorText, endIdx + 1);
                                if (startByte < 0)
                                    startByte = 0;
                                if (endByte < 0)
                                    endByte = strlen(editorText);

                                int selectionLen = endByte - startByte;
                                char *selectedText = malloc(selectionLen + 1);
                                if (selectedText) {
                                    memcpy(selectedText, editorText + startByte, selectionLen);
                                    selectedText[selectionLen] = '\0';

                                    record_delete_action(&undo, startByte, selectedText, cursorPos,
                                                         startByte);
                                    SDL_SetClipboardText(selectedText);

                                    // Delete selection
                                    int new_cursor = delete_selection_lazy(
                                        &editorText, selectionStart, selectionEnd, &rd);
                                    if (new_cursor >= 0) {
                                        cursorPos = new_cursor;
                                        // Invalidate cached blocks beginning at deletion start
                                        if (rd.lazy_mode) {
                                            int startIdx = selectionStart < selectionEnd
                                                               ? selectionStart
                                                               : selectionEnd;
                                            invalidate_cluster_blocks_after(&rd, startIdx);
                                        }
                                        selectionStart = selectionEnd = -1;
                                        mark_document_modified(&document, true);
                                        update_render_data(renderer, font, editorText, text_area_x,
                                                           text_area_y, maxTextWidth, &rd);
                                    }

                                    free(selectedText);
                                }
                            }
                        }
                        status_bar.needs_update = true;
                    }
                    // Enhanced navigation
                    else if (key == SDLK_LEFT) {
                        if (mod & KMOD_GUI) {
                            // Move to beginning of line
                            cursorPos = move_cursor_line_start(editorText, cursorPos);
                        } else if (mod & KMOD_ALT) {
                            // Move to previous word
                            cursorPos = move_cursor_word_left(editorText, cursorPos);
                        } else {
                            // Move one character left
                            if (cursorPos > 0) {
                                int pos = cursorPos - 1;
                                while (pos > 0 &&
                                       ((unsigned char) editorText[pos] & 0xC0) == 0x80) {
                                    pos--;
                                }
                                cursorPos = pos;
                            }
                        }
                        selectionStart = selectionEnd = -1;
                        status_bar.needs_update = true;
                    } else if (key == SDLK_RIGHT) {
                        int curLen = strlen(editorText);
                        if (mod & KMOD_GUI) {
                            // Move to end of line
                            cursorPos = move_cursor_line_end(editorText, cursorPos);
                        } else if (mod & KMOD_ALT) {
                            // Move to next word
                            cursorPos = move_cursor_word_right(editorText, cursorPos);
                        } else {
                            // Move one character right
                            if (cursorPos < curLen) {
                                int pos = cursorPos + 1;
                                while (pos < curLen &&
                                       ((unsigned char) editorText[pos] & 0xC0) == 0x80) {
                                    pos++;
                                }
                                cursorPos = pos;
                            }
                        }
                        selectionStart = selectionEnd = -1;
                        status_bar.needs_update = true;
                    } else if (key == SDLK_HOME) {
                        cursorPos = move_cursor_line_start(editorText, cursorPos);
                        selectionStart = selectionEnd = -1;
                        status_bar.needs_update = true;
                    } else if (key == SDLK_PAGEUP) {
                        // Page up
                        rd.scrollY -= text_area_height;
                        if (rd.scrollY < 0)
                            rd.scrollY = 0;
                        if (rd.lazy_mode)
                            prepare_visible_texture(renderer, font, editorText, text_area_x,
                                                    text_area_y, maxTextWidth, &rd, rd.scrollY,
                                                    text_area_height);
                        status_bar.needs_update = true;
                    } else if (key == SDLK_PAGEDOWN) {
                        // Page down
                        rd.scrollY += text_area_height;
                        if (rd.textH > 0 && rd.scrollY > rd.textH - text_area_height)
                            rd.scrollY = rd.textH - text_area_height;
                        if (rd.lazy_mode)
                            prepare_visible_texture(renderer, font, editorText, text_area_x,
                                                    text_area_y, maxTextWidth, &rd, rd.scrollY,
                                                    text_area_height);
                        status_bar.needs_update = true;
                    } else if (key == SDLK_END) {
                        cursorPos = move_cursor_line_end(editorText, cursorPos);
                        selectionStart = selectionEnd = -1;
                        status_bar.needs_update = true;
                    } else if (key == SDLK_BACKSPACE) {
                        if (selectionStart >= 0 && selectionEnd >= 0 &&
                            selectionStart != selectionEnd) {
                            // Delete selection
                            int startIdx =
                                selectionStart < selectionEnd ? selectionStart : selectionEnd;
                            int endIdx =
                                selectionStart < selectionEnd ? selectionEnd : selectionStart;

                            int startByte = get_cluster_byte_offset(&rd, editorText, startIdx);
                            int endByte = get_cluster_byte_offset(&rd, editorText, endIdx + 1);
                            if (startByte < 0)
                                startByte = 0;
                            if (endByte < 0)
                                endByte = strlen(editorText);

                            char *deleted_text = malloc(endByte - startByte + 1);
                            if (deleted_text) {
                                memcpy(deleted_text, editorText + startByte, endByte - startByte);
                                deleted_text[endByte - startByte] = '\0';
                                record_delete_action(&undo, startByte, deleted_text, cursorPos,
                                                     startByte);
                                free(deleted_text);
                            }

                            int new_cursor = delete_selection_lazy(&editorText, selectionStart,
                                                                   selectionEnd, &rd);
                            if (new_cursor >= 0) {
                                cursorPos = new_cursor;
                                if (rd.lazy_mode) {
                                    int startIdx = selectionStart < selectionEnd ? selectionStart
                                                                                 : selectionEnd;
                                    invalidate_cluster_blocks_after(&rd, startIdx);
                                }
                                selectionStart = selectionEnd = -1;
                            }
                        } else if (cursorPos > 0) {
                            // Regular backspace
                            int prevPos = cursorPos - 1;
                            while (prevPos > 0 &&
                                   ((unsigned char) editorText[prevPos] & 0xC0) == 0x80) {
                                prevPos--;
                            }
                            int rem = cursorPos - prevPos;
                            if (rem > 0) {
                                char *deleted_text = malloc(rem + 1);
                                if (deleted_text) {
                                    memcpy(deleted_text, editorText + prevPos, rem);
                                    deleted_text[rem] = '\0';
                                    record_delete_action(&undo, prevPos, deleted_text, cursorPos,
                                                         prevPos);
                                    free(deleted_text);
                                }

                                // Compute cluster index at prevPos before modifying text
                                int cluster_before = -1;
                                if (rd.lazy_mode) {
                                    cluster_before =
                                        get_cluster_index_at_cursor(editorText, prevPos, &rd);
                                }

                                int curLen = (int) strlen(editorText);
                                char *newText = malloc(curLen - rem + 1);
                                if (!newText)
                                    continue;
                                if (prevPos > 0) {
                                    memcpy(newText, editorText, prevPos);
                                }
                                memcpy(newText + prevPos, editorText + cursorPos,
                                       curLen - cursorPos + 1);
                                free(editorText);
                                editorText = newText;
                                cursorPos = prevPos;
                                if (rd.lazy_mode && cluster_before >= 0) {
                                    invalidate_cluster_blocks_after(&rd, cluster_before);
                                }
                            }
                        }
                        mark_document_modified(&document, true);
                        update_render_data(renderer, font, editorText, text_area_x, text_area_y,
                                           maxTextWidth, &rd);
                        status_bar.needs_update = true;
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        // Insert newline character
                        record_insert_action(&undo, cursorPos, "\n", cursorPos, cursorPos + 1);

                        // Delete selection if any
                        if (selectionStart >= 0 && selectionEnd >= 0 &&
                            selectionStart != selectionEnd) {
                            int new_cursor = delete_selection_lazy(&editorText, selectionStart,
                                                                   selectionEnd, &rd);
                            if (new_cursor >= 0) {
                                cursorPos = new_cursor;
                                selectionStart = selectionEnd = -1;
                            }
                        }

                        // Insert newline
                        int curLen = (int) strlen(editorText);
                        char *newText =
                            malloc(curLen + 2); // +1 for newline, +1 for null terminator
                        if (newText) {
                            memcpy(newText, editorText, cursorPos);
                            newText[cursorPos] = '\n';
                            memcpy(newText + cursorPos + 1, editorText + cursorPos,
                                   curLen - cursorPos + 1);
                            free(editorText);
                            editorText = newText;
                            cursorPos += 1;

                            mark_document_modified(&document, true);
                            update_render_data(renderer, font, editorText, text_area_x, text_area_y,
                                               maxTextWidth, &rd);
                            status_bar.needs_update = true;
                        }
                    } else if (key == SDLK_ESCAPE) {
                        selectionStart = selectionEnd = -1;
                        mouseSelecting = 0;
                        clear_search(&search);
                        status_bar.needs_update = true;
                    }
                    // Copy
                    else if (key == SDLK_c && (mod & KMOD_GUI)) {
                        if (selectionStart >= 0 && selectionEnd >= 0 &&
                            selectionStart != selectionEnd) {
                            int startIdx =
                                selectionStart < selectionEnd ? selectionStart : selectionEnd;
                            int endIdx =
                                selectionStart < selectionEnd ? selectionEnd : selectionStart;

                            if (startIdx < rd.numClusters && endIdx < rd.numClusters) {
                                int startByte = get_cluster_byte_offset(&rd, editorText, startIdx);
                                int endByte = get_cluster_byte_offset(&rd, editorText, endIdx + 1);
                                if (startByte < 0)
                                    startByte = 0;
                                if (endByte < 0)
                                    endByte = strlen(editorText);

                                int selectionLen = endByte - startByte;
                                char *selectedText = malloc(selectionLen + 1);
                                if (selectedText) {
                                    memcpy(selectedText, editorText + startByte, selectionLen);
                                    selectedText[selectionLen] = '\0';
                                    SDL_SetClipboardText(selectedText);
                                    free(selectedText);
                                }
                            }
                        }
                    }
                    // Paste
                    else if (key == SDLK_v && (mod & KMOD_GUI)) {
                        char *clipboard_text = SDL_GetClipboardText();
                        if (clipboard_text) {
                            record_insert_action(&undo, cursorPos, clipboard_text, cursorPos,
                                                 cursorPos + strlen(clipboard_text));

                            // Delete selection if any
                            if (selectionStart >= 0 && selectionEnd >= 0 &&
                                selectionStart != selectionEnd) {
                                int new_cursor = delete_selection_lazy(&editorText, selectionStart,
                                                                       selectionEnd, &rd);
                                if (new_cursor >= 0) {
                                    cursorPos = new_cursor;
                                    selectionStart = selectionEnd = -1;
                                }
                            }

                            int pasteLen = (int) strlen(clipboard_text);
                            int curLen = (int) strlen(editorText);
                            char *newText = malloc(curLen + pasteLen + 1);
                            if (newText) {
                                memcpy(newText, editorText, cursorPos);
                                memcpy(newText + cursorPos, clipboard_text, pasteLen);
                                memcpy(newText + cursorPos + pasteLen, editorText + cursorPos,
                                       curLen - cursorPos + 1);
                                free(editorText);
                                editorText = newText;
                                cursorPos += pasteLen;
                                mark_document_modified(&document, true);
                                update_render_data(renderer, font, editorText, text_area_x,
                                                   text_area_y, maxTextWidth, &rd);
                            }
                            SDL_free(clipboard_text);
                        }
                        status_bar.needs_update = true;
                    }
                } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                           event.button.button == SDL_BUTTON_LEFT) {
                    // Check if click is in text area (not status bar)
                    if (event.button.y < text_area_height &&
                        SDL_PointInRect(&(SDL_Point){event.button.x, event.button.y},
                                        &rd.textRect)) {
                        int relativeX = event.button.x - rd.textRect.x;
                        int nearestCluster = 0;
                        int minDist = INT_MAX;
                        if (rd.numClusters > 0) {
                            for (int i = 0; i < rd.numClusters; i++) {
                                int clusterX = 0;
                                bool havePos = false;
                                if (rd.glyphOffsets && i < rd.numGlyphs) {
                                    clusterX = rd.glyphOffsets[i];
                                    havePos = true;
                                } else if (rd.clusterRects) {
                                    clusterX = rd.clusterRects[i].x;
                                    havePos = true;
                                }
                                if (!havePos)
                                    continue;
                                int dist = abs(clusterX - relativeX);
                                if (dist < minDist) {
                                    minDist = dist;
                                    nearestCluster = i;
                                }
                            }
                            selectionStart = nearestCluster;
                            if (nearestCluster < rd.numClusters) {
                                int off = get_cluster_byte_offset(&rd, editorText, nearestCluster);
                                if (off >= 0)
                                    cursorPos = off;
                            }
                            mouseSelecting = 1;
                        }
                        status_bar.needs_update = true;
                    }
                } else if (event.type == SDL_MOUSEMOTION && mouseSelecting) {
                    if (event.motion.y < text_area_height) {
                        int relativeX = event.motion.x - rd.textRect.x;
                        int nearestCluster = 0;
                        int minDist = INT_MAX;
                        for (int i = 0; i < rd.numClusters; i++) {
                            int clusterX = 0;
                            bool havePos = false;
                            if (rd.glyphOffsets && i < rd.numGlyphs) {
                                clusterX = rd.glyphOffsets[i];
                                havePos = true;
                            } else if (rd.clusterRects) {
                                clusterX = rd.clusterRects[i].x;
                                havePos = true;
                            }
                            if (!havePos)
                                continue;
                            int dist = abs(clusterX - relativeX);
                            if (dist < minDist) {
                                minDist = dist;
                                nearestCluster = i;
                            }
                        }
                        selectionEnd = nearestCluster;
                        {
                            int off = get_cluster_byte_offset(&rd, editorText, nearestCluster);
                            if (off >= 0)
                                cursorPos = off;
                        }
                        status_bar.needs_update = true;
                    }
                } else if (event.type == SDL_MOUSEBUTTONUP &&
                           event.button.button == SDL_BUTTON_LEFT) {
                    mouseSelecting = 0;
                    if (selectionStart >= 0) {
                        selectionEnd = get_cluster_index_at_cursor(editorText, cursorPos, &rd);
                    }
                }
            } // End event poll

            // Regular mode rendering (when not using continuous resize)
            // Check for auto-save
            if (should_auto_save(&auto_save, document.is_modified)) {
                perform_auto_save(&auto_save, &document, editorText);
            }

            // Update content hash for change detection
            if (lastText) {
                uint32_t content_hash = 0;
                for (const char *p = editorText; *p; p++) {
                    content_hash = ((content_hash << 5) + content_hash) + *p;
                }
                if (content_hash != state.last_content_hash || windowWidth != state.last_width) {
                    state.needs_update = true;
                    state.last_content_hash = content_hash;
                    state.last_width = windowWidth;
                }
            }

            if (state.needs_update) {
                update_render_data(renderer, font, editorText, text_area_x, text_area_y,
                                   maxTextWidth, &rd);
                state.needs_update = false;
            }

            // If in lazy mode, prepare an on-demand viewport texture
            if (rd.lazy_mode) {
                prepare_visible_texture(renderer, font, editorText, text_area_x, text_area_y,
                                        maxTextWidth, &rd, rd.scrollY, text_area_height);
            }

            // Update status bar
            update_status_bar(&status_bar, renderer, &document, &search, cursorPos, editorText,
                              windowWidth);

            // Update line numbers
            int font_height = TTF_FontLineSkip(font);
            int line_numbers_area_height =
                windowHeight - status_bar.height; // Full height minus status bar
            int visible_lines = line_numbers_area_height / font_height;
            update_line_numbers(&line_numbers, renderer, editorText, 1, visible_lines);
            line_numbers.rect.y = 0; // Line numbers go all the way to the top

            // Render everything
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer, 22, 24, 32, 255);
            SDL_RenderClear(renderer);

            // Compute cursor line to keep it visible in regular mode
            int cursor_font_height = TTF_FontLineSkip(font);
            int cursor_line = 0;
            int line_start_pos = 0;
            for (int i = 0; i < cursorPos && editorText[i]; i++) {
                if (editorText[i] == '\n') {
                    cursor_line++;
                    line_start_pos = i + 1;
                }
            }

            // Ensure scrollY is within valid bounds for regular mode as well
            if (rd.scrollY < 0)
                rd.scrollY = 0;
            if (rd.textH > 0 && rd.scrollY > rd.textH - text_area_height)
                rd.scrollY = rd.textH - text_area_height;

            // Leave scrollY controlled by user (mouse wheel, PageUp/PageDown, etc.)
            // Only ensure scrollY stays within bounds (clamping already done above).

            // Render text using floating-point positioning for macOS-like precision
            if (rd.textTexture && rd.textRect.w > 0 && rd.textRect.h > 0 && rd.textRect.x >= 0 &&
                rd.textRect.y >= 0) {
                SDL_Rect src = {0, rd.scrollY, rd.textRect.w, text_area_height};
                SDL_FRect dst = {(float) rd.textRect.x, (float) rd.textRect.y,
                                 (float) rd.textRect.w, (float) text_area_height};
                SDL_RenderCopyF(renderer, rd.textTexture, &src, &dst);
            }

            // Render selection highlight
            if (selectionStart >= 0 && selectionEnd >= 0 && selectionStart != selectionEnd) {
                int startIdx = selectionStart < selectionEnd ? selectionStart : selectionEnd;
                int endIdx = selectionStart < selectionEnd ? selectionEnd : selectionStart;
                if (startIdx < rd.numClusters && endIdx < rd.numClusters) {
                    SDL_Rect hl = {rd.textRect.x + rd.glyphOffsets[startIdx], rd.textRect.y,
                                   rd.glyphOffsets[endIdx] - rd.glyphOffsets[startIdx] +
                                       rd.clusterRects[endIdx].w,
                                   rd.textRect.h};
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(renderer, 200, 200, 180, 128);
                    SDL_RenderFillRect(renderer, &hl);
                }
            }

            // Render search highlights
            if (search.is_active && has_matches(&search)) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                for (int i = 0; i < search.num_matches; i++) {
                    int match_pos = search.match_positions[i];
                    int cluster_idx = get_cluster_index_at_cursor(editorText, match_pos, &rd);

                    if (cluster_idx < rd.numClusters) {
                        SDL_Rect search_hl = {rd.textRect.x + rd.glyphOffsets[cluster_idx],
                                              rd.textRect.y,
                                              search.match_lengths[i] * 8, // Approximate width
                                              rd.textRect.h};

                        if (i == search.current_match) {
                            SDL_SetRenderDrawColor(renderer, 255, 255, 0,
                                                   100); // Yellow for current match
                        } else {
                            SDL_SetRenderDrawColor(renderer, 255, 200, 0,
                                                   80); // Orange for other matches
                        }
                        SDL_RenderFillRect(renderer, &search_hl);
                    }
                }
            }

            // Render cursor (cursor_line and line_start_pos were computed earlier in this block)
            // Calculate cursor position on the current line
            int cursor_pos_in_line = cursorPos - line_start_pos;

            // Get the width of text from line start to cursor
            char temp_line[1024] = {0};
            int copy_len = cursor_pos_in_line;
            if (copy_len > 1023)
                copy_len = 1023;
            memcpy(temp_line, editorText + line_start_pos, copy_len);

            int cursorX = rd.textRect.x;
            if (strlen(temp_line) > 0) {
                int text_width;
                TTF_SizeUTF8(font, temp_line, &text_width, NULL);
                cursorX += text_width;
            }

            int cursorY = rd.textRect.y + (cursor_line * cursor_font_height);

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawLine(renderer, cursorX, cursorY, cursorX, cursorY + cursor_font_height);

            // Render line numbers
            line_numbers.rect.y = 0; // Line numbers go all the way to the top
            render_line_numbers(&line_numbers, renderer);

            // Render status bar
            render_status_bar(&status_bar, renderer);

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_RenderPresent(renderer);

            uint32_t frame_time = SDL_GetTicks() - frame_start;
            if (frame_time < 16) {
                SDL_Delay(16 - frame_time);
            }
        } // End regular mode
    }     // End main loop

    SDL_StopTextInput();

    // Cleanup continuous resize system
    if (g_continuous_resize_active) {
        debug_print(L"Shutting down continuous resize system\n");

        // Signal render thread to exit
        g_continuous_resize_active = false;
        shutdown_event_queue(&g_event_queue);

        // Remove event watch
        SDL_DelEventWatch(event_watch_callback, NULL);

        // Wait for render thread to finish
        pthread_join(g_render_thread, NULL);

        // Cleanup event queue
        cleanup_event_queue(&g_event_queue);

        debug_print(L"Continuous resize system shut down\n");
    }

    // Cleanup
    if (lastText) {
        free(lastText);
    }
    cleanup_render_data(&rd);
    if (editorText) {
        free(editorText);
    }
    cleanup_document_state(&document);
    cleanup_undo_system(&undo);
    cleanup_search_state(&search);
    cleanup_status_bar(&status_bar);
    cleanup_line_numbers(&line_numbers);
    // Note: auto_save doesn't have a cleanup function
    TTF_CloseFont(font);
    TTF_CloseFont(status_font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}
