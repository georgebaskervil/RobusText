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

format:
	./format.sh

check-format:
	@echo "Checking code formatting..."
	@./format.sh > /dev/null
	@if git diff --quiet; then \
		echo "✓ Code is properly formatted"; \
	else \
		echo "✗ Code formatting issues found. Run 'make format' to fix."; \
		git diff --name-only; \
		exit 1; \
	fi

install-hooks:
	@echo "Installing pre-commit hooks..."
	@if command -v pre-commit >/dev/null 2>&1; then \
		pre-commit install; \
		echo "✓ Pre-commit hooks installed"; \
	else \
		echo "Installing pre-commit..."; \
		pip3 install pre-commit; \
		pre-commit install; \
		echo "✓ Pre-commit installed and hooks configured"; \
	fi

.PHONY: all clean format check-format install-hooks
