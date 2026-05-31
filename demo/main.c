#include "btree_index.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

static void print_menu(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     B-tree Index with Custom Suballocator - Demo Program     ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  1. Insert string                                            ║\n");
    printf("║  2. Search string                                            ║\n");
    printf("║  3. Delete string                                            ║\n");
    printf("║  4. Range query                                              ║\n");
    printf("║  5. Get minimum string                                       ║\n");
    printf("║  6. Get maximum string                                       ║\n");
    printf("║  7. Print tree structure (horizontal)                        ║\n");
    printf("║  8. Print tree structure (levels)                            ║\n");
    printf("║  9. Print statistics                                         ║\n");
    printf("║ 10. Print memory stats                                       ║\n");
    printf("║ 11. Validate tree                                            ║\n");
    printf("║ 12. Defragment memory                                        ║\n");
    printf("║ 13. Save to file                                             ║\n");
    printf("║ 14. Load from file                                           ║\n");
    printf("║ 15. Clear all data                                           ║\n");
    printf("║ 16. Bulk insert (10000 random strings)                       ║\n");
    printf("║ 17. Performance benchmark                                    ║\n");
    printf("║ 18. Interactive demo                                         ║\n");
    printf("║  0. Exit                                                     ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("Choose option: ");
}

static void generate_random_string(char* buffer, size_t length) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < length - 1; i++) {
        buffer[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    buffer[length - 1] = '\0';
}

static void interactive_demo(btree_index_t* index) {
    printf("\n=== Interactive Demo ===\n");
    printf("This demo shows basic string operations with sample data.\n\n");
    
    const char* sample_data[] = {
        "apple", "banana", "cherry", "date", "elderberry",
        "fig", "grape", "honeydew", "kiwi", "lemon"
    };
    int num_samples = sizeof(sample_data) / sizeof(sample_data[0]);
    
    printf("Step 1: Inserting sample strings...\n");
    for (int i = 0; i < num_samples; i++) {
        btree_index_insert(index, sample_data[i]);
        printf("  Inserted: %s\n", sample_data[i]);
    }
    
    printf("\nStep 2: Searching for existing and non-existing strings...\n");
    printf("  Search 'cherry': %s\n", btree_index_search(index, "cherry") ? "Found" : "Not found");
    printf("  Search 'orange': %s\n", btree_index_search(index, "orange") ? "Found" : "Not found");
    
    printf("\nStep 3: Range query 'banana' to 'grape'...\n");
    char** keys;
    size_t count;
    if (btree_index_range_query(index, "banana", "grape", &keys, &count)) {
        printf("  Found %zu strings:\n", count);
        for (size_t i = 0; i < count && i < 10; i++) {
            printf("    %s\n", keys[i]);
        }
        if (count > 10) {
            printf("    ... and %zu more\n", count - 10);
        }
    }
    
    printf("\nStep 4: Getting min and max strings...\n");
    char buffer[64];
    if (btree_index_get_min(index, buffer, sizeof(buffer))) {
        printf("  Min string: %s\n", buffer);
    }
    if (btree_index_get_max(index, buffer, sizeof(buffer))) {
        printf("  Max string: %s\n", buffer);
    }
    
    printf("\nStep 5: Deleting 'apple'...\n");
    btree_index_delete(index, "apple");
    printf("  Search 'apple' after deletion: %s\n", 
           btree_index_search(index, "apple") ? "Still exists" : "Deleted");
    
    printf("\nStep 6: Printing tree structure...\n");
    btree_index_print_structure(index);
    
    printf("\nDemo completed!\n");
}

static void run_benchmark(btree_index_t* index) {
    printf("\n=== Performance Benchmark ===\n");
    
    btree_index_clear(index);
    
    clock_t start, end;
    const int num_operations = 50000;
    char key[32];
    
    printf("Benchmark 1: Inserting %d random strings...\n", num_operations);
    start = clock();
    for (int i = 0; i < num_operations; i++) {
        snprintf(key, sizeof(key), "bench%08d", i);
        btree_index_insert(index, key);
    }
    end = clock();
    printf("  Time: %.3f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);
    if (end > start) {
        printf("  Operations per second: %.0f\n", 
               num_operations / ((double)(end - start) / CLOCKS_PER_SEC));
    }
    
    printf("\nBenchmark 2: Searching %d strings...\n", num_operations);
    start = clock();
    for (int i = 0; i < num_operations; i++) {
        snprintf(key, sizeof(key), "bench%08d", i);
        btree_index_search(index, key);
    }
    end = clock();
    printf("  Time: %.3f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);
    if (end > start) {
        printf("  Operations per second: %.0f\n", 
               num_operations / ((double)(end - start) / CLOCKS_PER_SEC));
    }
    
    printf("\nBenchmark 3: Deleting %d strings...\n", num_operations / 2);
    start = clock();
    for (int i = 0; i < num_operations / 2; i++) {
        snprintf(key, sizeof(key), "bench%08d", i);
        btree_index_delete(index, key);
    }
    end = clock();
    printf("  Time: %.3f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);
    if (end > start) {
        printf("  Operations per second: %.0f\n", 
               (num_operations / 2) / ((double)(end - start) / CLOCKS_PER_SEC));
    }
    
    printf("\nBenchmark completed!\n");
}

int main(int argc, char* argv[]) {
    srand(time(NULL));
    
    printf("\nB-tree Index Library\n");
    
    btree_index_config_t config = btree_index_config_default();
    config.initial_memory_pool = 10 * 1024 * 1024;
    config.btree_order = 4;
    config.enable_statistics = 1;
    config.enable_auto_defrag = 1;
    
    if (argc > 1) {
        config.persist_path = argv[1];
    }
    
    btree_index_t* index = btree_index_create(&config);
    if (!index) {
        printf("Failed to create index\n");
        return 1;
    }
    
    printf("Index created successfully\n");
    printf("Initial configuration:\n");
    printf("  Memory pool: %zu MB\n", config.initial_memory_pool / (1024 * 1024));
    printf("  B-tree order: %d\n", config.btree_order);
    printf("  Auto-defragmentation: %s\n", config.enable_auto_defrag ? "Enabled" : "Disabled");
    
    int choice;
    char key[64];
    char key2[64];
    char buffer[64];
    
    do {
        print_menu();
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input\n");
            while (getchar() != '\n');
            continue;
        }
        getchar();
        
        switch (choice) {
            case 1:
                printf("Enter string (1-63 chars): ");
                char insert_key[128];
                fgets(insert_key, sizeof(insert_key), stdin);
                
                size_t len = strlen(insert_key);
                if (len > 0 && insert_key[len - 1] != '\n') {
                    printf("String too long! Maximum is 63 characters.\n");
                    int c;
                    while ((c = getchar()) != '\n' && c != EOF);
                    break;
                }
                
                insert_key[strcspn(insert_key, "\n")] = 0;
                len = strlen(insert_key);
                
                if (len == 0) {
                    printf("String cannot be empty\n");
                } else if (len > 63) {
                    printf("String too long (%zu chars). Maximum is 63 characters.\n", len);
                } else if (btree_index_insert(index, insert_key)) {
                    printf("String '%s' inserted successfully\n", insert_key);
                } else {
                    printf("%s", btree_index_get_last_error(index));
                }
                break;
                
            case 2:
                printf("Enter string to search: ");
                fgets(key, sizeof(key), stdin);
                key[strcspn(key, "\n")] = 0;
                
                if (btree_index_search(index, key)) {
                    printf("String '%s' found\n", key);
                } else {
                    printf("String '%s' not found\n", key);
                }
                break;
                
            case 3:
                printf("Enter string to delete: ");
                fgets(key, sizeof(key), stdin);
                key[strcspn(key, "\n")] = 0;
                
                if (btree_index_delete(index, key)) {
                    printf("String '%s' deleted successfully\n", key);
                } else {
                    printf("Failed to delete: %s\n", btree_index_get_last_error(index));
                }
                break;
                
            case 4:
                printf("Enter start string: ");
                fgets(key, sizeof(key), stdin);
                key[strcspn(key, "\n")] = 0;
                
                printf("Enter end string: ");
                fgets(key2, sizeof(key2), stdin);
                key2[strcspn(key2, "\n")] = 0;
                
                {
                    char** keys;
                    size_t count;
                    
                    if (btree_index_range_query(index, key, key2, &keys, &count)) {
                        printf("\nFound %zu strings in range ['%s' .. '%s']:\n", count, key, key2);
                        for (size_t i = 0; i < count && i < 20; i++) {
                            printf("  %zu. %s\n", i + 1, keys[i]);
                        }
                        if (count > 20) {
                            printf("  ... and %zu more\n", count - 20);
                        }
                    } else {
                        printf("Range query failed: %s\n", btree_index_get_last_error(index));
                    }
                }
                break;
                
            case 5:
                if (btree_index_get_min(index, buffer, sizeof(buffer))) {
                    printf("Minimum string: '%s'\n", buffer);
                } else {
                    printf("Tree is empty\n");
                }
                break;
                
            case 6:
                if (btree_index_get_max(index, buffer, sizeof(buffer))) {
                    printf("Maximum string: '%s'\n", buffer);
                } else {
                    printf("Tree is empty\n");
                }
                break;
                
            case 7:
                btree_index_print_structure(index);
                break;
                
            case 8:
                btree_index_print_level_order(index);
                break;

            case 9:
                btree_index_print_stats(index);
                break;
                
            case 10:
                btree_index_dump_memory_stats(index);
                break;
                
            case 11:
                if (btree_index_validate(index)) {
                    printf("Tree is valid\n");
                } else {
                    printf("Tree validation failed: %s\n", btree_index_get_last_error(index));
                }
                break;
                
            case 12:
                if (btree_index_defragment(index)) {
                    printf("Defragmentation completed\n");
                } else {
                    printf("Defragmentation failed or not needed\n");
                }
                break;
                
            case 13:
                printf("Enter filename: ");
                fgets(key, sizeof(key), stdin);
                key[strcspn(key, "\n")] = 0;
                
                if (btree_index_save(index, key)) {
                    printf("Index saved successfully to '%s'\n", key);
                } else {
                    printf("Failed to save: %s\n", btree_index_get_last_error(index));
                }
                break;
                
            case 14:
                printf("Enter filename: ");
                fgets(key, sizeof(key), stdin);
                key[strcspn(key, "\n")] = 0;
                
                {
                    btree_index_t* loaded = btree_index_load(key);
                    if (loaded) {
                        btree_index_destroy(index);
                        index = loaded;
                        printf("Index loaded successfully from '%s'\n", key);
                    } else {
                        printf("Failed to load index\n");
                    }
                }
                break;
                
            case 15:
                btree_index_clear(index);
                printf("All data cleared\n");
                break;
                
            case 16:
                printf("Bulk inserting 10000 random strings...\n");
                {
                    clock_t start = clock();
                    for (int i = 0; i < 10000; i++) {
                        generate_random_string(key, 8 + (rand() % 20));
                        btree_index_insert(index, key);
                    }
                    clock_t end = clock();
                    printf("Completed in %.3f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);
                }
                break;
                
            case 17:
                run_benchmark(index);
                break;
                
            case 18:
                interactive_demo(index);
                break;
                
            case 0:
                printf("Exiting...\n");
                break;
                
            default:
                printf("Invalid option\n");
        }
        
        if (choice != 0) {
            printf("\nPress Enter to continue...");
            getchar();
        }
        
    } while (choice != 0);
    
    btree_index_destroy(index);
    
    return 0;
}