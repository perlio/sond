#!/bin/bash
# GTK3 Windows Release Packager - Vollständige Version
# Mit DBus, GIO-Module, hicolor Theme

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

echo "=== GTK3 Release Packager (Vollständig) ==="
echo "Erstelle: $RELEASE_DIR"
echo ""

rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR/bin"

# 1. EXE
echo "[1/9] Kopiere Executable..."
cp "$EXE_FILE" "$RELEASE_DIR/bin/"

# 2. DLLs - einfach und direkt
echo "[2/9] Kopiere DLLs..."
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

# 3. DBus (behebt GLib-GIO-WARNING)
echo "[3/9] Kopiere DBus..."
if [ -f "/ucrt64/bin/dbus-daemon.exe" ]; then
    cp /ucrt64/bin/dbus-daemon.exe "$RELEASE_DIR/bin/"
    echo "  + dbus-daemon.exe"
    
    # DBus Konfiguration
    mkdir -p "$RELEASE_DIR/etc/dbus-1/session.d"
    if [ -d "/ucrt64/etc/dbus-1" ]; then
        cp -r /ucrt64/etc/dbus-1/* "$RELEASE_DIR/etc/dbus-1/" 2>/dev/null || true
    fi
    
    # DBus zusätzliche DLLs
    for dbus_dll in /ucrt64/bin/libdbus-*.dll; do
        if [ -f "$dbus_dll" ]; then
            cp "$dbus_dll" "$RELEASE_DIR/bin/"
            echo "  + $(basename "$dbus_dll")"
        fi
    done
    echo "  → Fertig"
else
    echo "  ! DBus nicht gefunden (optional)"
fi

# 4. GIO Module (behebt GLib-GIO-CRITICAL Fehler)
echo "[4/9] Kopiere GIO Module..."
if [ -d "/ucrt64/lib/gio/modules" ]; then
    mkdir -p "$RELEASE_DIR/lib/gio/modules"
    cp /ucrt64/lib/gio/modules/*.dll "$RELEASE_DIR/lib/gio/modules/" 2>/dev/null || true
    
    # GIO Module DLL-Abhängigkeiten
    for gio_module in /ucrt64/lib/gio/modules/*.dll; do
        if [ -f "$gio_module" ]; then
            echo "  + $(basename "$gio_module")"
            ldd "$gio_module" | grep ucrt64 | awk '{print $3}' >> /tmp/dll_list.txt
        fi
    done
    
    # Kopiere zusätzliche DLLs
    while read dll_path; do
        if [ -f "$dll_path" ]; then
            dll_name=$(basename "$dll_path")
            if [ ! -f "$RELEASE_DIR/bin/$dll_name" ]; then
                cp "$dll_path" "$RELEASE_DIR/bin/"
            fi
        fi
    done < /tmp/dll_list.txt
    
    echo "  → Fertig"
else
    echo "  ! GIO Module nicht gefunden"
fi

# 5. gdk-pixbuf
echo "[5/9] Kopiere gdk-pixbuf Loader..."
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

# 6. Icons - Adwaita UND hicolor (behebt missing-image Fehler)
echo "[6/9] Kopiere Icon Themes..."
mkdir -p "$RELEASE_DIR/share/icons"

# Adwaita Theme
if [ -d "/ucrt64/share/icons/Adwaita" ]; then
    cp -r /ucrt64/share/icons/Adwaita "$RELEASE_DIR/share/icons/"
    find "$RELEASE_DIR/share/icons/Adwaita" -type d \( -name "512x512" -o -name "256x256" \) -exec rm -rf {} + 2>/dev/null || true
    echo "  + Adwaita"
fi

# hicolor Theme (WICHTIG - wird von GTK benötigt!)
if [ -d "/ucrt64/share/icons/hicolor" ]; then
    cp -r /ucrt64/share/icons/hicolor "$RELEASE_DIR/share/icons/"
    # Reduziere Größe - behalte nur wichtige Größen
    find "$RELEASE_DIR/share/icons/hicolor" -type d \( -name "512x512" -o -name "256x256" -o -name "192x192" -o -name "128x128" -o -name "96x96" -o -name "72x72" \) -exec rm -rf {} + 2>/dev/null || true
    echo "  + hicolor"
fi

# Icon Cache aktualisieren
if [ -f "/ucrt64/bin/gtk-update-icon-cache.exe" ]; then
    /ucrt64/bin/gtk-update-icon-cache.exe "$RELEASE_DIR/share/icons/Adwaita" 2>/dev/null || true
    /ucrt64/bin/gtk-update-icon-cache.exe "$RELEASE_DIR/share/icons/hicolor" 2>/dev/null || true
fi
echo "  → Fertig"

# 7. Schemas (nur gschemas.compiled kopieren)
echo "[7/9] Kopiere GSettings Schemas..."
PROJECT_SCHEMA_COMPILED="$(dirname "$EXE_FILE")/../share/glib-2.0/schemas/gschemas.compiled"
if [ -f "$PROJECT_SCHEMA_COMPILED" ]; then
    mkdir -p "$RELEASE_DIR/share/glib-2.0/schemas"
    cp "$PROJECT_SCHEMA_COMPILED" "$RELEASE_DIR/share/glib-2.0/schemas/"
    echo "  + gschemas.compiled (vom Makefile erstellt)"
    echo "  → Fertig"
else
    echo "  ! gschemas.compiled nicht gefunden in $PROJECT_SCHEMA_COMPILED"
    echo "  ! Bitte zuerst 'make zond' ausführen"
fi

# 8. Tesseract OCR Daten
echo "[8/9] Kopiere Tesseract Daten..."
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

# 9. libmagic Daten (magic.mgc)
echo "[9/10] Kopiere libmagic Daten..."
MAGIC_FOUND=false

# Suche nach magic.mgc in verschiedenen möglichen Pfaden
for magic_path in "/ucrt64/share/misc/magic.mgc" "/ucrt64/share/file/magic.mgc" "/ucrt64/share/magic.mgc"; do
    if [ -f "$magic_path" ]; then
        mkdir -p "$RELEASE_DIR/share/misc"
        cp "$magic_path" "$RELEASE_DIR/share/misc/magic.mgc"
        echo "  + magic.mgc (von $magic_path)"
        MAGIC_FOUND=true
        break
    fi
done

if [ "$MAGIC_FOUND" = false ]; then
    echo "  ! magic.mgc nicht gefunden"
    echo "  ! Falls libmagic verwendet wird: pacman -S mingw-w64-ucrt-x86_64-file"
fi
echo "  → Fertig"

# 10. Config & Launcher
echo "[10/10] Erstelle Konfiguration..."

mkdir -p "$RELEASE_DIR/etc/gtk-3.0"
cat > "$RELEASE_DIR/etc/gtk-3.0/settings.ini" << 'EOF'
[Settings]
gtk-theme-name=Adwaita
gtk-icon-theme-name=Adwaita
gtk-font-name=Segoe UI 9
gtk-fallback-icon-theme=hicolor
EOF

echo "@echo off" > "$RELEASE_DIR/${APP_NAME}.bat"
echo "setlocal" >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "APP_DIR=%~dp0"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "APP_DIR=%APP_DIR:~0,-1%"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "GTK_DATA_PREFIX=%APP_DIR%"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "GTK_EXE_PREFIX=%APP_DIR%"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "GDK_PIXBUF_MODULEDIR=%APP_DIR%\lib\gdk-pixbuf-2.0\2.10.0\loaders"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "GDK_PIXBUF_MODULE_FILE=%APP_DIR%\lib\gdk-pixbuf-2.0\2.10.0\loaders.cache"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "GIO_MODULE_DIR=%APP_DIR%\lib\gio\modules"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "GSETTINGS_SCHEMA_DIR=%APP_DIR%\share\glib-2.0\schemas"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "XDG_DATA_DIRS=%APP_DIR%\share"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "MAGIC=%APP_DIR%\share\misc\magic.mgc"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "PATH=%APP_DIR%\bin;%PATH%"' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'REM Unterdrücke DBus-Warnung wenn DBus nicht verwendet wird' >> "$RELEASE_DIR/${APP_NAME}.bat"
echo 'set "DBUS_SESSION_BUS_ADDRESS=disabled:"' >> "$RELEASE_DIR/${APP_NAME}.bat"
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
  dbus-daemon.exe   - DBus Support
lib/                - GTK-Module
  gdk-pixbuf-2.0/   - Bildformat-Loader
  gio/modules/      - GIO-Module
share/              - Ressourcen
  icons/Adwaita/    - Standard-Icons
  icons/hicolor/    - Fallback-Icons
  glib-2.0/schemas/ - GSettings
  tessdata/         - Tesseract OCR
etc/                - Konfiguration

SYSTEMANFORDERUNGEN:
- Windows 10+ (64-bit)

HINWEISE:
- GIO-Module und DBus sind für erweiterte Funktionalität enthalten
- hicolor Icon Theme verhindert "missing-image" Warnungen

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
echo "WICHTIG: Testen Sie mit:"
echo "    cd $RELEASE_DIR && ./${APP_NAME}.bat"
echo ""
echo "Die .bat-Datei setzt alle benötigten Umgebungsvariablen!"
echo ""