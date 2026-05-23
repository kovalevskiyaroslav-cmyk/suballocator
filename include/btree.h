#ifndef BTREE_H
#define BTREE_H

#include "suballocator.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTREE_VERSION_MAJOR 1
#define BTREE_VERSION_MINOR 0
#define BTREE_VERSION_PATCH 0

#define BTREE_DEFAULT_ORDER 64
#define BTREE_MIN_ORDER 4
#define BTREE_MAX_ORDER 256
#define BTREE_MAX_KEY_SIZE 64

typedef enum {
    BTREE_ERROR_NONE = 0,
    BTREE_ERROR_OUT_OF_MEMORY = -1,
    BTREE_ERROR_INVALID_PARAMETER = -2,
    BTREE_ERROR_KEY_NOT_FOUND = -3,
    BTREE_ERROR_KEY_EXISTS = -4,
    BTREE_ERROR_KEY_TOO_LONG = -5,
    BTREE_ERROR_TREE_CORRUPTED = -6,
    BTREE_ERROR_IO_ERROR = -7
} btree_error_t;

typedef struct btree_stats {
    size_t num_keys;
    size_t num_nodes;
    size_t num_leaves;
    size_t height;
    size_t min_degree;
    size_t max_degree;
    size_t memory_used;
    size_t num_rotations;
    size_t num_splits;
    size_t num_merges;
    size_t num_searches;
    size_t num_inserts;
    size_t num_deletes;
    double avg_fill_factor;
} btree_stats_t;

typedef struct btree_config {
    int order;
    int allow_duplicates;
    int enable_statistics;
    int enable_validation;
    int (*key_compare)(const void* a, const void* b);
} btree_config_t; 

typedef struct btree btree_t;

const char* btree_get_error_string(btree_error_t error);

btree_config_t btree_config_default(void);

btree_t* btree_create(suballocator_t* allocator, const btree_config_t* config);
void btree_destroy(btree_t* tree);

btree_error_t btree_insert(btree_t* tree, const char* key);
int btree_search(btree_t* tree, const char* key);
btree_error_t btree_delete(btree_t* tree, const char* key);

size_t btree_size(const btree_t* tree);
int btree_is_empty(const btree_t* tree);
void btree_clear(btree_t* tree);

btree_error_t btree_range_query(btree_t* tree, const char* start_key, const char* end_key,
                                 char*** keys, size_t* count);
btree_error_t btree_get_min(btree_t* tree, char* key_buffer, size_t buffer_size);
btree_error_t btree_get_max(btree_t* tree, char* key_buffer, size_t buffer_size);

int btree_validate(const btree_t* tree);
void btree_get_stats(const btree_t* tree, btree_stats_t* stats);
void btree_print_stats(const btree_t* tree);
void btree_print_structure(const btree_t* tree);
void btree_dump_to_file(const btree_t* tree, const char* filename);
btree_t* btree_load_from_file(suballocator_t* allocator, const char* filename);
void btree_print_level_order(const btree_t* tree);

btree_error_t btree_get_last_error(const btree_t* tree);
void btree_clear_error(btree_t* tree);

#ifdef __cplusplus
}
#endif

#endif