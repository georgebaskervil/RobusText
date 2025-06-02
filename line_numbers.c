#include "line_numbers.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define LINE_NUMBERS_PADDING 10
#define LINE_NUMBERS_MIN_WIDTH 40
#define TEXT_MARGIN 20  // Should match the margin in sdl_window.c

void init_line_numbers(LineNumbers *line_nums, SDL_Renderer *renderer, TTF_Font *font, int window_height) {
    (void)renderer;  // Currently unused but kept for future extensibility
    line_nums->texture = NULL;
    line_nums->font = font;
    line_nums->width = LINE_NUMBERS_MIN_WIDTH;
    line_nums->rect = (SDL_Rect){0, 0, line_nums->width, window_height - 24};  // Subtract status bar height
    line_nums->line_count = 0;
    line_nums->needs_update = true;
    line_nums->enabled = true;  // Enabled by default
}

void cleanup_line_numbers(LineNumbers *line_nums) {
    if (line_nums->texture) {
        SDL_DestroyTexture(line_nums->texture);
        line_nums->texture = NULL;
    }
}

int count_lines(const char *text) {
    if (!text || !*text) return 1;
    
    int lines = 1;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') lines++;
    }
    return lines;
}

void update_line_numbers(LineNumbers *line_nums, SDL_Renderer *renderer, const char *text, int first_visible_line, int visible_lines) {
    if (!line_nums->enabled) {
        line_nums->width = 0;
        return;
    }
    
    int total_lines = count_lines(text);
    
    // Calculate required width based on number of digits in line count
    char max_line_str[32];
    snprintf(max_line_str, sizeof(max_line_str), "%d", total_lines);
    int text_width, text_height;
    TTF_SizeText(line_nums->font, max_line_str, &text_width, &text_height);
    line_nums->width = text_width + LINE_NUMBERS_PADDING * 2;
    
    if (line_nums->width < LINE_NUMBERS_MIN_WIDTH) {
        line_nums->width = LINE_NUMBERS_MIN_WIDTH;
    }
    
    line_nums->rect.w = line_nums->width;
    
    // Only update if line count changed or forced update
    if (total_lines != line_nums->line_count || line_nums->needs_update) {
        line_nums->line_count = total_lines;
        line_nums->needs_update = false;
        
        // Destroy old texture
        if (line_nums->texture) {
            SDL_DestroyTexture(line_nums->texture);
            line_nums->texture = NULL;
        }
        
        // Create surface for line numbers
        SDL_Color bg_color = {40, 42, 50, 255};  // Status bar background color
        SDL_Color text_color = {200, 200, 200, 255};  // Light gray text
        
        // Calculate surface dimensions
        int line_height = TTF_FontLineSkip(line_nums->font); // Use line skip instead of font height
        int surface_height = visible_lines * line_height;
        
        // Use proper platform-independent surface creation
        SDL_Surface *surface = SDL_CreateRGBSurface(0, line_nums->width, surface_height, 32, 
                                                   0, 0, 0, 0);  // Let SDL determine the masks
        
        if (surface) {
            // Fill background
            SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, bg_color.r, bg_color.g, bg_color.b));
            
            // Render line numbers
            for (int i = 0; i < visible_lines && (first_visible_line + i) <= total_lines; i++) {
                char line_str[32];
                snprintf(line_str, sizeof(line_str), "%d", first_visible_line + i);
                
                // Use blended rendering for macOS-like anti-aliasing
                SDL_Surface *text_surface = TTF_RenderText_Blended(line_nums->font, line_str, text_color);
                if (text_surface) {
                    // Calculate y position to align with text area 
                    // Text area starts at 20px (margin), so first line number should align with first text line
                    int y_pos = i * line_height + TEXT_MARGIN; // Align with text area starting position
                    if (y_pos < TEXT_MARGIN) y_pos = TEXT_MARGIN;  // Ensure minimum position
                    
                    SDL_Rect dst_rect = {
                        line_nums->width - text_surface->w - LINE_NUMBERS_PADDING,  // Right-aligned
                        y_pos,  // Use calculated position with bounds checking
                        text_surface->w,
                        text_surface->h
                    };
                    SDL_BlitSurface(text_surface, NULL, surface, &dst_rect);
                    SDL_FreeSurface(text_surface);
                }
            }
            
            // Create texture from surface
            line_nums->texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
        }
    }
}

void render_line_numbers(LineNumbers *line_nums, SDL_Renderer *renderer) {
    if (!line_nums->enabled || !line_nums->texture) return;
    
    // Ensure rect has valid coordinates
    if (line_nums->rect.x < 0) line_nums->rect.x = 0;
    if (line_nums->rect.y < 0) line_nums->rect.y = 0;
    if (line_nums->rect.w <= 0 || line_nums->rect.h <= 0) return;
    
    // Draw separator line
    SDL_SetRenderDrawColor(renderer, 60, 62, 70, 255);  // Slightly lighter gray for separator
    SDL_RenderDrawLine(renderer, line_nums->width - 1, 0, line_nums->width - 1, line_nums->rect.h);
    
    // Render line numbers texture using floating-point positioning for macOS-like precision
    SDL_FRect line_rect_f = {
        (float)line_nums->rect.x,
        (float)line_nums->rect.y,
        (float)line_nums->rect.w,
        (float)line_nums->rect.h
    };
    SDL_RenderCopyF(renderer, line_nums->texture, NULL, &line_rect_f);
}

void resize_line_numbers(LineNumbers *line_nums, int window_height) {
    // Line numbers should extend from top to just above status bar (24 pixels high)
    line_nums->rect.h = window_height - 24;  // STATUS_BAR_HEIGHT = 24
    line_nums->needs_update = true;
}

void toggle_line_numbers(LineNumbers *line_nums) {
    line_nums->enabled = !line_nums->enabled;
    line_nums->needs_update = true;
    if (!line_nums->enabled) {
        line_nums->width = 0;
    }
}

bool line_numbers_enabled(const LineNumbers *line_nums) {
    return line_nums->enabled;
}

int get_line_numbers_width(const LineNumbers *line_nums) {
    return line_nums->enabled ? line_nums->width : 0;
}
