/* ToDo-Liste

- suchen
    - Menu Struktur vrschieben
    - Einheitliche Suche (Dateiname, node_text, text)
    - ggf. Beschränkbarkeit auf Bäume/Zweige/Punkte

- BAUM_INHALT
    - Anbindungen in PDF einfügen
    - Anbindungen in PDF löschen

- BAUM_FS
    - Umbennennen und verschieben nicht, wenn Pdf geöffnet ist

- Viewer
    - Farben für Markieren
    - Rummalen
    - Angezeigte Datei in Statusleiste, nicht in Headerbar
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

    void pdf_clean_document(fz_context *ctx, pdf_document *doc)
    {
        globals glo = { 0 };

        glo.ctx = ctx;
        glo.doc = doc;
        char *pages[1];

        pages[0] = "1-N";

        fz_try(ctx) retainpages(ctx, &glo, 1, pages);
        fz_catch(ctx)
        {
            fz_rethrow(ctx);
        }

        return;
    }

einfügen


pdf/clean.h:

    void pdf_clean_document(fz_context *ctx, pdf_document *doc);

einfügen


Makefile für  mupdf modifizieren:

# --- Rules ---

$(OUT)/%.a :
	$(file >arscript.sh,@$(AR_CMD))
	bash -x arscript.sh
	$(RANLIB_CMD)

Makethird modifizieren:
- in Abschnitt jbig2dec:
- DHAVE_ ??? memento entfernen
- alle includes mit stddef.h und stdlib.h aus allen files entfernen

*/
