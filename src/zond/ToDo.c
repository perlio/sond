/*
Bugs:
    - Kopieren aus suchen-Fenster klappt nicht richtig

ToDo:
- Synchron-Punkte

- Abschnitte neu organisieren

- suchen
    - Menu Struktur verschieben
    - ggf. Beschränkbarkeit auf Bäume/Zweige/Punkte

- BAUM_INHALT
    - Anbindungen in PDF einfügen
    - Anbindungen in PDF löschen

- Viewer
    - pdf_document_page und viewer_page erst bei Anforderung erzeugen
        (kompliziertes y_pos-Management erforderlich)
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
