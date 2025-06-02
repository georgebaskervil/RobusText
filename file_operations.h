#ifndef FILE_OPERATIONS_H
#define FILE_OPERATIONS_H

#include <stdbool.h>

// File operations structure to track document state
typedef struct {
    char *filename;
    char *filepath;
    bool is_modified;
    bool is_new_file;
} DocumentState;

// File operation functions
bool open_file(const char *filepath, char **content);
bool save_file(const char *filepath, const char *content);
bool save_file_as(const char *filepath, const char *content);
char *get_file_dialog(bool is_save);
void init_document_state(DocumentState *doc);
void cleanup_document_state(DocumentState *doc);
void set_document_filename(DocumentState *doc, const char *filepath);
void mark_document_modified(DocumentState *doc, bool modified);

// Utility functions
const char *get_filename_from_path(const char *filepath);
bool file_exists(const char *filepath);

#endif // FILE_OPERATIONS_H
