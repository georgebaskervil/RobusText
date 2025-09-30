/* emscripten_config.h
 * Force some defines when compiling for Emscripten so the emscripten
 * fakesdl headers are enabled and typed symbols (SDL_Renderer, TTF_Font)
 * are visible during preprocessing.
 */
#ifndef ROBUSTEXT_EMSCRIPTEN_CONFIG_H
#define ROBUSTEXT_EMSCRIPTEN_CONFIG_H

#ifndef USE_SDL
#define USE_SDL 2
#endif
#ifndef USE_SDL_TTF
#define USE_SDL_TTF 2
#endif
#ifndef USE_FREETYPE
#define USE_FREETYPE 1
#endif

#endif /* ROBUSTEXT_EMSCRIPTEN_CONFIG_H */
