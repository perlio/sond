PROJECT_SCHEMA_DIR = schemas
COMPILED_SCHEMA_DIR = schemas
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
SRCS += $(filter-out $(SRC_DIRS)/zond/40viewer/stand_alone.c, \
	$(shell find $(SRC_DIRS)/zond -name '*.c')) \
	$(SRC_DIRS)/misc_stdlib.c $(SRC_DIRS)/misc.c \
	$(SRC_DIRS)/sond_fileparts.c $(SRC_DIRS)/sond_treeview.c $(SRC_DIRS)/sond_treeviewfm.c \
	$(SRC_DIRS)/sond_renderer.c $(SRC_DIRS)/sond_ocr.c $(SRC_DIRS)/sond_log_and_error.c \
	$(SRC_DIRS)/sond_pdf_helper.c $(SRC_DIRS)/sond_gmessage_helper.c $(SRC_DIRS)/sond_mime.c \
	$(SRC_DIRS)/sond_file_helper.c $(SRC_DIRS)/sond_process_file.c $(SRC_DIRS)/sond_index.c \
	$(SRC_DIRS)/sond_result_view.c $(SRC_DIRS)/sond_treeviewfm_seadrive.c
CFLAGS += $(shell pkg-config --cflags libsoup-3.0 libmagic libxml-2.0 gtk+-3.0 gobject-2.0 json-glib-1.0 gmime-3.0) \
	-DCONFIG_$(CONFIG)
LDFLAGS += $(shell pkg-config --libs libmagic libxml-2.0 gtk+-3.0 sqlite3 libcurl tesseract \
	libzip json-glib-1.0 gmime-3.0 mupdf)
ifneq ($(CONFIG), Debug_Linux)
LDFLAGS += -lshlwapi -llexbor
endif
endif

ifneq (,$(findstring $(MAKECMDGOALS), viewer))
SRCS += $(shell find $(SRC_DIRS)/zond/40viewer -name '*.c') \
	$(SRC_DIRS)/sond_file_helper.c $(SRC_DIRS)/sond_pdf_helper.c $(SRC_DIRS)/sond_gmessage_helper.c \
	$(SRC_DIRS)/sond_log_and_error.c $(SRC_DIRS)/sond_fileparts.c $(SRC_DIRS)/sond_renderer.c\
	$(SRC_DIRS)/zond/zond_pdf_document.c $(SRC_DIRS)/zond/99conv/general.c $(SRC_DIRS)/zond/pdf_ocr.c $(SRC_DIRS)/sond_ocr.c \
	$(SRC_DIRS)/misc.c $(SRC_DIRS)/misc_stdlib.c $(SRC_DIRS)/sond_mime.c
CFLAGS += -DVIEWER $(shell pkg-config --cflags libmagic gtk+-3.0 gobject-2.0 json-glib-1.0 gmime-3.0 \
	libxml-2.0)
LDFLAGS += $(shell pkg-config --libs libmagic gtk+-3.0 gobject-2.0 sqlite3 libcurl tesseract libzip \
	json-glib-1.0 mupdf gmime-3.0 libxml-2.0) \
	-lshlwapi -llexbor
endif

ifneq (,$(findstring $(MAKECMDGOALS), sond_server))
SRCS += $(shell find $(SRC_DIRS)/sond_server -name '*.c') $(SRC_DIRS)/sond_log_and_error.c \
	$(shell find $(SRC_DIRS)/sond_graph -name '*.c') $(SRC_DIRS)/sond_misc.c $(SRC_DIRS)/sond_ocr.c \
	$(SRC_DIRS)/sond_pdf_helper.c $(SRC_DIRS)/sond_gmessage_helper.c $(SRC_DIRS)/sond_file_helper.c \
	$(SRC_DIRS)/sond_process_file.c $(SRC_DIRS)/sond_index.c
CFLAGS += $(shell pkg-config --cflags gobject-2.0 glib-2.0 gio-2.0 gmime-3.0 libmariadb \
	libsoup-3.0 json-glib-1.0) -I/usr/local/include/mupdf
LDFLAGS += $(shell pkg-config --libs gmime-3.0 gio-2.0 libmagic libmariadb libsoup-3.0 json-glib-1.0 \
	sqlite3 tesseract libzip) /usr/local/lib/libmupdf.a /usr/local/lib/libmupdf-third.a -lm
endif

ifneq (,$(findstring $(MAKECMDGOALS), sond_client))
SRCS += $(shell find $(SRC_DIRS)/sond_client -name '*.c') \
	$(shell find $(SRC_DIRS)/sond_graph -name '*.c') $(SRC_DIRS)/sond_file_helper.c \
	$(SRC_DIRS)/sond_log_and_error.c $(SRC_DIRS)/misc_stdlib.c
CFLAGS += $(shell pkg-config --cflags gtk4 libsoup-3.0 json-glib-1.0 libmariadb jansson)
LDFLAGS += $(shell pkg-config --libs gtk4 libsoup-3.0 json-glib-1.0 libmariadb jansson)
endif

ifneq (,$(findstring $(MAKECMDGOALS), zond_installer))
SRCS += $(SRC_DIRS)/zond/zond_installer.c \
	$(SRC_DIRS)/sond_file_helper.c $(SRC_DIRS)/sond_log_and_error.c \
	$(SRC_DIRS)/misc_stdlib.c
CFLAGS += $(shell pkg-config --cflags glib-2.0 gio-2.0)
LDFLAGS += $(shell pkg-config --libs glib-2.0 gio-2.0)
ifneq ($(CONFIG), Debug_Linux)
LDFLAGS += -lshlwapi
endif
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
.PHONY: zond viewer sond_server sond_client zond_installer seafile_test
zond: $(GSCHEMAS_COMPILED) $(BIN_DIR)/zond.exe

viewer: $(GSCHEMAS_COMPILED) $(BIN_DIR)/viewer.exe

sond_server: $(BIN_DIR)/sond_server.exe

sond_client: $(BIN_DIR)/sond_client.exe

zond_installer: $(BIN_DIR)/zond_installer.exe

seafile_test: $(BIN_DIR)/seafile_test.exe

# Linking
$(BIN_DIR)/$(MAKECMDGOALS).exe: $(SRCS:%=$(OBJ_DIR)/%.o)
	echo $(SRCS) $(BIN_DIR)/$(MAKECMDGOALS).exe
	@mkdir -p $(BIN_DIR)
	$(CC) $(SRCS:%=$(OBJ_DIR)/%.o) $(LDFLAGS) $(LDFLAGS_CONFIG) -o $@

# Verzeichnisse anlegen
$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

# Compiling
$(OBJ_DIR)/%.c.o: %.c | $(OBJ_DIR)
	@mkdir -p $(@D)
	$(CC) -c $< $(CFLAGS_CONFIG) $(CFLAGS) -MMD -MP -o $@

$(GSCHEMAS_COMPILED): $(SCHEMA_SOURCES)
	@echo "Copying system schemas..."
	@mkdir -p $(COMPILED_SCHEMA_DIR)
	@cp $(SYSTEM_SCHEMAS) $(COMPILED_SCHEMA_DIR)/
	@echo "Compiling GSettings schemas..."
	glib-compile-schemas $(COMPILED_SCHEMA_DIR)
	@echo "Removing temporary system schemas..."
	@cd $(COMPILED_SCHEMA_DIR) && rm -f org.gtk.Settings.FileChooser.gschema.xml org.gtk.Settings.ColorChooser.gschema.xml

# Clean
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/obj
	rm -rf $(BUILD_DIR)/bin

# Release packaging
ZOND_INIT_H     := src/zond/zond_init.h
ZOND_VER_MAJOR  := $(shell grep -oP '(?<=define ZOND_VERSION_MAJOR )[0-9]+' $(ZOND_INIT_H))
ZOND_VER_MINOR  := $(shell grep -oP '(?<=define ZOND_VERSION_MINOR )[0-9]+' $(ZOND_INIT_H))
ZOND_VER_PATCH  := $(shell grep -oP '(?<=define ZOND_VERSION_PATCH )[0-9]+' $(ZOND_INIT_H))
RELEASE_VERSION ?= $(ZOND_VER_MAJOR).$(ZOND_VER_MINOR).$(ZOND_VER_PATCH)
RELEASE_EXE     := Release/bin/zond.exe
VIEWER_EXE      := Release/bin/viewer.exe
INSTALLER_EXE   := Release/bin/zond_installer.exe
RELEASE_DIR      = zond-$(RELEASE_VERSION)-win64
RELEASE_ZIP      = $(RELEASE_DIR).zip
GITHUB_TAG       = v$(RELEASE_VERSION)
GITHUB_ASSET_ZIP = zond-x86_64-$(GITHUB_TAG).zip

.PHONY: release
release: release-dir

.PHONY: release-dir
release-dir:
	$(MAKE) CONFIG=Release CFLAGS_CONFIG=-O3 LDFLAGS_CONFIG=-mwindows zond
	$(MAKE) CONFIG=Release CFLAGS_CONFIG=-O3 LDFLAGS_CONFIG=-mwindows viewer
	$(MAKE) CONFIG=Release CFLAGS_CONFIG=-O3 LDFLAGS_CONFIG=-mwindows zond_installer
	bash create-gtk-release.sh $(RELEASE_EXE) $(RELEASE_VERSION)
	cp $(VIEWER_EXE) $(RELEASE_DIR)/bin/
	@echo ""
	@echo "=== Release-Verzeichnis fertig ==="
	@echo "Verzeichnis: $(RELEASE_DIR)"
	@echo ""
	@echo "Zum Veroeffentlichen: make publish MSG=\"Beschreibung\""

.PHONY: release-zip
release-zip: release-dir
	zip -q -r $(RELEASE_ZIP) $(RELEASE_DIR)
	@echo "ZIP: $(RELEASE_ZIP)"

.PHONY: bump-patch
NEW_PATCH := $(shell expr $(ZOND_VER_PATCH) + 1)
bump-patch:
	sed -i "s/define ZOND_VERSION_PATCH [0-9]\+/define ZOND_VERSION_PATCH $(NEW_PATCH)/" $(ZOND_INIT_H)
	@echo "Patch-Version erhoeht: $(ZOND_VER_MAJOR).$(ZOND_VER_MINOR).$(NEW_PATCH)"

.PHONY: bump-minor
NEW_MINOR := $(shell expr $(ZOND_VER_MINOR) + 1)
bump-minor:
	sed -i "s/define ZOND_VERSION_MINOR [0-9]\+/define ZOND_VERSION_MINOR $(NEW_MINOR)/" $(ZOND_INIT_H)
	sed -i "s/define ZOND_VERSION_PATCH [0-9]\+/define ZOND_VERSION_PATCH 0/" $(ZOND_INIT_H)
	@echo "Minor-Version erhoeht: $(ZOND_VER_MAJOR).$(NEW_MINOR).0"

.PHONY: bump-major
NEW_MAJOR := $(shell expr $(ZOND_VER_MAJOR) + 1)
bump-major:
	sed -i "s/define ZOND_VERSION_MAJOR [0-9]\+/define ZOND_VERSION_MAJOR $(NEW_MAJOR)/" $(ZOND_INIT_H)
	sed -i "s/define ZOND_VERSION_MINOR [0-9]\+/define ZOND_VERSION_MINOR 0/" $(ZOND_INIT_H)
	sed -i "s/define ZOND_VERSION_PATCH [0-9]\+/define ZOND_VERSION_PATCH 0/" $(ZOND_INIT_H)
	@echo "Major-Version erhoeht: $(NEW_MAJOR).0.0"

.PHONY: tag
tag:
	@if [ -z "$(MSG)" ]; then \
		echo "Fehler: Bitte Beschreibung angeben, z.B. make tag MSG=\"GtkTextView-Rendering\""; \
		exit 1; \
	fi
	@if ! git diff --quiet || ! git diff --cached --quiet; then \
		echo "Fehler: Working tree ist nicht clean. Erst committen, dann taggen."; \
		git status --short; \
		exit 1; \
	fi
	@if git rev-parse "$(GITHUB_TAG)" >/dev/null 2>&1; then \
		echo "Fehler: Tag $(GITHUB_TAG) existiert bereits."; \
		exit 1; \
	fi
	git tag -a $(GITHUB_TAG) -m "zond: $(MSG)"
	@echo "Tag $(GITHUB_TAG) erstellt. Push mit:"
	@echo "  git push origin $(GITHUB_TAG)"

# Oeffentliches Release: Version erhoehen (Default: patch), committen,
# bauen, zippen, taggen, pushen, GitHub-Release anlegen
BUMP ?= patch
.PHONY: publish
publish:
	@if ! git diff --quiet || ! git diff --cached --quiet; then \
		echo "Fehler: Working tree ist nicht clean. Erst committen."; \
		git status --short; \
		exit 1; \
	fi
	$(MAKE) bump-$(BUMP)
	$(MAKE) do-publish-commit

.PHONY: do-publish-commit
do-publish-commit:
	git commit -am "Bump to $(RELEASE_VERSION)"
	$(MAKE) do-publish MSG="$(if $(MSG),$(MSG),Bump to $(RELEASE_VERSION))"

.PHONY: do-publish
do-publish:
	$(MAKE) release
	$(MAKE) zip-only
	cp $(RELEASE_ZIP) $(GITHUB_ASSET_ZIP)
	$(MAKE) tag MSG="$(MSG)"
	git push origin $(GITHUB_TAG)
	gh release create $(GITHUB_TAG) $(GITHUB_ASSET_ZIP) \
		--title "zond $(RELEASE_VERSION)" \
		--notes "$(MSG)"
	rm -f $(RELEASE_ZIP) $(GITHUB_ASSET_ZIP)
	@echo ""
	@echo "=== Veroeffentlicht: $(GITHUB_TAG) ==="
	@echo "Lokales Test-Verzeichnis (fuer Seafile-Sync): $(RELEASE_DIR)"

.PHONY: zip-only
zip-only:
	zip -q -r $(RELEASE_ZIP) $(RELEASE_DIR)
	@echo "ZIP: $(RELEASE_ZIP)"