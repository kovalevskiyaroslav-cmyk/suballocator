#include "btree_index.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

struct btree_index {
    suballocator_t* allocator;
    btree_t* tree;
    btree_index_config_t config;
    char last_error[256];
    size_t operations_count;
    int auto_defrag_counter;
};

static void set_error(btree_index_t* index, const char* format, ...) {
    if (!index) return;
    
    va_list args;
    va_start(args, format);
    vsnprintf(index->last_error, sizeof(index->last_error), format, args);
    va_end(args);
}

btree_index_config_t btree_index_config_default(void) {
    btree_index_config_t config = {
        .initial_memory_pool = 1024 * 1024,
        .max_memory_pool = 0,
        .btree_order = 64,
        .allow_duplicates = 0,
        .enable_statistics = 1,
        .enable_auto_defrag = 1,
        .defrag_threshold = 30,
        .persist_path = NULL
    };
    return config;
}

btree_index_t* btree_index_create(const btree_index_config_t* config) {
    btree_index_t* index = (btree_index_t*)malloc(sizeof(btree_index_t));
    if (!index) return NULL;
    
    if (config) {
        index->config = *config;
    } else {
        index->config = btree_index_config_default();
    }
    
    suballocator_config_t alloc_config = suballocator_config_default();
    alloc_config.initial_pool_size = index->config.initial_memory_pool;
    alloc_config.max_pool_size = index->config.max_memory_pool;
    alloc_config.enable_statistics = index->config.enable_statistics;
    alloc_config.enable_defragmentation = index->config.enable_auto_defrag;
    
    index->allocator = suballocator_create(&alloc_config);
    if (!index->allocator) {
        free(index);
        return NULL;
    }
    
    btree_config_t tree_config = btree_config_default();
    tree_config.order = index->config.btree_order;
    tree_config.allow_duplicates = index->config.allow_duplicates;
    tree_config.enable_statistics = index->config.enable_statistics;
    
    index->tree = btree_create(index->allocator, &tree_config);
    if (!index->tree) {
        suballocator_destroy(index->allocator);
        free(index);
        return NULL;
    }
    
    index->operations_count = 0;
    index->auto_defrag_counter = 0;
    memset(index->last_error, 0, sizeof(index->last_error));
    
    return index;
}

void btree_index_destroy(btree_index_t* index) {
    if (!index) return;
    
    if (index->tree) {
        btree_destroy(index->tree);
    }
    
    if (index->allocator) {
        suballocator_destroy(index->allocator);
    }
    
    free(index);
}

int btree_index_insert(btree_index_t* index, const char* key) {
    if (!index || !key) {
        set_error(index, "Invalid parameters");
        return 0;
    }
    
    if (strlen(key) == 0 || strlen(key) >= 64) {
        set_error(index, "Key length must be between 1 and 63 characters");
        return 0;
    }
    
    btree_error_t result = btree_insert(index->tree, key);
    
    if (result != BTREE_ERROR_NONE) {
        set_error(index, "Insert failed: %s", btree_get_error_string(result));
        return 0;
    }
    
    index->operations_count++;
    
    if (index->config.enable_auto_defrag) {
        index->auto_defrag_counter++;
        if (index->auto_defrag_counter >= (int)index->config.defrag_threshold) {
            btree_index_defragment(index);
            index->auto_defrag_counter = 0;
        }
    }
    
    return 1;
}

int btree_index_search(btree_index_t* index, const char* key) {
    if (!index || !key) {
        set_error(index, "Invalid parameters");
        return 0;
    }
    
    return btree_search(index->tree, key);
}

int btree_index_delete(btree_index_t* index, const char* key) {
    if (!index || !key) {
        set_error(index, "Invalid parameters");
        return 0;
    }
    
    btree_error_t result = btree_delete(index->tree, key);
    
    if (result != BTREE_ERROR_NONE) {
        set_error(index, "%s", btree_get_error_string(result));
        return 0;
    }
    
    index->operations_count++;
    
    return 1;
}

size_t btree_index_size(const btree_index_t* index) {
    return index ? btree_size(index->tree) : 0;
}

int btree_index_is_empty(const btree_index_t* index) {
    return index ? btree_is_empty(index->tree) : 1;
}

void btree_index_clear(btree_index_t* index) {
    if (!index) return;
    
    if (index->tree) {
        btree_clear(index->tree);
    }
    
    index->operations_count = 0;
    index->auto_defrag_counter = 0;
    
    btree_index_clear_error(index);
}

int btree_index_range_query(btree_index_t* index, const char* start_key, const char* end_key,
                            char*** keys, size_t* count) {
    if (!index || !keys || !count) {
        set_error(index, "Invalid parameters");
        return 0;
    }
    
    btree_error_t result = btree_range_query(index->tree, start_key, end_key, keys, count);
    
    if (result != BTREE_ERROR_NONE) {
        set_error(index, "Range query failed: %s", btree_get_error_string(result));
        return 0;
    }
    
    return 1;
}

int btree_index_get_min(btree_index_t* index, char* key_buffer, size_t buffer_size) {
    if (!index || !key_buffer) {
        set_error(index, "Invalid parameters");
        return 0;
    }
    
    btree_error_t result = btree_get_min(index->tree, key_buffer, buffer_size);
    
    if (result != BTREE_ERROR_NONE) {
        set_error(index, "Get min failed: %s", btree_get_error_string(result));
        return 0;
    }
    
    return 1;
}

int btree_index_get_max(btree_index_t* index, char* key_buffer, size_t buffer_size) {
    if (!index || !key_buffer) {
        set_error(index, "Invalid parameters");
        return 0;
    }
    
    btree_error_t result = btree_get_max(index->tree, key_buffer, buffer_size);
    
    if (result != BTREE_ERROR_NONE) {
        set_error(index, "Get max failed: %s", btree_get_error_string(result));
        return 0;
    }
    
    return 1;
} 

void btree_index_print_stats(const btree_index_t* index) {
    if (!index) return;
    
    printf("\n=== B-tree Index Statistics ===\n");
    btree_print_stats(index->tree);
    suballocator_print_stats(index->allocator);
    printf("Operations count: %zu\n", index->operations_count);
}

void btree_index_print_structure(const btree_index_t* index) {
    if (!index) return;
    btree_print_structure(index->tree);
}

void btree_index_dump_memory_stats(const btree_index_t* index) {
    if (!index) return;
    suballocator_dump_heap(index->allocator);
}

int btree_index_save(btree_index_t* index, const char* filename) {
    if (!index || !filename) {
        set_error(index, "Invalid parameters");
        return 0;
    }
    
    btree_dump_to_file(index->tree, filename);
    
    btree_error_t error = btree_get_last_error(index->tree);
    if (error != BTREE_ERROR_NONE) {
        set_error(index, "Save failed: %s", btree_get_error_string(error));
        return 0;
    }
    
    return 1;
}

btree_index_t* btree_index_load(const char* filename) {
    if (!filename) return NULL;
    
    btree_index_t* index = (btree_index_t*)malloc(sizeof(btree_index_t));
    if (!index) return NULL;
    
    index->config = btree_index_config_default();
    
    suballocator_config_t alloc_config = suballocator_config_default();
    index->allocator = suballocator_create(&alloc_config);
    
    if (!index->allocator) {
        free(index);
        return NULL;
    }
    
    index->tree = btree_load_from_file(index->allocator, filename);
    if (!index->tree) {
        suballocator_destroy(index->allocator);
        free(index);
        return NULL;
    }
    
    index->operations_count = 0;
    index->auto_defrag_counter = 0;
    memset(index->last_error, 0, sizeof(index->last_error));
    
    return index;
}

int btree_index_validate(btree_index_t* index) {
    if (!index) {
        set_error(index, "Invalid parameters");
        return 0;
    }
    
    return btree_validate(index->tree);
}

int btree_index_defragment(btree_index_t* index) {
    if (!index) {
        set_error(index, "Invalid parameters");
        return 0;
    }
    
    return suballocator_defragment(index->allocator);
}

void btree_index_clear_error(btree_index_t* index) {
    if (!index) return;
    memset(index->last_error, 0, sizeof(index->last_error));
    btree_clear_error(index->tree);
    suballocator_clear_error(index->allocator);
}

const char* btree_index_get_last_error(btree_index_t* index) {
    if (!index) return "Invalid index";
    return index->last_error[0] ? index->last_error : "No error";
}

void btree_index_print_level_order(const btree_index_t* index) {
    if (!index) return;
    btree_print_level_order(index->tree);
}