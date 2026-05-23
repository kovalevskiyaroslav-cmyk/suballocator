#ifndef BTREE_INDEX_H
#define BTREE_INDEX_H

#include "btree.h"
#include "suballocator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BTREE_INDEX_VERSION_MAJOR 1
#define BTREE_INDEX_VERSION_MINOR 0
#define BTREE_INDEX_VERSION_PATCH 0

typedef struct btree_index btree_index_t;

typedef struct btree_index_config {
    size_t initial_memory_pool;
    size_t max_memory_pool;
    int btree_order;
    int allow_duplicates;
    int enable_statistics;
    int enable_auto_defrag;
    size_t defrag_threshold;
    const char* persist_path;
} btree_index_config_t;

btree_index_config_t btree_index_config_default(void);

btree_index_t* btree_index_create(const btree_index_config_t* config);
void btree_index_destroy(btree_index_t* index);

int btree_index_insert(btree_index_t* index, const char* key);
int btree_index_search(btree_index_t* index, const char* key);
int btree_index_delete(btree_index_t* index, const char* key);

size_t btree_index_size(const btree_index_t* index);
int btree_index_is_empty(const btree_index_t* index); 
void btree_index_clear(btree_index_t* index);

int btree_index_range_query(btree_index_t* index, const char* start_key, const char* end_key,
                            char*** keys, size_t* count);
int btree_index_get_min(btree_index_t* index, char* key_buffer, size_t buffer_size);
int btree_index_get_max(btree_index_t* index, char* key_buffer, size_t buffer_size);

void btree_index_print_stats(const btree_index_t* index);
void btree_index_print_structure(const btree_index_t* index);
void btree_index_dump_memory_stats(const btree_index_t* index);

int btree_index_save(btree_index_t* index, const char* filename);
btree_index_t* btree_index_load(const char* filename);

int btree_index_validate(btree_index_t* index);
int btree_index_defragment(btree_index_t* index);
void btree_index_clear_error(btree_index_t* index);
const char* btree_index_get_last_error(btree_index_t* index);
void btree_index_print_level_order(const btree_index_t* index);

#ifdef __cplusplus
}
#endif

#endif