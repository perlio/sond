# Build Directories
BUILD_DIR := $(CONFIG)
BIN_DIR := $(BUILD_DIR)/bin
OBJ_DIR := $(BUILD_DIR)/obj

# Source files
SRC_DIRS := src

# Compiler and linker flags
CFLAGS := -Wall
LDFLAGS := -Wl,--allow-multiple-definition

# Find source files
ifneq (,$(findstring $(MAKECMDGOALS), zond))
SRCS += $(shell find $(SRC_DIRS)/zond -name '*.c') $(SRC_DIRS)/misc_stdlib.c $(SRC_DIRS)/misc.c
CFLAGS += $(shell pkg-config --cflags gtk+-3.0 gobject-2.0 json-glib-1.0)
LDFLAGS += $(shell pkg-config --libs gtk+-3.0 gobject-2.0 sqlite3 libcurl tesseract libzip json-glib-1.0) \
	-lshlwapi -lmupdf -lmupdf-third -lpodofo
endif

# Object files
OBJS := $(SRCS:%=$(OBJ_DIR)/%.o)

# Dependencies
DEPS := $(OBJS:.o=.d)

# Default build target
.PHONY: all
all: zond

# Include dependency files
-include $(DEPS)

zond: $(BIN_DIR)/zond.exe

# Linking
$(BIN_DIR)/$(MAKECMDGOALS).exe: $(SRCS:%=$(OBJ_DIR)/%.o)
	echo $(SRCS) $(BIN_DIR)/$(MAKECMDGOALS).exe
	@mkdir -p $(BIN_DIR)
	$(CC) $(SRCS:%=$(OBJ_DIR)/%.o) $(LDFLAGS) -o $@

# Compiling
$(OBJ_DIR)/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) -c $< $(CFLAGS_CONFIG) $(CFLAGS) -MMD -MP -o $@

# Clean
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)