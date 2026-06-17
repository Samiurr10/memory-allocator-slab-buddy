CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L
AR ?= ar
BUILD := build
DOCS := docs
LIB := $(BUILD)/libsbmalloc.a

.PHONY: all test bench stats clean asan

all: $(LIB) $(BUILD)/test_allocator $(BUILD)/bench_allocator

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/sbmalloc.o: src/sbmalloc.c include/sbmalloc.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(BUILD)/sbmalloc.o
	$(AR) rcs $@ $<

$(BUILD)/test_allocator: tests/test_allocator.c $(LIB) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) -o $@

$(BUILD)/bench_allocator: bench/bench_allocator.c $(LIB) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) -o $@

test: $(BUILD)/test_allocator
	./$(BUILD)/test_allocator

asan:
	$(MAKE) BUILD=build-asan CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -O1 -g -fsanitize=address,undefined" test

bench: $(BUILD)/bench_allocator
	./$(BUILD)/bench_allocator

stats: $(BUILD)/bench_allocator
	mkdir -p $(DOCS)
	./$(BUILD)/bench_allocator > $(DOCS)/performance.md

clean:
	rm -rf build build-asan perf_stats.md
