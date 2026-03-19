PROJECT := spcl
CC ?= cc
AR := llvm-ar
RM ?= rm -f
MKDIR_P ?= mkdir -p

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin
LIB_DIR := $(BUILD_DIR)/lib

SRC_DIR := src
INC_DIR := include
TEST_DIR := tests

WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wwrite-strings
OPT ?= -O2
CSTD := -std=c17
CPPFLAGS := -I$(INC_DIR) -MMD -MP -DSP_PS_DISABLE
CFLAGS ?= $(CSTD) $(OPT) $(WARNINGS)
LDFLAGS ?=
LDLIBS ?=

LIB_SRCS := $(SRC_DIR)/sp_impl.c $(SRC_DIR)/parser.c $(SRC_DIR)/model.c $(SRC_DIR)/io.c
LIB_OBJS := $(LIB_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
LIB_DEPS := $(LIB_OBJS:.o=.d)

CLI_SRCS := $(SRC_DIR)/cclq.c
CLI_OBJS := $(CLI_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
CLI_DEPS := $(CLI_OBJS:.o=.d)

TEST_SRCS := $(TEST_DIR)/test_spcl.c
TEST_OBJS := $(TEST_SRCS:$(TEST_DIR)/%.c=$(OBJ_DIR)/tests_%.o)
TEST_DEPS := $(TEST_OBJS:.o=.d)

LIB_TARGET := $(LIB_DIR)/lib$(PROJECT).a
CLI_TARGET := $(BIN_DIR)/cclq
TEST_TARGET := $(BIN_DIR)/test_spcl

.PHONY: all clean test run sanitize lint format compdb

all: $(CLI_TARGET)

$(OBJ_DIR) $(BIN_DIR) $(LIB_DIR):
	$(MKDIR_P) $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/tests_%.o: $(TEST_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB_TARGET): $(LIB_OBJS) | $(LIB_DIR)
	$(AR) rcs $@ $^

$(CLI_TARGET): $(CLI_OBJS) $(LIB_TARGET) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST_TARGET): $(TEST_OBJS) $(LIB_TARGET) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

test: $(TEST_TARGET)
	$(TEST_TARGET)

run: $(CLI_TARGET)
	$(CLI_TARGET) --help

sanitize: CFLAGS := $(CSTD) -O1 -g3 -fno-omit-frame-pointer $(WARNINGS) -fsanitize=address,undefined
sanitize: LDFLAGS += -fsanitize=address,undefined
sanitize: clean all test

lint:
	@command -v clang-tidy >/dev/null 2>&1 || { echo "clang-tidy not found"; exit 0; }
	clang-tidy $(LIB_SRCS) $(CLI_SRCS) $(TEST_SRCS) -- -I$(INC_DIR) $(CSTD)

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found"; exit 0; }
	clang-format -i $(INC_DIR)/*.h $(SRC_DIR)/*.c $(TEST_DIR)/*.c

compdb:
	@command -v compiledb >/dev/null 2>&1 || { echo "compiledb not found"; exit 0; }
	compiledb make -B all test

clean:
	$(RM) -r $(BUILD_DIR)

-include $(LIB_DEPS) $(CLI_DEPS) $(TEST_DEPS)
