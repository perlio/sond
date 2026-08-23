/*
 ToDo:

- Rows mit Text Farbe
- Copy_Auswertung wenn root dann Verweis auf root?
- Wenn in BAUM_INHALT Section angebunden, copy_auswertung öffnet ganze Datei

 - Abschnitte neu organisieren

 - BAUM_INHALT
 - Anbindungen in PDF einfügen
 - Anbindungen in PDF löschen

 - Viewer
 - Farben für Markieren
 - Rummalen
 - angezeigte Seiten als Datei speichern


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

Änderung Rules war beim letzten Kompilieren (16.06.25) nicht erforderlich
 # --- Rules ---

 $(OUT)/%.a :
 $(file >arscript.sh,@$(AR_CMD))
 bash -x arscript.sh
 $(RANLIB_CMD)


 - mingw32-make libs


 Bugs (Review 23.08.2026, Viewer/OCR/Save - mit Claude durchgegangen):

 - sond_ocr.c, sond_ocr_do_tasks(): fast alle Log-Meldungen (Seite
   übersprungen/Fehler bei versteckt-Text-Prüfung/Rendern/OSD/Transform/
   Thread-Push/Einfügen) benutzten "i" (Index in arr_tasks) statt der
   echten Seitenzahl. Da seiten.c (cb_pv_seiten_ocr) sond_ocr_do_tasks()
   pro Seite einzeln mit genau einem Task aufruft, war "i" dort immer 0 -
   im Info-Fenster stand deshalb immer "Seite 0: ...".
   BEHOBEN (23.08.2026): überall auf task->page->super.number umgestellt
   (wie schon bei der Erfolgsmeldung "OCR abgeschlossen" gemacht).

 - sond_ocr.c, sond_ocr_osd(): OSD-Fehlschlag führte zu UAF/NULL-Deref in
   calculate_ocr_transform() (task->pixmap freigegeben aber nicht NULL,
   oder pdf_render_pixmap() fehlgeschlagen).
   BEHOBEN (23.08.2026): Seite wird bei OSD-Fehler jetzt sofort aufgegeben
   (status=4), statt mit ungültigem Pixmap weiterzumachen.

 - sond_ocr.c: task->pixmap wird nie fz_drop_pixmap()t - weder in
   sond_ocr_task_free() noch nach erfolgreichem OCR. Leck pro Task,
   verstärkt bei Multi-Resolution-Retry (ocr_scales[]).
   BEHOBEN (23.08.2026): Drop vor Retry-Rerender (Zeile ~302), Drop+NULL
   nach OSD-Rotation in sond_ocr_osd(), korrigierter (nicht pauschaler)
   Drop im OSD-Fehlerpfad (3 mögliche Fehlerursachen, nicht 2 - eine davon
   läßt den Pixmap unangetastet gültig), und Drop des letzten Pixmaps in
   sond_ocr_task_free().

 - sond_ocr.c, sond_ocr_task_new()/_free(): font_ref wird per pdf_keep_obj()
   zusätzlich referenziert (~Zeile 434), aber nie wieder pdf_drop_obj()t -
   Refcount-Leck pro Task.
   BEHOBEN (23.08.2026): pdf_drop_obj(task->ctx, task->font_ref) in
   sond_ocr_task_free() ergänzt (Gegenstück zum pdf_keep_obj() in
   sond_ocr_task_new()).

 - viewer_save.c, viewer_do_save_dd() (~Zeile 283-320): Seite, die in
   derselben Sitzung eingefügt UND wieder gelöscht wurde (pdfp->deleted &&
   pdfp->inserted gleichzeitig), wird von keinem der beiden Zweige erfaßt -
   Code lädt stattdessen eine falsche reale Seite (page_orig) und wendet
   Annot-Änderungen der Phantom-Seite darauf an; page_orig-- läuft trotzdem
   und verschiebt alle folgenden Seiten der Schleife um 1.
   BEHOBEN (23.08.2026): dritter Zweig ergänzt (pdfp->deleted &&
   pdfp->inserted -> continue, ohne pdf_delete_page() und ohne
   page_orig--), da anbindung_get_orig() diese Phantom-Seite schon von
   Anfang an nicht in page_orig mitzählt (dort wird jede Seite mit
   ->inserted abgezogen, unabhängig von ->deleted).

 - seiten.c, cb_pv_seiten_ocr() (~Zeile 464-472): Wenn OCR erfolgreich war
   (content_changed==TRUE), aber das erneute Lesen des Content-Streams für
   den Journal-Eintrag fehlschlägt, wird kein Journal-Eintrag angelegt -
   Seite bleibt im Speicher verändert, aber unsaveable/unsichtbar dirty.
   BEHOBEN (23.08.2026): erneutes Lesen komplett entfernt statt abgesichert
   (ein Rollback über buf_old wäre selbst fehleranfällig gewesen, da
   pdf_set_content_stream() nicht atomar ist - Contents wird erst auf ein
   neues leeres Objekt umgehängt, dann erst befüllt). Stattdessen liefert
   add_ocr_layer_to_page() (sond_ocr.c) den bereits fertig zusammengesetzten
   neuen Content-Stream jetzt über einen Out-Parameter zurück
   (task->buf_content_new), den seiten.c direkt für entry.ocr.buf_new
   übernimmt - kein zweiter Lesevorgang, keine Fehlerquelle mehr. Dabei
   nebenbei entdecktes Leck mitbehoben: add_ocr_layer_to_page() hat diesen
   Buffer bisher nach pdf_set_content_stream() weder zurückgegeben noch
   gedroppt (weder im Erfolgs- noch im Fehlerfall).

 - viewer_search.c, viewer_highlight_at_char_pos() (~Zeile 174-176):
   strlen() statt Rückgabewert von fz_buffer_storage() für flat_len - bei
   eingebetteten NUL-Bytes im geflatteten Text (Glyphen ohne
   Unicode-Mapping) falsche Länge, im schlimmsten Fall Read-over-bounds.

 - sond_fileparts.c, sond_file_part_pdf_save_and_close() (~Zeile 1551-1553):
   pdf_doc wird bei fehlschlagendem pdf_doc_to_buf() nicht gedroppt - Leck.
   BEHOBEN (23.08.2026): pdf_drop_document() direkt nach pdf_doc_to_buf()
   vorgezogen, vor die Erfolgsprüfung - pdf_doc wird von pdf_doc_to_buf()
   nur gelesen, nie übernommen, Drop ist also unabhängig vom Ergebnis
   korrekt.

 - viewer_save.c, viewer_save_dirty_dds() (~Zeile 611-616): g_prefix_error()
   auf error, danach derselbe error-Pointer an dbase_zond_rollback() - falls
   die Rollback-Funktion selbst einen Fehler setzt, GError-Vertrag verletzt.
   Nicht in zond_dbase.c nachgeprüft.

 - viewer_save.c (~Zeile 505-512): first_page/last_page-Reassignment beim
   Löschen der jeweils anderen dd-Grenzseite - Indexarithmetik nicht
   durchgerechnet, evtl. harmlos.

 - sond_fileparts.c: kein Locking beim Speichern zweier embedded files
   desselben Parents - potentielles Lost-Update, hängt vom Threading-Modell
   der Aufrufer ab (nicht geprüft).

 - sond_fileparts.c, sond_file_part_delete(): gibt nach erfolgreichem
   Schreiben noch -1 zurück, wenn nachträgliches
   sond_file_part_test_for_children() fehlschlägt - irreführender
   Fehlerstatus trotz erfolgreicher Aktion.

 */

