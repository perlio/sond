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

 - viewer_render.c: drei Punkte -

   1) unsynchronisierter Zugriff auf pv->arr_rendered (Lock nur bei nicht-
      leerer Pool-Queue). Verifiziert - g_thread_pool_unprocessed() zählt
      nur noch nicht gestartete Tasks, nicht gerade laufende (bis zu 3
      parallele Worker); bei leerer Warteschlange, aber laufendem Worker
      liest/verändert viewer_render_check() (Idle-Callback, Hauptthread)
      pv->arr_rendered ohne Lock, während der Worker unter Lock anhängt -
      Daten-Race auf einem GArray.
      BEHOBEN (26.08.2026): Optimierung gestrichen, viewer_render_check()
      ruft viewer_render_transfer_rendered() jetzt immer mit Lock auf.
      Der Overhead eines unumkämpften Mutex ist gegenüber dem Risiko
      vernachlässigbar.

   2) fz_context wird in viewer_render_page() nur im Erfolgsfall gedroppt,
      auf allen fünf Fehlerpfaden nicht.
      BEHOBEN (26.08.2026): fz_drop_context(ctx) vor jedem der fünf
      return-Fehlerpfade ergänzt (Laden, Display-List, Stext, Pixmap,
      Thumb). Der ganz erste Fehlerpfad (fz_clone_context() selbst
      schlägt fehl) braucht keinen Drop, da dort noch gar kein ctx
      existiert.

   3) cb_viewer_render_page_for_printing() liest display_list ohne
      Erfolgsprüfung - NULL-Deref bei fehlgeschlagenem Rendern statt
      Fehlerdialog.
      BEHOBEN (26.08.2026): direkt nach viewer_render_wait_for_transfer()
      wird jetzt geprüft, ob pdf_document_page->display_list tatsächlich
      gesetzt ist (die Funktion selbst meldet Fehlschläge nicht nach
      außen, nur LOG_WARN intern) - wenn nicht, derselbe display_message()
      -Fehlerdialog wie bei den übrigen Fehlerpfaden dieser Funktion,
      statt fz_run_display_list() mit NULL aufzurufen (Nullpointer-Zugriff
      in mupdf, läuft nicht über fz_throw() und wird daher auch nicht von
      fz_try/fz_catch abgefangen).

 - document.c, get_pdf_pos(): zweiter, unabhängiger else-Zweig
   überschreibt ges_bis_seite nochmal mit der Gesamtseitenzahl, obwohl nur
   im ersten else (leeres anbindung_ges) korrekt - kann beim Sprung zu
   einer Position die falsche (zu späte) Seite ansteuern.
   VERIFIZIERT UND BEHOBEN (26.08.2026): bestätigt - Copy-Paste-Fehler aus
   dem ges-Zweig. War anbindung_ges nicht leer (z.B. Anbindung Seite 5-10
   einer Datei) und anbindung_node leer, überschrieb der zweite else-Zweig
   das bereits korrekt gesetzte ges_bis_seite (10) fälschlich mit der
   Gesamtseitenzahl des ganzen Dokuments - beim Sprung ans Ende der
   Anbindung (node_bis_seite==0 && node_bis_index==0-Fallback) landete man
   dadurch auf der letzten Seite des gesamten PDFs statt am Ende der
   Anbindung. Fix: zweiten else-Zweig ersatzlos gestrichen - die node_*-
   Variablen behalten bei leerem anbindung_node ohnehin schon korrekt
   ihren Default-Wert 0 aus der Deklaration.

 - stand_alone.c, cb_datei_oeffnen(): lehnt Anwender beim Öffnen einer
   neuen Datei trotz ungespeicherter Änderungen "trotzdem schließen" ab,
   öffnet die Funktion die neue Datei trotzdem - altes Dokument wird
   verwaist statt gedroppt, ungespeicherte Änderungen gehen trotz der
   eigentlich schützenden Abfrage verloren.
   BEHOBEN (26.08.2026): pv_schliessen_datei() gibt jetzt gint statt void
   zurück (0: Datei tatsächlich geschlossen; -1: Anwender hat "Trotzdem
   schließen?" nach gescheitertem Speichern abgelehnt, pv->dd bleibt
   unverändert auf dem alten, ungespeicherten Dokument stehen).
   cb_datei_oeffnen() prüft diesen Rückgabewert jetzt und bricht bei -1
   sofort ab (Dateiname wird freigegeben, kein pv_oeffnen_datei()-Aufruf
   mehr) - vorher wurde der Rückgabewert ignoriert und die neue Datei
   trotzdem geladen, wobei viewer_display_document() pv->dd ungeprüft
   überschrieben und damit das eben geschützte, ungespeicherte Dokument
   verwaist hätte. cb_datei_schliessen() ignoriert den Rückgabewert
   weiterhin bewußt, da dort kein Folgeschritt existiert, der pv->dd
   überschreiben könnte.

 - stand_alone.c: URI->Pfad-Umrechnung mit "+8"-Offset - vom Review nur
   mit niedriger Konfidenz gemeldet (unklar, ob für die Zielumgebung
   Windows überhaupt falsch).
   GEPRÜFT (26.08.2026): für reines Windows-Ziel wäre der Offset korrekt
   gewesen (g_file_get_uri() liefert dort "file:///C:/...", "file:///"
   ist genau 8 Zeichen lang) - aber die App soll ggf. auch unter Linux
   laufen ("file:///home/user/..." + 8 ergäbe fälschlich
   "home/user/..." ohne führenden Slash). BEHOBEN (26.08.2026): open_app()
   in stand_alone.c baut den Pfad nicht mehr manuell aus dem URI-String
   zusammen (g_file_get_uri()/g_uri_unescape_string()/"+8"-Offset
   entfernt), sondern verwendet g_file_get_path() auf dem von GTK bereits
   gelieferten GFile* - das liefert plattformunabhängig den nativen
   lokalen Pfad (inkl. Un-Escaping) und funktioniert so auf Windows und
   Linux gleichermaßen. Liefert g_file_get_path() NULL (kein lokaler
   Pfad ermittelbar), wird das jetzt mit einer Fehlermeldung abgefangen
   statt mit einem kaputten Pfad weiterzuarbeiten.

 - viewer_ui.c: Menüpunkt "Entnehmen" hat projektweit keinen
   "activate"-Handler (per grep verifiziert), ist aber sichtbar und
   anklickbar - macht nichts, ohne dass der Anwender das erkennen kann.
   BEHOBEN (26.08.2026): Menüpunkt vorerst ersatzlos entfernt statt mit
   einem Handler versehen - die geplante Funktion (mehrere Seiten zu
   einem neuen PDF kombinieren) ist noch nicht ausgereift und soll
   später vermutlich über eine Kombination von Anbindungen gelöst
   werden, nicht im Viewer selbst. pv->item_entnehmen (viewer.h) sowie
   alle Verwendungen (Erzeugung/Einhängen in viewer_ui.c,
   gtk_widget_set_sensitive() in stand_alone.c) entfernt.

 - seiten.c, seiten_ocr_abfrage_hidden_text() (vom Anwender gemeldet, nicht
   Teil des ursprünglichen Reviews): Rückfrage-Dialog bei Seiten mit bereits
   vorhandener versteckter Textebene hatte nur "Verwerfen und neu OCRen"
   und "Seite überspringen" - keine Möglichkeit, den gesamten (u.U.
   mehrseitigen) OCR-Lauf abzubrechen. Bei vielen bereits-OCRten Seiten
   musste man sich durch jede einzelne Seite durchklicken.
   BEHOBEN (26.08.2026): dritter Button "Abbrechen" (GTK_RESPONSE_CANCEL)
   ergänzt. my_dialog_run() (misc.c) verknüpft das delete-event ("X")
   bereits generisch mit GTK_RESPONSE_CANCEL - damit bricht auch das
   Schließen des Fensters jetzt den gesamten Lauf ab, nicht nur diese
   Seite. In cb_pv_seiten_ocr() (Aufrufer) wird GTK_RESPONSE_CANCEL jetzt
   gesondert behandelt: dieselbe cancel-Flag wie beim Abbrechen-Button des
   InfoWindow wird gesetzt (info_window->cancel, atomar, konsistent mit
   cb_abbrechen_clicked()/cb_info_window_delete_event() in misc.c) und die
   Schleife sofort verlassen - die aktuelle Seite wurde an dieser Stelle
   noch nicht angefasst (kein Content-Stream gelesen, kein Task angelegt),
   ein direktes break ist daher unproblematisch.

 Bugs (Review 26.08.2026, Runde 2 - Viewer-Gesamtdurchsicht per Subagenten,
 die wichtigsten/überraschendsten Funde vor Übernahme in diese Liste am
 Code nachgeprüft - Konfidenz einzeln vermerkt):

 - viewer.c, viewer_handle_button_press() (Zeile ~821): prüft vor Aufruf
   von viewer_on_text() "thread & 4" statt "thread & 8" (verifiziert -
   viewer_set_cursor() an der praktisch identischen Stelle, Zeile 492,
   prüft korrekt "& 8"). Bit 4 zeigt nur an, daß die display_list fertig
   ist, Bit 8 erst, daß stext_page (von viewer_on_text() zwingend
   gebraucht) fertig ist. Scheitert der stext-Render-Schritt für eine
   Seite, bleibt thread bei 6 statt 14 stehen (viewer_render_transfer_
   rendered() in viewer_render.c) - stext_page ist dann NULL, während Bit 4
   schon gesetzt ist. viewer_on_text() dereferenziert dann NULL->
   first_block - roher NULL-Zugriff, nicht über fz_throw, von keinem
   fz_try/fz_catch abfangbar - Absturz bei einem einzelnen Linksklick auf
   eine solche Seite.
   BEHOBEN (26.08.2026): "thread & 4" auf "thread & 8" korrigiert, analog
   zu viewer_set_cursor() an der praktisch identischen Stelle.

 - viewer.c, viewer_handle_layout_motion_notify() (Zeile ~603/638):
   Bedingung fürs Neuzeichnen während Text-Drag prüft nur
   "pdf_document_page->thread & 8" (stext fertig), zeichnet dann aber
   viewer_page->image_page. image_page wird erst mit viewer_page->thread &
   2 (anderes Struct, anderes Flag) angelegt. viewer_render_stext_page_
   fast() (aus der Textsuche heraus aufgerufen, viewer_search.c:126) kann
   Bit 8 synchron setzen, ohne image_page anzulegen. Zieht man während/nach
   einer solchen Suche eine Textmarkierung über eine so vorbereitete Seite,
   wird gtk_widget_queue_draw(NULL) aufgerufen - GLib-"critical", unter
   G_DEBUG=fatal-criticals/-warnings (in Debug-Umgebungen üblich) fatal.
   BEHOBEN (26.08.2026): gtk_widget_queue_draw(viewer_page_loop->image_page)
   nur noch aufgerufen, wenn image_page tatsächlich gesetzt ist. Die
   Markierungsberechnung (pv->highlight) bleibt unverändert - sie ist auch
   ohne image_page schon korrekt (stext_page ist ja da), nur das sofortige
   Neuzeichnen entfällt für eine noch nicht angelegte Seite; sobald sie
   später normal gerendert wird, zeigt sie die Markierung ohnehin an.

 - viewer.c, viewer_handle_layout_motion_notify() (PDF_ANNOT_TEXT-Zweig,
   Zeile ~665-679): beim Ziehen einer Text-Annotation wird für die
   Bereitschaftsprüfung und fürs Neuzeichnen die Seite unter dem aktuellen
   Mauszeiger benutzt (viewer_page), nicht die Seite, auf der die gezogene
   Annotation tatsächlich liegt (pv->clicked_annot->annot). Dieselbe
   Fehlerklasse wie der schon behobene Bug bei Zeile 652, hier an einer
   zweiten, noch unbehobenen Stelle. Wandert die Maus beim Ziehen über eine
   Seitengrenze, wird die falsche Seite neu gezeichnet - die Annotation
   hängt visuell fest, bis ein unabhängiges Neuzeichnen die richtige Seite
   erfasst.
   GEPRÜFT (26.08.2026), KEIN BUG (bewusst so belassen): die Rect-
   Koordinaten der Annotation werden unabhängig von viewer_page korrekt
   fortgeschrieben (reine Delta-Rechnung über pv->x/pv->y) - nur das
   sofortige Neuzeichnen bleibt beim Verlassen der eigenen Seite aus,
   rein kosmetisch. Anwender-Einschätzung: Annotation bleibt beim
   Herausziehen über den Seitenrand optisch am Rand "kleben", bis entweder
   die Maus wieder auf die eigene Seite zurückkehrt (dann normal
   mitgezogen) oder losgelassen wird (dann bleibt sie visuell am Rand
   stehen) - kein eigenständiger Fix nötig.

 - viewer.c, viewer_handle_button_press() (Anbindung "umdrehen", Zeile
   ~879-882, nur #ifndef VIEWER): "page_pdf >= pv->anbindung.von.seite" ist
   bei gleicher Seite schon für sich allein wahr, die zusätzliche
   punktgenau/y-Prüfung kann das Ergebnis dann nicht mehr ändern - der
   else-Zweig ("umdrehen") wird bei gleicher Seite nie erreicht. Im
   punktgenauen (Shift-Klick-)Modus: setzt man den "bis"-Punkt auf
   derselben Seite oberhalb (kleinere y) des schon gesetzten "von"-Punkts,
   entsteht trotzdem eine Anbindung mit von.index > bis.index statt daß
   von/bis vertauscht werden (Auswirkung auf zond_anbindung_erzeugen() nicht
   mitgeprüft).
   BEHOBEN (26.08.2026): "page_pdf >= pv->anbindung.von.seite" auf
   "page_pdf > pv->anbindung.von.seite" korrigiert (durchgerechnet: für
   spätere/frühere Seite sowie den nicht-punktgenauen Fall auf derselben
   Seite ändert sich dadurch nichts - nur der beschriebene Fehlerfall
   (punktgenau, gleiche Seite, zweiter Klick oberhalb des ersten) wechselt
   jetzt korrekt in den "umdrehen"-Zweig).

 - viewer_save.c, viewer_do_save_dd(), JOURNAL_TYPE_ANNOT_CHANGED-Zweig
   (Zeile ~442-459, verifiziert): pdf_document_page_annot_get_index()
   liefert die rohe (Karteileichen-inklusive) Position in arr_annots - für
   pdf_document_page_annot_get_pdf_annot() auf der LIVE-Seite richtig, da
   dort nie physisch gelöscht wird (nur Hidden-Flag). Hier wird derselbe
   rohe Index aber gegen pdf_page verwendet - eine frisch neu geöffnete
   Kopie der Datei vom letzten Speicherstand. Wurde bei einem früheren
   Speichern schon eine Annotation vor dieser im arr_annots-Index physisch
   gelöscht (pdf_delete_annot(), Zeile ~462-506), hat die neu geöffnete
   Datei weniger Annots als arr_annots Einträge. pdf_annot_lookup_index()
   (sond_pdf_helper.c:1040, verifiziert) prüft dabei keine Grenzen - läuft
   über das Ende der echten Liste hinaus einfach auf NULL. Der Rückgabewert
   wird hier ungeprüft an viewer_annot_do_change() weitergereicht, das dann
   pdf_set_annot_rect()/pdf_set_annot_contents()/pdf_update_annot() auf
   NULL aufruft - roher NULL-Zugriff in MuPDF, nicht fz_try/fz_catch-fähig.
   Trigger: Seite mit zwei Annots A (Index 0) und B (Index 1); A löschen
   und speichern (A bleibt für immer als Karteileiche in arr_annots an
   Index 0, physische Datei hat jetzt nur noch B); später B in derselben
   Sitzung bearbeiten und erneut speichern - Absturz beim zweiten
   Speichern.
   BEHOBEN (26.08.2026): neue Hilfsfunktion viewer_save_get_physical_
   annot_index() zieht dieselbe Karteileichen-Ausschluss-Logik wie die
   "Annots löschen"-Schleife weiter unten (dort: pdf_ann bewußt nicht
   vorrücken, hier: Zähler bewußt nicht erhöhen bei ->deleted &&
   ->on_disk_deleted) in eine wiederverwendbare Funktion und ersetzt damit
   die rohe arr_annots-Position im ANNOT_CHANGED-Zweig. Zusätzlich wird
   der Rückgabewert von pdf_annot_lookup_index() jetzt geprüft (vorher
   ungeprüft an viewer_annot_do_change() weitergereicht) - schlägt die
   Auflösung trotzdem fehl, gibt es jetzt eine GError-Meldung statt eines
   Absturzes.

 - viewer_save.c, viewer_entry_in_dd() (Zeile ~78-99): bei einem dd, dessen
   erste UND letzte Seite dieselbe Seite ist (einseitiger Ausschnitt, oben
   UND unten beschnitten - z.B. Zitat mitten auf einer Seite), greift immer
   nur der erste if-Zweig (first_page-Vergleich); first_index wird als
   obere Grenze verwendet, last_index (untere Grenze) dabei komplett
   ignoriert (rect.y1 = ganze Seite statt last_index). Journal-Einträge
   unterhalb von last_index auf dieser Seite werden fälschlich als "im dd
   sichtbar" gewertet - kann viewer_reset_dirty_dds() fälschlich "dirty"
   setzen und/oder viewer_do_save_dd()s Journal-Aufräumschleife einen
   Eintrag zu früh/falsch zuordnen aus arr_journal entfernen.
   BEHOBEN (26.08.2026): is_first/is_last jetzt unabhängig voneinander
   ermittelt statt über if/else-if-Reihenfolge; obere Grenze (first_index)
   gilt nur, wenn die Seite die erste ist, untere (last_index) nur, wenn
   sie die letzte ist - eine Seite, die beides ist (einseitiger
   Ausschnitt), bekommt jetzt beide Grenzen zugleich statt nur die obere.
   Die "keine Beschneidung"-Kurzschlüsse (sofort TRUE) für die drei
   bisherigen Fälle bleiben über den y0==0/y1==Seitenende-Vergleich
   erhalten.

 - viewer_annot.c, viewer_annot_check_diff() (Zeile ~299-315): setzt
   crop.y0/y1 immer auf first_index/last_index, unabhängig davon, ob die
   geprüfte Seite nur first_page oder nur last_page eines mehrseitigen dd
   ist. Für eine Seite, die nur first_page ist, sollte unten keine Grenze
   gelten (Inhalt geht auf Folgeseiten weiter), für eine Seite, die nur
   last_page ist, oben keine - hier wird aber immer die jeweils andere,
   fachlich irrelevante Grenze mit reingemischt. Verschiebt ein Anwender
   eine Text-Annotation auf der ersten Seite eines mehrseitigen dd
   innerhalb der Seite nach unten (weit über last_index der LETZTEN Seite
   hinaus, aber völlig legitim), meldet viewer_annot_check_diff()
   fälschlich eine Sichtbarkeitsänderung - die Verschiebung wird
   zurückgenommen und ein irreführender Fehlerdialog ("Annotation würde in
   geöffnetem Abschnitt entfernt oder hinzugefügt") gezeigt. Noch nicht
   behoben.

 - viewer_annot.c, viewer_annot_create_markup() (Zeile ~686-687): Rückgabe
   TRUE (= Fehler laut Konvention) beim "Seite noch nicht fertig gerendert"
   -Abbruch, ohne *error zu setzen - derselbe Bug, der bei
   viewer_annot_handle_release_clicked_annot() schon behoben wurde (s.o.),
   hier an einer weiteren, noch unbehobenen Stelle. Jeder Aufrufer, der der
   GError-Konvention folgt (rc!=0 -> error->message lesen) dereferenziert
   ein noch-NULL GError*. Noch nicht behoben.

 - viewer_annot.c, viewer_annot_delete() (Zeile ~97): setzt *error = ...
   ohne vorheriges "if (error)" - jede andere Fehlerstelle in dieser
   Funktion und in viewer_annot_handle_release_clicked_annot() prüft das
   korrekt. Ruft ein Aufrufer (laut Konvention zulässig) mit error==NULL
   auf und schlägt genau dieser Zweig fehl (pdf_annot nicht auflösbar),
   Absturz. Noch nicht behoben.

 - viewer_annot.c, viewer_annot_do_change() (TEXT-Zweig, Zeile ~177-215,
   niedrigere Konfidenz): pdf_set_annot_contents()/pdf_set_annot_rect() und
   pdf_update_annot() laufen in zwei getrennten fz_try-Blöcken. Schlägt nur
   der zweite (pdf_update_annot()) fehl, geben beide Aufrufer die
   In-Memory-Annot auf den alten Stand zurück und legen keinen
   Journal-Eintrag an - aber die erste fz_try hat das PDF-Objekt (Rect/
   Contents) vermutlich schon direkt verändert. Möglicher Fall von "physisch
   schon passiert, Buchführung sagt nein" - dieselbe Fehlerklasse wie der
   schon für viewer_annot_do_create() behobene Bug, hier für den
   Änderungs- statt den Erzeugungspfad, nicht abschließend gegen MuPDFs
   internes Commit-Verhalten verifiziert. Noch nicht geprüft.

 - viewer_render.c, viewer_close_thread_pool_and_transfer(): g_thread_pool_
   free(pool, immediate=TRUE, wait_=TRUE) verwirft wartende (noch nicht an
   einen Worker vergebene) Tasks stillschweigend - für jeden gibt es nie
   ein RenderResponse in arr_rendered, aber pv->count_active_thread wurde
   für jeden schon beim Einreihen hochgezählt (viewer_render_thread()) und
   wird nur beim tatsächlichen Empfang in arr_rendered wieder runtergezählt
   (viewer_render_transfer_rendered()). Bei mehr als 3 gleichzeitig
   sichtbaren/angestoßenen Seiten (Pool-Größe 3) und einem Aufruf dieser
   Funktion währenddessen (z.B. seiten_drehen()/seiten_cb_loesche_seite() in
   seiten.c, die den pv danach offen lassen) bleibt count_active_thread
   dauerhaft über 0 - viewer_render_check()s Idle-Quelle terminiert nie
   mehr, läuft für den Rest der Sitzung mit voller Rate weiter (ein
   CPU-Kern dauerhaft ausgelastet). Noch nicht behoben.

 - viewer_render.c, viewer_render_stext_page_from_page() (Zeile ~453-461):
   im fz_catch-Zweig fehlt fz_drop_stext_page(ctx, stext_page) - die
   Parallel-Funktion viewer_render_stext_page_from_display_list() macht das
   in ihrem eigenen fz_catch korrekt. Wirft pdf_run_page() bei einer
   beschädigten Seite eine Exception, leakt ein fz_stext_page pro
   Fehlversuch - kann sich bei Retries (viewer_render_stext_page_fast() wird
   z.B. bei jeder erneuten Suche/jedem Sprung auf dieselbe Seite erneut
   aufgerufen) aufsummieren. Noch nicht behoben.

 - viewer_search.c, viewer_handle_text_search() (Überlauf-Prüfung, Zeile
   ~441-472): pdf_pos.index steht an dieser Stelle immer schon fest auf 0
   (vorwärts) bzw. EOP=99999 (rückwärts) - die Prüfung "pdf_pos.index <=
   pdf_punkt.punkt.y" (bzw. >=) ist dadurch für jede realistische
   Koordinate immer wahr, der else-Zweig (der die Ausgangsseite noch ein
   zweites Mal vollständig durchsuchen würde) ist toter Code. Sucht man
   nach einem Begriff, der nur auf der aktuell sichtbaren Seite, aber
   OBERHALB (vorwärts) bzw. UNTERHALB (rückwärts) der aktuellen
   Scroll-Position vorkommt, meldet die Suche fälschlich "Kein Treffer",
   obwohl der Treffer sichtbar auf der Seite steht - reproduzierbar im
   normalen "Begriff eingeben, Weiter/Zurück klicken"-Ablauf. Noch nicht
   behoben.

 - seiten.c, cb_pv_seiten_ocr() (Zeile ~353-361): scheitert sond_ocr_pool_
   new() (z.B. tessdata-Verzeichnis fehlt), wird sofort zurückgekehrt, ohne
   info_window_close() aufzurufen. info_window_open() hat den Dialog aber
   schon sichtbar/modal geschaltet und Abbrechen-Button/delete-event fest
   mit info_window->cancel verdrahtet - das zeigt auf die lokale Variable
   "cancel" dieser Funktion, deren Stack-Frame nach dem return nicht mehr
   existiert. Klickt der Anwender danach Abbrechen oder schließt das
   Fenster per "X", schreibt der Handler in bereits freigegebenen
   Stack-Speicher. Außerdem bleiben GtkDialog und InfoWindow-Struct
   dauerhaft undestroyed/ungefreed und das (modale) Fenster blockiert den
   Viewer. Noch nicht behoben.

 - seiten.c, cb_pv_seiten_ocr() (Zeile ~528): pdf_drop_obj(ctx, font_ref)
   unbedingt am Ende, obwohl font_ref auf drei Wegen gesetzt werden kann -
   pdf_new_indirect()/pdf_put_sond_font() liefern eine eigene (zu
   droppende) Referenz, pdf_get_sond_font() (per pdf_dict_gets(), wie
   sonst im Projekt üblich) aber eine GELIEHENE. Findet pdf_get_sond_font()
   eine bereits im Dokument vorhandene SOND-Schriftart (z.B. weil die PDF
   in einer früheren Sitzung schon OCRt und neu geöffnet wurde, bevor
   zond_pdf_document_get_ocr_num() den Font neu gecacht hat), dekrementiert
   dieser Drop eine fremde Referenz ohne vorherigen eigenen Keep - Gefahr
   eines Use-after-free auf das Font-Objekt beim nächsten Zugriff (Rendern/
   Speichern). Noch nicht behoben.

 - seiten.c, seiten_anbindung()/seiten_anbindung_int() (Zeile ~860-927):
   beide Aufrufe übergeben identisch pv->zond->dbase_zond->zond_dbase_store
   - zond_dbase_work (wo neu angelegte, noch nicht gespeicherte Anbindungen
   der aktuellen Sitzung liegen, s. dbase_zond_update_sections()-Muster in
   project.c) wird nie geprüft; der Parameter "attached" bleibt im
   Funktionskörper ungenutzt. Eine in der laufenden Sitzung neu angelegte,
   noch nicht gespeicherte Anbindung verhindert das Löschen der
   betreffenden Seite dadurch nicht (cb_pv_seiten_loeschen() erlaubt das
   Löschen fälschlich) - die Anbindung wird verwaist. Noch nicht behoben.

 - seiten.c, seiten_drehen() (Zeile ~725-792, niedrigere Konfidenz):
   seiten_drehen_pdf() schreibt /Rotate direkt und dauerhaft auf das
   pdf_obj; scheitert das anschließende zond_pdf_document_load_page() (Neu-
   Laden nach dem Verwerfen von display_list/stext_page/arr_annots), bricht
   die Funktion mit -1 ab, OHNE einen Journal-Eintrag für die schon
   physisch gedrehte aktuelle Seite anzulegen, und verarbeitet die
   restlichen ausgewählten Seiten der Schleife gar nicht mehr - "physisch
   passiert, aber weder journalisiert noch für die übrigen Seiten
   fortgesetzt", ähnliche Fehlerklasse wie der schon behobene OCR-
   Abbruch-Bug. Noch nicht behoben.

 - document.c, get_pdf_pos() (Zeile ~85-92, verifiziert - unabhängig vom
   bereits behobenen Copy-Paste-Bug in derselben Funktion): die Schleife
   zum Herausrechnen gelöschter Seiten läuft vom ABSOLUTEN ges_von_seite
   bis zum RELATIVEN pdf_pos.seite (das ist node_von_seite - ges_von_seite
   bzw. node_bis_seite - ges_von_seite, s. Zeile 66/73) - bei jeder
   Anbindung, die nicht bei Seite 0 des PDF beginnt, ist ges_von_seite
   meist schon größer als dieser kleine relative Offset, die
   Schleifenbedingung "i < pdf_pos.seite" ist dann sofort falsch, 0
   Iterationen. Zusätzlich schrumpft die Schleifengrenze bei jedem Treffer
   selbst (pdf_pos.seite wird im Rumpf dekrementiert und ist zugleich die
   Abbruchbedingung) - kann bei mehreren gelöschten Seiten hintereinander
   vorzeitig abbrechen, bevor alle relevanten Seiten geprüft wurden.
   Ergebnis: falsche (zu frühe oder zu weit vorgezogene) Zielseite beim
   Sprung zu einem Node/einer Anbindung in einem PDF, in dem in der
   aktuellen Sitzung schon Seiten gelöscht wurden - im Extremfall ein
   Index außerhalb von pv->arr_pages, das mehrere Stellen in viewer.c
   ungeprüft per g_ptr_array_index() lesen. Noch nicht behoben.

 - stand_alone.c, pv_schliessen_datei() (Zeile ~74-90, verifiziert): sowohl
   die "Speichern?"- als auch die "Trotzdem schließen?"-Abfrage
   (abfrage_frage(), nur Ja/Nein-Buttons) werden nur auf GTK_RESPONSE_YES
   bzw. GTK_RESPONSE_NO geprüft. my_dialog_run() (misc.c) verknüpft das
   Schließen per "X" aber generisch mit einem DRITTEN Response,
   GTK_RESPONSE_CANCEL, das hier an keiner der beiden Stellen abgefangen
   wird - fällt bei "Speichern?" durch wie "Nein" (kein Speichern, keine
   weitere Rückfrage), fällt bei "Trotzdem schließen?" durch wie "Ja"
   (Datei wird trotz gescheitertem Speichern geschlossen) - genau der Fall,
   den der heute (26.08.2026) an dieser Funktion gemachte Fix eigentlich
   verhindern sollte. Schließt man eine dieser beiden Abfragen per "X"
   statt per Button, gehen ungespeicherte Änderungen trotzdem verloren.
   Noch nicht behoben.

 - stand_alone.c, pv_schliessen_datei() (Zeile ~102-107): pv->tree_thumb
   wird zerstört und durch eine neue GtkTreeView ersetzt, aber nirgends
   wieder in seinen Container (pv->swindow_tree) eingehängt -
   gtk_container_add() dafür gibt es nur einmal im ganzen Projekt, in
   viewer_ui.c beim initialen Fensteraufbau. Nach dem ersten Schließen
   einer Datei (auch implizit beim Öffnen einer neuen über
   cb_datei_oeffnen()) bleibt das Thumbnail-Panel für den Rest der
   Sitzung dauerhaft leer, unabhängig davon, was danach geöffnet wird.
   Noch nicht behoben.

 - stand_alone.c, pv_oeffnen_datei() (Zeile ~127-144): sond_file_part_from_
   filepart() liefert eine eigene Referenz (g_object_ref); document_new_
   displayed_document()/zpdfd_part_peek() übernimmt diese nicht, sondern
   hält selbst eine unabhängige Referenz. Das im Projekt sonst überall
   übliche Muster (Aufrufer unreft sfp nach dem Aufruf, s. zond_treeview.c)
   fehlt hier komplett - auf beiden Rückkehrpfaden (Erfolg wie
   document_new_displayed_document()-Fehlschlag). Jede geöffnete Datei
   leakt eine Referenz auf ihr SondFilePart; über eine Sitzung mit
   mehreren Öffnen/Schließen-Zyklen läppert sich das. Noch nicht behoben.

 - projektweit, niedrigere Konfidenz: mehrere display_message()/
   abfrage_frage()-Aufrufe geben error->message (kann u.a. aus Dateipfaden
   oder MuPDF-Fehlertexten stammen) direkt als printf-artiges
   message_format an GTK weiter, ohne Format-Escaping - ein "%" im
   dynamischen Text wird als Formatspezifizierer ohne passendes Argument
   interpretiert (u.a. stand_alone.c:81/183/213, viewer_ui.c mehrfach).
   Betrifft laut Review auch etliche Stellen außerhalb des Viewer-Moduls -
   eher ein projektweites, bestehendes Muster als ein neu eingeführter
   Bug. Noch nicht geprüft/entschieden, ob das behoben werden soll.

 */

