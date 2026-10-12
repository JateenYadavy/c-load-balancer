# ==============================================================================
# Makefile - Build Automation for CPU Task Assigner & Schedulers
# ==============================================================================
#
# WHAT WE WANT IN THIS MAKEFILE:
# 1. Default Build (make):
#    - Compiles with full optimizations (-O2) and warnings (-Wall -Wextra).
#    - Links POSIX threads (-pthread) and math library (-lm).
#
# 2. AddressSanitizer Build (make asan):
#    - Compiles with -fsanitize=address,undefined -g.
#    - Automatically catches memory leaks, buffer overflows, and use-after-free bugs.
#
# 3. ThreadSanitizer Build (make tsan):
#    - Compiles with -fsanitize=thread -g.
#    - Automatically detects data races, lock-order deadlocks, and unprotected memory.
#
# 4. Utilities:
#    - make run: Compiles and runs the benchmark suite.
#    - make clean: Cleans up binaries and compiled artifacts.
# ==============================================================================

CC ?= gcc
CFLAGS = -Wall -Wextra -Iinclude -pthread
LDFLAGS = -pthread -lm

SRCS = src/threadpool.c src/scheduler.c src/main.c
TARGET = load_balancer

# Default: Optimized release build
all: CFLAGS += -O2
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $(TARGET)

# Memory Safety Build (Catches memory leaks, out-of-bounds, use-after-free)
asan: CFLAGS += -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
asan: clean $(TARGET)

# Concurrency Race Detector (Catches data races, race conditions, deadlocks)
tsan: CFLAGS += -g -O1 -fsanitize=thread -fno-omit-frame-pointer
tsan: clean $(TARGET)

# Run the compiled test binary
run: all
	./$(TARGET)

# Clean build artifacts
clean:
	rm -f $(TARGET) *.o

.PHONY: all asan tsan run clean
