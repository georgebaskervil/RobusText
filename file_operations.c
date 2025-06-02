#include "file_operations.h"
#include "debug.h"
#include "dialog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void init_document_state(DocumentState *doc)
{
    doc->filename = NULL;
    doc->filepath = NULL;
    doc->is_modified = false;
    doc->is_new_file = true;
}

void cleanup_document_state(DocumentState *doc)
{
    if (doc->filename) {
        free(doc->filename);
        doc->filename = NULL;
    }
    if (doc->filepath) {
        free(doc->filepath);
        doc->filepath = NULL;
    }
}

void set_document_filename(DocumentState *doc, const char *filepath)
{
    cleanup_document_state(doc);

    if (filepath) {
        doc->filepath = strdup(filepath);
        doc->filename = strdup(get_filename_from_path(filepath));
        doc->is_new_file = false;
    } else {
        doc->is_new_file = true;
    }
}

void mark_document_modified(DocumentState *doc, bool modified)
{
    doc->is_modified = modified;
}

const char *get_filename_from_path(const char *filepath)
{
    if (!filepath)
        return "Untitled";

    const char *filename = strrchr(filepath, '/');
    if (filename) {
        return filename + 1;
    }
    return filepath;
}

bool file_exists(const char *filepath)
{
    struct stat st;
    return (stat(filepath, &st) == 0);
}

bool open_file(const char *filepath, char **content)
{
    if (!filepath || !content)
        return false;

    FILE *file = fopen(filepath, "r");
    if (!file) {
        debug_print(L"Failed to open file: %s\n", filepath);
        return false;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    // Allocate buffer
    *content = malloc(file_size + 1);
    if (!*content) {
        fclose(file);
        debug_print(L"Failed to allocate memory for file content\n");
        return false;
    }

    // Read file
    size_t bytes_read = fread(*content, 1, file_size, file);
    (*content)[bytes_read] = '\0';

    fclose(file);
    debug_print(L"Successfully opened file: %s (%ld bytes)\n", filepath, file_size);
    return true;
}

bool save_file(const char *filepath, const char *content)
{
    if (!filepath || !content)
        return false;

    FILE *file = fopen(filepath, "w");
    if (!file) {
        debug_print(L"Failed to create/open file for writing: %s\n", filepath);
        return false;
    }

    size_t content_len = strlen(content);
    size_t bytes_written = fwrite(content, 1, content_len, file);

    fclose(file);

    if (bytes_written != content_len) {
        debug_print(L"Failed to write complete content to file: %s\n", filepath);
        return false;
    }

    debug_print(L"Successfully saved file: %s (%zu bytes)\n", filepath, content_len);
    return true;
}

bool save_file_as(const char *filepath, const char *content)
{
    return save_file(filepath, content);
}

// Simple file dialog implementation using system calls
// For a production app, you'd want to use native file dialogs
char *get_file_dialog(bool is_save)
{
    if (is_save) {
        return show_save_as_dialog();
    } else {
        return show_open_dialog();
    }
}
