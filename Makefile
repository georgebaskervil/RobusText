CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -I/opt/homebrew/include/SDL2
LDFLAGS = -L/opt/homebrew/lib
LIBS = -lSDL2 -lSDL2_ttf
TARGET = main

all: $(TARGET)

$(TARGET): main.c debug.c unicode_processor.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) main.c debug.c unicode_processor.c sdl_window.c text_renderer.c $(LIBS)

clean:
	rm -f $(TARGET)