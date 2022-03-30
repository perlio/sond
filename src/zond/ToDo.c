/*
Bugs:

ToDo:
- Viewer
    - Riesen-PDFs handhabbar machen
    - Markieren über mehrere Seiten
    - Kommentare

- Synchron-Punkte

- Abschnitte neu organisieren

- suchen
    - Menu Struktur verschieben
    - ggf. Beschränkbarkeit auf Bäume/Zweige/Punkte

- BAUM_INHALT
    - Anbindungen in PDF einfügen
    - Anbindungen in PDF löschen

- Viewer
    - Farben für Markieren
    - Rummalen
    - TextTreffer in Fenster anzeigen
    - angezeigte Seiten als Datei speichern

- Kontextmenu Trees
    - icons
    - öffnen
        - öffnen
        - öffnen mit
    - Copy/Paste
    - PDF: OCR/Reparieren/Textsuche

- PDF
    - mupdf/tesseract
    - wenn ich namestrees rebalancieren könnte: auch podofo ersetzen (niedrig)

- datei_oeffnen:
    - nicht-Win32 (niedrig)

- Textsuche PDF:
    - silbengetrennte Wörte als ein Treffer erfassen und anzeigen


pdf_clean_file.c:

    retainfiles extern machen (.h)
    globals überflüssig


Makefile für  mupdf modifizieren:

# --- Rules ---

$(OUT)/%.a :
	$(file >arscript.sh,@$(AR_CMD))
	bash -x arscript.sh
	$(RANLIB_CMD)

*/
