rm -fr release
mkdir release

mkdir release/bin
cp bin/zond.exe release/bin/zond.exe
cp bin/viewer.exe release/bin/viewer.exe

mkdir release/logs

ldd release/bin/zond.exe | grep '\/mingw.*\.dll' -o | xargs -I{} cp "{}" release/bin
cp /mingw64/bin/gspawn-win64-helper.exe release/bin
cp /mingw64/bin/gspawn-win64-helper-console.exe release/bin

mkdir -p release/share/glib-2.0/schemas
cp /mingw64/share/glib-2.0/schemas/gschemas.compiled release/share/glib-2.0/schemas

mkdir -p release/share/icons/Adwaita
# cp /mingw64/share/icons/Adwaita/icon-theme.cache release/share/icons/Adwaita
# cp /mingw64/share/icons/Adwaita/index.theme release/share/icons/Adwaita
cp -r /mingw64/share/icons/Adwaita/* release/share/icons/Adwaita

mkdir -p release/lib/gdk-pixbuf-2.0/2.10.0/loaders
cp /mingw64/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll release/lib/gdk-pixbuf-2.0/2.10.0/loaders
cp /mingw64/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache release/lib/gdk-pixbuf-2.0/2.10.0

mkdir release/share/tessdata
cp -r /mingw64/share/tessdata/* release/share/tessdata