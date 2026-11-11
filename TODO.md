# TODO

Offene Punkte für zond. Erledigtes steht in [HISTORY.md](HISTORY.md), Hinweise zum Bauen in [BUILD.md](BUILD.md).

Neue Einträge werden ab #200 nummeriert, die nächste freie Nummer ist #250.

## Inhalt

Offen: #245, #247, #249
Zurückgestellt: #202, #220, #234, #236, #238, #239, #240, #241
Ideen: #167, #169, #205, #232, #233

## Offen

### #245 Containerprüfung in der Konsistenzprüfung (Rest von #231)

Status: offen (Idee, aufwendig)

```text
 Die Prüfung auf fehlende Dateien (#231, Regel E4) prüft nur den Pfad der obersten
 Datei. Einträge innerhalb von Containern (ZIP-Dateien, E-Mails, PDF-Anhänge)
 werden nicht geprüft: ob ein Eintrag "x.zip//a.pdf" im Container noch vorhanden ist,
 zeigt erst das Öffnen des Containers.
 Vorschlag (Nutzer): zusätzliches Häkchen "Container prüfen", sichtbar, wenn die
 Dateiprüfung gewählt ist, mit Unterwahl "gesperrte öffnen".
 Aufwand/Fragen: Container müssen geöffnet werden, bei SeaDrive vorher laden (Hydrierung,
 Fortschritt und Abbruch wie bei E4); gesperrte ZIP/PDF brauchen das Passwort-Konzept
 (bisher nur die Anzeige gebaut); zuerst klären, ob die Index-DB (container_entrycount,
 pdf_embedded) das Öffnen erspart. Befunde: fehlender Eintrag im vorhandenen Container;
 Aktionen wie bei E4 (Löschen, wenn nichts darauf verweist).
```

### #247 Einfügen von Teilen in eine E-Mail (BAUM_FS)

Status: offen (nicht implementiert)

```text
 Einfügen (Kopieren oder Verschieben, auch Umsortieren innerhalb derselben
 E-Mail) in eine E-Mail scheitert in sond_file_part_insert() (sond_fileparts.c)
 mit "Einfügen in E-Mail noch nicht unterstützt". Es gibt keine Datenschäden:
 beim Kopieren bricht paste_clipboard_foreach vor der Baumänderung ab, beim
 Ausschneiden rollt zond_treeviewfm_after(FALSE) zurück.
 Bereits vorbereitet, aber heute nie erreicht: Platzhalter "alpha" und Renummerierung
 der Mimepart-Indizes (zond_treeviewfm_before_move(), gmessage_index_renumber(),
 dbase_zond_update_gmessage_index()), Nachführen der Geschwister-Pfade in
 sond_treeviewfm_paste_clipboard_foreach(). Alles ungetestet.
 Umgesetzt sind: Löschen eines Mimeparts (sond_file_part_gmessage_mod_part()),
 Umbenennen (nur Anzeigename). Nicht umgesetzt: Löschen eines ganzen
 Multipart-Verzeichnisses, Kopieren des Message-Eintrags.
 Zu tun: Einfügen in sond_file_part_insert() für GMESSAGE (MIME-Baum ändern,
 Mail zurückschreiben, Index-DB und DB-Pfade nachführen), danach die
 vorbereiteten Teile testen.
```

### #249 Pfade mit führendem Punkt im Dateiverzeichnis ausblenden

Status: offen (geplant)

```text
 Im Dateiverzeichnis (BAUM_FS) sollen Einträge, deren Name mit "." beginnt, nicht
 angezeigt werden. Betrifft vor allem .<library_name>.ZND (vom sond_server beim Anlegen
 der Akte in die Library-Wurzel gelegte Projektdatei) und .sond_index.db.
 Zu klären: Verhalten beim Öffnen, Anbinden, in der Konsistenzprüfung und beim
 Indizieren (diese Dateien werden schon heute nicht indiziert, s.
 sond_file_name_not_indexable()).
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

### #234 sond_open() unter Nicht-Windows

Status: zurückgestellt (erst nötig, wenn zond unter Linux oder macOS laufen soll)

```text
 #234 sond_open() (sond_file_helper.c) unter Nicht-Windows (niedrig):
 Zweig ruft xdg-open per g_spawn_async auf. Lücken: open_with wird
 ignoriert (Windows: "openas"); kein macOS ("open" statt xdg-open); Fehler
 von xdg-open selbst (keine passende Anwendung) werden nicht gemeldet.
 Vorschlag: g_app_info_launch_default_for_uri() mit g_filename_to_uri(),
 "Öffnen mit" über einen GtkAppChooserDialog (GUI-Code, nicht in
 sond_file_helper.c). Die Endung der Temp-Dateien auf allen Systemen und
 das Aufräumen sind erledigt (HISTORY.md, "Temp-Dateien aufräumen", #242).
```

### #236 Atomarität store/work: Index-Verbindung in mehreren Threads

Status: zurückgestellt (erst nötig, wenn ein UI-Schreibzugriff auf den Index während einer Indizierung möglich wird)

```text
 Phase 1, der Fehlerzweig beim fehlgeschlagenen Commit, die Einbindung der
 Index-DB in die Dual-Write-Transaktion und deren Fehlertests sind erledigt
 (HISTORY.md, "Atomarität store/work"). Zurückgestellt bleibt:

 Dieselbe Index-Verbindung in mehreren Threads (geringes Risiko).
    wctx (mit index_ctx, eine feste Connection) geht an den Indexier-Thread
    (headerbar.c, zond_index_lauf(): td->wctx = zond->wctx). Von den Feldern teilt er
    sich mit der UI nur index_ctx; cancel, log_func_data und ocr_mode setzt die UI
    nur vor dem Start bzw. über den Abbrechen-Button, locked liest sie erst nach dem
    Ende des Threads. UI-Schreibzugriffe mit offener Transaktion auf index_ctx->db
    (rohes BEGIN/COMMIT/ROLLBACK) gibt es nur noch in headerbar.c (Index löschen).
    Sie sind nutzergesteuert und während der Indizierung nicht erreichbar, weil das
    Info-Fenster modal ist (misc.c, info_window_open()). Übrig bleiben lesende Zugriffe
    ohne Nutzereingabe: Badge-Status beim Aktualisieren der Bäume (Root-Monitor-Timer,
    sond_treeviewfm.c) und die semantische Suche im Chat-Thread (zond_chat.c,
    benutzt index_ctx ebenfalls in einem eigenen Thread). Der SeaDrive-Watcher
    (sond_seadrive.c) greift nicht auf den Index zu. Lesezugriffe stören die
    offene Transaktion nicht (SQLite serialisiert pro Connection; nicht geprüft:
    sqlite3_threadsafe() der eingesetzten Bibliothek). Wäre nur dann zu lösen, wenn
    ein UI-Schreibzugriff während eines Laufs möglich wird (z.B. nicht modaler
    Indexlauf): Mutex um die Index-Transaktionen.
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
