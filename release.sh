zip zond-x86_64-$1.zip bin bin/zond.exe bin/viewer.exe bin/zond_installer.exe

zip zond-x86_64-$1.zip logs

zip zond-x86_64-$1.zip share/
zip zond-x86_64-$1.zip share/glib-2.0/
zip zond-x86_64-$1.zip share/glib-2.0/schemas/

#gschema frisch kompilieren...
mkdir -p tmp/share/glib-2.0/schemas/
cp share/glib-2.0/schemas/*.xml tmp/share/glib-2.0/schemas/
glib-compile-schemas tmp/share/glib-2.0/schemas
cd tmp
zip ~/Projekte/sond/zond-x86_64-$1.zip share/glib-2.0/schemas/gschemas.compiled
cd ~/Projekte/sond/
rm -r tmp

cd /ucrt64
#zip ~/Projekte/sond/zond-x86_64-$1.zip bin/gdbus.exe
#ldd ~/Projekte/sond/bin/zond.exe | grep '\/ucrt64.*\.dll' -o | xargs -I{} basename {} | xargs -I{} echo bin/{} | xargs zip ~/Projekte/sond/zond-x86_64-$1.zip {}
#ldd bin/gdbus.exe | grep '\/ucrt64.*\.dll' -o | xargs -I{} basename {} | xargs -I{} echo bin/{} | xargs zip ~/Projekte/sond/zond-x86_64-$1.zip {}

#zip ~/Projekte/sond/zond-x86_64-$1.zip bin/*.dll

zip -r ~/Projekte/sond/zond-x86_64-$1.zip bin/

zip -r ~/Projekte/sond/zond-x86_64-$1.zip share/tessdata

zip ~/Projekte/sond/zond-x86_64-$1.zip share/icons/
zip ~/Projekte/sond/zond-x86_64-$1.zip share/icons/Adwaita/
zip ~/Projekte/sond/zond-x86_64-$1.zip share/icons/Adwaita/icon-theme.cache
zip ~/Projekte/sond/zond-x86_64-$1.zip share/icons/Adwaita/index.theme
zip -r ~/Projekte/sond/zond-x86_64-$1.zip share/icons/Adwaita/scalable
zip -r ~/Projekte/sond/zond-x86_64-$1.zip share/icons/Adwaita/symbolic
zip -r ~/Projekte/sond/zond-x86_64-$1.zip share/icons/hicolor/

zip ~/Projekte/sond/zond-x86_64-$1.zip lib/
zip ~/Projekte/sond/zond-x86_64-$1.zip lib/gdk-pixbuf-2.0/
zip ~/Projekte/sond/zond-x86_64-$1.zip lib/gdk-pixbuf-2.0/2.10.0/
zip ~/Projekte/sond/zond-x86_64-$1.zip lib/gdk-pixbuf-2.0/2.10.0/loaders.cache
zip ~/Projekte/sond/zond-x86_64-$1.zip lib/gdk-pixbuf-2.0/2.10.0/loaders/
zip ~/Projekte/sond/zond-x86_64-$1.zip lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.dll lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-png.dll 



