zip zond-x86_64-$1.zip bin bin/zond.exe bin/viewer.exe

zip zond-x86_64-$1.zip logs

zip zond-x86_64-$1.zip share/glib-2.0/schemas/gschemas.compiled

cd /ucrt64 

zip -r ~/Projekte/sond/zond-x86_64-$1.zip bin

zip -r ~/Projekte/sond/zond-x86_64-$1.zip share/tessdata

zip -r ~/Projekte/sond/zond-x86_64-$1.zip share/icons/Adwaita

zip -r ~/Projekte/sond/zond-x86_64-$1.zip lib/gdk-pixbuf-2.0

#ldd release/bin/zond.exe | grep '\/ucrt64.*\.dll' -o | xargs -I{} cp "{}" release/bin
#cp /ucrt64/bin/gspawn-win64-helper.exe release/bin
#cp /ucrt64/bin/gspawn-win64-helper-console.exe release/bin
