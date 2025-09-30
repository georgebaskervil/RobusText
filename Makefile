CC = gcc
CFLAGS = -O3 -march=native -mtune=native -flto -funroll-loops -fomit-frame-pointer -ffast-math -Wall -Wextra -pipe -g -I/opt/homebrew/include/SDL2 -include mimalloc_force.h
LDFLAGS = -flto -Wl,-dead_strip -L/opt/homebrew/lib

ifeq ($(findstring .dylib,$(MIMALLOC_LIB)),.dylib)
# If we're linking against the dynamic mimalloc library, add an rpath so
# the loader can find it at runtime when using an in-tree install.
LDFLAGS += -Wl,-rpath,$(MIMALLOC_DIR)/lib
endif
LIBS = -lSDL2 -lSDL2_ttf

# Mimalloc configuration (required). Set MIMALLOC_DIR to the mimalloc root if not
# installed system-wide. This Makefile will fail if mimalloc headers or the static
# library cannot be found to enforce using mimalloc by default.
# Allow `make mimalloc` and other helper targets to run without the mimalloc
# checks. Only perform checks when building the main target(s).
MIMALLOC_DIR ?= /usr/local
MIMALLOC_INCLUDE = $(MIMALLOC_DIR)/include
# Prefer dynamic library so symbols (e.g. mi_malloc) are visible to dlsym.
MIMALLOC_LIB_DY = $(MIMALLOC_DIR)/lib/libmimalloc.dylib
MIMALLOC_LIB_A  = $(MIMALLOC_DIR)/lib/libmimalloc.a

## Do not error at parse time. If the mimalloc header and libs are present
## set MIMALLOC_LIB appropriately; otherwise leave it empty so
## `ensure-mimalloc` can build the in-tree mimalloc as needed.
ifneq (,$(wildcard $(MIMALLOC_INCLUDE)/mimalloc.h))
ifneq (,$(wildcard $(MIMALLOC_LIB_DY)))
MIMALLOC_LIB = $(MIMALLOC_LIB_DY)
else ifneq (,$(wildcard $(MIMALLOC_LIB_A)))
MIMALLOC_LIB = $(MIMALLOC_LIB_A)
endif
endif

# If linking against the dynamic mimalloc library, add an rpath so dyld can
# locate it at runtime (useful for in-tree installs). This is determined
# after selecting MIMALLOC_LIB above.
ifneq (,$(findstring .dylib,$(MIMALLOC_LIB)))
MIMALLOC_RPATH = -Wl,-rpath,$(MIMALLOC_DIR)/lib
else
MIMALLOC_RPATH =
endif
TARGET = RobusText

SOURCES = main.c debug.c unicode_processor.c sdl_window.c text_renderer.c \
          file_operations.c undo_system.c search_system.c status_bar.c line_numbers.c auto_save.c dialog.c

all: $(TARGET)

$(TARGET): ensure-mimalloc $(SOURCES)
	# Link statically against mimalloc to ensure it's used by default
	$(CC) $(CFLAGS) -I$(MIMALLOC_INCLUDE) $(LDFLAGS) $(MIMALLOC_RPATH) -o $(TARGET) $(SOURCES) $(MIMALLOC_LIB) $(LIBS)

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

.PHONY: mimalloc

.PHONY: ensure-mimalloc
ensure-mimalloc:
	@# If mimalloc headers and libraries are present in MIMALLOC_DIR, do nothing.
	@# Otherwise build mimalloc in-tree and restart make with the in-tree install.
	@ if [ -f "$(MIMALLOC_INCLUDE)/mimalloc.h" ] && { [ -f "$(MIMALLOC_LIB_DY)" ] || [ -f "$(MIMALLOC_LIB_A)" ]; }; then \
		echo "mimalloc found in $(MIMALLOC_DIR)"; \
	else \
		echo "mimalloc not found in $(MIMALLOC_DIR). Building in-tree..."; \
		$(MAKE) mimalloc; \
		echo "restarting make with MIMALLOC_DIR=$(CURDIR)/third_party/mimalloc/install"; \
		$(MAKE) $(MAKECMDGOALS) MIMALLOC_DIR="$(CURDIR)/third_party/mimalloc/install"; \
		exit 0; \
	fi

# Build and install mimalloc into third_party/mimalloc/install. This target
# is idempotent: it clones the repository if missing, then configures and
# installs into the in-tree `third_party/mimalloc/install` prefix.
mimalloc:
	@./scripts/build_mimalloc.sh

.PHONY: wasm
wasm:
	@echo "Building WebAssembly (WASM) with Emscripten..."
	@if ! command -v emcc >/dev/null 2>&1; then \
		echo "emcc not found in PATH. Install Emscripten and ensure emcc is available."; exit 1; \
	fi
	# create output dir
	mkdir -p build/wasm
	# emcc flags: enable SDL2/TTF support, allow memory growth for safety, preload assets
	EMFLAGS="-O2 -s WASM=1 -s USE_SDL=2 -s USE_SDL_TTF=2 -s USE_FREETYPE=1 -s USE_LIBPNG=1 -s ALLOW_MEMORY_GROWTH=1 -s ASSERTIONS=1 -s EXIT_RUNTIME=1"
	# For wasm builds we must NOT force-include mimalloc; build without mimalloc glue
	# Provide a conservative set of CFLAGS for emscripten
	EMCFLAGS="-O2 -g0 -Wall -Wextra -I. -D__EMSCRIPTEN__ -DUSE_SDL=2 -DUSE_SDL_TTF=2 -DUSE_FREETYPE=1"
		# Include emscripten_config.h to ensure macros are available early
		EMCFLAGS="-O2 -g0 -Wall -Wextra -I. -D__EMSCRIPTEN__ -DUSE_SDL=2 -DUSE_SDL_TTF=2 -DUSE_FREETYPE=1 -include emscripten_config.h"

	# Avoid pulling native SDL headers via CFLAGS in the environment; clear CFLAGS/LDFLAGS
	CFLAGS="$$EMCFLAGS"
	LDFLAGS=
	# Source files (space-separated) - expand Makefile variable into a shell variable
	SRCS="$(SOURCES)"
	# Preload assets (font and testdata directory)
	PRELOAD="--preload-file Inter_18pt-Regular.ttf --preload-file testdata@/testdata"
	# Build using emcc
	emcc $$EMCFLAGS $$EMFLAGS $$PRELOAD -o build/wasm/RobusText.html main.c debug.c unicode_processor.c sdl_window.c \
		text_renderer.c file_operations.c undo_system.c search_system.c status_bar.c line_numbers.c auto_save.c dialog.c
	# Copy assets
	# assets are preloaded by emcc; still copy README or extras if desired
	@echo "WASM build complete: open build/wasm/RobusText.html in a browser (use a local server)"
	@echo "WASM build complete: open build/wasm/RobusText.html in a browser"
