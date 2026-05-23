CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -fPIC -Iinclude
LDFLAGS = -lpthread -lm
AR = ar
ARFLAGS = rcs

SRC_DIR = src
BUILD_DIR = build
LIB_DIR = lib
BIN_DIR = bin
TEST_DIR = tests
DEMO_DIR = demo

SUBALLOCATOR_SRC = $(SRC_DIR)/suballocator/suballocator.c
BTREE_SRC = $(SRC_DIR)/btree/btree.c
INDEX_SRC = $(SRC_DIR)/btree_index.c

SUBALLOCATOR_OBJ = $(BUILD_DIR)/suballocator.o
BTREE_OBJ = $(BUILD_DIR)/btree.o
INDEX_OBJ = $(BUILD_DIR)/btree_index.o

STATIC_LIB = $(LIB_DIR)/libbtreeindex.a
SHARED_LIB = $(LIB_DIR)/libbtreeindex.so

TEST_SUBALLOCATOR = $(BIN_DIR)/test_suballocator
TEST_BTREE = $(BIN_DIR)/test_btree
DEMO_PROGRAM = $(BIN_DIR)/btree_demo

all: directories static shared tests demo

directories:
	mkdir -p $(BUILD_DIR) $(LIB_DIR) $(BIN_DIR)

static: $(STATIC_LIB)

shared: $(SHARED_LIB)

$(BUILD_DIR)/suballocator.o: $(SUBALLOCATOR_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/btree.o: $(BTREE_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/btree_index.o: $(INDEX_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(STATIC_LIB): $(SUBALLOCATOR_OBJ) $(BTREE_OBJ) $(INDEX_OBJ)
	$(AR) $(ARFLAGS) $@ $^

$(SHARED_LIB): $(SUBALLOCATOR_OBJ) $(BTREE_OBJ) $(INDEX_OBJ)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

tests: $(TEST_SUBALLOCATOR) $(TEST_BTREE)

$(TEST_SUBALLOCATOR): $(TEST_DIR)/test_suballocator.c $(STATIC_LIB)
	$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbtreeindex -o $@ $(LDFLAGS)

$(TEST_BTREE): $(TEST_DIR)/test_btree.c $(STATIC_LIB)
	$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbtreeindex -o $@ $(LDFLAGS)

demo: $(DEMO_PROGRAM)

$(DEMO_PROGRAM): $(DEMO_DIR)/main.c $(STATIC_LIB)
	$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbtreeindex -o $@ $(LDFLAGS)

run-tests: tests
	@printf "\n=== Running Suballocator Tests ===\n\n"
	-LD_LIBRARY_PATH=$(LIB_DIR) $(TEST_SUBALLOCATOR)
	@printf "\n=== Running B-tree Tests ===\n\n"
	-LD_LIBRARY_PATH=$(LIB_DIR) $(TEST_BTREE)

run-demo: demo
	LD_LIBRARY_PATH=$(LIB_DIR) $(DEMO_PROGRAM)

benchmark: demo
	LD_LIBRARY_PATH=$(LIB_DIR) $(DEMO_PROGRAM) --benchmark

install: all
	sudo cp $(LIB_DIR)/libbtreeindex.so /usr/local/lib/
	sudo cp $(LIB_DIR)/libbtreeindex.a /usr/local/lib/
	sudo cp include/*.h /usr/local/include/
	sudo ldconfig

uninstall:
	sudo rm -f /usr/local/lib/libbtreeindex.*
	sudo rm -f /usr/local/include/suballocator.h
	sudo rm -f /usr/local/include/btree.h
	sudo rm -f /usr/local/include/btree_index.h
	sudo ldconfig

clean:
	rm -rf $(BUILD_DIR) $(LIB_DIR) $(BIN_DIR)

distclean: clean
	rm -f *.idx test_btree_dump.idx

docs:
	doxygen Doxyfile

package:
	tar -czf btree_index_$(VERSION).tar.gz include src tests demo Makefile README.md LICENSE

.PHONY: all directories static shared tests demo run-tests run-demo benchmark install uninstall clean distclean docs package