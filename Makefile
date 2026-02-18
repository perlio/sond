
PROJECT_SCHEMA_DIR = schemas
COMPILED_SCHEMA_DIR = share/glib-2.0/schemas
SYSTEM_SCHEMA_DIR = /ucrt64/share/glib-2.0/schemas
GSCHEMAS_COMPILED = $(COMPILED_SCHEMA_DIR)/gschemas.compiled
SCHEMA_SOURCES = $(wildcard $(PROJECT_SCHEMA_DIR)/*.gschema.xml)
SYSTEM_SCHEMAS = $(SYSTEM_SCHEMA_DIR)/org.gtk.Settings.FileChooser.gschema.xml \
                 $(SYSTEM_SCHEMA_DIR)/org.gtk.Settings.ColorChooser.gschema.xml
      
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
	$(SRC_DIRS)/sond_renderer.c $(SRC_DIRS)/sond_ocr.c $(SRC_DIRS)/sond_log_and_error.c \
	$(SRC_DIRS)/sond_pdf_helper.c $(SRC_DIRS)/sond_gmessage_helper.c $(SRC_DIRS)/sond_misc.c \
	$(SRC_DIRS)/sond_file_helper.c
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
SRCS += $(shell find $(SRC_DIRS)/sond_server -name '*.c') $(SRC_DIRS)/sond_log_and_error.c \
	$(shell find $(SRC_DIRS)/sond_graph -name '*.c') $(SRC_DIRS)/sond_misc.c $(SRC_DIRS)/sond_ocr.c \
	$(SRC_DIRS)/sond_pdf_helper.c $(SRC_DIRS)/sond_gmessage_helper.c
CFLAGS += $(shell pkg-config --cflags gtk+-3.0 gmime-3.0 libmariadb libsoup-3.0 json-glib-1.0)
LDFLAGS += $(shell pkg-config --libs gmime-3.0 libmagic libmariadb libsoup-3.0 json-glib-1.0 \
	mupdf tesseract)
endif

ifneq (,$(findstring $(MAKECMDGOALS), sond_client))
SRCS += $(shell find $(SRC_DIRS)/sond_client -name '*.c') \
	$(shell find $(SRC_DIRS)/sond_graph -name '*.c') \
	$(SRC_DIRS)/sond_log_and_error.c $(SRC_DIRS)/misc_stdlib.c
CFLAGS += $(shell pkg-config --cflags gtk4 libsoup-3.0 json-glib-1.0 libmariadb jansson)
LDFLAGS += $(shell pkg-config --libs gtk4 libsoup-3.0 json-glib-1.0 libmariadb jansson)
endif

# Object files
OBJS := $(SRCS:%=$(OBJ_DIR)/%.o)

# Dependencies
DEPS := $(OBJS:.o=.d)

# Default build target
.PHONY: all
all: zond

# Include dependency files
#-include $(DEPS)
zond: $(GSCHEMAS_COMPILED) $(BIN_DIR)/zond.exe

viewer: $(GSCHEMAS_COMPILED) $(BIN_DIR)/viewer.exe

sond_server: $(BIN_DIR)/sond_server.exe

sond_client: $(BIN_DIR)/sond_client.exe

seafile_test: $(BIN_DIR)/seafile_test.exe

# Linking
$(BIN_DIR)/$(MAKECMDGOALS).exe: $(SRCS:%=$(OBJ_DIR)/%.o)
	echo $(SRCS) $(BIN_DIR)/$(MAKECMDGOALS).exe
	@mkdir -p $(BIN_DIR)
	$(CC) $(SRCS:%=$(OBJ_DIR)/%.o) $(LDFLAGS) $(LDFLAGS_CONFIG) -o $@

# Compiling
$(OBJ_DIR)/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) -c $< $(CFLAGS_CONFIG) $(CFLAGS) -MMD -MP -o $@

$(GSCHEMAS_COMPILED): $(SCHEMA_SOURCES) $(SYSTEM_SCHEMAS)
	@echo "Copying project schemas to compilation directory..."
	@cp $(SCHEMA_SOURCES) $(COMPILED_SCHEMA_DIR)/
	@echo "Copying system schemas..."
	@cp $(SYSTEM_SCHEMAS) $(COMPILED_SCHEMA_DIR)/
	@echo "Compiling GSettings schemas..."
	glib-compile-schemas $(COMPILED_SCHEMA_DIR)
	@echo "Removing temporary schemas..."
	@cd $(COMPILED_SCHEMA_DIR) && rm -f $(notdir $(SCHEMA_SOURCES)) org.gtk.Settings.FileChooser.gschema.xml org.gtk.Settings.ColorChooser.gschema.xml

# Clean
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/obj
	rm -rf $(BUILD_DIR)/bin