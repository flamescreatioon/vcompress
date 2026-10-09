CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -O3 -Iinclude
DEBUG_CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -g -Iinclude
LDFLAGS = -pthread -lpsapi -lm -lole32 -lmfplat -lmfreadwrite -lmfuuid

SRCS = src/main.c \
       src/cli.c \
       src/file.c \
       src/buffer.c \
       src/frame.c \
       src/compressor.c \
       src/encoder.c \
       src/decoder.c \
       src/container.c \
       src/thread_pool.c \
       src/utils.c

OBJS = $(SRCS:.c=.o)
TARGET = vcompress.exe

TEST_SRCS = tests/test_main.c \
            src/cli.c \
            src/file.c \
            src/buffer.c \
            src/frame.c \
            src/compressor.c \
            src/encoder.c \
            src/decoder.c \
            src/container.c \
            src/thread_pool.c \
            src/utils.c

TEST_TARGET = tests/test_runner.exe

SAMPLE_SRCS = examples/generate_sample.c \
              src/file.c \
              src/buffer.c \
              src/frame.c \
              src/container.c \
              src/utils.c

SAMPLE_TARGET = examples/generate_sample.exe

DIAGNOSTIC_SRCS = tests/test_diagnostic.c \
                  src/cli.c \
                  src/file.c \
                  src/buffer.c \
                  src/frame.c \
                  src/compressor.c \
                  src/encoder.c \
                  src/decoder.c \
                  src/container.c \
                  src/thread_pool.c \
                  src/utils.c

DIAGNOSTIC_TARGET = tests/test_diagnostic.exe

.PHONY: all clean test diagnostic examples debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

debug: CFLAGS = $(DEBUG_CFLAGS)
debug: clean $(TARGET)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_TARGET): $(TEST_SRCS)
	$(CC) $(CFLAGS) $(TEST_SRCS) -o $@ $(LDFLAGS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(DIAGNOSTIC_TARGET): $(DIAGNOSTIC_SRCS)
	$(CC) $(CFLAGS) $(DIAGNOSTIC_SRCS) -o $@ $(LDFLAGS)

diagnostic: $(DIAGNOSTIC_TARGET)
	./$(DIAGNOSTIC_TARGET)

$(SAMPLE_TARGET): $(SAMPLE_SRCS)
	$(CC) $(CFLAGS) $(SAMPLE_SRCS) -o $@ $(LDFLAGS)

examples: $(SAMPLE_TARGET)

clean:
	cmd /c "del /q /f src\*.o *.exe tests\*.exe examples\*.exe 2>nul || exit 0"
