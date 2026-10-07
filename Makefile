# Makefile for Custom Memory Allocator Project
# Target: Linux/WSL, gcc, -std=c11

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -g -Iinclude
BENCH_CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude

# Handle executable extension across POSIX and Windows/MinGW environments
ifeq ($(OS),Windows_NT)
    EXE = .exe
    RM = powershell -NoProfile -Command "& { foreach ($$f in $$args) { Remove-Item -Force -ErrorAction SilentlyContinue $$f } }"
else
    EXE =
    RM = rm -f
endif

TEST_BIN = test_alloc$(EXE)
BENCH_BIN = bench_runner$(EXE)
SWEEP_BIN = seed_sweep$(EXE)

SRCS = src/myalloc.c
TEST_SRCS = tests/test_alloc.c
BENCH_SRCS = bench/bench.c
SWEEP_SRCS = bench/seed_sweep.c
HEADERS = include/myalloc.h

.PHONY: all test bench sweep report valgrind clean

all: $(TEST_BIN) $(BENCH_BIN) $(SWEEP_BIN)

$(TEST_BIN): $(SRCS) $(TEST_SRCS) $(HEADERS)
	$(CC) $(CFLAGS) $(SRCS) $(TEST_SRCS) -o $@

$(BENCH_BIN): $(SRCS) $(BENCH_SRCS) $(HEADERS)
	$(CC) $(BENCH_CFLAGS) $(SRCS) $(BENCH_SRCS) -o $@

$(SWEEP_BIN): $(SRCS) $(SWEEP_SRCS) $(HEADERS)
	$(CC) $(BENCH_CFLAGS) $(SRCS) $(SWEEP_SRCS) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

bench: $(BENCH_BIN)
	./$(BENCH_BIN)

seed_sweep: $(SWEEP_BIN)

sweep: $(SWEEP_BIN)
	./$(SWEEP_BIN)

report:
	python3 report/generate_report_table.py bench/results.csv report/REPORT.md || python report/generate_report_table.py bench/results.csv report/REPORT.md

valgrind: $(TEST_BIN)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TEST_BIN)

clean:
	$(RM) $(TEST_BIN) $(BENCH_BIN) $(SWEEP_BIN) test_alloc test_alloc.exe bench_runner bench_runner.exe seed_sweep seed_sweep.exe *.o src/*.o tests/*.o bench/*.o
