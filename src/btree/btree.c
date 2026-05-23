#include "btree.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BTREE_MAGIC 0x8B7EE123
#define NODE_MAGIC 0xB7E3E001

typedef struct btree_node {
    uint32_t magic;
    int is_leaf;
    int num_keys;
    char** keys;
    struct btree_node** children;
    struct btree_node* parent;
    struct btree_node* next;
    struct btree_node* prev;
} btree_node_t;

struct btree {
    uint32_t magic;
    btree_node_t* root;
    suballocator_t* allocator;
    btree_config_t config;
    btree_error_t last_error;
    size_t num_keys;
    size_t num_nodes;
    size_t num_rotations;
    size_t num_splits;
    size_t num_merges;
    size_t num_searches;
    size_t num_inserts;
    size_t num_deletes;
};

static int default_key_compare(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b);
}

static btree_node_t* btree_node_create(btree_t* tree, int is_leaf) {
    btree_node_t* node = (btree_node_t*)suballocator_calloc(tree->allocator, 1, sizeof(btree_node_t));
    if (!node) {
        tree->last_error = BTREE_ERROR_OUT_OF_MEMORY;
        return NULL;
    }
    
    node->magic = NODE_MAGIC;
    node->is_leaf = is_leaf;
    node->num_keys = 0;
    
    size_t key_size = sizeof(char*) * (2 * tree->config.order - 1);
    size_t child_size = sizeof(btree_node_t*) * (2 * tree->config.order);
    
    node->keys = (char**)suballocator_calloc(tree->allocator, 1, key_size);
    
    if (!is_leaf) {
        node->children = (btree_node_t**)suballocator_calloc(tree->allocator, 1, child_size);
    }
    
    if (!node->keys || (!is_leaf && !node->children)) {
        if (node->keys) suballocator_free(tree->allocator, node->keys);
        if (node->children) suballocator_free(tree->allocator, node->children);
        suballocator_free(tree->allocator, node);
        tree->last_error = BTREE_ERROR_OUT_OF_MEMORY;
        return NULL;
    }
    
    tree->num_nodes++;
    
    return node;
}

static void btree_node_destroy(btree_t* tree, btree_node_t* node) {
    if (!node || node->magic != NODE_MAGIC) return;
    
    for (int i = 0; i < node->num_keys; i++) {
        if (node->keys[i]) {
            suballocator_free(tree->allocator, node->keys[i]);
            node->keys[i] = NULL;
        }
    }
    
    if (node->keys) {
        suballocator_free(tree->allocator, node->keys);
        node->keys = NULL;
    }
    
    if (node->children) {
        suballocator_free(tree->allocator, node->children);
        node->children = NULL;
    }
    
    node->magic = 0;
    
    suballocator_free(tree->allocator, node);
    
    if (tree) {
        tree->num_nodes--;
    }
}

static void btree_split_child(btree_t* tree, btree_node_t* parent, int index) {
    btree_node_t* child = parent->children[index];
    btree_node_t* new_node = btree_node_create(tree, child->is_leaf);
    
    if (!new_node) return;
    
    int t = tree->config.order;
    new_node->num_keys = t - 1;
    
    for (int i = 0; i < t - 1; i++) {
        new_node->keys[i] = child->keys[i + t];
    }
    
    if (!child->is_leaf) {
        for (int i = 0; i < t; i++) {
            new_node->children[i] = child->children[i + t];
            if (new_node->children[i]) {
                new_node->children[i]->parent = new_node;
            }
        }
    }
    
    child->num_keys = t - 1;
    
    for (int i = parent->num_keys; i > index; i--) {
        parent->children[i + 1] = parent->children[i];
    }
    
    parent->children[index + 1] = new_node;
    new_node->parent = parent;
    
    for (int i = parent->num_keys - 1; i >= index; i--) {
        parent->keys[i + 1] = parent->keys[i];
    }
    
    parent->keys[index] = child->keys[t - 1];
    parent->num_keys++;
    
    tree->num_splits++;
}

static void btree_insert_non_full(btree_t* tree, btree_node_t* node, char* key) {
    int i = node->num_keys - 1;
    
    if (node->is_leaf) {
        while (i >= 0 && tree->config.key_compare(key, node->keys[i]) < 0) {
            node->keys[i + 1] = node->keys[i];
            i--;
        }
        
        if (i >= 0 && tree->config.key_compare(key, node->keys[i]) == 0) {
            if (!tree->config.allow_duplicates) {
                tree->last_error = BTREE_ERROR_KEY_EXISTS;
                return;
            }
        }
        
        node->keys[i + 1] = key;
        node->num_keys++;
        tree->num_keys++;
    } else {
        while (i >= 0 && tree->config.key_compare(key, node->keys[i]) < 0) {
            i--;
        }
        i++;
        
        if (node->children[i]->num_keys == 2 * tree->config.order - 1) {
            btree_split_child(tree, node, i);
            if (tree->config.key_compare(key, node->keys[i]) > 0) {
                i++;
            }
        }
        btree_insert_non_full(tree, node->children[i], key);
    }
}

static btree_node_t* btree_search_node(btree_t* tree, btree_node_t* node, const char* key) {
    if (!node) return NULL;
    
    int i = 0;
    while (i < node->num_keys && tree->config.key_compare(key, node->keys[i]) > 0) {
        i++;
    }
    
    tree->num_searches++;
    
    if (i < node->num_keys && tree->config.key_compare(key, node->keys[i]) == 0) {
        return node;
    }
    
    if (node->is_leaf) {
        return NULL;
    }
    
    return btree_search_node(tree, node->children[i], key);
}

static int btree_find_key_index(btree_t* tree, btree_node_t* node, const char* key) {
    int idx = 0;
    while (idx < node->num_keys && tree->config.key_compare(node->keys[idx], key) < 0) {
        idx++;
    }
    return idx;
}

static void btree_remove_from_leaf(btree_t* tree, btree_node_t* node, int idx) {
    suballocator_free(tree->allocator, node->keys[idx]);
    
    for (int i = idx + 1; i < node->num_keys; i++) {
        node->keys[i - 1] = node->keys[i];
    }
    node->num_keys--;
    tree->num_keys--;
}

static void btree_remove_from_non_leaf(btree_t* tree, btree_node_t* node, int idx) {
    char* key = node->keys[idx];
    int t = tree->config.order;
    
    if (node->children[idx]->num_keys >= t) {
        btree_node_t* pred_node = node->children[idx];
        while (!pred_node->is_leaf) {
            pred_node = pred_node->children[pred_node->num_keys];
        }
        char* pred_key = pred_node->keys[pred_node->num_keys - 1];
        
        suballocator_free(tree->allocator, node->keys[idx]);
        node->keys[idx] = suballocator_strdup(tree->allocator, pred_key);
        
        btree_delete(tree, pred_key);
    } else if (node->children[idx + 1]->num_keys >= t) {
        btree_node_t* succ_node = node->children[idx + 1];
        while (!succ_node->is_leaf) {
            succ_node = succ_node->children[0];
        }
        char* succ_key = succ_node->keys[0];
        
        suballocator_free(tree->allocator, node->keys[idx]);
        node->keys[idx] = suballocator_strdup(tree->allocator, succ_key);
        
        btree_delete(tree, succ_key);
    } else {
        btree_node_t* child = node->children[idx];
        btree_node_t* sibling = node->children[idx + 1];
        
        child->keys[t - 1] = suballocator_strdup(tree->allocator, node->keys[idx]);
        
        for (int i = 0; i < sibling->num_keys; i++) {
            child->keys[i + t] = sibling->keys[i];
        }
        
        if (!child->is_leaf) {
            for (int i = 0; i <= sibling->num_keys; i++) {
                child->children[i + t] = sibling->children[i];
                if (child->children[i + t]) {
                    child->children[i + t]->parent = child;
                }
            }
        }
        
        child->num_keys = 2 * t - 1;
        
        suballocator_free(tree->allocator, node->keys[idx]);
        for (int i = idx + 1; i < node->num_keys; i++) {
            node->keys[i - 1] = node->keys[i];
        }
        
        for (int i = idx + 2; i <= node->num_keys; i++) {
            node->children[i - 1] = node->children[i];
        }
        
        node->num_keys--;
        tree->num_merges++;
        
        sibling->num_keys = 0;
        btree_node_destroy(tree, sibling);
        
        btree_delete(tree, key);
    }
}

static void btree_borrow_from_prev(btree_t* tree, btree_node_t* node, int idx) {
    btree_node_t* child = node->children[idx];
    btree_node_t* sibling = node->children[idx - 1];
    
    for (int i = child->num_keys - 1; i >= 0; i--) {
        child->keys[i + 1] = child->keys[i];
    }
    
    if (!child->is_leaf) {
        for (int i = child->num_keys; i >= 0; i--) {
            child->children[i + 1] = child->children[i];
        }
    }
    
    child->keys[0] = suballocator_strdup(tree->allocator, node->keys[idx - 1]);
    
    if (!child->is_leaf) {
        child->children[0] = sibling->children[sibling->num_keys];
        if (child->children[0]) {
            child->children[0]->parent = child;
        }
    }
    
    suballocator_free(tree->allocator, node->keys[idx - 1]);
    node->keys[idx - 1] = suballocator_strdup(tree->allocator, sibling->keys[sibling->num_keys - 1]);
    
    child->num_keys++;
    sibling->num_keys--;
    tree->num_rotations++;
}

static void btree_borrow_from_next(btree_t* tree, btree_node_t* node, int idx) {
    btree_node_t* child = node->children[idx];
    btree_node_t* sibling = node->children[idx + 1];
    
    child->keys[child->num_keys] = suballocator_strdup(tree->allocator, node->keys[idx]);
    
    if (!child->is_leaf) {
        child->children[child->num_keys + 1] = sibling->children[0];
        if (child->children[child->num_keys + 1]) {
            child->children[child->num_keys + 1]->parent = child;
        }
    }
    
    suballocator_free(tree->allocator, node->keys[idx]);
    node->keys[idx] = suballocator_strdup(tree->allocator, sibling->keys[0]);
    
    for (int i = 1; i < sibling->num_keys; i++) {
        sibling->keys[i - 1] = sibling->keys[i];
    }
    
    if (!sibling->is_leaf) {
        for (int i = 1; i <= sibling->num_keys; i++) {
            sibling->children[i - 1] = sibling->children[i];
        }
    }
    
    child->num_keys++;
    sibling->num_keys--;
    tree->num_rotations++;
}

static void btree_merge_children(btree_t* tree, btree_node_t* node, int idx) {
    btree_node_t* left = node->children[idx];
    btree_node_t* right = node->children[idx + 1];
    int t = tree->config.order;
    
    left->keys[t - 1] = suballocator_strdup(tree->allocator, node->keys[idx]);
    
    for (int i = 0; i < right->num_keys; i++) {
        left->keys[i + t] = right->keys[i];
    }
    
    if (!left->is_leaf) {
        for (int i = 0; i <= right->num_keys; i++) {
            left->children[i + t] = right->children[i];
            if (left->children[i + t]) {
                left->children[i + t]->parent = left;
            }
        }
    }
    
    left->num_keys = 2 * t - 1;
    
    suballocator_free(tree->allocator, node->keys[idx]);
    
    for (int i = idx + 1; i < node->num_keys; i++) {
        node->keys[i - 1] = node->keys[i];
    }
    
    for (int i = idx + 2; i <= node->num_keys; i++) {
        node->children[i - 1] = node->children[i];
    }
    
    node->num_keys--;
    
    right->num_keys = 0;
    btree_node_destroy(tree, right);
    tree->num_merges++;
}

static void btree_fill(btree_t* tree, btree_node_t* node, int idx) {
    int t = tree->config.order;
    
    if (idx != 0 && node->children[idx - 1]->num_keys >= t) {
        btree_borrow_from_prev(tree, node, idx);
    } else if (idx != node->num_keys && node->children[idx + 1]->num_keys >= t) {
        btree_borrow_from_next(tree, node, idx);
    } else {
        if (idx != node->num_keys) {
            btree_merge_children(tree, node, idx);
        } else {
            btree_merge_children(tree, node, idx - 1);
        }
    }
}

static btree_error_t btree_delete_key(btree_t* tree, btree_node_t* node, const char* key) {
    int idx = btree_find_key_index(tree, node, key);
    int t = tree->config.order;
    
    if (idx < node->num_keys && tree->config.key_compare(node->keys[idx], key) == 0) {
        if (node->is_leaf) {
            btree_remove_from_leaf(tree, node, idx);
        } else {
            btree_remove_from_non_leaf(tree, node, idx);
        }
        return BTREE_ERROR_NONE;
    }
    
    if (node->is_leaf) {
        return BTREE_ERROR_KEY_NOT_FOUND;
    }
    
    int flag = (idx == node->num_keys) ? 1 : 0;
    
    if (node->children[idx]->num_keys < t) {
        btree_fill(tree, node, idx);
    }
    
    if (flag && idx > node->num_keys) {
        return btree_delete_key(tree, node->children[idx - 1], key);
    } else {
        return btree_delete_key(tree, node->children[idx], key);
    }
}

static void btree_destroy_recursive(btree_t* tree, btree_node_t* node) {
    if (!node) return;
    
    // First destroy all children
    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_keys; i++) {
            if (node->children[i]) {
                btree_destroy_recursive(tree, node->children[i]);
            }
        }
    }
    
    // Then destroy this node
    btree_node_destroy(tree, node);
}

const char* btree_get_error_string(btree_error_t error) {
    switch (error) {
        case BTREE_ERROR_NONE: return "No error";
        case BTREE_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case BTREE_ERROR_INVALID_PARAMETER: return "Invalid parameter";
        case BTREE_ERROR_KEY_NOT_FOUND: return "Key not found";
        case BTREE_ERROR_KEY_EXISTS: return "Key already exists";
        case BTREE_ERROR_KEY_TOO_LONG: return "Key too long";
        case BTREE_ERROR_TREE_CORRUPTED: return "Tree corrupted";
        case BTREE_ERROR_IO_ERROR: return "I/O error";
        default: return "Unknown error";
    }
}

btree_config_t btree_config_default(void) {
    btree_config_t config = {
        .order = BTREE_DEFAULT_ORDER,
        .allow_duplicates = 0,
        .enable_statistics = 1,
        .enable_validation = 1,
        .key_compare = default_key_compare
    };
    return config;
}

btree_t* btree_create(suballocator_t* allocator, const btree_config_t* config) {
    if (!allocator) return NULL;
    
    btree_t* tree = (btree_t*)suballocator_calloc(allocator, 1, sizeof(btree_t));
    if (!tree) return NULL;
    
    tree->magic = BTREE_MAGIC;
    tree->allocator = allocator;
    
    if (config) {
        tree->config = *config;
    } else {
        tree->config = btree_config_default();
    }
    
    if (tree->config.order < BTREE_MIN_ORDER) tree->config.order = BTREE_MIN_ORDER;
    if (tree->config.order > BTREE_MAX_ORDER) tree->config.order = BTREE_MAX_ORDER;
    
    tree->root = btree_node_create(tree, 1);
    if (!tree->root) {
        suballocator_free(allocator, tree);
        return NULL;
    }
    
    tree->last_error = BTREE_ERROR_NONE;
    
    return tree;
}

void btree_destroy(btree_t* tree) {
    if (!tree || tree->magic != BTREE_MAGIC) return;
    
    btree_destroy_recursive(tree, tree->root);
    
    tree->magic = 0;
    suballocator_free(tree->allocator, tree);
}

btree_error_t btree_insert(btree_t* tree, const char* key) {
    if (!tree || tree->magic != BTREE_MAGIC || !key) {
        if (tree) tree->last_error = BTREE_ERROR_INVALID_PARAMETER;
        return BTREE_ERROR_INVALID_PARAMETER;
    }
    
    if (strlen(key) >= BTREE_MAX_KEY_SIZE) {
        tree->last_error = BTREE_ERROR_KEY_TOO_LONG;
        return BTREE_ERROR_KEY_TOO_LONG;
    }
    
    if (btree_search(tree, key) && !tree->config.allow_duplicates) {
        tree->last_error = BTREE_ERROR_KEY_EXISTS;
        return BTREE_ERROR_KEY_EXISTS;
    }
    
    char* key_copy = suballocator_strdup(tree->allocator, key);
    if (!key_copy) {
        tree->last_error = BTREE_ERROR_OUT_OF_MEMORY;
        return BTREE_ERROR_OUT_OF_MEMORY;
    }
    
    if (tree->root->num_keys == 2 * tree->config.order - 1) {
        btree_node_t* new_root = btree_node_create(tree, 0);
        if (!new_root) {
            suballocator_free(tree->allocator, key_copy);
            return BTREE_ERROR_OUT_OF_MEMORY;
        }
        
        new_root->children[0] = tree->root;
        tree->root->parent = new_root;
        tree->root = new_root;
        btree_split_child(tree, new_root, 0);
        btree_insert_non_full(tree, new_root, key_copy);
    } else {
        btree_insert_non_full(tree, tree->root, key_copy);
    }
    
    tree->num_inserts++;
    tree->last_error = BTREE_ERROR_NONE;
    
    return BTREE_ERROR_NONE;
}

int btree_search(btree_t* tree, const char* key) {
    if (!tree || tree->magic != BTREE_MAGIC || !key) {
        return 0;
    }
    return btree_search_node(tree, tree->root, key) != NULL;
}

btree_error_t btree_delete(btree_t* tree, const char* key) {
    if (!tree || tree->magic != BTREE_MAGIC || !key) {
        if (tree) tree->last_error = BTREE_ERROR_INVALID_PARAMETER;
        return BTREE_ERROR_INVALID_PARAMETER;
    }
    
    if (!btree_search(tree, key)) {
        tree->last_error = BTREE_ERROR_KEY_NOT_FOUND;
        return BTREE_ERROR_KEY_NOT_FOUND;
    }
    
    btree_error_t result = btree_delete_key(tree, tree->root, key);
    
    if (tree->root->num_keys == 0) {
        btree_node_t* old_root = tree->root;
        if (tree->root->is_leaf) {
            tree->root = btree_node_create(tree, 1);
        } else {
            tree->root = tree->root->children[0];
            tree->root->parent = NULL;
        }
        btree_node_destroy(tree, old_root); 
    }
    
    if (result == BTREE_ERROR_NONE) {
        tree->num_deletes++;
    }
    
    tree->last_error = result;
    return result;
}

size_t btree_size(const btree_t* tree) {
    return tree ? tree->num_keys : 0;
}

int btree_is_empty(const btree_t* tree) {
    return tree ? tree->num_keys == 0 : 1;
}

void btree_clear(btree_t* tree) {
    if (!tree || tree->magic != BTREE_MAGIC) return;
    
    btree_destroy_recursive(tree, tree->root);
    tree->root = btree_node_create(tree, 1);
    tree->num_keys = 0;
    tree->num_nodes = 1;
}

static void btree_collect_range(btree_t* tree, btree_node_t* node, 
                                const char* start_key, const char* end_key,
                                char*** keys, size_t* count, size_t* capacity) {
    if (!node) return;
    
    int i;
    for (i = 0; i < node->num_keys; i++) {
        if (!node->is_leaf) {
            btree_collect_range(tree, node->children[i], start_key, end_key, keys, count, capacity);
        }
        
        int in_range = 1;
        if (start_key && tree->config.key_compare(node->keys[i], start_key) < 0) in_range = 0;
        if (end_key && tree->config.key_compare(node->keys[i], end_key) > 0) in_range = 0;
        
        if (in_range) {
            if (*count >= *capacity) {
                *capacity *= 2;
                *keys = (char**)suballocator_realloc(tree->allocator, *keys, *capacity * sizeof(char*));
            }
            
            (*keys)[*count] = suballocator_strdup(tree->allocator, node->keys[i]);
            (*count)++;
        }
    }
    
    if (!node->is_leaf) {
        btree_collect_range(tree, node->children[i], start_key, end_key, keys, count, capacity);
    }
}

btree_error_t btree_range_query(btree_t* tree, const char* start_key, const char* end_key,
                                 char*** keys, size_t* count) {
    if (!tree || !keys || !count) {
        if (tree) tree->last_error = BTREE_ERROR_INVALID_PARAMETER;
        return BTREE_ERROR_INVALID_PARAMETER;
    }
    
    size_t capacity = 100;
    *keys = (char**)suballocator_malloc(tree->allocator, capacity * sizeof(char*));
    *count = 0;
    
    btree_collect_range(tree, tree->root, start_key, end_key, keys, count, &capacity);
    
    tree->last_error = BTREE_ERROR_NONE;
    return BTREE_ERROR_NONE;
}

btree_error_t btree_get_min(btree_t* tree, char* key_buffer, size_t buffer_size) {
    if (!tree || !key_buffer) {
        if (tree) tree->last_error = BTREE_ERROR_INVALID_PARAMETER;
        return BTREE_ERROR_INVALID_PARAMETER;
    }
    
    btree_node_t* node = tree->root;
    while (!node->is_leaf) {
        node = node->children[0];
    }
    
    if (node->num_keys > 0) {
        strncpy(key_buffer, node->keys[0], buffer_size - 1);
        key_buffer[buffer_size - 1] = '\0';
        tree->last_error = BTREE_ERROR_NONE;
        return BTREE_ERROR_NONE;
    }
    
    tree->last_error = BTREE_ERROR_KEY_NOT_FOUND;
    return BTREE_ERROR_KEY_NOT_FOUND;
}

btree_error_t btree_get_max(btree_t* tree, char* key_buffer, size_t buffer_size) {
    if (!tree || !key_buffer) {
        if (tree) tree->last_error = BTREE_ERROR_INVALID_PARAMETER;
        return BTREE_ERROR_INVALID_PARAMETER;
    }
    
    btree_node_t* node = tree->root;
    while (!node->is_leaf) {
        node = node->children[node->num_keys];
    }
    
    if (node->num_keys > 0) {
        strncpy(key_buffer, node->keys[node->num_keys - 1], buffer_size - 1);
        key_buffer[buffer_size - 1] = '\0';
        tree->last_error = BTREE_ERROR_NONE;
        return BTREE_ERROR_NONE;
    }
    
    tree->last_error = BTREE_ERROR_KEY_NOT_FOUND;
    return BTREE_ERROR_KEY_NOT_FOUND;
}

static void btree_validate_recursive(btree_t* tree, btree_node_t* node, int* valid) {
    if (!node || !*valid) return;
    
    if (node->magic != NODE_MAGIC) {
        *valid = 0;
        return;
    }
    
    for (int i = 0; i < node->num_keys - 1; i++) {
        if (tree->config.key_compare(node->keys[i], node->keys[i + 1]) >= 0) {
            *valid = 0;
            return;
        }
    }
    
    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_keys; i++) {
            if (!node->children[i]) {
                *valid = 0;
                return;
            }
            if (node->children[i]->parent != node) {
                *valid = 0;
                return;
            }
            btree_validate_recursive(tree, node->children[i], valid);
        }
    }
}

int btree_validate(const btree_t* tree) {
    if (!tree || tree->magic != BTREE_MAGIC) return 0;
    
    int valid = 1;
    btree_validate_recursive((btree_t*)tree, tree->root, &valid);
    return valid;
}

static void btree_calculate_stats(btree_t* tree, btree_node_t* node, btree_stats_t* stats, size_t depth) {
    if (!node) return;
    
    if (depth > stats->height) stats->height = depth;
    if (node->is_leaf) stats->num_leaves++;
    
    float fill_factor = (float)node->num_keys / (2 * tree->config.order - 1);
    stats->avg_fill_factor += fill_factor;
    
    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_keys; i++) {
            btree_calculate_stats(tree, node->children[i], stats, depth + 1);
        }
    }
}

void btree_get_stats(const btree_t* tree, btree_stats_t* stats) {
    if (!tree || !stats) return;
    
    memset(stats, 0, sizeof(btree_stats_t));
    
    stats->num_keys = tree->num_keys;
    stats->num_nodes = tree->num_nodes;
    stats->min_degree = tree->config.order;
    stats->max_degree = 2 * tree->config.order - 1;
    stats->num_rotations = tree->num_rotations;
    stats->num_splits = tree->num_splits;
    stats->num_merges = tree->num_merges;
    stats->num_searches = tree->num_searches;
    stats->num_inserts = tree->num_inserts;
    stats->num_deletes = tree->num_deletes;
    
    btree_calculate_stats((btree_t*)tree, tree->root, stats, 1);
    
    if (stats->num_nodes > 0) {
        stats->avg_fill_factor /= stats->num_nodes;
    }
    
    suballocator_stats_t alloc_stats;
    suballocator_get_stats(tree->allocator, &alloc_stats);
    stats->memory_used = alloc_stats.used_memory;
}

void btree_print_stats(const btree_t* tree) {
    if (!tree) return;
    
    btree_stats_t stats;
    btree_get_stats(tree, &stats);
    
    printf("\nB-tree Statistics:\n");
    printf("==================\n");
    printf("Order: %zu\n", stats.min_degree);
    printf("Total keys: %zu\n", stats.num_keys);
    printf("Total nodes: %zu\n", stats.num_nodes);
    printf("Leaf nodes: %zu\n", stats.num_leaves);
    printf("Height: %zu\n", stats.height);
    printf("Memory used: %zu bytes (%.2f KB)\n", stats.memory_used, stats.memory_used / 1024.0);
    printf("Average fill factor: %.2f%%\n", stats.avg_fill_factor * 100);
    printf("Operations:\n");
    printf("  Searches: %zu\n", stats.num_searches);
    printf("  Inserts: %zu\n", stats.num_inserts);
    printf("  Deletes: %zu\n", stats.num_deletes);
    printf("  Splits: %zu\n", stats.num_splits);
    printf("  Merges: %zu\n", stats.num_merges);
    printf("  Rotations: %zu\n", stats.num_rotations);
}

static void btree_print_node(btree_node_t* node, int level) {
    if (!node) return;
    
    for (int i = 0; i < level; i++) printf("  ");
    printf("[");
    for (int i = 0; i < node->num_keys; i++) {
        printf("%s", node->keys[i]);
        if (i < node->num_keys - 1) printf(", ");
    }
    printf("]\n");
    
    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_keys; i++) {
            btree_print_node(node->children[i], level + 1);
        }
    }
}

void btree_print_structure(const btree_t* tree) {
    if (!tree || !tree->root) {
        printf("Tree is empty\n");
        return;
    }
    
    printf("\nB-tree Structure:\n");
    printf("=================\n");
    btree_print_node(tree->root, 0);
}

static void btree_serialize_node(btree_node_t* node, FILE* file) {
    if (!node) return;
    
    fwrite(&node->is_leaf, sizeof(int), 1, file);
    fwrite(&node->num_keys, sizeof(int), 1, file);
    
    for (int i = 0; i < node->num_keys; i++) {
        size_t len = strlen(node->keys[i]) + 1;
        fwrite(&len, sizeof(size_t), 1, file);
        fwrite(node->keys[i], sizeof(char), len, file);
    }
    
    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_keys; i++) {
            btree_serialize_node(node->children[i], file);
        }
    }
}

void btree_dump_to_file(const btree_t* tree, const char* filename) {
    if (!tree || !filename) return;
    
    FILE* file = fopen(filename, "wb");
    if (!file) {
        ((btree_t*)tree)->last_error = BTREE_ERROR_IO_ERROR;
        return;
    }
    
    uint32_t magic = BTREE_MAGIC;
    fwrite(&magic, sizeof(uint32_t), 1, file);
    fwrite(&tree->config.order, sizeof(int), 1, file);
    fwrite(&tree->num_keys, sizeof(size_t), 1, file);
    
    btree_serialize_node(tree->root, file);
    
    fclose(file);
}

static btree_node_t* btree_deserialize_node(btree_t* tree, FILE* file) {
    btree_node_t* node = btree_node_create(tree, 0);
    if (!node) return NULL;
    
    fread(&node->is_leaf, sizeof(int), 1, file);
    fread(&node->num_keys, sizeof(int), 1, file);
    
    for (int i = 0; i < node->num_keys; i++) {
        size_t len;
        fread(&len, sizeof(size_t), 1, file);
        node->keys[i] = (char*)suballocator_malloc(tree->allocator, len);
        fread(node->keys[i], sizeof(char), len, file);
    }
    
    if (!node->is_leaf) {
        node->children = (btree_node_t**)suballocator_calloc(tree->allocator, 
            2 * tree->config.order, sizeof(btree_node_t*));
        for (int i = 0; i <= node->num_keys; i++) {
            node->children[i] = btree_deserialize_node(tree, file);
            if (node->children[i]) {
                node->children[i]->parent = node;
            }
        }
    }
    
    return node;
}

btree_t* btree_load_from_file(suballocator_t* allocator, const char* filename) {
    if (!allocator || !filename) return NULL;
    
    FILE* file = fopen(filename, "rb");
    if (!file) return NULL;
    
    uint32_t magic;
    fread(&magic, sizeof(uint32_t), 1, file);
    if (magic != BTREE_MAGIC) {
        fclose(file);
        return NULL;
    }
    
    btree_config_t config = btree_config_default();
    fread(&config.order, sizeof(int), 1, file);
    
    btree_t* tree = btree_create(allocator, &config);
    if (!tree) {
        fclose(file);
        return NULL;
    }
    
    fread(&tree->num_keys, sizeof(size_t), 1, file);
    
    btree_node_destroy(tree, tree->root);
    tree->root = btree_deserialize_node(tree, file);
    
    fclose(file);
    
    return tree;
}

btree_error_t btree_get_last_error(const btree_t* tree) {
    return tree ? tree->last_error : BTREE_ERROR_INVALID_PARAMETER;
}

void btree_clear_error(btree_t* tree) {
    if (tree) {
        tree->last_error = BTREE_ERROR_NONE;
    }
}

void btree_print_level_order(const btree_t* tree) {
    if (!tree || !tree->root) {
        printf("Tree is empty\n");
        return;
    }
    
    printf("\n=== B-tree Level Order ===\n\n");
    
    // Calculate maximum possible nodes based on tree size
    size_t max_nodes = tree->num_nodes + 100;  // Use actual node count!
    
    btree_node_t** queue = (btree_node_t**)malloc(max_nodes * sizeof(btree_node_t*));
    int* levels = (int*)malloc(max_nodes * sizeof(int));
    
    if (!queue || !levels) {
        printf("Failed to allocate queue for printing\n");
        if (queue) free(queue);
        if (levels) free(levels);
        return;
    }
    
    int front = 0, rear = 0;
    
    queue[rear] = tree->root;
    levels[rear] = 0;
    rear++;
    
    int current_level = 0;
    
    printf("Level 0: ");
    
    while (front < rear && rear < (int)max_nodes) {
        btree_node_t* node = queue[front];
        int level = levels[front];
        front++;
        
        // ADD NULL CHECK:
        if (!node) {
            continue;  // Skip NULL nodes
        }
        
        if (level > current_level) {
            printf("\n\nLevel %d: ", level);
            current_level = level;
        }
        
        printf("[");
        for (int i = 0; i < node->num_keys; i++) {
            printf("%s", node->keys[i] ? node->keys[i] : "NULL");
            if (i < node->num_keys - 1) {
                printf(", ");
            }
        }
        printf("] ");
        
        if (!node->is_leaf) {
            for (int i = 0; i <= node->num_keys; i++) {
                if (node->children[i]) {
                    queue[rear] = node->children[i];
                    levels[rear] = level + 1;
                    rear++;
                    
                    // Check for overflow
                    if (rear >= (int)max_nodes) {
                        printf("\n... (queue full, stopping)");
                        break;
                    }
                }
            }
        }
    }
    
    printf("\n\n");
    
    free(queue);
    free(levels);
}