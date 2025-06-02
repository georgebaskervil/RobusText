#ifndef UNDO_SYSTEM_H
#define UNDO_SYSTEM_H

#include <stdbool.h>

typedef enum {
    UNDO_INSERT,
    UNDO_DELETE,
    UNDO_REPLACE
} UndoType;

typedef struct UndoAction {
    UndoType type;
    int position;
    char *text;
    int length;
    int cursor_before;
    int cursor_after;
    struct UndoAction *next;
    struct UndoAction *prev;
} UndoAction;

typedef struct {
    UndoAction *current;
    UndoAction *head;
    int max_actions;
    int action_count;
} UndoSystem;

// Initialize and cleanup
void init_undo_system(UndoSystem *undo, int max_actions);
void cleanup_undo_system(UndoSystem *undo);

// Record actions
void record_insert_action(UndoSystem *undo, int position, const char *text, int cursor_before, int cursor_after);
void record_delete_action(UndoSystem *undo, int position, const char *deleted_text, int cursor_before, int cursor_after);

// Undo/Redo operations
bool can_undo(UndoSystem *undo);
bool can_redo(UndoSystem *undo);
bool perform_undo(UndoSystem *undo, char **text, int *cursor_pos);
bool perform_redo(UndoSystem *undo, char **text, int *cursor_pos);

// Utility
void clear_redo_history(UndoSystem *undo);

#endif // UNDO_SYSTEM_H
