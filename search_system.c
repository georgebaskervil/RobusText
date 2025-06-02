#include "search_system.h"
#include "debug.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void init_search_state(SearchState *search)
{
    search->search_term = NULL;
    search->replace_term = NULL; // Initialize new field
    search->match_positions = NULL;
    search->match_lengths = NULL;
    search->num_matches = 0;
    search->current_match = -1;
    search->case_sensitive = false;
    search->whole_word = false;
    search->is_active = false;
    search->replace_mode = false; // Initialize new field
}

void cleanup_search_state(SearchState *search)
{
    clear_search(search);
}

void clear_search(SearchState *search)
{
    if (search->search_term) {
        free(search->search_term);
        search->search_term = NULL;
    }
    if (search->replace_term) { // Clean up replace term
        free(search->replace_term);
        search->replace_term = NULL;
    }
    if (search->match_positions) {
        free(search->match_positions);
        search->match_positions = NULL;
    }
    if (search->match_lengths) {
        free(search->match_lengths);
        search->match_lengths = NULL;
    }
    search->num_matches = 0;
    search->current_match = -1;
    search->is_active = false;
}

static bool is_word_boundary(const char *text, int pos, int text_len)
{
    if (pos <= 0 || pos >= text_len)
        return true;

    bool prev_is_word = isalnum(text[pos - 1]) || text[pos - 1] == '_';
    bool curr_is_word = isalnum(text[pos]) || text[pos] == '_';

    return prev_is_word != curr_is_word;
}

static char *to_lowercase(const char *str)
{
    int len = strlen(str);
    char *lower = malloc(len + 1);
    if (!lower)
        return NULL;

    for (int i = 0; i < len; i++) {
        lower[i] = tolower(str[i]);
    }
    lower[len] = '\0';
    return lower;
}

void perform_search(SearchState *search, const char *text, const char *search_term)
{
    clear_search(search);

    if (!text || !search_term || strlen(search_term) == 0) {
        return;
    }

    search->search_term = strdup(search_term);
    search->is_active = true;

    const char *haystack = text;
    const char *needle = search_term;
    char *haystack_lower = NULL;
    char *needle_lower = NULL;

    // Convert to lowercase if case insensitive
    if (!search->case_sensitive) {
        haystack_lower = to_lowercase(text);
        needle_lower = to_lowercase(search_term);
        if (!haystack_lower || !needle_lower) {
            free(haystack_lower);
            free(needle_lower);
            return;
        }
        haystack = haystack_lower;
        needle = needle_lower;
    }

    int text_len = strlen(text);
    int needle_len = strlen(search_term);

    // Count matches first
    int match_count = 0;
    const char *pos = haystack;
    while ((pos = strstr(pos, needle)) != NULL) {
        int byte_pos = pos - haystack;

        // Check word boundary if whole word option is enabled
        if (search->whole_word) {
            if (!is_word_boundary(text, byte_pos, text_len) ||
                !is_word_boundary(text, byte_pos + needle_len, text_len)) {
                pos++;
                continue;
            }
        }

        match_count++;
        pos++;
    }

    if (match_count == 0) {
        free(haystack_lower);
        free(needle_lower);
        return;
    }

    // Allocate arrays for matches
    search->match_positions = malloc(match_count * sizeof(int));
    search->match_lengths = malloc(match_count * sizeof(int));
    if (!search->match_positions || !search->match_lengths) {
        free(haystack_lower);
        free(needle_lower);
        clear_search(search);
        return;
    }

    // Find all matches
    int match_index = 0;
    pos = haystack;
    while ((pos = strstr(pos, needle)) != NULL && match_index < match_count) {
        int byte_pos = pos - haystack;

        // Check word boundary if whole word option is enabled
        if (search->whole_word) {
            if (!is_word_boundary(text, byte_pos, text_len) ||
                !is_word_boundary(text, byte_pos + needle_len, text_len)) {
                pos++;
                continue;
            }
        }

        search->match_positions[match_index] = byte_pos;
        search->match_lengths[match_index] = needle_len;
        match_index++;
        pos++;
    }

    search->num_matches = match_index;
    search->current_match = search->num_matches > 0 ? 0 : -1;

    free(haystack_lower);
    free(needle_lower);

    debug_print(L"Search found %d matches for '%s'\n", search->num_matches, search_term);
}

void find_next(SearchState *search)
{
    if (!has_matches(search))
        return;

    search->current_match = (search->current_match + 1) % search->num_matches;
    debug_print(L"Moved to next match: %d/%d\n", search->current_match + 1, search->num_matches);
}

void find_previous(SearchState *search)
{
    if (!has_matches(search))
        return;

    search->current_match = (search->current_match - 1 + search->num_matches) % search->num_matches;
    debug_print(L"Moved to previous match: %d/%d\n", search->current_match + 1,
                search->num_matches);
}

int get_current_match_position(SearchState *search)
{
    if (!has_matches(search))
        return -1;
    return search->match_positions[search->current_match];
}

int get_current_match_length(SearchState *search)
{
    if (!has_matches(search))
        return 0;
    return search->match_lengths[search->current_match];
}

void set_case_sensitive(SearchState *search, bool sensitive)
{
    search->case_sensitive = sensitive;
}

void set_whole_word(SearchState *search, bool whole_word)
{
    search->whole_word = whole_word;
}

bool has_matches(const SearchState *search)
{
    return search->num_matches > 0 && search->current_match >= 0;
}

// Replace operations
void set_replace_term(SearchState *search, const char *replace_term)
{
    if (search->replace_term) {
        free(search->replace_term);
    }
    search->replace_term = strdup(replace_term);
}

char *replace_current_match(SearchState *search, const char *text)
{
    if (!has_matches(search) || !search->replace_term) {
        return strdup(text); // Return copy of original text
    }

    int match_pos = search->match_positions[search->current_match];
    int match_len = search->match_lengths[search->current_match];
    int text_len = strlen(text);
    int replace_len = strlen(search->replace_term);

    // Calculate new text length
    int new_len = text_len - match_len + replace_len;
    char *new_text = malloc(new_len + 1);

    // Copy text before match
    strncpy(new_text, text, match_pos);

    // Copy replacement text
    strcpy(new_text + match_pos, search->replace_term);

    // Copy text after match
    strcpy(new_text + match_pos + replace_len, text + match_pos + match_len);

    return new_text;
}

char *replace_all_matches(SearchState *search, const char *text)
{
    if (!has_matches(search) || !search->replace_term) {
        return strdup(text); // Return copy of original text
    }

    int text_len = strlen(text);
    int replace_len = strlen(search->replace_term);
    int total_match_len = 0;

    // Calculate total length of all matches
    for (int i = 0; i < search->num_matches; i++) {
        total_match_len += search->match_lengths[i];
    }

    // Calculate new text length
    int new_len = text_len - total_match_len + (search->num_matches * replace_len);
    char *new_text = malloc(new_len + 1);

    int src_pos = 0;
    int dst_pos = 0;

    // Process each match in order
    for (int i = 0; i < search->num_matches; i++) {
        int match_pos = search->match_positions[i];
        int match_len = search->match_lengths[i];

        // Copy text before this match
        int copy_len = match_pos - src_pos;
        strncpy(new_text + dst_pos, text + src_pos, copy_len);
        dst_pos += copy_len;

        // Copy replacement text
        strcpy(new_text + dst_pos, search->replace_term);
        dst_pos += replace_len;

        // Move source position past the match
        src_pos = match_pos + match_len;
    }

    // Copy remaining text after last match
    strcpy(new_text + dst_pos, text + src_pos);

    return new_text;
}
