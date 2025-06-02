CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -I/opt/homebrew/include/SDL2
LDFLAGS = -L/opt/homebrew/lib
LIBS = -lSDL2 -lSDL2_ttf
TARGET = main

SOURCES = main.c debug.c unicode_processor.c sdl_window.c text_renderer.c \
          file_operations.c undo_system.c search_system.c status_bar.c line_numbers.c auto_save.c dialog.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SOURCES) $(LIBS)

clean:
	rm -f $(TARGET)