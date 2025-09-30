/* platform_sdl.h
 * Include a platform-compatible SDL header. On native builds, include the
 * system SDL headers. On Emscripten builds, avoid pulling Homebrew headers
 * and instead provide minimal forward declarations that satisfy compilation
 * (the emscripten SDL port will be linked at link-time).
 */
#ifndef ROBUSTEXT_PLATFORM_SDL_H
#define ROBUSTEXT_PLATFORM_SDL_H

/* Provide lightweight forward declarations for public headers so they don't
   need to include heavy SDL headers. Translation units (.c files) that need
   SDL functions should include <SDL.h> and <SDL_ttf.h> directly. */
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_Surface SDL_Surface;

#endif /* ROBUSTEXT_PLATFORM_SDL_H */
