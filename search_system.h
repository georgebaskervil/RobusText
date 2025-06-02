#ifndef SEARCH_SYSTEM_H
#define SEARCH_SYSTEM_H

#include <stdbool.h>

typedef struct {
    char *search_term;
    char *replace_term; // New field for replace functionality
    int *match_positions;
    int *match_lengths;
    int num_matches;
    int current_match;
    bool case_sensitive;
    bool whole_word;
    bool is_active;
    bool replace_mode; // New field to track if in replace mode
} SearchState;

// Initialize and cleanup
void init_search_state(SearchState *search);
void cleanup_search_state(SearchState *search);

// Search operations
void perform_search(SearchState *search, const char *text, const char *search_term);
void find_next(SearchState *search);
void find_previous(SearchState *search);
int get_current_match_position(SearchState *search);
int get_current_match_length(SearchState *search);

// Replace operations
void set_replace_term(SearchState *search, const char *replace_term);
char *replace_current_match(SearchState *search, const char *text);
char *replace_all_matches(SearchState *search, const char *text);

// Search options
void set_case_sensitive(SearchState *search, bool sensitive);
void set_whole_word(SearchState *search, bool whole_word);

// Utility
bool has_matches(const SearchState *search);
int get_match_count(const SearchState *search);
void clear_search(SearchState *search);

#endif // SEARCH_SYSTEM_H
