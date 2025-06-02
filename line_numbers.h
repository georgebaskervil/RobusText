#ifndef LINE_NUMBERS_H
#define LINE_NUMBERS_H

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>

typedef struct {
    SDL_Texture *texture;
    TTF_Font *font;
    SDL_Rect rect;
    int width;
    int line_count;
    bool needs_update;
    bool enabled;
} LineNumbers;

// Initialize and cleanup
void init_line_numbers(LineNumbers *line_nums, SDL_Renderer *renderer, TTF_Font *font, int window_height);
void cleanup_line_numbers(LineNumbers *line_nums);

// Update and render
void update_line_numbers(LineNumbers *line_nums, SDL_Renderer *renderer, const char *text, int first_visible_line, int visible_lines);
void render_line_numbers(LineNumbers *line_nums, SDL_Renderer *renderer);
void resize_line_numbers(LineNumbers *line_nums, int window_height);

// Utility
int count_lines(const char *text);
void toggle_line_numbers(LineNumbers *line_nums);
bool line_numbers_enabled(const LineNumbers *line_nums);
int get_line_numbers_width(const LineNumbers *line_nums);

#endif
