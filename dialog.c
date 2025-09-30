#include "dialog.h"
#include "debug.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global dialog context
static struct {
    SDL_Renderer *renderer;
    TTF_Font *font;
    SDL_Window *window;
} dialog_ctx = {NULL, NULL, NULL};

void set_dialog_context(SDL_Renderer *renderer, TTF_Font *font, SDL_Window *window)
{
    dialog_ctx.renderer = renderer;
    dialog_ctx.font = font;
    dialog_ctx.window = window;
}

// Helper function to draw a centered dialog box
static SDL_Rect draw_dialog_background(int dialog_width, int dialog_height, const char *title)
{
    if (!dialog_ctx.renderer || !dialog_ctx.font)
        return (SDL_Rect){0, 0, 0, 0};

    int window_width, window_height;
    SDL_GetWindowSize(dialog_ctx.window, &window_width, &window_height);

    int x = (window_width - dialog_width) / 2;
    int y = (window_height - dialog_height) / 2;

    SDL_Rect dialog_rect = {x, y, dialog_width, dialog_height};

    // Draw semi-transparent overlay
    SDL_SetRenderDrawBlendMode(dialog_ctx.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(dialog_ctx.renderer, 0, 0, 0, 128);
    SDL_RenderFillRect(dialog_ctx.renderer, NULL);

    // Draw dialog background
    SDL_SetRenderDrawBlendMode(dialog_ctx.renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(dialog_ctx.renderer, 240, 240, 240, 255);
    SDL_RenderFillRect(dialog_ctx.renderer, &dialog_rect);

    // Draw border
    SDL_SetRenderDrawColor(dialog_ctx.renderer, 100, 100, 100, 255);
    SDL_RenderDrawRect(dialog_ctx.renderer, &dialog_rect);

    // Draw title if provided
    if (title && strlen(title) > 0) {
        SDL_Color text_color = {20, 20, 20, 255};
        SDL_Surface *title_surface = TTF_RenderText_Solid(dialog_ctx.font, title, text_color);
        if (title_surface) {
            SDL_Texture *title_texture =
                SDL_CreateTextureFromSurface(dialog_ctx.renderer, title_surface);
            if (title_texture) {
                SDL_Rect title_rect = {x + 10, y + 10, title_surface->w, title_surface->h};
                SDL_RenderCopy(dialog_ctx.renderer, title_texture, NULL, &title_rect);
                SDL_DestroyTexture(title_texture);
            }
            SDL_FreeSurface(title_surface);
        }
    }

    return dialog_rect;
}

// Helper function to draw a button
static SDL_Rect draw_button(int x, int y, int width, int height, const char *text, bool highlighted)
{
    SDL_Rect button_rect = {x, y, width, height};

    // Draw button background
    if (highlighted) {
        SDL_SetRenderDrawColor(dialog_ctx.renderer, 0, 120, 215, 255); // Blue highlight
    } else {
        SDL_SetRenderDrawColor(dialog_ctx.renderer, 225, 225, 225, 255); // Light gray
    }
    SDL_RenderFillRect(dialog_ctx.renderer, &button_rect);

    // Draw button border
    SDL_SetRenderDrawColor(dialog_ctx.renderer, 173, 173, 173, 255);
    SDL_RenderDrawRect(dialog_ctx.renderer, &button_rect);

    // Draw button text using blended rendering for macOS-like anti-aliasing
    SDL_Color text_color =
        highlighted ? (SDL_Color){255, 255, 255, 255} : (SDL_Color){20, 20, 20, 255};
    SDL_Surface *text_surface = TTF_RenderText_Blended(dialog_ctx.font, text, text_color);
    if (text_surface) {
        SDL_Texture *text_texture = SDL_CreateTextureFromSurface(dialog_ctx.renderer, text_surface);
        if (text_texture) {
            SDL_Rect text_rect = {x + (width - text_surface->w) / 2,
                                  y + (height - text_surface->h) / 2, text_surface->w,
                                  text_surface->h};
            SDL_RenderCopy(dialog_ctx.renderer, text_texture, NULL, &text_rect);
            SDL_DestroyTexture(text_texture);
        }
        SDL_FreeSurface(text_surface);
    }

    return button_rect;
}

// Helper function to draw text using blended rendering for macOS-like anti-aliasing
static void draw_text(int x, int y, const char *text, SDL_Color color)
{
    SDL_Surface *text_surface = TTF_RenderText_Blended(dialog_ctx.font, text, color);
    if (text_surface) {
        SDL_Texture *text_texture = SDL_CreateTextureFromSurface(dialog_ctx.renderer, text_surface);
        if (text_texture) {
            SDL_Rect text_rect = {x, y, text_surface->w, text_surface->h};
            SDL_RenderCopy(dialog_ctx.renderer, text_texture, NULL, &text_rect);
            SDL_DestroyTexture(text_texture);
        }
        SDL_FreeSurface(text_surface);
    }
}

DialogResult show_save_confirmation_dialog(const char *filename)
{
    debug_print(L"Showing save confirmation dialog for: %s\n", filename ? filename : "Untitled");

    if (!dialog_ctx.renderer || !dialog_ctx.font) {
        debug_print(L"Dialog context not set, falling back to debug output\n");
        return DIALOG_CANCEL;
    }

    const int dialog_width = 400;
    const int dialog_height = 150;
    const int button_width = 80;
    const int button_height = 30;

    int selected_button = 0; // 0=Yes, 1=No, 2=Cancel
    bool dialog_running = true;
    DialogResult result = DIALOG_CANCEL;

    while (dialog_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                result = DIALOG_CANCEL;
                dialog_running = false;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    result = DIALOG_CANCEL;
                    dialog_running = false;
                } else if (event.key.keysym.sym == SDLK_RETURN ||
                           event.key.keysym.sym == SDLK_KP_ENTER) {
                    result = (selected_button == 0)   ? DIALOG_YES
                             : (selected_button == 1) ? DIALOG_NO
                                                      : DIALOG_CANCEL;
                    dialog_running = false;
                } else if (event.key.keysym.sym == SDLK_LEFT ||
                           event.key.keysym.sym == SDLK_RIGHT || event.key.keysym.sym == SDLK_TAB) {
                    if (event.key.keysym.sym == SDLK_LEFT ||
                        (event.key.keysym.sym == SDLK_TAB && (event.key.keysym.mod & KMOD_SHIFT))) {
                        selected_button = (selected_button + 2) % 3;
                    } else {
                        selected_button = (selected_button + 1) % 3;
                    }
                } else if (event.key.keysym.sym == SDLK_y) {
                    result = DIALOG_YES;
                    dialog_running = false;
                } else if (event.key.keysym.sym == SDLK_n) {
                    result = DIALOG_NO;
                    dialog_running = false;
                } else if (event.key.keysym.sym == SDLK_c) {
                    result = DIALOG_CANCEL;
                    dialog_running = false;
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int window_width, window_height;
                    SDL_GetWindowSize(dialog_ctx.window, &window_width, &window_height);
                    int dialog_x = (window_width - dialog_width) / 2;
                    int dialog_y = (window_height - dialog_height) / 2;

                    int button_y = dialog_y + dialog_height - 50;
                    int yes_x = dialog_x + 20;
                    int no_x = dialog_x + 120;
                    int cancel_x = dialog_x + 220;

                    if (event.button.y >= button_y && event.button.y <= button_y + button_height) {
                        if (event.button.x >= yes_x && event.button.x <= yes_x + button_width) {
                            result = DIALOG_YES;
                            dialog_running = false;
                        } else if (event.button.x >= no_x &&
                                   event.button.x <= no_x + button_width) {
                            result = DIALOG_NO;
                            dialog_running = false;
                        } else if (event.button.x >= cancel_x &&
                                   event.button.x <= cancel_x + button_width) {
                            result = DIALOG_CANCEL;
                            dialog_running = false;
                        }
                    }
                }
            }
        }

        // Clear screen
        SDL_SetRenderDrawColor(dialog_ctx.renderer, 40, 40, 40, 255);
        SDL_RenderClear(dialog_ctx.renderer);

        // Draw dialog
        SDL_Rect dialog_rect =
            draw_dialog_background(dialog_width, dialog_height, "Unsaved Changes");

        // Draw message
        char message[256];
        if (filename) {
            snprintf(message, sizeof(message), "The file '%s' has unsaved changes.", filename);
        } else {
            strcpy(message, "The document has unsaved changes.");
        }

        SDL_Color text_color = {20, 20, 20, 255};
        draw_text(dialog_rect.x + 10, dialog_rect.y + 40, message, text_color);
        draw_text(dialog_rect.x + 10, dialog_rect.y + 65, "Do you want to save before continuing?",
                  text_color);

        // Draw buttons
        int button_y = dialog_rect.y + dialog_height - 50;
        draw_button(dialog_rect.x + 20, button_y, button_width, button_height, "Yes",
                    selected_button == 0);
        draw_button(dialog_rect.x + 120, button_y, button_width, button_height, "No",
                    selected_button == 1);
        draw_button(dialog_rect.x + 220, button_y, button_width, button_height, "Cancel",
                    selected_button == 2);

        SDL_RenderPresent(dialog_ctx.renderer);
        SDL_Delay(16); // ~60fps
    }

    debug_print(L"Save confirmation dialog result: %d\n", result);
    return result;
}

bool show_error_dialog(const char *title, const char *message)
{
    debug_print(L"Showing error dialog: %s - %s\n", title, message);

    if (!dialog_ctx.renderer || !dialog_ctx.font) {
        debug_print(L"Dialog context not set, falling back to debug output\n");
        return true;
    }

    const int dialog_width = 400;
    const int dialog_height = 120;
    const int button_width = 80;
    const int button_height = 30;

    bool dialog_running = true;

    while (dialog_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                dialog_running = false;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_RETURN ||
                    event.key.keysym.sym == SDLK_KP_ENTER || event.key.keysym.sym == SDLK_SPACE) {
                    dialog_running = false;
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int window_width, window_height;
                    SDL_GetWindowSize(dialog_ctx.window, &window_width, &window_height);
                    int dialog_x = (window_width - dialog_width) / 2;
                    int dialog_y = (window_height - dialog_height) / 2;

                    int button_y = dialog_y + dialog_height - 50;
                    int button_x = dialog_x + (dialog_width - button_width) / 2;

                    if (event.button.x >= button_x && event.button.x <= button_x + button_width &&
                        event.button.y >= button_y && event.button.y <= button_y + button_height) {
                        dialog_running = false;
                    }
                }
            }
        }

        // Clear screen
        SDL_SetRenderDrawColor(dialog_ctx.renderer, 40, 40, 40, 255);
        SDL_RenderClear(dialog_ctx.renderer);

        // Draw dialog
        SDL_Rect dialog_rect = draw_dialog_background(dialog_width, dialog_height, title);

        // Draw message
        SDL_Color text_color = {20, 20, 20, 255};
        draw_text(dialog_rect.x + 10, dialog_rect.y + 40, message, text_color);

        // Draw OK button
        int button_x = dialog_rect.x + (dialog_width - button_width) / 2;
        int button_y = dialog_rect.y + dialog_height - 50;
        draw_button(button_x, button_y, button_width, button_height, "OK", true);

        SDL_RenderPresent(dialog_ctx.renderer);
        SDL_Delay(16); // ~60fps
    }

    return true;
}

char *show_save_as_dialog(void)
{
    debug_print(L"Showing save as dialog\n");

    if (!dialog_ctx.renderer || !dialog_ctx.font) {
        debug_print(L"Dialog context not set, falling back to debug output\n");
        return NULL;
    }

    const int dialog_width = 450;
    const int dialog_height = 140;
    const int button_width = 80;
    const int button_height = 30;
    const int input_height = 25;

    char input_buffer[256] = {0};
    int input_pos = 0;
    bool dialog_running = true;
    char *result = NULL;

    SDL_StartTextInput();

    while (dialog_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                dialog_running = false;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    dialog_running = false;
                } else if (event.key.keysym.sym == SDLK_RETURN ||
                           event.key.keysym.sym == SDLK_KP_ENTER) {
                    if (strlen(input_buffer) > 0) {
                        result = malloc(strlen(input_buffer) + 1);
                        if (result) {
                            strcpy(result, input_buffer);
                        }
                    }
                    dialog_running = false;
                } else if (event.key.keysym.sym == SDLK_BACKSPACE && input_pos > 0) {
                    input_pos--;
                    input_buffer[input_pos] = '\0';
                }
            } else if (event.type == SDL_TEXTINPUT) {
                size_t new_len = strlen(event.text.text);
                if (input_pos + new_len < sizeof(input_buffer) - 1) {
                    strcpy(input_buffer + input_pos, event.text.text);
                    input_pos += (int) new_len;
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int window_width, window_height;
                    SDL_GetWindowSize(dialog_ctx.window, &window_width, &window_height);
                    int dialog_x = (window_width - dialog_width) / 2;
                    int dialog_y = (window_height - dialog_height) / 2;

                    int button_y = dialog_y + dialog_height - 50;
                    int save_x = dialog_x + dialog_width - 190;
                    int cancel_x = dialog_x + dialog_width - 90;

                    if (event.button.y >= button_y && event.button.y <= button_y + button_height) {
                        if (event.button.x >= save_x && event.button.x <= save_x + button_width) {
                            if (strlen(input_buffer) > 0) {
                                result = malloc(strlen(input_buffer) + 1);
                                if (result) {
                                    strcpy(result, input_buffer);
                                }
                            }
                            dialog_running = false;
                        } else if (event.button.x >= cancel_x &&
                                   event.button.x <= cancel_x + button_width) {
                            dialog_running = false;
                        }
                    }
                }
            }
        }

        // Clear screen
        SDL_SetRenderDrawColor(dialog_ctx.renderer, 40, 40, 40, 255);
        SDL_RenderClear(dialog_ctx.renderer);

        // Draw dialog
        SDL_Rect dialog_rect = draw_dialog_background(dialog_width, dialog_height, "Save As");

        // Draw prompt
        SDL_Color text_color = {20, 20, 20, 255};
        draw_text(dialog_rect.x + 10, dialog_rect.y + 40, "Enter filename:", text_color);

        // Draw input field
        SDL_Rect input_rect = {dialog_rect.x + 10, dialog_rect.y + 65, dialog_width - 20,
                               input_height};
        SDL_SetRenderDrawColor(dialog_ctx.renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(dialog_ctx.renderer, &input_rect);
        SDL_SetRenderDrawColor(dialog_ctx.renderer, 100, 100, 100, 255);
        SDL_RenderDrawRect(dialog_ctx.renderer, &input_rect);

        // Draw input text
        if (strlen(input_buffer) > 0) {
            draw_text(input_rect.x + 5, input_rect.y + 3, input_buffer, text_color);
        }

        // Draw cursor
        static int cursor_blink = 0;
        cursor_blink = (cursor_blink + 1) % 60;
        if (cursor_blink < 30) {
            int text_width = 0;
            if (strlen(input_buffer) > 0) {
                TTF_SizeText(dialog_ctx.font, input_buffer, &text_width, NULL);
            }
            SDL_SetRenderDrawColor(dialog_ctx.renderer, 0, 0, 0, 255);
            SDL_RenderDrawLine(dialog_ctx.renderer, input_rect.x + 5 + text_width, input_rect.y + 3,
                               input_rect.x + 5 + text_width, input_rect.y + input_height - 3);
        }

        // Draw buttons
        int button_y = dialog_rect.y + dialog_height - 50;
        draw_button(dialog_rect.x + dialog_width - 190, button_y, button_width, button_height,
                    "Save", false);
        draw_button(dialog_rect.x + dialog_width - 90, button_y, button_width, button_height,
                    "Cancel", false);

        SDL_RenderPresent(dialog_ctx.renderer);
        SDL_Delay(16); // ~60fps
    }

    SDL_StopTextInput();
    debug_print(L"Save as dialog result: %s\n", result ? result : "NULL");
    return result;
}

char *show_open_dialog(void)
{
    debug_print(L"Showing open dialog\n");

    if (!dialog_ctx.renderer || !dialog_ctx.font) {
        debug_print(L"Dialog context not set, falling back to debug output\n");
        return NULL;
    }

    const int dialog_width = 450;
    const int dialog_height = 140;
    const int button_width = 80;
    const int button_height = 30;
    const int input_height = 25;

    char input_buffer[256] = {0};
    int input_pos = 0;
    bool dialog_running = true;
    char *result = NULL;

    SDL_StartTextInput();

    while (dialog_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                dialog_running = false;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    dialog_running = false;
                } else if (event.key.keysym.sym == SDLK_RETURN ||
                           event.key.keysym.sym == SDLK_KP_ENTER) {
                    if (strlen(input_buffer) > 0) {
                        result = malloc(strlen(input_buffer) + 1);
                        if (result) {
                            strcpy(result, input_buffer);
                        }
                    }
                    dialog_running = false;
                } else if (event.key.keysym.sym == SDLK_BACKSPACE && input_pos > 0) {
                    input_pos--;
                    input_buffer[input_pos] = '\0';
                }
            } else if (event.type == SDL_TEXTINPUT) {
                size_t new_len = strlen(event.text.text);
                if (input_pos + new_len < sizeof(input_buffer) - 1) {
                    strcpy(input_buffer + input_pos, event.text.text);
                    input_pos += (int) new_len;
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int window_width, window_height;
                    SDL_GetWindowSize(dialog_ctx.window, &window_width, &window_height);
                    int dialog_x = (window_width - dialog_width) / 2;
                    int dialog_y = (window_height - dialog_height) / 2;

                    int button_y = dialog_y + dialog_height - 50;
                    int open_x = dialog_x + dialog_width - 190;
                    int cancel_x = dialog_x + dialog_width - 90;

                    if (event.button.y >= button_y && event.button.y <= button_y + button_height) {
                        if (event.button.x >= open_x && event.button.x <= open_x + button_width) {
                            if (strlen(input_buffer) > 0) {
                                result = malloc(strlen(input_buffer) + 1);
                                if (result) {
                                    strcpy(result, input_buffer);
                                }
                            }
                            dialog_running = false;
                        } else if (event.button.x >= cancel_x &&
                                   event.button.x <= cancel_x + button_width) {
                            dialog_running = false;
                        }
                    }
                }
            }
        }

        // Clear screen
        SDL_SetRenderDrawColor(dialog_ctx.renderer, 40, 40, 40, 255);
        SDL_RenderClear(dialog_ctx.renderer);

        // Draw dialog
        SDL_Rect dialog_rect = draw_dialog_background(dialog_width, dialog_height, "Open File");

        // Draw prompt
        SDL_Color text_color = {20, 20, 20, 255};
        draw_text(dialog_rect.x + 10, dialog_rect.y + 40, "Enter filename:", text_color);

        // Draw input field
        SDL_Rect input_rect = {dialog_rect.x + 10, dialog_rect.y + 65, dialog_width - 20,
                               input_height};
        SDL_SetRenderDrawColor(dialog_ctx.renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(dialog_ctx.renderer, &input_rect);
        SDL_SetRenderDrawColor(dialog_ctx.renderer, 100, 100, 100, 255);
        SDL_RenderDrawRect(dialog_ctx.renderer, &input_rect);

        // Draw input text
        if (strlen(input_buffer) > 0) {
            draw_text(input_rect.x + 5, input_rect.y + 3, input_buffer, text_color);
        }

        // Draw cursor
        static int cursor_blink = 0;
        cursor_blink = (cursor_blink + 1) % 60;
        if (cursor_blink < 30) {
            int text_width = 0;
            if (strlen(input_buffer) > 0) {
                TTF_SizeText(dialog_ctx.font, input_buffer, &text_width, NULL);
            }
            SDL_SetRenderDrawColor(dialog_ctx.renderer, 0, 0, 0, 255);
            SDL_RenderDrawLine(dialog_ctx.renderer, input_rect.x + 5 + text_width, input_rect.y + 3,
                               input_rect.x + 5 + text_width, input_rect.y + input_height - 3);
        }

        // Draw buttons
        int button_y = dialog_rect.y + dialog_height - 50;
        draw_button(dialog_rect.x + dialog_width - 190, button_y, button_width, button_height,
                    "Open", false);
        draw_button(dialog_rect.x + dialog_width - 90, button_y, button_width, button_height,
                    "Cancel", false);

        SDL_RenderPresent(dialog_ctx.renderer);
        SDL_Delay(16); // ~60fps
    }

    SDL_StopTextInput();
    debug_print(L"Open dialog result: %s\n", result ? result : "NULL");
    return result;
}
