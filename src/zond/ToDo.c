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
   BEHOBEN (23.08.2026): flat_len jetzt aus dem Rückgabewert von
   fz_buffer_storage(). (char_pos_in_page selbst, aus sond_index.c/FTS5
   highlight(), landet zwar immer auf einer UTF-8-Zeichengrenze im damals
   indizierten Text - das war aber gar nicht das Problem, sondern der zur
   Klickzeit frisch erzeugte flat_data-Buffer selbst, der potenziell NULs
   enthält bzw. nicht terminiert ist.)

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
   BEHOBEN (23.08.2026): Ursache war project.c, dbase_zond_rollback() -
   reichte denselben (evtl. schon belegten) error an zwei
   zond_dbase_rollback()-Aufrufe (store/work) durch; schlug das
   ROLLBACK-Statement selbst fehl (zond_dbase.h, ERROR_Z_DBASE-Makro),
   wurde der ursprüngliche Fehler überschrieben statt kombiniert. Jetzt
   bekommt jeder der beiden Aufrufe einen eigenen error_int; ist der
   gesetzt, wird die Meldung an einen schon vorhandenen error angehängt
   (add_string(), wie im ROLLBACK_TO_STATEMENT-Makro) statt ihn zu
   überschreiben - beide Fehlerursachen erreichen den Anwender. Betrifft
   nur den seltenen Fall, daß das ROLLBACK-Statement selbst scheitert;
   ERROR_Z_DBASE und die übrigen Aufrufer bleiben unverändert.

 - viewer_save.c (~Zeile 505-512): first_page/last_page-Reassignment beim
   Löschen der jeweils anderen dd-Grenzseite - Indexarithmetik nicht
   durchgerechnet, evtl. harmlos.
   GEPRÜFT (24.08.2026), KEIN BUG: seiten_loeschen() (seiten.c, ~Zeile
   807-856) zählt vor jeder einzelnen zu löschenden Seite die noch nicht
   gelöschten Seiten des Dokuments und überspringt (continue) die
   Markierung als gelöscht, sobald nur noch eine übrig wäre (Zeile
   820-831) - läuft pro Seite, gilt also auch beim Löschen mehrerer Seiten
   auf einmal. Ein Dokument kann dadurch nie auf 0 Seiten leerlaufen, die
   in viewer_save.c befürchtete Situation (first_page/last_page-Array wird
   leer, first_page zeigt danach ins Leere) ist also nicht erreichbar.
   Einziger (kleinerer) Nebenpunkt: die übersprungene letzte Seite wird
   stillschweigend nicht gelöscht, ohne Meldung an den Anwender.

 - sond_fileparts.c: kein Locking beim Speichern zweier embedded files
   desselben Parents - potentielles Lost-Update, hängt vom Threading-Modell
   der Aufrufer ab (nicht geprüft).
   GEPRÜFT (24.08.2026), KEIN BUG: bei der aktuellen Architektur wird nur
   sequentiell in Dateien geschrieben; Threads existieren nur beim Rendern
   und OCRen, nicht beim Speichern.

 - sond_fileparts.c, sond_file_part_delete(): gibt nach erfolgreichem
   Schreiben noch -1 zurück, wenn nachträgliches
   sond_file_part_test_for_children() fehlschlägt - irreführender
   Fehlerstatus trotz erfolgreicher Aktion.
   BEHOBEN (24.08.2026): test_for_children()-Fehlschlag wird jetzt wie
   schon in sond_file_part_create_from_mime_type() (Vorbild, Zeile
   188-192) nur noch per LOG_WARN() protokolliert statt als -1 der
   eigentlich schon erfolgreichen Löschung durchgereicht zu werden.
   has_children des Parents bleibt in diesem seltenen Fehlerfall auf dem
   letzten bekannten Stand (reines Anzeige-Detail für die Baumansicht,
   keine Dateninkonsistenz).

 Bugs (Review 23.08.2026, Textsuche im Viewer - mit Claude durchgegangen):

 - viewer_ui.c, pv-Erzeugung (~Zeile 729): text_occ.index_act/page_act
   werden nie explizit initialisiert - pv kommt aus g_malloc0(), beide
   starten also bei 0 statt beim überall sonst verwendeten Sentinel -1.
   button_nachher/button_vorher sind von Anfang an klickbar. Klick auf
   "Weiter" als allererste Aktion (vor jeder Suche) führt dazu, daß
   index_act >= 0 fälschlich als "Treffer wird angezeigt" gilt,
   index_act auf 1 hochgezählt und viewer_anzeigen_text_occ() darauf
   g_array_index() auf einem LEEREN arr_quad ausführt - undefined
   behavior/Absturzgefahr.
   BEHOBEN (23.08.2026): index_act = -1 und page_act = -1 direkt bei
   pv-Erzeugung ergänzt.

 - viewer_ui.c, cb_viewer_text_search_entry_buffer_changed() (~Zeile
   126-134) sowie seiten.c, seiten_cb_loesche_seite() (~Zeile 712) und
   der Seiten-Einfügen-Handler (~Zeile 965): setzen text_occ.index_act
   (und teils arr_quad) zurück, aber nicht text_occ.page_act. Bleibt man
   beim Ändern des Suchbegriffs (bzw. nach Seiten löschen/einfügen) auf
   derselben Seitennummer, hält viewer_handle_text_search() diese Seite
   fälschlich für "schon durchsucht" und überspringt sie dauerhaft -
   auch nach vollem Rundlauf. Ergebnis: "Kein Treffer", obwohl der
   Begriff auf der sichtbaren Seite steht.
   BEHOBEN (23.08.2026): page_act = -1 an allen drei Stellen ergänzt.
   (Bei viewer.c:767, dem einfachen Klick-Handler, bewußt NICHT
   geändert - dort bleiben arr_quad/page_act gültig, da sich weder
   Seite noch Suchbegriff ändern.)

 - viewer_search.c, viewer_text_occ_search_next(): Vereinfacht - das
   fehleranfällige "idx bei -1/len starten, vor Prüfung inkrementieren"-
   Idiom (do/while mit continue) durch eine simple Richtungs-Schleife
   ersetzt. Verhält sich identisch, ist aber auch bei leerem Array von
   sich aus sicher statt auf den Aufrufer-Guard angewiesen zu sein.

 Architektur-Plan (24.08.2026, noch nicht umgesetzt - erstmal zurückgestellt):
 Atomarität store/work bei den Dual-Write-Stellen

 Betroffene Stellen (Stand 24.08.2026, per grep verifiziert):
 - viewer_save.c, viewer_save_dirty_dds() (~Zeile 604-647): dbase_zond_begin/
   commit/rollback um dbase_zond_update_sections() (Seiten löschen/einfügen).
 - zond_treeviewfm.c, zond_treeviewfm_before_move()/_after() (~Zeile 215-369):
   dbase_zond_begin/commit/rollback um dbase_zond_update_path() und
   mehrfach dbase_zond_update_gmessage_index() (Datei/Verzeichnis umbenennen/
   verschieben, inkl. GMessage-Sonderfälle). ZUSÄTZLICH dort eine dritte,
   unabhängige Transaktion auf index_ctx->db (FTS-Suchindex, eigene
   sqlite3-Verbindung, eigenes rohes sqlite3_exec("BEGIN/COMMIT/ROLLBACK")),
   die bislang nicht mit der store/work-Transaktion verklammert ist.

 Problem: store und work sind zwei unabhängige sqlite3-Verbindungen; die
 obigen Stellen schreiben sequenziell in beide (dbase_zond_begin/commit/
 rollback, project.c). Schlägt der zweite Commit nach erfolgreichem ersten
 fehl, oder das Rollback selbst, entsteht potenziell ein inkonsistenter
 Zustand zwischen store und work - keine echte Atomarität über beide Dateien.

 Plan:

 1. journal_mode-Sicherung (eigenständig, zuerst umsetzbar, unabhängig von
    2.-5.): vor jedem Öffnen des Projekts (project_create_dbase_zond()) UND
    unmittelbar vor jeder der o.g. Dual-Write-Transaktionen den aktuellen
    journal_mode beider Dateien per "PRAGMA journal_mode;" abfragen (über
    die jeweils schon offene Verbindung - journal_mode ist eine
    Dateieigenschaft, nicht verbindungsgebunden). Ist er nicht Rollback-
    Journal (z.B. WAL, weil extern z.B. mit DB Browser for SQLite
    umgestellt), Rückwechsel versuchen ("PRAGMA journal_mode=DELETE;") UND
    den zurückgelieferten Wert prüfen (ein gescheiterter Wechsel wirft
    keinen Fehler, sondern liefert stillschweigend den alten Modus zurück -
    sqlite.org/pragma.html). Bleibt es bei WAL, Operation NICHT ausführen,
    klare Fehlermeldung an den Anwender ("Datenbank wird von einem anderen
    Programm verwendet"). Grund: ATTACH-Transaktionen sind nur im Rollback-
    Journal-Modus dateiübergreifend atomar (Master-Journal-Mechanismus),
    unter WAL nicht - und der Modus kann jederzeit von außen (auch bei
    offener eigener Verbindung, da SQLite außerhalb aktiver Statements kein
    Lock hält) unbemerkt umgestellt werden.

 2. work bei Projekt-Öffnen an store attachen (project_create_dbase_zond()):
    zusätzlich zur weiterhin bestehenden eigenen work-Verbindung (die alle
    "normalen", bereits heute nur work betreffenden Operationen unverändert
    bedient - KEINE der ca. 80 einzelnen zond_dbase_*-Funktionen wird
    angefasst) wird auf der store-Verbindung per
    "ATTACH DATABASE '<path_tmp>' AS work;" work zusätzlich als zweites
    Schema eingehängt (einmalig beim Öffnen, nicht pro Operation). store
    bleibt "main", unqualifizierte Tabellennamen bestehender store-
    Funktionen bleiben dadurch unverändert korrekt. Wichtig: sequenzielles
    (nie gleichzeitiges) Schreiben auf work über zwei verschiedene
    Verbindungen (die eigene work-Verbindung UND store-mit-attachtem-work)
    ist bei SQLite unproblematisch, s. Diskussion vom 24.08. Willkommener
    Nebeneffekt: sqlite3_update_hook() (auf work's eigener Verbindung
    registriert, für project_set_changed()/"changed"-Tracking) feuert NICHT
    für Schreibzugriffe, die über die attachte Verbindung laufen - und das
    ist hier richtig so, nicht nachzuholen: Dual-Write-Änderungen sollen den
    Hook gerade NICHT auslösen, weil store dabei ja ohnehin synchron
    mitgeschrieben wird (kein "unsaved delta" gegenüber store, changed soll
    für diesen Fall nicht auf TRUE gehen). Bisher musste das umgekehrt extra
    abgefangen werden, weil die alten Dual-Write-Funktionen über work's
    eigene (Hook-tragende) Verbindung liefen: viewer_save_dirty_dds()
    (viewer_save.c, Zeile 582-584 sichert changed, Zeile 650-652 setzt ihn
    zurück, falls vorher FALSE) und zond_treeviewfm_before_move()/_after()
    (zond_treeviewfm.c, changed_tmp, Zeile 246/366) machen genau das. Mit
    der attachten Verbindung entfällt der Hook-Aufruf von vornherein - diese
    Sicherungs-/Rücksetzungs-Logik an beiden Stellen wird überflüssig und
    kann ersatzlos entfernt werden.

 3. dbase_zond_update_sections()/update_path()/update_gmessage_index()
    (project.c) neu schreiben: statt zweimal dieselbe Einzel-DB-Funktion auf
    unterschiedlichen ZondDBase-Objekten aufzurufen, EIN schemaqualifiziertes
    SQL-Statement-Paar (bzw. mehrere, je nach Funktion) auf der store-mit-
    attachtem-work-Verbindung, innerhalb einer Transaktion ("work.tabelle"
    für work, unqualifiziert/"main.tabelle" für store). Nur diese Handvoll
    Funktionen ändern sich - alle anderen, einzel-DB-operierenden
    zond_dbase_*-Funktionen (~80 Aufrufer) bleiben unangetastet.

 4. dbase_zond_begin/commit/rollback (project.c) entsprechend vereinfachen:
    statt Schleife über zwei ZondDBase-Objekte (zond_dbase_store,
    zond_dbase_work) mit je eigenem BEGIN/COMMIT/ROLLBACK nur noch ein
    einziges BEGIN/COMMIT/ROLLBACK auf der einen store-mit-work-Verbindung.
    Eigene Datei/eigenen klar abgegrenzten Abschnitt erwägen, da eng an das
    ATTACH-Setup gekoppelt (Schema-Namen, journal_mode-Check aus 1.).

 5. Rückbau der aktuellen Rollback-Fehlerbehandlung: der kürzlich gebaute
    Error-Merge in dbase_zond_rollback() (zwei separate error_int für
    store- und work-Rollback, zusammengeführt via add_string(), s. Eintrag
    oben "viewer_save.c, viewer_save_dirty_dds()...") wird durch 4.
    überflüssig - es gibt nur noch einen einzigen ROLLBACK-Aufruf, der
    scheitern kann. Diese Merge-Logik kann komplett entfernt werden
    zugunsten eines normalen einzelnen GError-Fehlerpfads.

 6. Offene Entscheidung vor Umsetzung: index_ctx (FTS-Suchindex,
    zond_treeviewfm.c) - ebenfalls ins ATTACH mit reinnehmen (drittes
    Schema, eine gemeinsame Drei-Wege-Transaktion), oder bewusst separat
    lassen (Inkonsistenz zwischen Suchindex und Projektdaten als
    tolerierbar einstufen, da der Index im Zweifel neu aufgebaut werden
    kann)? Muss vor 3./4. geklärt werden, da es die Form der Transaktion
    in zond_treeviewfm.c betrifft.

 7. Separat notiert, nicht Teil dieser Atomaritäts-Umstellung, aber im
    selben Bereich entdeckt: zond_treeviewfm.c, zond_treeviewfm_after()
    (~Zeile 351-356) - exit(EXIT_FAILURE) bei fehlgeschlagenem
    dbase_zond_commit(), unabhängig von der genauen Fehlerursache (auch
    bei einem gewöhnlichen ersten-Commit-Fehler, nicht nur bei echter
    store/work-Inkonsistenz). Kein Error-Dialog, keine Chance, sonstige
    ungesicherte Änderungen der Sitzung zu retten. Ggf. eigenständig zu
    behandeln.

 8. Testschritt (nach Umsetzung): gezielt einen Fehler mitten in einer
    Dual-Write-Operation provozieren (z.B. künstliche Constraint-
    Verletzung nur im zweiten Statement), prüfen, ob das Rollback wirklich
    beide Schemata (store und work) zurücksetzt.

 Bugs (Review 25.08.2026, Viewer-Gesamtdurchsicht nach den obigen Änderungen -
 mit Claude durchgegangen, Teilreviews per Subagenten, Funde vor Übernahme in
 diese Liste stichprobenartig am Code nachgeprüft):

 - viewer_save.c, viewer_save_dirty_dds() (Zeile 631-633, 640-643):
   pdf_drop_document(ctx, doc) doppelt - viewer_do_save_dd() droppt doc auf
   JEDEM eigenen Rückkehrpfad (Erfolg wie Fehler) schon selbst, u.a. seit
   dem Fix an sond_file_part_pdf_save_and_close() (droppt pdf_doc jetzt
   unabhängig vom Ergebnis). viewer_save_dirty_dds() droppte danach sowohl
   bei rc!=0 von viewer_do_save_dd() als auch bei einem nachfolgenden
   Commit-Fehlschlag nochmal - Doppel-Free bei praktisch jedem Fehler in
   dieser Speicherkette.
   BEHOBEN (25.08.2026): beide redundanten pdf_drop_document()-Aufrufe in
   viewer_save_dirty_dds() entfernt, Kommentar ergänzt.

 - viewer_save.c: physisches Speichern (Zeile 468) und Sync der gelöschten
   Seiten ins Live-Dokument (Zeile 483-567) laufen nacheinander - scheitert
   Letzteres, ist die Datei/das Journal schon "fertig", das In-Memory-
   Dokument aber nur teilweise nachgezogen.
   BEHOBEN (26.08.2026): Grundproblem war, daß pdf_delete_page()/
   pdf_delete_annot() auf dem Live-pdf_doc UND das Nachziehen von
   arr_pages/arr_annots (Entfernen + Umnumerieren von page_akt) in
   viewer_do_save_dd() als EIN untrennbarer Schritt behandelt wurden -
   ein Fehlschlag mittendrin ließ arr_pages/page_akt in einem Zustand
   zurück, der weder dem Vorher noch dem Nachher entsprach. Statt das
   robuster zu machen: neues Feld PdfDocumentPage/PdfDocumentPageAnnot->
   on_disk_deleted (zond_pdf_document.h) trennt "beim letzten Speichern
   bereits aus der Datei ausgeschlossen" von "aus der Live-Buchführung
   (arr_pages/arr_annots) entfernt" - Letzteres ist jetzt reine Best-
   Effort-Kosmetik ohne Korrektheitsrelevanz. viewer_do_save_dd()s dritte
   Schleife löscht nicht mehr aus dem Live-pdf_doc und faßt arr_pages/
   arr_annots nicht mehr an (kein pdf_delete_page()/pdf_delete_annot(),
   keine Umnumerierung, kein Anpassen von first_page/last_page mehr
   nötig) - sie setzt nur noch Flags (inserted zurücksetzen, deleted->
   on_disk_deleted nachziehen), reine Struct-Zuweisungen, kann nicht mehr
   fehlschlagen. Gelöschte Seiten/Annotationen bleiben dauerhaft als
   "Karteileichen" liegen, sind aber über pdfp->deleted schon überall
   (Rendering: viewer_new_page(); FTS-Index: viewer_update_index_for_
   save(); Sprung-Positionen: get_pdf_pos()) korrekt unsichtbar/
   ausgeschlossen - das war vorher schon so gebaut, unabhängig vom
   Aufräumen. anbindung_get_orig() (general.c) entsprechend symmetrisch
   um "pdfp->deleted && pdfp->on_disk_deleted" ergänzt (Karteileichen
   fehlen wie inserted-Seiten in der frisch geöffneten Speicher-Kopie),
   die erste Schleife in viewer_do_save_dd() prüft vor jedem pdf_delete_
   page()/pdf_delete_annot() jetzt on_disk_deleted (kein Doppel-Löschen
   einer schon länger ausgeschlossenen Karteileiche mehr) und setzt es
   nach Erfolg. anbindung_korrigieren() (general.c) um den jetzt
   hinfälligen "gelöschte Seiten verschieben Numerierung"-Zweig gekürzt
   (Speichern kompaktiert die Live-Numerierung nicht mehr).
   Nebenbei entdeckter und mit demselben Muster behobener Bug: der
   Annot-Lösch-Zweig der (jetzt entfernten) dritten Schleife trug
   pdfp_annot aus arr_annots aus, BEVOR pdf_delete_annot() überhaupt
   versucht wurde - bei einem Fehlschlag dort dachte die Buchführung
   "weg", obwohl die Annotation physisch noch vorhanden war. In der
   ersten Schleife jetzt: erst löschen versuchen, nur bei Erfolg
   on_disk_deleted setzen.

 - viewer.c (Zeile 772-773): pv->von_alt wird zweimal gesetzt, bis_alt
   bleibt auf altem Wert - Copy-Paste-Fehler, potenziell Index außerhalb
   arr_pages beim Dokumentwechsel.
   BEHOBEN (26.08.2026): zweite Zuweisung war ein Copy-Paste-Fehler von
   pv->von_alt = pdf_punkt.seite; statt pv->bis_alt = pdf_punkt.seite;.
   von_alt/bis_alt werden sonst nur als Paar benutzt (viewer.c, Zeile
   659-660: pv->von_alt = von.seite; pv->bis_alt = bis.seite;) und
   treiben ungeprüft eine Schleife über pv->arr_pages
   (viewer_handle_layout_motion_notify(), Zeile 643 ff.) - ein stehen
   gebliebenes altes bis_alt aus einem vorher in demselben pv-Fenster
   angezeigten, längeren Dokument hätte bei der nächsten Mausbewegung zu
   einem Zugriff außerhalb arr_pages führen können. Fix: Zeile 773 ->
   pv->bis_alt = pdf_punkt.seite;. Vor der Umsetzung noch einmal separat
   (eigene Re-Analyse + unabhängiger Subagent) verifiziert.

 - viewer.c (Zeile 652): prüft viewer_page->thread (Seite unterm
   Mauszeiger) statt viewer_page_old_range->thread (Seite mit der
   wegzuräumenden alten Markierung) - alte Highlights bleiben ggf. stehen.
   BEHOBEN (26.08.2026): Bit 2 von ViewerPageNew->thread bedeutet "image
   gerendert" (Kommentar viewer_render.c:160) und wird überall sonst im
   Code auf genau der Struct-Instanz geprüft, deren image_page angefaßt
   wird (z.B. viewer.c:665, viewer_annot.c:61) - hier aber wurde
   viewer_page (Hover-Seite, äußerer Scope) statt
   viewer_page_old_range (die Seite, deren image_page unmittelbar danach
   per gtk_widget_queue_draw() neu gezeichnet wird) geprüft. Fix: Zeile
   652 -> if (!(viewer_page_old_range->thread & 2)) continue;. Vor der
   Umsetzung noch einmal separat (eigene Re-Analyse + unabhängiger
   Subagent) verifiziert.

 - viewer.c, viewer_springen_zu_pos_pdf()/viewer_abfragen_pdf_punkt(): kein
   Guard für arr_pages->len == 0, obwohl an anderer Stelle im selben File
   vorhanden - potenziell Index -1/NULL-Deref, wenn ein dd nur aus
   gelöschten Seiten besteht.
   GEPRÜFT (26.08.2026): kein Bug - ein dd hat immer mindestens eine
   Seite, die letzte Seite eines dd kann nicht gelöscht werden.

 - viewer_annot.c, viewer_annot_handle_release_clicked_annot(): lokales
   GError* error überschattet den GError**-Parameter; jeder interne
   Fehlerpfad gibt TRUE zurück, ohne je den äußeren *error zu setzen -
   Aufrufer (viewer_ui.c, Zeile 322-325) dereferenziert error->message auf
   seinem eigenen, weiterhin NULLen error - Absturz bei praktisch jedem
   Fehler beim Verschieben einer Text-Annotation.
   BEHOBEN (26.08.2026): lokale Schattierungsvariable entfernt; jeder
   interne Fehlerpfad im "verschoben?"-Zweig setzt jetzt stattdessen den
   äußeren *error-Parameter (per g_error_new(), analog zu
   viewer_annot_do_create() und den übrigen GError**-Funktionen in dieser
   Datei) und ruft nicht mehr selbst display_message() auf - die Anzeige
   der Meldung bleibt wie überall sonst Sache des Aufrufers. Der bislang
   komplett stumme Fehlerpfad (Zeile 356-357, Seite noch nicht fertig
   gerendert) zeigt dem Nutzer jetzt ebenfalls eine Meldung statt
   stillschweigend abzubrechen. Der Aufruf von viewer_annot_do_change()
   übergibt jetzt direkt den äußeren error-Parameter statt einer lokalen
   Variable.
   Zusätzlich (auf Nachfrage): die Rückgabewerte TRUE/FALSE (gboolean-
   Konstanten - kompiliert zwar folgenlos, weil gboolean in GLib nur ein
   typedef auf gint ist, war aber ein Stilbruch gegenüber der
   Rückgabewert-Konvention aller anderen GError**-Funktionen in diesem
   Code, die -1/0 als Fehler-/Erfolgscode verwenden) durch -1/0 ersetzt.

 - viewer_annot.c: drei weitere Punkte -

   1) annot_after bleibt bei gelöschten Annotationen auf NULL (verfälschter
      "ungespeichert"-Status/verwaiste Journal-Einträge möglich).
      BEHOBEN (26.08.2026): viewer_annot_delete() (viewer_annot.c) setzt für
      JOURNAL_TYPE_ANNOT_DELETED-Einträge bewußt nur annot_before (es gibt
      kein "danach" mehr) - annot_after bleibt {0}. viewer_entry_in_dd()
      (viewer_save.c) prüfte aber bei Annot-Journal-Einträgen, die auf der
      ersten/letzten (angeschnittenen) Seite eines zpdfd_part liegen,
      unbedingt annot_after, um zu bestimmen, ob die Annotation im
      sichtbaren/gespeicherten Ausschnitt liegt - bei DELETED damit ein
      bedeutungsloses Rect {0,0,0,0} statt der tatsächlichen letzten
      Position der gelöschten Annotation. Betraf zwei Aufrufer:
      viewer_reset_dirty_dds() (dirty-Flag pro Ausschnitt kann fälschlich
      FALSE bleiben, obwohl dort eine ungespeicherte Löschung liegt) und die
      Journal-Bereinigung direkt nach erfolgreichem Speichern (Löschung
      bleibt fälschlich für immer im Journal hängen, oder wird - je nach
      Ausgang der Rect-Prüfung mit dem bedeutungslosen Rect - fälschlich
      schon entfernt, bevor der tatsächlich betroffene Ausschnitt gespeichert
      wurde, wodurch die Löschung dort verlorenginge). Fix: annot je nach
      entry->type wählen - bei JOURNAL_TYPE_ANNOT_DELETED annot_before
      (letzter bekannter Zustand vor dem Löschen), sonst weiterhin
      annot_after (aktueller Zustand nach Erstellen/Ändern).

   2) pdf_update_annot() ohne umgebendes fz_try/fz_catch; kein Rollback der
      arr_annots-Buchführung, wenn nach erfolgreichem pdf_create_annot() ein
      späterer Schritt scheitert.
      BEHOBEN (26.08.2026): pdf_update_annot() in viewer_annot_do_change()
      jetzt in fz_try/fz_catch, analog zu den übrigen Aufrufen in derselben
      Funktion.
      Für den Rollback kein pdf_delete_annot()-Versuch (der seinerseits
      scheitern könnte, s. Diskussion) - stattdessen genau wie bei den
      Karteileichen aus Finding 2 (on_disk_deleted) gelöst: viewer_annot_
      do_create() bekommt einen neuen Out-Parameter gboolean* created, der
      direkt nach erfolgreichem pdf_create_annot() auf TRUE gesetzt wird
      (unabhängig vom weiteren Ausgang der Funktion). Scheitert ein
      späterer Schritt (Farbe/Icon setzen, viewer_annot_do_change()),
      erzeugt der Aufrufer viewer_annot_create() bei created == TRUE einen
      PdfDocumentPageAnnot-Eintrag mit deleted = TRUE, on_disk_deleted =
      TRUE und hängt ihn an arr_annots an - reine Struct-Zuweisungen, kann
      nicht fehlschlagen. Notwendig (nicht nur bequem): pdf_document_page_
      annot_get_pdf_annot() (zond_pdf_document.c) löst den zu einem
      PdfDocumentPageAnnot gehörenden pdf_annot* rein positionell auf
      (Index in arr_annots == n-te Annotation von pdf_first_annot()/pdf_
      next_annot() auf der Live-Seite) - die physisch bereits an letzter
      Stelle der Live-Seite hängende Phantom-Annotation MUSS also in
      arr_annots mitgezählt werden, sonst verschöbe sich diese Zuordnung
      für alle danach auf dieser Seite neu erstellten Annotationen um 1.
      Dank deleted/on_disk_deleted bleibt sie dabei überall (Rendering-
      Overlay, künftige Speichervorgänge, Hit-Testing) korrekt unsichtbar/
      übersprungen, exakt wie eine echte, bereits gespeicherte Löschung.
      Der Aufruf von viewer_annot_do_create() beim Nachvollziehen eines
      JOURNAL_TYPE_ANNOT_CREATED-Journal-Eintrags in viewer_do_save_dd()
      (viewer_save.c) übergibt created = NULL, weil dort bei jedem
      Fehlschlag ohnehin das komplette frisch geöffnete doc verworfen wird
      - kein Bookkeeping nötig.

 - seiten.c (Zeile 1241), cb_pv_seiten_einfuegen(): g_object_unref(sfp)
   beim Erfolgspfad ohne Guard - beim Datei-Pfad (ret==1) korrekt mit
   "if (ret == 1)" geschützt (Zeile 1223), beim Clipboard-Pfad (ret==2,
   sfp bleibt NULL) fehlt der Schutz - g_object_unref(NULL) bei jedem
   normalen "Seiten einfügen aus Zwischenablage".
   BEHOBEN (26.08.2026): Zeile 1241 wie an den drei anderen Stellen
   derselben Funktion (Zeile 1198, 1210, 1223-1224) mit "if (ret == 1)"
   geschützt.

 - seiten.c, cb_pv_seiten_ocr(): wird "Abbrechen" genau geklickt, während
   der letzte OCR-Task noch erfolgreich fertig wird, liefert
   sond_ocr_do_tasks() rc==1; der break-Zweig überspringt dann Journal-
   Eintrag und Aufräumen, obwohl die Seite im Speicher schon geändert
   wurde - Buffer-Leak und/oder verlorene Änderung.
   VERIFIZIERT UND BEHOBEN (26.08.2026): bestätigt über sond_ocr.c,
   sond_ocr_do_tasks() (Zeile 228-449) - bei Abbruch werden keine neuen
   Tasks mehr gestartet, ein bereits laufender (status 1) aber ganz normal
   zu Ende abgewartet (sonst Use-after-free im Worker-Thread) und kann
   erfolgreich fertig werden (task->content_changed = TRUE, Zeile 413 -
   nur im Erfolgsfall gesetzt, bei echtem Taskfehler/rc==-1 bleibt es
   FALSE). Der Rückgabewert ist trotzdem "return cancelled ? 1 : 0;" -
   unabhängig davon, ob der eine Task dieses Aufrufs noch erfolgreich
   fertig wurde. Fix: rc==1 löst kein sofortiges break mehr aus, sondern
   setzt nur ein Flag (stop_after_this_page); die schon vorhandene normale
   Verarbeitung (Journal-Eintrag, Aufräumen, viewer_foreach()) für die
   eine, gerade fertig gewordene Seite läuft dadurch unverändert durch -
   erst danach (an allen drei Ausstiegspunkten dieser Seite: content_
   changed==FALSE, buf_content_new fehlt, normales Ende) wird abgebrochen.

 - viewer_render.c: drei noch nicht selbst verifizierte Punkte -
   unsynchronisierter Zugriff auf pv->arr_rendered (Lock nur bei nicht-
   leerer Pool-Queue); fz_context wird in viewer_render_page() nur im
   Erfolgsfall gedroppt, auf allen fünf Fehlerpfaden nicht;
   cb_viewer_render_page_for_printing() liest display_list ohne
   Erfolgsprüfung - NULL-Deref bei fehlgeschlagenem Rendern statt
   Fehlerdialog.

 - document.c, get_pdf_pos(): zweiter, unabhängiger else-Zweig
   überschreibt ges_bis_seite nochmal mit der Gesamtseitenzahl, obwohl nur
   im ersten else (leeres anbindung_ges) korrekt - kann beim Sprung zu
   einer Position die falsche (zu späte) Seite ansteuern. Noch nicht
   verifiziert/behoben.

 - stand_alone.c, cb_datei_oeffnen(): lehnt Anwender beim Öffnen einer
   neuen Datei trotz ungespeicherter Änderungen "trotzdem schließen" ab,
   öffnet die Funktion die neue Datei trotzdem - altes Dokument wird
   verwaist statt gedroppt, ungespeicherte Änderungen gehen trotz der
   eigentlich schützenden Abfrage verloren. Noch nicht behoben.

 - stand_alone.c: URI->Pfad-Umrechnung mit "+8"-Offset - vom Review nur
   mit niedriger Konfidenz gemeldet (unklar, ob für die Zielumgebung
   Windows überhaupt falsch). Noch nicht geprüft.

 - viewer_ui.c: Menüpunkt "Entnehmen" hat projektweit keinen
   "activate"-Handler (per grep verifiziert), ist aber sichtbar und
   anklickbar - macht nichts, ohne dass der Anwender das erkennen kann.
   Noch nicht behoben.

 */

