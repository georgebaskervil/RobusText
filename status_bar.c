#include "status_bar.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>

#define STATUS_BAR_HEIGHT 24
#define STATUS_BAR_PADDING 10

void init_status_bar(StatusBar *status, SDL_Renderer *renderer, TTF_Font *font, int window_width,
                     int window_height)
{
    (void) renderer; // Currently unused but kept for future extensibility
    status->texture = NULL;
    status->font = font;
    status->height = STATUS_BAR_HEIGHT;
    status->rect =
        (SDL_Rect){0, window_height - STATUS_BAR_HEIGHT, window_width, STATUS_BAR_HEIGHT};
    status->needs_update = true;
}

void cleanup_status_bar(StatusBar *status)
{
    if (status->texture) {
        SDL_DestroyTexture(status->texture);
        status->texture = NULL;
    }
}

void get_line_column_from_position(const char *text, int pos, int *line, int *column)
{
    *line = 1;
    *column = 1;

    for (int i = 0; i < pos && text[i]; i++) {
        if (text[i] == '\n') {
            (*line)++;
            *column = 1;
        } else {
            (*column)++;
        }
    }
}

void update_status_bar(StatusBar *status, SDL_Renderer *renderer, const DocumentState *doc,
                       const SearchState *search, int cursor_pos, const char *text,
                       int window_width)
{
    // Update rect width for window resizing
    status->rect.w = window_width;

    if (!status->needs_update)
        return;

    // Clean up old texture
    if (status->texture) {
        SDL_DestroyTexture(status->texture);
        status->texture = NULL;
    }

    // Get cursor line and column
    int line, column;
    get_line_column_from_position(text, cursor_pos, &line, &column);

    // Build status text
    char status_text[512];
    const char *filename = doc->filename ? doc->filename : "Untitled";
    const char *modified = doc->is_modified ? "*" : "";

    if (search->is_active && search->replace_mode && has_matches(search)) {
        snprintf(status_text, sizeof(status_text), "%s%s | Ln %d, Col %d | Replace: %d/%d matches",
                 filename, modified, line, column, search->current_match + 1, search->num_matches);
    } else if (search->is_active && search->replace_mode) {
        snprintf(status_text, sizeof(status_text), "%s%s | Ln %d, Col %d | Replace: No matches",
                 filename, modified, line, column);
    } else if (search->is_active && has_matches(search)) {
        snprintf(status_text, sizeof(status_text), "%s%s | Ln %d, Col %d | Search: %d/%d matches",
                 filename, modified, line, column, search->current_match + 1, search->num_matches);
    } else if (search->is_active) {
        snprintf(status_text, sizeof(status_text), "%s%s | Ln %d, Col %d | Search: No matches",
                 filename, modified, line, column);
    } else {
        snprintf(status_text, sizeof(status_text), "%s%s | Ln %d, Col %d", filename, modified, line,
                 column);
    }

    // Create surface with text using blended rendering for macOS-like anti-aliasing
    SDL_Color text_color = {200, 200, 200, 255};
    SDL_Color bg_color = {40, 42, 50, 255};

    SDL_Surface *text_surface = TTF_RenderText_Blended(status->font, status_text, text_color);
    if (!text_surface) {
        debug_print(L"Failed to create status bar text surface\n");
        return;
    }

    // Create background surface with platform-independent color masks
    SDL_Surface *bg_surface = SDL_CreateRGBSurface(0, window_width, STATUS_BAR_HEIGHT, 32, 0, 0, 0,
                                                   0); // Let SDL determine the masks
    if (!bg_surface) {
        SDL_FreeSurface(text_surface);
        return;
    }

    // Fill background
    SDL_FillRect(bg_surface, NULL,
                 SDL_MapRGB(bg_surface->format, bg_color.r, bg_color.g, bg_color.b));

    // Blit text onto background
    SDL_Rect text_rect = {STATUS_BAR_PADDING, (STATUS_BAR_HEIGHT - text_surface->h) / 2,
                          text_surface->w, text_surface->h};
    SDL_BlitSurface(text_surface, NULL, bg_surface, &text_rect);

    // Create texture
    status->texture = SDL_CreateTextureFromSurface(renderer, bg_surface);

    SDL_FreeSurface(text_surface);
    SDL_FreeSurface(bg_surface);

    status->needs_update = false;
    debug_print(L"Updated status bar: %s\n", status_text);
}

void render_status_bar(StatusBar *status, SDL_Renderer *renderer)
{
    if (status->texture) {
        // Use floating-point positioning for macOS-like precision
        SDL_FRect status_rect_f = {(float) status->rect.x, (float) status->rect.y,
                                   (float) status->rect.w, (float) status->rect.h};
        SDL_RenderCopyF(renderer, status->texture, NULL, &status_rect_f);
    }
}
