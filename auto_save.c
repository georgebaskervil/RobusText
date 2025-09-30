#include "auto_save.h"
#include "debug.h"
#include <SDL.h>
#include <stdint.h>

#define DEFAULT_AUTO_SAVE_INTERVAL 60000 // 60 seconds

void init_auto_save(AutoSave *auto_save, uint32_t interval_ms)
{
    auto_save->last_save_time = SDL_GetTicks();
    auto_save->save_interval = interval_ms > 0 ? interval_ms : DEFAULT_AUTO_SAVE_INTERVAL;
    auto_save->enabled = true; // Enable by default
    auto_save->needs_save = false;
}

void set_auto_save_enabled(AutoSave *auto_save, bool enabled)
{
    auto_save->enabled = enabled;
    if (enabled) {
        reset_auto_save_timer(auto_save);
    }
}

void set_auto_save_interval(AutoSave *auto_save, uint32_t interval_ms)
{
    auto_save->save_interval = interval_ms > 1000 ? interval_ms : 1000; // Minimum 1 second
    reset_auto_save_timer(auto_save);
}

bool should_auto_save(AutoSave *auto_save, bool is_modified)
{
    if (!auto_save->enabled || !is_modified) {
        return false;
    }

    uint32_t current_time = SDL_GetTicks();
    return (current_time - auto_save->last_save_time) >= auto_save->save_interval;
}

void mark_for_auto_save(AutoSave *auto_save)
{
    auto_save->needs_save = true;
}

void reset_auto_save_timer(AutoSave *auto_save)
{
    auto_save->last_save_time = SDL_GetTicks();
    auto_save->needs_save = false;
}

bool perform_auto_save(AutoSave *auto_save, DocumentState *doc, const char *text)
{
    if (!auto_save->enabled || !doc->filename) {
        return false;
    }

    // Create auto-save filename by appending .autosave
    char auto_save_path[512];
    snprintf(auto_save_path, sizeof(auto_save_path), "%s.autosave", doc->filename);

    if (save_file(auto_save_path, text)) {
        reset_auto_save_timer(auto_save);
        debug_print(L"Auto-saved to %s\n", auto_save_path);
        return true;
    }

    debug_print(L"Auto-save failed for %s\n", auto_save_path);
    return false;
}
