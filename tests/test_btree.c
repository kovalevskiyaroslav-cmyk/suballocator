#include "btree.h"
#include "suballocator.h"
#include <stdio.h>
#include <stdlib.h>
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

static suballocator_t* g_allocator = NULL;

static int test_create_destroy(void) {
    printf("Testing create/destroy... ");
    
    btree_t* tree = btree_create(g_allocator, NULL);
    TEST_ASSERT(tree != NULL, "Failed to create tree");
    TEST_ASSERT(btree_is_empty(tree) == 1, "New tree should be empty");
    
    btree_destroy(tree);
    
    printf("PASSED\n");
    return 1;
}

static int test_insert_search(void) {
    printf("Testing insert/search... ");
    
    btree_t* tree = btree_create(g_allocator, NULL);
    TEST_ASSERT(tree != NULL, "Failed to create tree");
    
    TEST_ASSERT(btree_insert(tree, "apple") == BTREE_ERROR_NONE, "Failed to insert apple");
    TEST_ASSERT(btree_insert(tree, "banana") == BTREE_ERROR_NONE, "Failed to insert banana");
    TEST_ASSERT(btree_insert(tree, "cherry") == BTREE_ERROR_NONE, "Failed to insert cherry");
    
    TEST_ASSERT(btree_search(tree, "apple") == 1, "apple not found");
    TEST_ASSERT(btree_search(tree, "banana") == 1, "banana not found");
    TEST_ASSERT(btree_search(tree, "cherry") == 1, "cherry not found");
    TEST_ASSERT(btree_search(tree, "durian") == 0, "durian should not exist");
    
    btree_destroy(tree);
    
    printf("PASSED\n");
    return 1;
}

static int test_insert_duplicate(void) {
    printf("Testing duplicate handling... ");
    
    btree_config_t config = btree_config_default();
    config.allow_duplicates = 0;
    
    btree_t* tree = btree_create(g_allocator, &config);
    TEST_ASSERT(tree != NULL, "Failed to create tree");
    
    TEST_ASSERT(btree_insert(tree, "test") == BTREE_ERROR_NONE, "Failed first insert");
    TEST_ASSERT(btree_insert(tree, "test") == BTREE_ERROR_KEY_EXISTS, "Duplicate should be rejected");
    
    btree_destroy(tree);
    
    config.allow_duplicates = 1;
    tree = btree_create(g_allocator, &config);
    
    TEST_ASSERT(btree_insert(tree, "test") == BTREE_ERROR_NONE, "Failed first insert");
    TEST_ASSERT(btree_insert(tree, "test") == BTREE_ERROR_NONE, "Duplicate should be allowed");
    
    btree_destroy(tree);
    
    printf("PASSED\n");
    return 1;
}

static int test_delete(void) {
    printf("Testing delete... ");
    
    btree_t* tree = btree_create(g_allocator, NULL);
    TEST_ASSERT(tree != NULL, "Failed to create tree");
    
    const char* keys[] = {"one", "two", "three", "four", "five", "six", "seven"};
    int num_keys = sizeof(keys) / sizeof(keys[0]);
    
    for (int i = 0; i < num_keys; i++) {
        btree_insert(tree, keys[i]);
    }
    
    TEST_ASSERT(btree_search(tree, "three") == 1, "three should exist");
    TEST_ASSERT(btree_delete(tree, "three") == BTREE_ERROR_NONE, "Failed to delete three");
    TEST_ASSERT(btree_search(tree, "three") == 0, "three should be deleted");
    
    TEST_ASSERT(btree_delete(tree, "nonexistent") == BTREE_ERROR_KEY_NOT_FOUND, 
                "Deleting non-existent key should fail");
    
    btree_destroy(tree);
    
    printf("PASSED\n");
    return 1;
}

static int test_range_query(void) {
    printf("Testing range query... ");
    
    btree_t* tree = btree_create(g_allocator, NULL);
    TEST_ASSERT(tree != NULL, "Failed to create tree");
    
    btree_insert(tree, "apple");
    btree_insert(tree, "banana");
    btree_insert(tree, "cherry");
    btree_insert(tree, "date");
    btree_insert(tree, "elderberry");
    
    char** keys;
    size_t count;
    
    btree_error_t result = btree_range_query(tree, "banana", "date", &keys, &count);
    TEST_ASSERT(result == BTREE_ERROR_NONE, "Range query failed");
    
    TEST_ASSERT(count == 3, "Wrong number of results");
    TEST_ASSERT(strcmp(keys[0], "banana") == 0, "Wrong first key");
    TEST_ASSERT(strcmp(keys[1], "cherry") == 0, "Wrong second key");
    TEST_ASSERT(strcmp(keys[2], "date") == 0, "Wrong third key");
    
    for (size_t i = 0; i < count; i++) {
        suballocator_free(g_allocator, keys[i]);
    }
    suballocator_free(g_allocator, keys);
    
    btree_destroy(tree);
    
    printf("PASSED\n");
    return 1;
}

static int test_min_max(void) {
    printf("Testing min/max... ");
    
    btree_t* tree = btree_create(g_allocator, NULL);
    TEST_ASSERT(tree != NULL, "Failed to create tree");
    
    btree_insert(tree, "zebra");
    btree_insert(tree, "apple");
    btree_insert(tree, "monkey");
    
    char buffer[64];
    
    TEST_ASSERT(btree_get_min(tree, buffer, sizeof(buffer)) == BTREE_ERROR_NONE, "Get min failed");
    TEST_ASSERT(strcmp(buffer, "apple") == 0, "Wrong min key");
    
    TEST_ASSERT(btree_get_max(tree, buffer, sizeof(buffer)) == BTREE_ERROR_NONE, "Get max failed");
    TEST_ASSERT(strcmp(buffer, "zebra") == 0, "Wrong max key");
    
    btree_destroy(tree);
    
    printf("PASSED\n");
    return 1;
}

static int test_serialization(void) {
    printf("Testing serialization... ");
    
    btree_t* tree = btree_create(g_allocator, NULL);
    TEST_ASSERT(tree != NULL, "Failed to create tree");
    
    const char* keys[] = {"test1", "test2", "test3", "test4", "test5"};
    for (int i = 0; i < 5; i++) {
        btree_insert(tree, keys[i]);
    }
    
    const char* filename = "test_btree_dump.idx";
    btree_dump_to_file(tree, filename);
    
    btree_t* loaded_tree = btree_load_from_file(g_allocator, filename);
    TEST_ASSERT(loaded_tree != NULL, "Failed to load tree");
    
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(btree_search(loaded_tree, keys[i]) == 1, "Key missing in loaded tree");
    }
    
    btree_destroy(tree);
    btree_destroy(loaded_tree);
    remove(filename);
    
    printf("PASSED\n");
    return 1;
}

static int test_validation(void) {
    printf("Testing validation... ");
    
    btree_t* tree = btree_create(g_allocator, NULL);
    TEST_ASSERT(tree != NULL, "Failed to create tree");
    
    for (int i = 0; i < 100; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key%03d", i);
        btree_insert(tree, key);
    }
    
    TEST_ASSERT(btree_validate(tree) == 1, "Tree validation failed");
    
    btree_destroy(tree);
    
    printf("PASSED\n");
    return 1;
}

int main(void) {
    g_allocator = suballocator_create_default();
    if (!g_allocator) {
        printf("Failed to create allocator\n");
        return 1;
    }
     
    printf("\n=== B-tree Test Suite ===\n\n");
    
    int passed = 0;
    int total = 8;
    
    passed += test_create_destroy();
    passed += test_insert_search();
    passed += test_insert_duplicate();
    passed += test_delete();
    passed += test_range_query();
    passed += test_min_max();
    passed += test_serialization();
    passed += test_validation();
    
    printf("\n=== Results: %d/%d tests passed ===\n", passed, total);
    
    suballocator_destroy(g_allocator);
    
    return passed == total ? 0 : 1;
}