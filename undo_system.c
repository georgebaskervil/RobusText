#include "undo_system.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>

void init_undo_system(UndoSystem *undo, int max_actions)
{
    undo->current = NULL;
    undo->head = NULL;
    undo->max_actions = max_actions;
    undo->action_count = 0;
}

void cleanup_undo_system(UndoSystem *undo)
{
    UndoAction *action = undo->head;
    while (action) {
        UndoAction *next = action->next;
        if (action->text) {
            free(action->text);
        }
        free(action);
        action = next;
    }
    undo->current = NULL;
    undo->head = NULL;
    undo->action_count = 0;
}

static void add_action(UndoSystem *undo, UndoAction *action)
{
    // Clear any redo history when adding a new action
    clear_redo_history(undo);

    // Link the new action
    action->prev = undo->current;
    action->next = NULL;

    if (undo->current) {
        undo->current->next = action;
    } else {
        undo->head = action;
    }

    undo->current = action;
    undo->action_count++;

    // Remove old actions if we exceed max_actions
    while (undo->action_count > undo->max_actions && undo->head) {
        UndoAction *old = undo->head;
        undo->head = old->next;
        if (undo->head) {
            undo->head->prev = NULL;
        }
        if (old->text) {
            free(old->text);
        }
        free(old);
        undo->action_count--;
    }
}

void record_insert_action(UndoSystem *undo, int position, const char *text, int cursor_before,
                          int cursor_after)
{
    UndoAction *action = malloc(sizeof(UndoAction));
    if (!action)
        return;

    action->type = UNDO_INSERT;
    action->position = position;
    action->text = strdup(text);
    action->length = strlen(text);
    action->cursor_before = cursor_before;
    action->cursor_after = cursor_after;

    add_action(undo, action);
    debug_print(L"Recorded insert action: pos=%d, text='%s'\n", position, text);
}

void record_delete_action(UndoSystem *undo, int position, const char *deleted_text,
                          int cursor_before, int cursor_after)
{
    UndoAction *action = malloc(sizeof(UndoAction));
    if (!action)
        return;

    action->type = UNDO_DELETE;
    action->position = position;
    action->text = strdup(deleted_text);
    action->length = strlen(deleted_text);
    action->cursor_before = cursor_before;
    action->cursor_after = cursor_after;

    add_action(undo, action);
    debug_print(L"Recorded delete action: pos=%d, text='%s'\n", position, deleted_text);
}

bool can_undo(UndoSystem *undo)
{
    return undo->current != NULL;
}

bool can_redo(UndoSystem *undo)
{
    return undo->current && undo->current->next;
}

bool perform_undo(UndoSystem *undo, char **text, int *cursor_pos)
{
    if (!can_undo(undo))
        return false;

    UndoAction *action = undo->current;
    int text_len = strlen(*text);

    switch (action->type) {
        case UNDO_INSERT: {
            // Remove the inserted text
            int new_len = text_len - action->length;
            char *new_text = malloc(new_len + 1);
            if (!new_text)
                return false;

            memcpy(new_text, *text, action->position);
            memcpy(new_text + action->position, *text + action->position + action->length,
                   text_len - action->position - action->length);
            new_text[new_len] = '\0';

            free(*text);
            *text = new_text;
            *cursor_pos = action->cursor_before;
            break;
        }

        case UNDO_DELETE: {
            // Restore the deleted text
            int new_len = text_len + action->length;
            char *new_text = malloc(new_len + 1);
            if (!new_text)
                return false;

            memcpy(new_text, *text, action->position);
            memcpy(new_text + action->position, action->text, action->length);
            memcpy(new_text + action->position + action->length, *text + action->position,
                   text_len - action->position);
            new_text[new_len] = '\0';

            free(*text);
            *text = new_text;
            *cursor_pos = action->cursor_before;
            break;
        }

        default:
            return false;
    }

    undo->current = action->prev;
    debug_print(L"Performed undo: type=%d, pos=%d\n", action->type, action->position);
    return true;
}

bool perform_redo(UndoSystem *undo, char **text, int *cursor_pos)
{
    if (!can_redo(undo))
        return false;

    UndoAction *action = undo->current ? undo->current->next : undo->head;
    if (!action)
        return false;

    int text_len = strlen(*text);

    switch (action->type) {
        case UNDO_INSERT: {
            // Re-insert the text
            int new_len = text_len + action->length;
            char *new_text = malloc(new_len + 1);
            if (!new_text)
                return false;

            memcpy(new_text, *text, action->position);
            memcpy(new_text + action->position, action->text, action->length);
            memcpy(new_text + action->position + action->length, *text + action->position,
                   text_len - action->position);
            new_text[new_len] = '\0';

            free(*text);
            *text = new_text;
            *cursor_pos = action->cursor_after;
            break;
        }

        case UNDO_DELETE: {
            // Re-delete the text
            int new_len = text_len - action->length;
            char *new_text = malloc(new_len + 1);
            if (!new_text)
                return false;

            memcpy(new_text, *text, action->position);
            memcpy(new_text + action->position, *text + action->position + action->length,
                   text_len - action->position - action->length);
            new_text[new_len] = '\0';

            free(*text);
            *text = new_text;
            *cursor_pos = action->cursor_after;
            break;
        }

        default:
            return false;
    }

    undo->current = action;
    debug_print(L"Performed redo: type=%d, pos=%d\n", action->type, action->position);
    return true;
}

void clear_redo_history(UndoSystem *undo)
{
    if (!undo->current || !undo->current->next)
        return;

    UndoAction *action = undo->current->next;
    while (action) {
        UndoAction *next = action->next;
        if (action->text) {
            free(action->text);
        }
        free(action);
        undo->action_count--;
        action = next;
    }

    undo->current->next = NULL;
}
