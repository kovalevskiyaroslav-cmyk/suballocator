#include "suballocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

#define SUBALLOCATOR_MAGIC 0x8DEADBEF
#define BLOCK_MAGIC_FREE 0x8F4EEB10
#define BLOCK_MAGIC_USED 0x85ED8B10
#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define HEADER_SIZE ALIGN(sizeof(block_header_t))
#define MIN_BLOCK_SIZE 64
#define CANARY_VALUE 0x8CAFEBAB

typedef struct block_header {
    uint32_t magic;
    size_t size;
    uint32_t canary_start;
    int is_free;
    struct block_header* next;
    struct block_header* prev;
    struct block_header* next_free;
    struct block_header* prev_free;
    const char* file;
    int line;
    uint32_t canary_end;
} block_header_t;

struct suballocator {
    void* memory_pool;
    size_t pool_size;
    size_t used_memory;
    size_t peak_memory;
    block_header_t* free_list;
    block_header_t* block_list;
    suballocator_config_t config;
    suballocator_error_t last_error;
    size_t total_allocs;
    size_t total_deallocs;
    size_t failed_allocs;
    pthread_mutex_t mutex;
};

static void check_canaries(const block_header_t* block, suballocator_t* allocator) {
    if (!allocator->config.enable_debug_checks) return;
    
    if (block->canary_start != CANARY_VALUE || block->canary_end != CANARY_VALUE) {
        allocator->last_error = SUBALLOCATOR_ERROR_BUFFER_OVERFLOW;
    }
}

static void set_canaries(block_header_t* block) {
    block->canary_start = CANARY_VALUE;
    block->canary_end = CANARY_VALUE;
}

static block_header_t* find_free_block_first(suballocator_t* allocator, size_t size) {
    block_header_t* current = allocator->free_list;
    while (current) {
        if (current->size >= size) {
            return current;
        }
        current = current->next_free;
    }
    return NULL;
}

static block_header_t* find_free_block_best(suballocator_t* allocator, size_t size) {
    block_header_t* current = allocator->free_list;
    block_header_t* best = NULL;
    size_t best_size = SIZE_MAX;
    
    while (current) {
        if (current->size >= size && current->size < best_size) {
            best = current;
            best_size = current->size;
        }
        current = current->next_free;
    }
    return best;
}

static block_header_t* find_free_block_worst(suballocator_t* allocator, size_t size) {
    block_header_t* current = allocator->free_list;
    block_header_t* worst = NULL;
    size_t worst_size = 0;
    
    while (current) {
        if (current->size >= size && current->size > worst_size) {
            worst = current;
            worst_size = current->size;
        }
        current = current->next_free;
    }
    return worst;
}

static block_header_t* find_free_block(suballocator_t* allocator, size_t size) {
    switch (allocator->config.fit_strategy) {
        case SUBALLOCATOR_FIT_FIRST:
            return find_free_block_first(allocator, size);
        case SUBALLOCATOR_FIT_BEST:
            return find_free_block_best(allocator, size);
        case SUBALLOCATOR_FIT_WORST:
            return find_free_block_worst(allocator, size);
        default:
            return find_free_block_first(allocator, size);
    }
}

static void add_to_free_list(suballocator_t* allocator, block_header_t* block) {
    block->next_free = allocator->free_list;
    block->prev_free = NULL;
    if (allocator->free_list) {
        allocator->free_list->prev_free = block;
    }
    allocator->free_list = block;
}

static void remove_from_free_list(suballocator_t* allocator, block_header_t* block) {
    if (block->prev_free) {
        block->prev_free->next_free = block->next_free;
    } else {
        allocator->free_list = block->next_free;
    }
    if (block->next_free) {
        block->next_free->prev_free = block->prev_free;
    }
    block->next_free = NULL;
    block->prev_free = NULL;
}

static void split_block(suballocator_t* allocator, block_header_t* block, size_t size) {
    if (block->size >= size + HEADER_SIZE + MIN_BLOCK_SIZE) {
        block_header_t* new_block = (block_header_t*)((char*)block + HEADER_SIZE + size);
        new_block->magic = BLOCK_MAGIC_FREE;
        new_block->size = block->size - size - HEADER_SIZE;
        new_block->is_free = 1;
        set_canaries(new_block);
        
        new_block->next = block->next;
        new_block->prev = block;
        
        if (block->next) {
            block->next->prev = new_block;
        }
        
        block->next = new_block;
        block->size = size;
        
        add_to_free_list(allocator, new_block);
    }
}

static void merge_blocks(suballocator_t* allocator, block_header_t* block) {
    if (block->next && block->next->is_free) {
        remove_from_free_list(allocator, block->next);
        block->size += HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }
    
    if (block->prev && block->prev->is_free) {
        remove_from_free_list(allocator, block->prev);
        block->prev->size += HEADER_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
        block = block->prev;
    }
    
    add_to_free_list(allocator, block);
}

static int expand_pool(suballocator_t* allocator, size_t required_size) {
    size_t additional_size = required_size;
    size_t growth = allocator->pool_size * allocator->config.growth_factor_percent / 100;
    if (growth > additional_size) {
        additional_size = growth;
    }
    
    size_t new_pool_size = allocator->pool_size + additional_size;
    
    if (allocator->config.max_pool_size > 0 && new_pool_size > allocator->config.max_pool_size) {
        new_pool_size = allocator->config.max_pool_size;
        if (new_pool_size <= allocator->pool_size) {
            allocator->last_error = SUBALLOCATOR_ERROR_OUT_OF_MEMORY;
            return 0;
        }
    }
    
    void* new_pool = realloc(allocator->memory_pool, new_pool_size);
    if (!new_pool) {
        allocator->last_error = SUBALLOCATOR_ERROR_OUT_OF_MEMORY;
        return 0;
    }
    
    ptrdiff_t offset = (char*)new_pool - (char*)allocator->memory_pool;
    if (offset != 0) {
        block_header_t* current = (block_header_t*)new_pool;
        while (current) {
            if (current->next) {
                current->next = (block_header_t*)((char*)current->next + offset);
            }
            if (current->prev) {
                current->prev = (block_header_t*)((char*)current->prev + offset);
            }
            if (current->next_free) {
                current->next_free = (block_header_t*)((char*)current->next_free + offset);
            }
            if (current->prev_free) {
                current->prev_free = (block_header_t*)((char*)current->prev_free + offset);
            }
            current = current->next;
        }
        
        if (allocator->free_list) {
            allocator->free_list = (block_header_t*)((char*)allocator->free_list + offset);
        }
        if (allocator->block_list) {
            allocator->block_list = (block_header_t*)((char*)allocator->block_list + offset);
        }
    }
    
    allocator->memory_pool = new_pool;
    
    block_header_t* new_block = (block_header_t*)((char*)new_pool + allocator->pool_size);
    new_block->magic = BLOCK_MAGIC_FREE;
    new_block->size = new_pool_size - allocator->pool_size - HEADER_SIZE;
    new_block->is_free = 1;
    new_block->next = NULL;
    new_block->prev = NULL;
    new_block->next_free = NULL;
    new_block->prev_free = NULL;
    set_canaries(new_block);
    
    block_header_t* last = allocator->block_list;
    while (last && last->next) last = last->next;
    if (last) {
        last->next = new_block;
        new_block->prev = last;
    }
    
    add_to_free_list(allocator, new_block);
    
    allocator->pool_size = new_pool_size;
    
    return 1;
}

const char* suballocator_get_error_string(suballocator_error_t error) {
    switch (error) {
        case SUBALLOCATOR_ERROR_NONE: return "No error";
        case SUBALLOCATOR_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case SUBALLOCATOR_ERROR_INVALID_PARAMETER: return "Invalid parameter";
        case SUBALLOCATOR_ERROR_ALREADY_FREED: return "Memory already freed";
        case SUBALLOCATOR_ERROR_CORRUPTED_HEAP: return "Corrupted heap detected";
        case SUBALLOCATOR_ERROR_DOUBLE_FREE: return "Double free detected";
        case SUBALLOCATOR_ERROR_BUFFER_OVERFLOW: return "Buffer overflow detected";
        default: return "Unknown error";
    }
}

suballocator_config_t suballocator_config_default(void) {
    suballocator_config_t config = {
        .initial_pool_size = 64 * 1024,
        .max_pool_size = 0,
        .growth_factor_percent = 100,
        .fit_strategy = SUBALLOCATOR_FIT_FIRST,
        .enable_statistics = 1,
        .enable_debug_checks = 1,
        .enable_defragmentation = 1,
        .enable_thread_safety = 1
    };
    return config;
}

suballocator_t* suballocator_create(const suballocator_config_t* config) {
    suballocator_t* allocator = (suballocator_t*)malloc(sizeof(suballocator_t));
    if (!allocator) return NULL;
    
    if (config) {
        allocator->config = *config;
    } else {
        allocator->config = suballocator_config_default();
    }
    
    allocator->pool_size = allocator->config.initial_pool_size;
    allocator->used_memory = 0;
    allocator->peak_memory = 0;
    allocator->last_error = SUBALLOCATOR_ERROR_NONE;
    allocator->total_allocs = 0;
    allocator->total_deallocs = 0;
    allocator->failed_allocs = 0;
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_init(&allocator->mutex, NULL);
    }
    
    allocator->memory_pool = malloc(allocator->pool_size);
    if (!allocator->memory_pool) {
        if (allocator->config.enable_thread_safety) {
            pthread_mutex_destroy(&allocator->mutex);
        }
        free(allocator);
        return NULL;
    }
    
    allocator->block_list = (block_header_t*)allocator->memory_pool;
    allocator->block_list->magic = BLOCK_MAGIC_FREE;
    allocator->block_list->size = allocator->pool_size - HEADER_SIZE;
    allocator->block_list->is_free = 1;
    allocator->block_list->next = NULL;
    allocator->block_list->prev = NULL;
    allocator->block_list->next_free = NULL;
    allocator->block_list->prev_free = NULL;
    set_canaries(allocator->block_list);
    
    allocator->free_list = allocator->block_list;
    
    return allocator;
}

suballocator_t* suballocator_create_default(void) {
    return suballocator_create(NULL);
}

void suballocator_destroy(suballocator_t* allocator) {
    if (!allocator) return;
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_destroy(&allocator->mutex);
    }
    
    free(allocator->memory_pool);
    free(allocator);
}

static void* suballocator_malloc_debug(suballocator_t* allocator, size_t size, const char* file, int line) {
    if (!allocator) {
        return NULL;
    }
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_lock(&allocator->mutex);
    }
    
    if (size == 0) {
        if (allocator->config.enable_thread_safety) {
            pthread_mutex_unlock(&allocator->mutex);
        }
        allocator->last_error = SUBALLOCATOR_ERROR_INVALID_PARAMETER;
        return NULL;
    }
    
    size_t aligned_size = ALIGN(size);
    block_header_t* block = find_free_block(allocator, aligned_size);
    
    if (!block) {
        if (expand_pool(allocator, aligned_size + HEADER_SIZE)) {
            block = find_free_block(allocator, aligned_size);
        }
    }
    
    if (!block) {
        allocator->failed_allocs++;
        if (allocator->config.enable_thread_safety) {
            pthread_mutex_unlock(&allocator->mutex);
        }
        allocator->last_error = SUBALLOCATOR_ERROR_OUT_OF_MEMORY;
        return NULL;
    }
    
    remove_from_free_list(allocator, block);
    split_block(allocator, block, aligned_size);
    
    block->magic = BLOCK_MAGIC_USED;
    block->is_free = 0;
    block->file = file;
    block->line = line;
    
    allocator->used_memory += aligned_size + HEADER_SIZE;
    if (allocator->used_memory > allocator->peak_memory) {
        allocator->peak_memory = allocator->used_memory;
    }
    
    allocator->total_allocs++;
    allocator->last_error = SUBALLOCATOR_ERROR_NONE;
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_unlock(&allocator->mutex);
    }
    
    return (void*)((char*)block + HEADER_SIZE);
}

void* suballocator_malloc(suballocator_t* allocator, size_t size) {
    return suballocator_malloc_debug(allocator, size, __FILE__, __LINE__);
}

void* suballocator_calloc(suballocator_t* allocator, size_t nmemb, size_t size) {
    size_t total_size = nmemb * size;
    void* ptr = suballocator_malloc(allocator, total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

void* suballocator_realloc(suballocator_t* allocator, void* ptr, size_t new_size) {
    if (!allocator) return NULL;
    
    if (!ptr) {
        return suballocator_malloc(allocator, new_size);
    }
    
    if (new_size == 0) {
        suballocator_free(allocator, ptr);
        return NULL;
    }
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_lock(&allocator->mutex);
    }
    
    block_header_t* block = (block_header_t*)((char*)ptr - HEADER_SIZE);
    
    if (block->magic != BLOCK_MAGIC_USED) {
        if (allocator->config.enable_thread_safety) {
            pthread_mutex_unlock(&allocator->mutex);
        }
        allocator->last_error = SUBALLOCATOR_ERROR_ALREADY_FREED;
        return NULL;
    }
    
    check_canaries(block, allocator);
    
    size_t aligned_new_size = ALIGN(new_size);
    
    if (block->size >= aligned_new_size) {
        split_block(allocator, block, aligned_new_size);
        if (allocator->config.enable_thread_safety) {
            pthread_mutex_unlock(&allocator->mutex);
        }
        return ptr;
    }
    
    if (block->next && block->next->is_free && 
        block->size + HEADER_SIZE + block->next->size >= aligned_new_size) {
        remove_from_free_list(allocator, block->next);
        block->size += HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
        split_block(allocator, block, aligned_new_size);
        if (allocator->config.enable_thread_safety) {
            pthread_mutex_unlock(&allocator->mutex);
        }
        return ptr;
    }
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_unlock(&allocator->mutex);
    }
    
    void* new_ptr = suballocator_malloc(allocator, new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        suballocator_free(allocator, ptr);
    }
    
    return new_ptr;
}

void suballocator_free(suballocator_t* allocator, void* ptr) {
    if (!allocator || !ptr) {
        if (allocator) allocator->last_error = SUBALLOCATOR_ERROR_INVALID_PARAMETER;
        return;
    }
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_lock(&allocator->mutex);
    }
    
    block_header_t* block = (block_header_t*)((char*)ptr - HEADER_SIZE);
    
    if (block->magic != BLOCK_MAGIC_USED) {
        if (allocator->config.enable_thread_safety) {
            pthread_mutex_unlock(&allocator->mutex);
        }
        allocator->last_error = SUBALLOCATOR_ERROR_DOUBLE_FREE;
        return;
    }
    
    check_canaries(block, allocator);
    
    block->magic = BLOCK_MAGIC_FREE;
    block->is_free = 1;
    allocator->used_memory -= block->size + HEADER_SIZE;
    allocator->total_deallocs++;
    
    merge_blocks(allocator, block);
    allocator->last_error = SUBALLOCATOR_ERROR_NONE;
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_unlock(&allocator->mutex);
    }
}

char* suballocator_strdup(suballocator_t* allocator, const char* str) {
    if (!allocator || !str) return NULL;
    
    size_t len = strlen(str) + 1;
    char* new_str = (char*)suballocator_malloc(allocator, len);
    if (new_str) {
        memcpy(new_str, str, len);
    }
    return new_str;
}

void* suballocator_memalign(suballocator_t* allocator, size_t alignment, size_t size) {
    if (!allocator || alignment == 0 || (alignment & (alignment - 1)) != 0) {
        if (allocator) allocator->last_error = SUBALLOCATOR_ERROR_INVALID_PARAMETER;
        return NULL;
    }
    
    size_t total_size = size + alignment + HEADER_SIZE;
    void* raw_ptr = suballocator_malloc(allocator, total_size);
    if (!raw_ptr) return NULL;
    
    void* aligned_ptr = (void*)(((uintptr_t)raw_ptr + alignment + HEADER_SIZE) & ~(alignment - 1));
    
    return aligned_ptr;
}

int suballocator_defragment(suballocator_t* allocator) {
    if (!allocator || !allocator->config.enable_defragmentation) return 0;
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_lock(&allocator->mutex);
    }
    
    block_header_t* current = allocator->block_list;
    int merged = 0;
    
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            remove_from_free_list(allocator, current->next);
            current->size += HEADER_SIZE + current->next->size;
            current->next = current->next->next;
            if (current->next) {
                current->next->prev = current;
            }
            merged = 1;
        } else {
            current = current->next;
        }
    }
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_unlock(&allocator->mutex);
    }
    
    return merged;
}

int suballocator_validate_heap(suballocator_t* allocator) {
    if (!allocator) return 0;
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_lock(&allocator->mutex);
    }
    
    block_header_t* current = allocator->block_list;
    int valid = 1;
    size_t total_size = 0;
    
    while (current) {
        if (current->magic != BLOCK_MAGIC_FREE && current->magic != BLOCK_MAGIC_USED) {
            allocator->last_error = SUBALLOCATOR_ERROR_CORRUPTED_HEAP;
            valid = 0;
            break;
        }
        
        check_canaries(current, allocator);
        if (allocator->last_error != SUBALLOCATOR_ERROR_NONE) {
            valid = 0;
            break;
        }
        
        total_size += current->size + HEADER_SIZE;
        
        if (current->next && current->next->prev != current) {
            allocator->last_error = SUBALLOCATOR_ERROR_CORRUPTED_HEAP;
            valid = 0;
            break;
        }
        
        current = current->next;
    }
    
    if (total_size > allocator->pool_size) {
        allocator->last_error = SUBALLOCATOR_ERROR_CORRUPTED_HEAP;
        valid = 0;
    }
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_unlock(&allocator->mutex);
    }
    
    return valid;
}

void suballocator_get_stats(const suballocator_t* allocator, suballocator_stats_t* stats) {
    if (!allocator || !stats) return;
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_lock((pthread_mutex_t*)&allocator->mutex);
    }
    
    stats->pool_size = allocator->pool_size;
    stats->used_memory = allocator->used_memory;
    stats->peak_memory = allocator->peak_memory;
    stats->free_memory = allocator->pool_size - allocator->used_memory;
    stats->total_allocations = allocator->total_allocs;
    stats->total_deallocations = allocator->total_deallocs;
    stats->failed_allocations = allocator->failed_allocs;
    stats->metadata_overhead = 0;
    
    size_t largest_block = 0;
    int free_blocks = 0;
    block_header_t* current = allocator->block_list;
    
    while (current) {
        stats->metadata_overhead += HEADER_SIZE;
        if (current->is_free) {
            free_blocks++;
            if (current->size > largest_block) {
                largest_block = current->size;
            }
        }
        current = current->next;
    }
    
    stats->largest_free_block = largest_block;
    stats->fragmentation_count = free_blocks;
    stats->fragmentation_ratio = free_blocks > 0 ? 
        (float)free_blocks / (allocator->pool_size / MIN_BLOCK_SIZE) : 0.0f;
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_unlock((pthread_mutex_t*)&allocator->mutex);
    }
}

void suballocator_print_stats(const suballocator_t* allocator) {
    if (!allocator) return;
    
    suballocator_stats_t stats;
    suballocator_get_stats(allocator, &stats); 
    
    printf("\nSuballocator Statistics:\n");
    printf("========================\n");
    printf("Pool size: %zu bytes (%.2f KB)\n", stats.pool_size, stats.pool_size / 1024.0);
    printf("Used memory: %zu bytes (%.2f KB)\n", stats.used_memory, stats.used_memory / 1024.0);
    printf("Peak memory: %zu bytes (%.2f KB)\n", stats.peak_memory, stats.peak_memory / 1024.0);
    printf("Free memory: %zu bytes (%.2f KB)\n", stats.free_memory, stats.free_memory / 1024.0);
    printf("Largest free block: %zu bytes\n", stats.largest_free_block);
    printf("Total allocations: %zu\n", stats.total_allocations);
    printf("Total deallocations: %zu\n", stats.total_deallocations);
    printf("Failed allocations: %zu\n", stats.failed_allocations);
    printf("Fragmentation count: %zu free blocks\n", stats.fragmentation_count);
    printf("Fragmentation ratio: %.2f%%\n", stats.fragmentation_ratio * 100);
    printf("Metadata overhead: %zu bytes\n", stats.metadata_overhead);
}

void suballocator_dump_heap(const suballocator_t* allocator) {
    if (!allocator) return;
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_lock((pthread_mutex_t*)&allocator->mutex);
    }
    
    printf("\nHeap Dump:\n");
    printf("==========\n");
    
    block_header_t* current = allocator->block_list;
    int block_num = 0;
    
    while (current) {
        printf("Block %d: [%s] Size: %zu, Location: %p",
               block_num++,
               current->is_free ? "FREE" : "USED",
               current->size,
               (void*)current);
        
        if (!current->is_free) {
            printf(", Allocated at: %s:%d", current->file, current->line);
        }
        printf("\n");
        
        current = current->next;
    }
    
    if (allocator->config.enable_thread_safety) {
        pthread_mutex_unlock((pthread_mutex_t*)&allocator->mutex);
    }
}

suballocator_error_t suballocator_get_last_error(const suballocator_t* allocator) {
    return allocator ? allocator->last_error : SUBALLOCATOR_ERROR_INVALID_PARAMETER;
}

void suballocator_clear_error(suballocator_t* allocator) {
    if (allocator) {
        allocator->last_error = SUBALLOCATOR_ERROR_NONE;
    }
}