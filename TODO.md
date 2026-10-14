# TODO

Offene Punkte für zond. Erledigtes steht in [HISTORY.md](HISTORY.md), Hinweise zum Bauen in [BUILD.md](BUILD.md).

Neue Einträge werden ab #200 nummeriert, die nächste freie Nummer ist #242.

## Inhalt

Offen: #236, #234
Zurückgestellt: #202, #220, #237, #238, #239, #240, #241
Ideen: #167, #169, #205, #231, #232, #233

## Offen

### #236 Atomarität store/work, Phase 2

Status: offen (Phase 1 und Punkt 8 in HISTORY.md)

```text
 Phase 1 (store/work atomar per ATTACH) und der Fehlerzweig beim fehlgeschlagenen
 Commit sind erledigt (HISTORY.md, "Atomarität store/work", 14.10.2026). Offen
 ist die Index-DB (index_ctx, .sond_index.db):

 1. Einbindung in die Dual-Write-Transaktion (früher 6.b). zond_treeviewfm_before_move()
    und _before_delete() (zond_treeviewfm.c) schreiben die Index-DB in einer
    eigenen Transaktion per rohem sqlite3_exec(index_ctx->db, "BEGIN;" ...), neben der
    atomaren store+work-Transaktion. Geplant: index_ctx dort lokal per ATTACH
    unmittelbar vor und DETACH unmittelbar nach der Transaktion als drittes Schema
    in die store+work-Connection aufnehmen (kein dauerhaftes ATTACH: der
    Hintergrund-Thread der Indizierung braucht seine eigene Connection). FTS5-Tabellen
    sind vom Super-Journal-Mechanismus mit abgedeckt. Voraussetzung: SQLITE_BUSY
    behandeln (busy_timeout setzen und/oder Retry, sonst Meldung "Indizierung läuft,
    bitte kurz warten"), denn läuft der Thread gerade in einer Schreibtransaktion,
    scheitert der ATTACH-Schreibversuch.
 2. Dieselbe Index-Verbindung in mehreren Threads (geringes Risiko, unabhängig von 1.).
    wctx (mit index_ctx, eine feste Connection) geht an den Indexier-Thread
    (headerbar.c, zond_index_lauf(): td->wctx = zond->wctx). Von den Feldern teilt er
    sich mit der UI nur index_ctx; cancel, log_func_data und ocr_mode setzt die UI
    nur vor dem Start bzw. über den Abbrechen-Button, locked liest sie erst nach dem
    Ende des Threads. UI-Schreibzugriffe mit offener Transaktion auf index_ctx->db
    (rohes BEGIN/COMMIT/ROLLBACK): zond_treeviewfm.c (Move, Delete), headerbar.c
    (Index löschen), viewer_save.c (Seiten löschen/einfügen). Sie sind
    nutzergesteuert und während der Indizierung nicht erreichbar, weil das
    Info-Fenster modal ist (misc.c, info_window_open()). Übrig bleiben lesende Zugriffe
    ohne Nutzereingabe: Badge-Status beim Aktualisieren der Bäume (Root-Monitor-Timer,
    sond_treeviewfm.c) und die semantische Suche im Chat-Thread (zond_chat.c,
    benutzt index_ctx ebenfalls in einem eigenen Thread). Der SeaDrive-Watcher
    (sond_seadrive.c) greift nicht auf den Index zu. Lesezugriffe stören die
    offene Transaktion nicht (SQLite serialisiert pro Connection; nicht geprüft:
    sqlite3_threadsafe() der eingesetzten Bibliothek). Wäre nur dann zu lösen, wenn
    ein UI-Schreibzugriff während eines Laufs möglich wird (z.B. nicht modaler
    Indexlauf): Mutex um die Index-Transaktionen oder eigene Connection per 1.
 3. Test nach der Umsetzung: einen Fehler mitten in einer Dual-Write-Operation
    provozieren (z.B. künstliche Constraint-Verletzung nur im zweiten Statement) und
    prüfen, dass das Rollback store, work (und die Index-DB) zurücksetzt; eine
    Dual-Write-Operation während der laufenden Indizierung auslösen und prüfen, dass
    SQLITE_BUSY sauber behandelt wird (kein Absturz, kein stiller Fehler).
```

### #234 sond_open() unter Nicht-Windows

Status: offen (niedrig)

```text
 #234 sond_open() (sond_file_helper.c) unter Nicht-Windows (niedrig):
 Zweig ruft xdg-open per g_spawn_async auf. Lücken: open_with wird
 ignoriert (Windows: "openas"); temporäre Dateien aus Containern bekommen
 die Endung nur unter __WIN32__ (sond_file_part_write_to_tmp_file()),
 sonst entscheidet xdg-open nach dem Inhalt, macOS nach der Endung; kein
 macOS ("open" statt xdg-open); Fehler von xdg-open selbst (keine
 passende Anwendung) werden nicht gemeldet. Vorschlag:
 g_app_info_launch_default_for_uri() mit g_filename_to_uri(), Endung der
 Temp-Datei auf allen Systemen anhängen, "Öffnen mit" über einen
 GtkAppChooserDialog (GUI-Code, nicht in sond_file_helper.c). Unabhängig
 vom System: die Temp-Datei wird nur bei Fehlschlag von sond_open()
 gelöscht (sond_fileparts.c), bei Erfolg bleibt sie liegen - Aufräumen
 nicht geprüft.
```

## Zurückgestellt

### #202 Dateisuche: Filter nach Endung, Größe, Datum

Status: zurückgestellt

```text
 #202 Dateisuche: Filter nach Dateiendung, Größe, Datum (zurückgestellt,
 bei Bedarf als weiteres Feld in SondTVFMSearchOpts).
```

### #220 Index löschen im UI-Thread

Status: bewußt so belassen

Reste aus dem Review (Erledigtes: HISTORY.md, Abschnitt "Review der Index-Implementation") von sond_index.c, sond_process_file.c, sond_text_extract.c,
sond_ocr.c, zond_indexsuche.c, headerbar.c (Index), zond_treeview.c (Auswahl).


```text
 #220 Index löschen läuft im UI-Thread (headerbar.c, zond_index_loeschen_...):
 bewußt so belassen. Gemessen auf einer Kopie des größten Index (23 MB, 15.519
 Chunks, 30 Dateien): 30 Dateien 1,3 s (rund 43 ms je Datei), ganzer Index
 0,9 s; hochgerechnet etwa 40 s Stillstand bei 1000 Dateien mit ähnlich vielen
 Chunks. Ohne Abbrechen-Button. Wird das im Betrieb zu langsam: Löschen in
 einen Thread mit Info-Fenster und Abbrechen (wie beim Indizieren) legen; der
 Thread benutzt index_ctx->db, das Indizieren ist währenddessen gesperrt.
 Bis dahin zeigt die Oberfläche beim Löschen einen Wartecursor (wait_cursor_set(), misc.c).
```

### #237 SeaDrive-Hydrierungsfehler: UX-Umgang

Status: zurückgestellt

```text
 SeaDrive-Hydrierungsfehler: UX-Umgang (11.09.2026, zurückgestellt).
 Anlass: s.o. ("Invalid argument"). Nutzerfrage: wie geht die App damit
 um, dass eine angeforderte Hydrierung (SeaDrive/Seafile-Server nicht
 erreichbar, oder anderer Fehler) fehlschlägt? In welchen Fällen kann
 das überhaupt auftreten? Gemeinsam begonnene, aber nicht fertig
 durchgegangene Liste der Zugriffsstellen:
 1. Doppelklick auf eine nicht hydrierte Datei (Öffnen im Viewer)
 2. Indizierung einer Datei (sond_index_erstellen, s.o.)
 3. (dritter Punkt vom Nutzer nur angerissen, nicht ausformuliert -
    offen)
 (weitere Kandidaten, aus dem Code bekannt, aber noch nicht mit dem
 Nutzer besprochen: Indexsuche-Abdeckungs-Check - s.o. größtenteils
 behoben -, "Gesamtes Projektverzeichnis"-Scans beim Laden von
 BAUM_FS-Verzeichnissen, Kopieren/Verschieben von Dateien im
 Projektbaum, PDF-Vorschau/Thumbnail-Erzeugung.)

 Technische Einschränkung: ein fehlgeschlagener Hydrierungsversuch
 äußert sich nur als generisches CRT errno=EINVAL ("Invalid argument")
 über die üblichen fread()/_wfopen()-Pfade - es gibt keinen
 unterscheidbaren Windows-Cloud-Files-API-Fehlercode, an dem man
 "Hydrierung fehlgeschlagen" zuverlässig von anderen Lesefehlern
 unterscheiden könnte. Rein reaktive Fehlerauswertung ist also
 unzuverlässig.

 Zwei denkbare Strategien, nicht entschieden, keine gegenüber der
 anderen priorisiert:
 A) Reaktiv: an den bekannten Zugriffsstellen bei Lesefehlern eine
    klarere, spezifischere Fehlermeldung anzeigen (Hinweis auf
    mögliche SeaDrive/Server-Nichterreichbarkeit), statt der rohen
    Systemfehlermeldung.
 B) Proaktiv: vor kritischen Aktionen (z.B. Indizierung) grob prüfen,
    ob der SeaDrive/Seafile-Server überhaupt erreichbar ist, und bei
    Nichterreichbarkeit vorab und gesammelt warnen, statt einzeln pro
    Datei zu scheitern. Es existiert aktuell kein Erreichbarkeits-
    Indikator im Code (das "seadrive-status"-Signal zählt nur
    pending_down/pending_up laufender Transfers, ist aber kein
    Erreichbarkeits-Flag).

 Nutzer-Entscheidung: "Merke Dir das. Stellen das zurück." - Thema
 nicht bearbeiten, bis der Nutzer es wieder aufgreift.
```

### #238 Fortschrittsanzeige beim Aufklappen in BAUM_FS

Status: zurückgestellt (Wartecursor erledigt)

Aus "Performance ZIP-Anbinden: verbleibende Silent-Freeze-Lücke" (HISTORY.md, 16.09.2026).


```text
 Wartecursor beim Aufklappen: umgesetzt und getestet (sond_treeviewfm_row_
 expanded(), wait_cursor_set_quick() ohne Ereignis-Pumpen, Zähler gegen
 verschachteltes Aufklappen). Handler, die ein Item aus dem Baum holen,
 prüfen jetzt auf NULL (Dummy-Zeile). Offen: echter Fortschrittsanzeiger
 (z.B. sich füllender Kreis, Abbrechen) über SondTVFMProgress (vorbereitet,
 an dieser Stelle nicht verdrahtet). Beim Pumpen noch zu klären: Zuklappen
 oder Schließen der gerade ladenden Zeile abfangen.
```

### #239 Löschen in BAUM_INHALT: verbleibende Kosten

Status: nicht behoben, vorerst akzeptiert

Aus "Performance Löschen (BAUM_INHALT)" (HISTORY.md, 16.09.2026).


```text
 - Tatsächliche verbleibende Kosten (Fund, NOCH NICHT behoben - Nutzer:
   "erstmal ja" heißt vorerst akzeptiert, nur dokumentiert):
   1. Pro gelöschtem Knoten laufen weiterhin mehrere einzelne DB-
      Lesequeries (s.o.: get_node/_get_baum_auswertung_copy/
      _is_file_part_copied/_get_baum_inhalt_file_from_file_part) - kein
      fsync mehr, aber die Statement-Ausführungen selbst kosten bei
      mehreren hundert Knoten × 5-6 Abfragen spürbar Zeit.
   2. Wahrscheinlich der größere Anteil: zond_tree_store_remove_node()
      (zond_tree_store.c) berechnet für JEDEN einzeln entfernten Knoten
      per zond_tree_store_get_path() dessen GtkTreePath (rekursiver
      Parent-Walk mit linearem Geschwister-Scan je Ebene) und feuert
      danach gtk_tree_model_row_deleted() - das an BAUM_INHALT hängende,
      sichtbare GtkTreeView verarbeitet dieses Signal SOFORT (Neu-
      berechnung sichtbarer Zeilen, Redraw) - einmal PRO Knoten statt
      gebündelt. Standard-GTK-Architektur (kein Bug), aber bei hunderten
      Einzel-Löschungen am sichtbaren Baum ein bekannter Performance-
      Fallstrick.

 - Möglicher weiterer Fix (vorgeschlagen, NICHT umgesetzt): Model
   während der Lösch-Schleife in zond_treeview_action_loeschen() kurz
   vom GtkTreeView abkoppeln (gtk_tree_view_set_model(view, NULL)) und
   danach wieder anhängen. row-deleted-Signale feuern weiterhin (Modell
   bleibt für andere Beobachter korrekt), aber ohne angehängten View
   verarbeitet GTK sie nicht einzeln - beim Wiederanhängen baut GTK die
   sichtbaren Zeilen einmalig neu auf statt hunderte Male inkrementell.
```

### #240 Viewer: Cache für gerenderte Seiten

Status: zurückgestellt (nicht besonders wichtig)

```text
 Bei großen PDFs bleibt vieles je Seite im Speicher, solange der Viewer
 offen ist: das gerenderte Pixbuf (pixbuf_page wird nur beim Zoomwechsel
 freigegeben, viewer_ui.c; A4 bei 100 % ca. 3-4 MB, bei 150 % gut 8 MB),
 das Thumbnail (pixbuf_thumb), die display_list (bei Scans mit dem Bild,
 mehrere MB), die geladene page und die stext_page (ca. 72 Byte je Zeichen,
 200-450 KB je Textseite). Idee: Cache, der nur die zuletzt gesehenen
 Seiten hält (letzte N oder Speichergrenze) und ältere freigibt; die
 stext_page aus der Textsuche (#235) wird dort mitbehandelt. Zu beachten:
 Render-Threads, thread-Bits in ViewerPageNew/PdfDocumentPage, erneutes
 Laden bei Bedarf, Annotationen/Highlights. Vorher an einer großen Akte
 messen.
```

### #241 Textsuche im Viewer: Silbentrennung über die Seitengrenze

Status: zurückgestellt

```text
 Ein getrenntes Wort am Seitenende ("Ver-" / "trag" auf der Folgeseite)
 wird nicht als ein Treffer gefunden (aus #235). Ursache: MuPDF setzt
 FZ_STEXT_LINE_FLAGS_JOINED nur bei einer Folgezeile auf derselben Seite
 (stext-device.c); beim Flachklopfen bleiben Strich und Zeilenumbruch
 am Seitenende stehen. Weicher Trennstrich (U+00AD) ginge ohnehin.

 Plan (ca. 3-4 Stunden):
 1. Endet die letzte Textzeile der Seite N auf einen Bindestrich und beginnt
    Seite N+1 mit einem Kleinbuchstaben, auf der Zeile JOINED setzen.
 2. Grenzsuche: iterative MuPDF-Suche (fz_new_search, fz_feed_search,
    fz_search_forwards) mit den Seiten N und N+1; nur Treffer mit Quads
    beider Seiten (fz_match_quad.seq) übernehmen. Vorprüfung auf den
    Bindestrich am Seitenende, damit Seite N+1 nur dann geladen wird.
 3. TextHit (viewer.h) um ein zweites Segment (first2/n2) für die Quads auf
    Seite N+1 erweitern; Grenztreffer gehört zur Seite N.
 4. viewer_anzeigen_text_occ(): Segment 2 mit page_act + 1 in
    pv->highlight.page[] eintragen; Crop der Seite N+1 beachten.
 5. Testdatei mit getrennten Wörtern am Seitenende (auch "E-" / "Mail",
    Gedankenstrich, weicher Trennstrich).

 Nicht abgedeckt: echte Bindestriche über die Seitengrenze ("E-Mail" mit
 Umbruch nach "E-"), da KEEP_HYPHENS am Blockende ein Zeilenende setzt.
 viewer_highlight_at_char_pos bleibt unverändert (Index-Texte sind je
 Seite getrennt).
```

## Ideen

### #167 Import fremder Projekte

Status: Idee

Aus "Offene Punkte (21.09.2026, Nutzer-Sammlung)".


```text
 #167 Import fremder Projekte in ein bestehendes Projekt: ein "Projekt"
 ist eine eigene zond_dbase (SQLite) mit eigenem knoten-Baum und
 Datei-Referenzen relativ zu einem Root-Verzeichnis. Import hieße:
 (Teil-)Baum einer Fremd-DB in die aktuelle DB übernehmen, inkl.
 abhängiger file_part-Zeilen. Zu klären: Auswahl-Granularität (ganzes
 Fremdprojekt? nur ein Teilbaum?), ID-Remapping (Fremd-IDs kollidieren
 mit eigenen), Umgang mit unterschiedlichen Root-Verzeichnissen (Dateien
 mitkopieren oder nur Pfade umschreiben?) und Konfliktbehandlung bei
 bereits vorhandenen gleichen Dateien/Pfaden. Technisch am ehesten
 verwandt mit der bestehenden zond_dbase_backup(), die aber komplette
 1:1-Kopien macht statt selektiver Teilmengen.
```

### #169 Projekt-Teilexport

Status: Idee

```text
 #169 Projekt-Teilexport: markierte Punkte eines Baums (z.B. BAUM_INHALT
 oder BAUM_AUSWERTUNG) samt zugehöriger Dateien als eigenständiges,
 kleineres Projekt exportieren. Spiegelbildlich zu #167 (Import) - beide
 bräuchten im Kern dieselbe Fähigkeit, einen Teilbaum samt abhängiger
 file_parts konsistent zu extrahieren (einmal Richtung "rein", einmal
 Richtung "raus" in eine neue zond_dbase mit eigenem Root-Verzeichnis,
 in das die betroffenen Dateien kopiert würden). Gemeinsame Infrastruktur
 für #167/#169 naheliegend.
```

### #205 odt/docx-Rendering verbessern

Status: Idee

```text
 #205 Rendering von odt/docx verbessern (Nutzer, 04.10.2026): Die Anzeige
 im Renderer (sond_renderer.c) und die Texteinheit im Export (Text aus
 sond_text_extract_odt/_docx) liefern flachen Text ohne Gestaltung.
 Prüfen, ob das besser geht (Überschriften, Listen, Tabellen, Fett/
 Kursiv, eingebettete Bilder). Was dabei entsteht, auch in den Export
 übernehmen (odt, docx und PDF-Ziel).
```

### #231 Konsistenzprüfung der Projektdatenbank (Extras)

Status: Idee

```text
 #231 Idee (13.10.2026, Nutzer): Funktion unter Extras, die eine
 Projektdatenbank auf Konsistenz prüft und ggf. korrigiert. Anlass: Reste
 in genutzten Projekten (FILE_PART-Zeilen zu Pfaden, die es nicht mehr gibt,
 doppelte Zeilen zum selben Pfad; beim Löschen einer PDF oder eines
 Verzeichnisses werden die FILE_PART-Zeilen der Anhänge bzw. Dateien darin
 nicht mitgelöscht). Unangebundene FILE_PART-Zeilen MIT
 Sections sind gewollt (BAUM_FS zeigt sie an, wiederverwendbar), nur melden,
 nicht löschen; "leere Wurzeln" ohne Sections ebenfalls nur melden.
 Ablauf: erst Bericht je Kategorie (ohne Änderung), dann je Kategorie
 "Korrigieren" mit Rückfrage; vorher Sicherungskopie der Projektdatei.
 Löschen nur bei unangebundenen Zeilen, Kaskade (link, parent_ID) beachten.
 Prüfungen, billig (nur DB): PRAGMA integrity_check/foreign_key_check;
 doppelte FILE_PART-Zeilen (gleicher file_part und section; unangebundene
 zusammenführen); Anker ohne Ziel, Copies/Links ohne Ziel; Geschwisterketten
 (older_sibling_ID/parent_ID lückenlos, ohne Zyklus); Unterschiede main/work.
 Prüfungen mit Dateizugriff (optional, mit Fortschrittsanzeige):
 FILE_PART-Zeilen, deren äußerer Pfad nicht mehr existiert; Reste innerhalb
 von Containern (z.B. emb_dup.pdf//vermddderk.txt) nur durch Öffnen des
 Containers erkennbar - bei ZIP/PDF mit Passwort und SeaDrive-Platzhaltern
 heikel/teuer. Index-DB: Chunks, Coverage, pdf_embedded zu Pfaden, die es
 nicht mehr gibt.
```

### #232 BAUM_INHALT: Anbindungen in PDF einfügen und löschen

Status: Idee

```text
 #232 BAUM_INHALT: Anbindungen in einer PDF einfügen und löschen
 (13.10.2026, Nutzer).
```

### #233 Viewer: Rummalen

Status: Idee

```text
 #233 Viewer: Rummalen (13.10.2026, Nutzer).
```
