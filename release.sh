zip zond-x86_64-$1.zip bin bin/zond.exe bin/viewer.exe bin/zond_installer.exe

zip zond-x86_64-$1.zip logs

zip zond-x86_64-$1.zip share/
zip zond-x86_64-$1.zip share/glib-2.0/
zip zond-x86_64-$1.zip share/glib-2.0/schemas/
zip zond-x86_64-$1.zip share/glib-2.0/schemas/gschemas.compiled

cd /ucrt64
ldd ~/Projekte/sond/bin/zond.exe | grep '\/ucrt64.*\.dll' -o | xargs -I{} basename {} | xargs -I{} echo bin/{} | xargs zip ~/Projekte/sond/zond-x86_64-$1.zip {}
ldd bin/gdbus.exe | grep '\/ucrt64.*\.dll' -o | xargs -I{} basename {} | xargs -I{} echo bin/{} | xargs zip ~/Projekte/sond/zond-x86_64-$1.zip {}

zip -r ~/Projekte/sond/zond-x86_64-$1.zip share/tessdata

zip -r ~/Projekte/sond/zond-x86_64-$1.zip share/icons/Adwaita

zip -r ~/Projekte/sond/zond-x86_64-$1.zip lib/gdk-pixbuf-2.0
