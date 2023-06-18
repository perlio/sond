/*
Bugs:
    - Kopieren aus suchen-Fenster klappt nicht richtig

ToDo:
- Abschnitte neu organisieren

- BAUM_INHALT
    - Anbindungen in PDF einfügen
    - Anbindungen in PDF löschen

- Viewer
    - pdf_document_page und viewer_page erst bei Anforderung erzeugen
        (kompliziertes y_pos-Management erforderlich)
    - Farben für Markieren
    - Rummalen
    - angezeigte Seiten als Datei speichern

- Kontextmenu Trees
    - öffnen
        - öffnen mit
    - PDF: OCR/Reparieren/Textsuche

- PDF
    - Fundstellen auf Seitenzahlen umstellen

- datei_oeffnen:
    - nicht-Win32 (niedrig)

- Textsuche PDF:
    - silbengetrennte Wörte als ein Treffer erfassen und anzeigen


Build mupdf:

- clonen von github
- git submodule update --init

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
