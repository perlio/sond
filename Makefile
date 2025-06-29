# Build Directories
BUILD_DIR := $(CONFIG)
BIN_DIR := $(BUILD_DIR)/bin
OBJ_DIR := $(BUILD_DIR)/obj/$(MAKECMDGOALS)

# Source files
SRC_DIRS := src

# Compiler and linker flags
CFLAGS := -Wall
LDFLAGS := -Wl,--allow-multiple-definition

# Find source files
ifneq (,$(findstring $(MAKECMDGOALS), zond))
SRCS += $(shell find $(SRC_DIRS)/zond -name '*.c') $(SRC_DIRS)/misc_stdlib.c $(SRC_DIRS)/misc.c \
	$(SRC_DIRS)/sond_fileparts.c
CFLAGS += $(shell pkg-config --cflags gtk+-3.0 gobject-2.0 json-glib-1.0) -DCONFIG_$(CONFIG)
LDFLAGS += $(shell pkg-config --libs gtk+-3.0 gobject-2.0 sqlite3 libcurl tesseract libzip json-glib-1.0) \
	-lshlwapi -lmupdf -lmupdf-third
endif

ifneq (,$(findstring $(MAKECMDGOALS), viewer))
SRCS += $(shell find $(SRC_DIRS)/zond/40viewer -name '*.c') $(SRC_DIRS)/zond/zond_pdf_document.c \
$(SRC_DIRS)/zond/99conv/general.c $(SRC_DIRS)/zond/99conv/pdf.c $(SRC_DIRS)/zond/pdf_ocr.c $(SRC_DIRS)/misc.c \
$(SRC_DIRS)/misc_stdlib.c
CFLAGS += -DVIEWER $(shell pkg-config --cflags gtk+-3.0 gobject-2.0 json-glib-1.0)
LDFLAGS += $(shell pkg-config --libs gtk+-3.0 gobject-2.0 sqlite3 libcurl tesseract libzip json-glib-1.0) \
	-lshlwapi -lmupdf -lmupdf-third
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

viewer: $(BIN_DIR)/viewer.exe

# Linking
$(BIN_DIR)/$(MAKECMDGOALS).exe: $(SRCS:%=$(OBJ_DIR)/%.o)
	echo $(SRCS) $(BIN_DIR)/$(MAKECMDGOALS).exe
	@mkdir -p $(BIN_DIR)
	$(CC) $(SRCS:%=$(OBJ_DIR)/%.o) $(LDFLAGS) $(LDFLAGS_CONFIG) -o $@

# Compiling
$(OBJ_DIR)/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) -c $< $(CFLAGS_CONFIG) $(CFLAGS) -MMD -MP -o $@

# Clean
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/obj
	rm -rf $(BUILD_DIR)/bin
