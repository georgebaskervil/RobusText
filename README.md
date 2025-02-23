# RobusText

RobusText is a lightweight text rendering and editing project built using SDL2 and SDL_ttf. It is designed to handle Unicode text with advanced layout features such as combining character processing and smooth scrolling support.

## Features

- **Unicode Support:** Handles complex Unicode strings including combining characters ([`unicode_processor.c`](unicode_processor.c), [`unicode_processor.h`](unicode_processor.h)).
- **Text Rendering:** Dynamically rendered text using SDL2/SDL_ttf. Implements line wrapping and glyph layout ([`text_renderer.c`](text_renderer.c), [`text_renderer.h`](text_renderer.h)).
- **Interactive Editing:** Simple text input and editing capabilities with cursor management and selection support ([`sdl_window.c`](sdl_window.c), [`sdl_window.h`](sdl_window.h)).
- **Debug Logging:** Provides detailed runtime logs to help trace rendering and text processing events ([`debug.c`](debug.c), [`debug.h`](debug.h)).
- **Makefile Build:** Easily compile and run the project using the provided Makefile ([`Makefile`](Makefile)).

## Installation

1. **Prerequisites:**
   - SDL2 and SDL2_ttf libraries must be installed. On Mac you can install these via Homebrew:
     ```sh
     brew install sdl2 sdl2_ttf
     ```
     
2. **Clone the Repository:**
   ```sh
   git clone https://github.com/yourusername/RobusText.git
   cd RobusText
   ```

## Usage

Examples of how to use the project.

## License

License details.
