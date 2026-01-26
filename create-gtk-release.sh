#!/bin/bash
# GTK3 Windows Release Packager - Einfache Version
# Direktes Kopieren ohne komplexe Logik

set -e

if [ $# -lt 1 ]; then
    echo "Verwendung: $0 <exe-datei> [version]"
    exit 1
fi

EXE_FILE="$1"
VERSION="${2:-1.0}"
APP_NAME=$(basename "$EXE_FILE" .exe)
RELEASE_DIR="${APP_NAME}-${VERSION}-win64"

if [ ! -f "$EXE_FILE" ]; then
    echo "Fehler: $EXE_FILE nicht gefunden!"
    exit 1
fi

echo "=== GTK3 Release Packager (Simple) ==="
echo "Erstelle: $RELEASE_DIR"
echo ""

rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR/bin"

# 1. EXE
echo "[1/6] Kopiere Executable..."
cp "$EXE_FILE" "$RELEASE_DIR/bin/"

# 2. DLLs - einfach und direkt
echo "[2/6] Kopiere DLLs..."
echo "  Sammle Liste der benötigten DLLs..."

# Hole alle DLL-Pfade
ldd "$EXE_FILE" | grep ucrt64 | awk '{print $3}' > /tmp/dll_list.txt

# Zähle sie
DLL_COUNT=$(wc -l < /tmp/dll_list.txt)
echo "  Gefunden: $DLL_COUNT DLLs"
echo "  Kopiere:"

# Kopiere alle und zeige Namen
while read dll_path; do
    if [ -f "$dll_path" ]; then
        dll_name=$(basename "$dll_path")
        echo "    + $dll_name"
        cp "$dll_path" "$RELEASE_DIR/bin/"
    fi
done < /tmp/dll_list.txt

echo "  → Fertig"

# 3. gdk-pixbuf
echo "[3/6] Kopiere gdk-pixbuf Loader..."
if [ -d "/ucrt64/lib/gdk-pixbuf-2.0" ]; then
    mkdir -p "$RELEASE_DIR/lib/gdk-pixbuf-2.0"
    cp -r /ucrt64/lib/gdk-pixbuf-2.0/2.10.0 "$RELEASE_DIR/lib/gdk-pixbuf-2.0/"
    
    # Sammle Loader-DLLs
    echo "  Sammle Loader-Abhängigkeiten..."
    for loader in /ucrt64/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll; do
        if [ -f "$loader" ]; then
            ldd "$loader" | grep ucrt64 | awk '{print $3}' >> /tmp/dll_list.txt
        fi
    done
    
    # Kopiere neue DLLs
    echo "  Zusätzliche Loader-DLLs:"
    while read dll_path; do
        if [ -f "$dll_path" ]; then
            dll_name=$(basename "$dll_path")
            if [ ! -f "$RELEASE_DIR/bin/$dll_name" ]; then
                echo "    + $dll_name"
                cp "$dll_path" "$RELEASE_DIR/bin/"
            fi
        fi
    done < /tmp/dll_list.txt
    
    # Pfade anpassen
    sed -i 's|/ucrt64/lib|lib|g' "$RELEASE_DIR/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache"
    echo "  → Fertig"
fi

# 4. Icons
echo "[4/6] Kopiere Icons..."
if [ -d "/ucrt64/share/icons/Adwaita" ]; then
    mkdir -p "$RELEASE_DIR/share/icons"
    cp -r /ucrt64/share/icons/Adwaita "$RELEASE_DIR/share/icons/"
    find "$RELEASE_DIR/share/icons/Adwaita" -type d \( -name "512x512" -o -name "256x256" \) -exec rm -rf {} + 2>/dev/null || true
    
    if [ -f "/ucrt64/bin/gtk-update-icon-cache.exe" ]; then
        /ucrt64/bin/gtk-update-icon-cache.exe "$RELEASE_DIR/share/icons/Adwaita" 2>/dev/null || true
    fi
    echo "  → Fertig"
fi

# 5. Schemas (minimal - nur gschemas.compiled)
echo "[5/7] Kopiere GSettings Schemas..."
if [ -d "/ucrt64/share/glib-2.0/schemas" ]; then
    mkdir -p /tmp/gtk_schemas_temp
    
    # Kopiere GTK-Standard-Schemas temporär
    cp /ucrt64/share/glib-2.0/schemas/org.gtk.Settings.FileChooser.gschema.xml /tmp/gtk_schemas_temp/ 2>/dev/null || true
    cp /ucrt64/share/glib-2.0/schemas/org.gtk.Settings.ColorChooser.gschema.xml /tmp/gtk_schemas_temp/ 2>/dev/null || true
    
    # Suche nach eigenen Schema-Dateien im Projekt
    PROJECT_SCHEMA_DIR="$(dirname "$EXE_FILE")/../../schemas"
    if [ -d "$PROJECT_SCHEMA_DIR" ]; then
        echo "  Gefundene Projekt-Schemas:"
        for schema_file in "$PROJECT_SCHEMA_DIR"/*.gschema.xml; do
            if [ -f "$schema_file" ]; then
                schema_name=$(basename "$schema_file")
                echo "    + $schema_name"
                cp "$schema_file" /tmp/gtk_schemas_temp/
            fi
        done
    else
        echo "  (keine Projekt-Schemas gefunden in $PROJECT_SCHEMA_DIR)"
    fi
    
    # Kompiliere alle Schemas zusammen
    if [ -f "/ucrt64/bin/glib-compile-schemas.exe" ]; then
        echo "  Kompiliere Schemas..."
        /ucrt64/bin/glib-compile-schemas.exe /tmp/gtk_schemas_temp/ 2>/dev/null || true
        
        # Kopiere NUR die kompilierte Datei (keine XML-Dateien!)
        mkdir -p "$RELEASE_DIR/share/glib-2.0/schemas"
        if [ -f /tmp/gtk_schemas_temp/gschemas.compiled ]; then
            cp /tmp/gtk_schemas_temp/gschemas.compiled "$RELEASE_DIR/share/glib-2.0/schemas/"
            echo "  → gschemas.compiled erstellt (ohne XML-Quellen)"
        fi
    fi
    
    # Cleanup
    rm -rf /tmp/gtk_schemas_temp
else
    echo "  → Übersprungen"
fi

# 6. Tesseract OCR Daten
echo "[6/7] Kopiere Tesseract Daten..."
if [ -d "/ucrt64/share/tessdata" ]; then
    mkdir -p "$RELEASE_DIR/share/tessdata"
    
    # Kopiere nur deutsche und OSD Trainingsdaten
    if [ -f "/ucrt64/share/tessdata/deu.traineddata" ]; then
        cp /ucrt64/share/tessdata/deu.traineddata "$RELEASE_DIR/share/tessdata/"
        echo "  + deu.traineddata"
    else
        echo "  ! deu.traineddata nicht gefunden"
    fi
    
    if [ -f "/ucrt64/share/tessdata/osd.traineddata" ]; then
        cp /ucrt64/share/tessdata/osd.traineddata "$RELEASE_DIR/share/tessdata/"
        echo "  + osd.traineddata"
    else
        echo "  ! osd.traineddata nicht gefunden"
    fi
    
    echo "  → Fertig"
else
    echo "  ! Tesseract nicht installiert (/ucrt64/share/tessdata nicht gefunden)"
fi

# 7. Config & Launcher
echo "[7/7] Erstelle Konfiguration..."

mkdir -p "$RELEASE_DIR/etc/gtk-3.0"
cat > "$RELEASE_DIR/etc/gtk-3.0/settings.ini" << 'EOF'
[Settings]
gtk-theme-name=Adwaita
gtk-icon-theme-name=Adwaita
gtk-font-name=Segoe UI 9
EOF

echo "@echo off" > "$RELEASE_DIR/${APP_NAME}.bat"
echo "setlocal" >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "APP_DIR=%~dp0"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "APP_DIR=%APP_DIR:~0,-1%"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "GTK_DATA_PREFIX=%APP_DIR%"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "GTK_EXE_PREFIX=%APP_DIR%"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "GDK_PIXBUF_MODULEDIR=%APP_DIR%\lib\gdk-pixbuf-2.0\2.10.0\loaders"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "GDK_PIXBUF_MODULE_FILE=%APP_DIR%\lib\gdk-pixbuf-2.0\2.10.0\loaders.cache"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "GSETTINGS_SCHEMA_DIR=%APP_DIR%\share\glib-2.0\schemas"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "XDG_DATA_DIRS=%APP_DIR%\share"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "PATH=%APP_DIR%\bin;%PATH%"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo '"%APP_DIR%\bin\'"${APP_NAME}"'.exe" %*' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo "endlocal" >> "$RELEASE_DIR/${APP_NAME}.bat"

cat > "$RELEASE_DIR/README.txt" << EOF
$APP_NAME - Version $VERSION

INSTALLATION:
1. Alle Dateien entpacken
2. ${APP_NAME}.bat starten (oder direkt bin/${APP_NAME}.exe)

VERZEICHNISSTRUKTUR:
bin/                - Anwendung und DLLs
  ${APP_NAME}.exe
  *.dll
lib/                - GTK-Module
share/              - Ressourcen (Icons, Schemas, Tesseract)

SYSTEMANFORDERUNGEN:
- Windows 10+ (64-bit)

Build: $(date '+%Y-%m-%d')
EOF

echo "  → Fertig"

# ZIP
echo ""
echo "Erstelle ZIP..."
zip -q -r "${RELEASE_DIR}.zip" "$RELEASE_DIR" 2>/dev/null || echo "  (zip nicht installiert)"

# Cleanup
rm -f /tmp/dll_list.txt

# Stats
TOTAL_SIZE=$(du -sh "$RELEASE_DIR" 2>/dev/null | awk '{print $1}')
FINAL_DLL_COUNT=$(ls "$RELEASE_DIR/bin"/*.dll 2>/dev/null | wc -l)

echo ""
echo "=== FERTIG ==="
echo "Verzeichnis: $RELEASE_DIR"
echo "Größe: $TOTAL_SIZE"
echo "DLLs: $FINAL_DLL_COUNT"
echo ""
echo "Testen: cd $RELEASE_DIR && ./bin/${APP_NAME}.exe"
echo "    oder: cd $RELEASE_DIR && ./${APP_NAME}.bat"
echo ""