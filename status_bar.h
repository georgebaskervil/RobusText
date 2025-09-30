#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include "file_operations.h"
#include "platform_sdl.h"
#include "search_system.h"
#include <SDL.h>
#include <SDL_ttf.h>

typedef struct {
    SDL_Texture *texture;
    SDL_Rect rect;
    TTF_Font *font;
    int height;
    bool needs_update;
} StatusBar;

// Initialize and cleanup
void init_status_bar(StatusBar *status, SDL_Renderer *renderer, TTF_Font *font, int window_width,
                     int window_height);
void cleanup_status_bar(StatusBar *status);

// Update status bar content
void update_status_bar(StatusBar *status, SDL_Renderer *renderer, const DocumentState *doc,
                       const SearchState *search, int cursor_pos, const char *text,
                       int window_width);

// Render status bar
void render_status_bar(StatusBar *status, SDL_Renderer *renderer);

// Utility functions
void get_line_column_from_position(const char *text, int pos, int *line, int *column);

#endif // STATUS_BAR_H
