# RobusText - Feature Complete Text Editor

RobusText is a **feature-complete text editor** built using SDL2 and SDL_ttf. What started as a lightweight text rendering project has evolved into a fully functional text editor with all the essential features users expect from modern text editing software.

## 🎯 Complete Feature Set

### Core Text Editing
- **Unicode Support**: Full UTF-8 text input and rendering with combining characters
- **Text Selection**: Mouse and keyboard-based selection with visual feedback
- **Clipboard Operations**: Copy (Cmd+C), Paste (Cmd+V), Cut (Cmd+X)
- **Select All**: Complete document selection (Cmd+A)
- **Undo/Redo System**: 100-action history with Cmd+Z/Cmd+Y

### File Management
- **File Operations**: New (Cmd+N), Open (Cmd+O), Save (Cmd+S)
- **Document State**: Filename tracking and modification status
- **Unsaved Changes**: Confirmation dialogs before data loss
- **Auto-Save**: Automatic saving every 30 seconds (Cmd+Shift+S to toggle)

### Search & Replace
- **Interactive Search**: Live search with Cmd+F
- **Replace Functionality**: Text replacement with Cmd+H
- **Search Navigation**: Find next/previous through matches
- **Visual Highlighting**: Current and all matches highlighted
- **Search Options**: Case sensitivity and whole word matching

### User Interface
- **Status Bar**: Filename, modification status, cursor position, search results
- **Line Numbers**: Toggleable display with Cmd+L
- **Professional Layout**: Clean interface with Inter font
- **Responsive Design**: Proper text wrapping and window management

### Advanced Navigation
- **Word Movement**: Alt+Arrow for word-by-word navigation
- **Line Navigation**: Cmd+Arrow for line start/end
- **Mouse Support**: Click anywhere to position cursor
- **Keyboard Shortcuts**: All standard text editor shortcuts

## 🏗️ Architecture

The editor features a **modular architecture** with separate systems:

- **Core Modules**: Window management, text rendering, Unicode processing
- **Feature Modules**: File operations, undo system, search system, status bar, line numbers, auto-save
- **Clean Separation**: Each feature in its own module with clear interfaces
- **Memory Safety**: Proper resource management and cleanup

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

The editor runs entirely through the GUI - no CLI interactions required.

```sh
# Compile the project
make

# Run the editor
./main

# Run with debug output (silent mode)
./main --debug

# Show help
./main --help

# Run automated tests
./test_features.sh
```

**Key Features:**
- File operations use sensible defaults (auto-save, default filenames)
- Error handling is non-intrusive (logged to debug output)
- Open file defaults to `demo.txt` if available
- Save operations generate automatic filenames when needed

### Quick Testing
1. **Start the editor**: `./main`
2. **Type some text**: Enter a few lines of text
3. **Test undo**: Press `Cmd+Z` to undo, `Cmd+Y` to redo
4. **Test search**: Press `Cmd+F` to search text
5. **Save file**: Press `Cmd+S` to save

### Documentation
- See `FEATURES.md` for complete feature list and keyboard shortcuts
- See `PROJECT_SUMMARY.md` for detailed technical overview
- Run `./test_features.sh` for automated functionality tests

## Recent Updates

**Cleanup (June 2025):**
- Removed all CLI interactions for seamless automation
- Auto-save behavior for unsaved changes
- Default file operations without user prompts
- Silent error handling with debug logging

## License

This project is open source. See `LICENSE` for details.
Font licensing: See `LICENSE-INTER.txt` for Inter font license.
