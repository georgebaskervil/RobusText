#ifndef SDL_WINDOW_H
#define SDL_WINDOW_H

// Displays the SDL window using the specified font and size. If initial_file is
// non-NULL the editor will attempt to open and load that file on startup.
void display_text_window(const char *font_path, int font_size, const char *initial_file);

#endif // SDL_WINDOW_H
