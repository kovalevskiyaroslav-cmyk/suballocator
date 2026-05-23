#include "suballocator.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAILED: %s\n", message); \
            return 0; \
        } \
    } while(0)

static int test_basic_allocation(void) {
    printf("Testing basic allocation... ");
    
    suballocator_t* alloc = suballocator_create_default();
    TEST_ASSERT(alloc != NULL, "Failed to create allocator");
    
    void* ptr1 = suballocator_malloc(alloc, 100);
    TEST_ASSERT(ptr1 != NULL, "Failed to allocate 100 bytes");
    
    void* ptr2 = suballocator_malloc(alloc, 200);
    TEST_ASSERT(ptr2 != NULL, "Failed to allocate 200 bytes");
    
    suballocator_free(alloc, ptr1);
    suballocator_free(alloc, ptr2);
    
    suballocator_destroy(alloc);
    
    printf("PASSED\n");
    return 1;
}

static int test_realloc(void) {
    printf("Testing realloc... ");
    
    suballocator_t* alloc = suballocator_create_default();
    TEST_ASSERT(alloc != NULL, "Failed to create allocator");
    
    char* str = (char*)suballocator_malloc(alloc, 10);
    TEST_ASSERT(str != NULL, "Failed to allocate string");
    strcpy(str, "Hello");
    
    str = (char*)suballocator_realloc(alloc, str, 20);
    TEST_ASSERT(str != NULL, "Failed to reallocate");
    TEST_ASSERT(strcmp(str, "Hello") == 0, "Data corrupted after realloc");
    strcat(str, " World");
    
    suballocator_free(alloc, str);
    suballocator_destroy(alloc);
    
    printf("PASSED\n");
    return 1;
}

static int test_calloc(void) {
    printf("Testing calloc... ");
    
    suballocator_t* alloc = suballocator_create_default();
    TEST_ASSERT(alloc != NULL, "Failed to create allocator");
    
    int* arr = (int*)suballocator_calloc(alloc, 10, sizeof(int));
    TEST_ASSERT(arr != NULL, "Failed to calloc array");
    
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT(arr[i] == 0, "Calloc didn't zero memory");
    }
    
    suballocator_free(alloc, arr);
    suballocator_destroy(alloc);
    
    printf("PASSED\n");
    return 1;
}

static int test_strdup(void) {
    printf("Testing strdup... ");
    
    suballocator_t* alloc = suballocator_create_default();
    TEST_ASSERT(alloc != NULL, "Failed to create allocator");
    
    const char* original = "Test string for strdup";
    char* copy = suballocator_strdup(alloc, original);
    
    TEST_ASSERT(copy != NULL, "Failed to duplicate string");
    TEST_ASSERT(strcmp(original, copy) == 0, "String content mismatch");
    TEST_ASSERT(original != copy, "Strdup returned same pointer");
    
    suballocator_free(alloc, copy);
    suballocator_destroy(alloc);
    
    printf("PASSED\n");
    return 1;
}

static int test_fragmentation(void) {
    printf("Testing fragmentation... ");
    
    suballocator_t* alloc = suballocator_create_default();
    TEST_ASSERT(alloc != NULL, "Failed to create allocator");
    
    void* ptrs[100];
    
    for (int i = 0; i < 100; i++) {
        ptrs[i] = suballocator_malloc(alloc, (i % 10 + 1) * 10);
        TEST_ASSERT(ptrs[i] != NULL, "Failed to allocate in fragmentation test");
    }
    
    for (int i = 0; i < 100; i += 2) {
        suballocator_free(alloc, ptrs[i]);
    }
    
    for (int i = 1; i < 100; i += 2) {
        suballocator_free(alloc, ptrs[i]);
    }
    
    int defragmented = suballocator_defragment(alloc);
    TEST_ASSERT(defragmented >= 0, "Defragmentation failed");
    
    suballocator_destroy(alloc);
    
    printf("PASSED\n");
    return 1;
}

static int test_statistics(void) {
    printf("Testing statistics... ");
    
    suballocator_t* alloc = suballocator_create_default();
    TEST_ASSERT(alloc != NULL, "Failed to create allocator");
    
    suballocator_stats_t stats;
    suballocator_get_stats(alloc, &stats);
    
    TEST_ASSERT(stats.pool_size > 0, "Invalid pool size");
    TEST_ASSERT(stats.free_memory > 0, "No free memory");
    
    void* ptr = suballocator_malloc(alloc, 1000);
    TEST_ASSERT(ptr != NULL, "Failed to allocate");
    
    suballocator_get_stats(alloc, &stats);
    TEST_ASSERT(stats.used_memory > 0, "Used memory not updated");
    
    suballocator_free(alloc, ptr);
    suballocator_destroy(alloc);
    
    printf("PASSED\n");
    return 1;
}

static int test_error_handling(void) {
    printf("Testing error handling... ");
    
    suballocator_t* alloc = suballocator_create_default();
    TEST_ASSERT(alloc != NULL, "Failed to create allocator");
    
    suballocator_free(alloc, NULL);
    TEST_ASSERT(suballocator_get_last_error(alloc) == SUBALLOCATOR_ERROR_INVALID_PARAMETER,
                "Wrong error code for NULL free");
    
    suballocator_clear_error(alloc);
    TEST_ASSERT(suballocator_get_last_error(alloc) == SUBALLOCATOR_ERROR_NONE,
                "Failed to clear error");
    
    suballocator_destroy(alloc);
    
    printf("PASSED\n");
    return 1;
}

static int test_validation(void) {
    printf("Testing heap validation... ");
    
    suballocator_t* alloc = suballocator_create_default();
    TEST_ASSERT(alloc != NULL, "Failed to create allocator");
    
    void* ptr1 = suballocator_malloc(alloc, 100);
    void* ptr2 = suballocator_malloc(alloc, 200);
    
    TEST_ASSERT(suballocator_validate_heap(alloc) == 1, "Heap validation failed");
    
    suballocator_free(alloc, ptr1);
    suballocator_free(alloc, ptr2);
    
    TEST_ASSERT(suballocator_validate_heap(alloc) == 1, "Heap validation failed after free");
    
    suballocator_destroy(alloc);
    
    printf("PASSED\n");
    return 1;
}

int main(void) {
    srand(time(NULL));
    
    printf("\n=== Suballocator Test Suite ===\n\n");
    
    int passed = 0;
    int total = 8; 
    
    passed += test_basic_allocation();
    passed += test_realloc();
    passed += test_calloc();
    passed += test_strdup();
    passed += test_fragmentation();
    passed += test_statistics();
    passed += test_error_handling();
    passed += test_validation();
    
    printf("\n=== Results: %d/%d tests passed ===\n", passed, total);
    
    return passed == total ? 0 : 1;
}