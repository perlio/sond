/*
 Bugs:

 - OCR bei leerer Pixmap: TessBaseApiRecognize geht in Endlosschleife


 ToDo:

 - Abschnitte neu organisieren

 - BAUM_INHALT
 - Anbindungen in PDF einfügen
 - Anbindungen in PDF löschen

 - Viewer
 - Farben für Markieren
 - Rummalen
 - angezeigte Seiten als Datei speichern

 - Kontextmenu Trees
 - PDF: OCR/Reparieren/Textsuche

 - datei_oeffnen:
 - nicht-Win32 (niedrig)

 - Textsuche PDF:
 - silbengetrennte Wörte als ein Treffer erfassen und anzeigen


 Build mupdf:

 - git clone --recursive git://git.ghostscript.com/mupdf.git
 - cd mupdf
 - git submodule update --init
 - Makefile für  mupdf modifizieren:

 Zeile 75: Compiler-Optionen -mavx -Wno-incompatible-pointer-types ergänzen
 "CC_CMD = $(QUIET_CC) $(MKTGTDIR) ; $(CC) $(CFLAGS) -mavx -Wno-incompatible-pointer-types -MMD -MP -o $@ -c $<"

 # --- Rules ---

 $(OUT)/%.a :
 $(file >arscript.sh,@$(AR_CMD))
 bash -x arscript.sh
 $(RANLIB_CMD)


 - mingw32-make libs


 */
