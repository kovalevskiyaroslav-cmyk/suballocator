# B-tree Index with Custom Suballocator

## Course Project - Memory Suballocator for B-tree Index

### Overview
This project implements a custom memory suballocator and a B-tree index library in C. The suballocator manages memory within the process space and interacts with the OS using standard malloc, realloc, and free functions. The B-tree index supports insertion, search, and deletion of string keys (1-63 bytes).

### Features

#### Suballocator
- Custom memory pool management
- Multiple allocation strategies (First-fit, Best-fit, Worst-fit)
- Automatic pool expansion using OS realloc
- Memory defragmentation
- Thread-safe operations
- Comprehensive statistics tracking
- Heap validation and debugging features
- Buffer overflow detection with canaries

#### B-tree Index
- Full B-tree implementation with configurable order
- Support for duplicate keys (optional)
- CRUD operations (Create, Read, Update, Delete)
- Range queries
- Min/Max key retrieval
- Tree traversal iterators
- Serialization to/from disk
- Automatic balancing (split/merge/rotate)
- Performance statistics
- Memory-efficient storage

### Requirements Fulfilled
- Suballocator in process space
- Interaction with OS via malloc/realloc/free
- Analogous malloc/free/realloc interface for index
- B-tree with insert/search/delete operations
- String keys from 1 to 63 bytes
- Complete library implementation
- Linux compatibility

### Building

make clean
make all