#ifndef AUTO_SAVE_H
#define AUTO_SAVE_H

#include "file_operations.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t last_save_time;
    uint32_t save_interval; // in milliseconds
    bool enabled;
    bool needs_save;
} AutoSave;

// Initialize and configure
void init_auto_save(AutoSave *auto_save, uint32_t interval_ms);
void set_auto_save_enabled(AutoSave *auto_save, bool enabled);
void set_auto_save_interval(AutoSave *auto_save, uint32_t interval_ms);

// Check and perform auto-save
bool should_auto_save(AutoSave *auto_save, bool is_modified);
void mark_for_auto_save(AutoSave *auto_save);
void reset_auto_save_timer(AutoSave *auto_save);
bool perform_auto_save(AutoSave *auto_save, DocumentState *doc, const char *text);

#endif
