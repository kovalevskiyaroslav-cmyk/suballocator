#ifndef SUBALLOCATOR_H
#define SUBALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBALLOCATOR_VERSION_MAJOR 1
#define SUBALLOCATOR_VERSION_MINOR 0
#define SUBALLOCATOR_VERSION_PATCH 0

typedef enum {
    SUBALLOCATOR_FIT_FIRST = 0,
    SUBALLOCATOR_FIT_BEST = 1,
    SUBALLOCATOR_FIT_WORST = 2
} suballocator_fit_strategy_t;

typedef enum {
    SUBALLOCATOR_ERROR_NONE = 0, 
    SUBALLOCATOR_ERROR_OUT_OF_MEMORY = -1,
    SUBALLOCATOR_ERROR_INVALID_PARAMETER = -2,
    SUBALLOCATOR_ERROR_ALREADY_FREED = -3,
    SUBALLOCATOR_ERROR_CORRUPTED_HEAP = -4,
    SUBALLOCATOR_ERROR_DOUBLE_FREE = -5,
    SUBALLOCATOR_ERROR_BUFFER_OVERFLOW = -6
} suballocator_error_t;

typedef struct suballocator_stats {
    size_t pool_size;
    size_t used_memory;
    size_t peak_memory;
    size_t free_memory;
    size_t largest_free_block;
    size_t total_allocations;
    size_t total_deallocations;
    size_t failed_allocations;
    size_t fragmentation_count;
    float fragmentation_ratio;
    size_t metadata_overhead;
} suballocator_stats_t;

typedef struct suballocator_config {
    size_t initial_pool_size;
    size_t max_pool_size;
    size_t growth_factor_percent;
    suballocator_fit_strategy_t fit_strategy;
    int enable_statistics;
    int enable_debug_checks;
    int enable_defragmentation;
    int enable_thread_safety;
} suballocator_config_t;

typedef struct suballocator suballocator_t;

const char* suballocator_get_error_string(suballocator_error_t error);

suballocator_config_t suballocator_config_default(void);

suballocator_t* suballocator_create(const suballocator_config_t* config);
suballocator_t* suballocator_create_default(void);
void suballocator_destroy(suballocator_t* allocator);

void* suballocator_malloc(suballocator_t* allocator, size_t size);
void* suballocator_calloc(suballocator_t* allocator, size_t nmemb, size_t size);
void* suballocator_realloc(suballocator_t* allocator, void* ptr, size_t new_size);
void suballocator_free(suballocator_t* allocator, void* ptr);

char* suballocator_strdup(suballocator_t* allocator, const char* str);
void* suballocator_memalign(suballocator_t* allocator, size_t alignment, size_t size);

int suballocator_defragment(suballocator_t* allocator);
int suballocator_validate_heap(suballocator_t* allocator);
void suballocator_get_stats(const suballocator_t* allocator, suballocator_stats_t* stats);
void suballocator_print_stats(const suballocator_t* allocator);
void suballocator_dump_heap(const suballocator_t* allocator);

suballocator_error_t suballocator_get_last_error(const suballocator_t* allocator);
void suballocator_clear_error(suballocator_t* allocator);

#ifdef __cplusplus
}
#endif

#endif