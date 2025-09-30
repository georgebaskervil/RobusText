CC = gcc
CFLAGS = -O3 -march=native -mtune=native -flto -funroll-loops -fomit-frame-pointer -ffast-math -Wall -Wextra -pipe -g -I/opt/homebrew/include/SDL2
LDFLAGS = -flto -Wl,-dead_strip -L/opt/homebrew/lib
LIBS = -lSDL2 -lSDL2_ttf
TARGET = RobusText

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
	@temp_dir=$$(mktemp -d); \
	find . -name "*.c" -o -name "*.h" | while read -r file; do \
		cp "$$file" "$$temp_dir/"; \
		clang-format -style=file "$$file" > "$$temp_dir/$$(basename $$file).formatted"; \
		if ! diff -q "$$file" "$$temp_dir/$$(basename $$file).formatted" > /dev/null; then \
			echo "✗ $$file needs formatting"; \
			rm -rf "$$temp_dir"; \
			exit 1; \
		fi; \
	done; \
	rm -rf "$$temp_dir"; \
	echo "✓ All files are properly formatted"

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
