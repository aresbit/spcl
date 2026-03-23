PROJECT := spcl
CC ?= cc
ifeq ($(origin AR), default)
AR := $(or $(shell command -v llvm-ar 2>/dev/null),$(shell command -v gcc-ar 2>/dev/null),$(shell command -v ar 2>/dev/null),ar)
endif
ARFLAGS ?= rcs
INSTALL ?= install
RM ?= rm -f
MKDIR_P ?= mkdir -p

PREFIX ?= /usr/local
EXEC_PREFIX ?= $(PREFIX)
BINDIR ?= $(EXEC_PREFIX)/bin
LIBINSTALLDIR ?= $(EXEC_PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
DATADIR ?= $(PREFIX)/share
PKGDATADIR ?= $(DATADIR)/$(PROJECT)
DESTDIR ?=

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
SPCL_TARGET := $(BIN_DIR)/spcl
TEST_TARGET := $(BIN_DIR)/test_spcl
PUBLIC_HEADERS := $(INC_DIR)/spcl.h $(INC_DIR)/sp_compat.h $(INC_DIR)/sp.h
INSTALL_SCRIPTS := tools/parsespcl.sh tools/llskill2spcl.sh
INSTALL_SCRIPT_NAMES := werk llskill2spcl
DOC_DATA_FILES := docs/SPCL-Skill-Composition-DSL.md docs/skill-spcl-template.spcl docs/skill-markdown-frontend.md

.PHONY: all clean test run sanitize lint format compdb install uninstall

all: $(CLI_TARGET) $(SPCL_TARGET)

$(OBJ_DIR) $(BIN_DIR) $(LIB_DIR):
	$(MKDIR_P) $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/tests_%.o: $(TEST_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB_TARGET): $(LIB_OBJS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

$(CLI_TARGET): $(CLI_OBJS) $(LIB_TARGET) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(SPCL_TARGET): $(CLI_OBJS) $(LIB_TARGET) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST_TARGET): $(TEST_OBJS) $(LIB_TARGET) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

test: $(TEST_TARGET)
	$(TEST_TARGET)

run: $(SPCL_TARGET)
	$(SPCL_TARGET) --help

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

install: all $(LIB_TARGET)
	$(MKDIR_P) $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBINSTALLDIR) $(DESTDIR)$(INCLUDEDIR) $(DESTDIR)$(PKGDATADIR)/docs
	$(INSTALL) -m 755 $(SPCL_TARGET) $(DESTDIR)$(BINDIR)/spcl
	$(INSTALL) -m 755 $(CLI_TARGET) $(DESTDIR)$(BINDIR)/cclq
	$(INSTALL) -m 755 tools/parsespcl.sh $(DESTDIR)$(BINDIR)/werk
	$(INSTALL) -m 755 tools/llskill2spcl.sh $(DESTDIR)$(BINDIR)/llskill2spcl
	$(INSTALL) -m 644 $(LIB_TARGET) $(DESTDIR)$(LIBINSTALLDIR)/lib$(PROJECT).a
	$(INSTALL) -m 644 $(PUBLIC_HEADERS) $(DESTDIR)$(INCLUDEDIR)/
	$(INSTALL) -m 644 $(DOC_DATA_FILES) $(DESTDIR)$(PKGDATADIR)/docs/

uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/spcl
	$(RM) $(DESTDIR)$(BINDIR)/cclq
	$(RM) $(DESTDIR)$(BINDIR)/werk
	$(RM) $(DESTDIR)$(BINDIR)/llskill2spcl
	$(RM) $(DESTDIR)$(LIBINSTALLDIR)/lib$(PROJECT).a
	$(RM) $(DESTDIR)$(INCLUDEDIR)/spcl.h
	$(RM) $(DESTDIR)$(INCLUDEDIR)/sp_compat.h
	$(RM) $(DESTDIR)$(INCLUDEDIR)/sp.h
	$(RM) $(DESTDIR)$(PKGDATADIR)/docs/SPCL-Skill-Composition-DSL.md
	$(RM) $(DESTDIR)$(PKGDATADIR)/docs/skill-spcl-template.spcl
	$(RM) $(DESTDIR)$(PKGDATADIR)/docs/skill-markdown-frontend.md

clean:
	$(RM) -r $(BUILD_DIR)

-include $(LIB_DEPS) $(CLI_DEPS) $(TEST_DEPS)
