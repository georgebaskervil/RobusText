#ifndef DIALOG_H
#define DIALOG_H

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>

typedef enum { DIALOG_YES, DIALOG_NO, DIALOG_CANCEL } DialogResult;

// GUI-based dialog system
DialogResult show_save_confirmation_dialog(const char *filename);
bool show_error_dialog(const char *title, const char *message);
char *show_save_as_dialog(void);
char *show_open_dialog(void);

// Internal dialog rendering functions
void set_dialog_context(SDL_Renderer *renderer, TTF_Font *font, SDL_Window *window);

#endif
