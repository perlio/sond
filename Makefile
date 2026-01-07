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
	$(SRC_DIRS)/sond_fileparts.c $(SRC_DIRS)/sond_treeview.c $(SRC_DIRS)/sond_treeviewfm.c \
	$(SRC_DIRS)/sond_renderer.c $(SRC_DIRS)/sond_ocr.c
CFLAGS += $(shell pkg-config --cflags libmagic libxml-2.0 gtk+-3.0 gobject-2.0 json-glib-1.0 gmime-3.0) \
	-DCONFIG_$(CONFIG)
LDFLAGS += $(shell pkg-config --libs libmagic libxml-2.0 gtk+-3.0 sqlite3 libcurl tesseract \
	libzip json-glib-1.0 gmime-3.0 mupdf)
ifneq ($(CONFIG), Debug_Linux)
LDFLAGS += -lshlwapi -llexbor
endif
endif

ifneq (,$(findstring $(MAKECMDGOALS), viewer))
SRCS += $(shell find $(SRC_DIRS)/zond/40viewer -name '*.c') $(SRC_DIRS)/zond/zond_pdf_document.c \
$(SRC_DIRS)/zond/99conv/general.c $(SRC_DIRS)/zond/99conv/pdf.c $(SRC_DIRS)/zond/pdf_ocr.c \
	$(SRC_DIRS)/misc.c $(SRC_DIRS)/misc_stdlib.c $(SRC_DIRS)/sond_fileparts.c $(SRC_DIRS)/sond_renderer.c
CFLAGS += -DVIEWER $(shell pkg-config --cflags libmagic gtk+-3.0 gobject-2.0 json-glib-1.0 gmime-3.0 \
	libxml-2.0)
LDFLAGS += $(shell pkg-config --libs libmagic gtk+-3.0 gobject-2.0 sqlite3 libcurl tesseract libzip \
	json-glib-1.0 mupdf gmime-3.0 libxml-2.0) \
	-lshlwapi -llexbor
endif

ifneq (,$(findstring $(MAKECMDGOALS), sond_server))
SRCS := $(shell find $(SRC_DIRS)/sond/sond_server -name '*.c')
CFLAGS += $(shell pkg-config --cflags libmariadb libsoup-3.0 json-glib-1.0)
LDFLAGS += $(shell pkg-config --libs libmariadb libsoup-3.0 json-glib-1.0)
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

sond_server: $(BIN_DIR)/sond_server.exe

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