# TODO

Offene Punkte für zond. Erledigtes steht in [HISTORY.md](HISTORY.md), Hinweise zum Bauen in [BUILD.md](BUILD.md).

Neue Einträge werden ab #200 nummeriert, die nächste freie Nummer ist #276.

## Inhalt

Offen: #245, #247, #256, #258
Zurückgestellt: #202, #220, #234, #236, #238, #239, #240, #241, #251, #252, #253, #259, #260, #262
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

### #256 seadrive_gtk3 ablösen

Status: offen (Voraussetzung: GTK4-Gegenstücke)

```text
 Der GTK4-Baum hat jetzt Ladedialog und pin_root (Hydrier-Threads und Lade-Auftrag liegen
 im Kern, seadrive_gtk3 nutzt sie). Zond und seadrive_gtk3 sind noch nicht umgestellt; der
 Zustandscode des GTK3-Ladedialogs ist bis dahin doppelt (Kern und seadrive_gtk3.c). Reste dort: sond_seadrive_is_offline,
 sond_treeviewfm_seadrive_item_dehydrated.
```

### #258 Kleine Altlasten

Status: offen

```text
 - 99conv/test.c samt Menü "test" und Make-Ziel test: bewusst belassen (Entscheidung 22.11.2026).
 - Stillgelegte Chat-Dateien (zond_chat, sond_chat): bewusst belassen (Entscheidung 22.11.2026).
 - Tests: für anbindung (znd/anbindung.c) und die Seitenzählung (zond/anbindung_zaehlung.c) angelegt
   (src/tests/test_anbindung.c, test_anbindung_zaehlung.c, im Makefile-Ziel test; die Zählung wird
   gegen die echte Datei und die echten Zugriffsfunktionen getestet). Für tvfm:
   test_tvfm_item.c (SondTVFMItem im Dateisystem), test_tvfm_container.c (ZIP, E-Mail, PDF mit
   Anhängen: Anlegen, Einlesen, Pfade, Umbenennen, Kopieren, Verschieben, Löschen, neues
   Verzeichnis), test_tvfm_search.c (Dateisuche), gemeinsamer Rahmen tvfm_test_support.[ch]
   (temporäres Projektverzeichnis, Eigentümer, Testdateien werden im Test erzeugt). Dokumentmodell
   des Viewers: test_zond_pdf_document.c (Öffnen über Abschnitte, Lebensdauer von Dokument und
   Abschnitten, nur teilweise initialisierte Seiten, Einfügen, Zuordnung eingefügter Seiten,
   Signale; erzeugt die PDF-Dateien selbst). Pflegelogik: test_projekt_pflege.c (Verschieben,
   Löschen, Einfügen, Abschnitte, Transaktionsende, Mimepart-Nummern und PDF-Anhänge gegen eine
   Projekt-DB und einen Index; Dateien werden selbst erzeugt). Index-Pflege des Dateisystems: test_tvfm_index_sync.c
   (Löschen, Verschieben, Einfügen gegen eine echte Index-DB). Passwortspeicher: test_sond_password.c (Ablage, Kandidaten,
   Umbenennen, Abfrage und Aktion mit nachgebautem Dialog); test_tvfm_passwort.c (Vorprüfung vor
   Kopieren und Verschieben, Entsperren von ZIP-Einträgen und PDFs mit selbst erzeugten
   geschützten Dateien). Anzeige-Entscheidungen und SeaDrive-Hilfen: test_tvfm_status.c (Texte, Badges, Indexstatus,
   Platzhalter-Erkennung; den SeaDrive-Zustand gibt es nur unter Windows, ein nicht geladener
   Platzhalter lässt sich dort nicht erzeugen und kommt in den Badge-Tests von einer
   nachgebauten Quelle). Unter Windows braucht der Test zusätzlich seadrive/sond_seadrive_core.c; das
   Makefile-Ziel test ist der Linux-Build und enthält es nicht.
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

### #251 g_chdir(root) in sond_treeviewfm_set_root() entbehrlich machen

Status: zurückgestellt (Prüfauftrag)

```text
 sond_treeviewfm_set_root() setzt das Arbeitsverzeichnis des Prozesses auf die
 Projektwurzel (g_chdir). Damit "funktionierten" projektrelative Pfade, die ohne
 vorangestellte Wurzel an Betriebssystem-Funktionen gingen. Die GTK4-Fassung
 (src/gtk4/sond_treeviewfm.c) setzt es bewusst nicht (prozessweit, nicht
 threadsicher, mehrere Bäume/Projekte nebeneinander nicht möglich); die Item-Schicht
 (sond_tvfm_item.c: Verzeichnis anlegen, Verzeichnis umbenennen) und
 sond_file_part_rename() arbeiten dafür inzwischen mit absoluten Pfaden.

 Zu klären, bevor das g_chdir in der GTK3-Fassung entfallen kann:
 1. Alle Zond-Dateien auf OS-Aufrufe (g_stat, sond_fopen, g_rename, ...) mit
    projektrelativen Pfaden ohne Wurzel/path_root durchsehen (zond_treeviewfm.c,
    Export, Import, Verwendung; zond_convert.c hat ein eigenes g_chdir(project_dir)).
 2. Dateidialoge: misc.c nennt das Arbeitsverzeichnis als Ursache falscher
    Startverzeichnisse (start_path wird schon übergeben); prüfen, ob etwas darauf baut.
 3. Testfassung ohne g_chdir: Dateibaum, Anbinden, Export, Import, Viewer, Indizieren
    durchgehen. Nutzen: Long-Path-Schutz auch für bisher relative Pfade.
 Nicht eilig; Entfernen nur nach der Prüfung.
```

### #252 GTK4-Dateibaum: noch fehlende Funktionen

Status: zurückgestellt (alles gebaut; Punkt 4 noch ungetestet)

```text
 Der GTK4-Dateibaum (src/gtk4/sond_treeviewfm.c) kann Wurzel setzen, Anzeige mit
 Overlays (Offline, Ordnerstatus, Index), Neues Verzeichnis, Umbenennen, Löschen,
 Öffnen, Kopieren, Ausschneiden, Einfügen, Drag-and-drop, Pinnen, SeaDrive-Watcher
 und Root-Monitor. Es fehlt gegenüber der GTK3-Fassung:
 1. Dateisuche: Kern GTK-frei (tvfm/sond_tvfm_search.c, GTK3 getestet). GTK4:
    sond_treeviewfm_search.c (Dialog, Ergebnisfenster, Treffer im Baum zeigen) und
    sond_busy_dialog.c (Fortschritt mit Abbrechen); getestet. Aufklappen eines nicht
    geladenen Containers lädt jetzt über den eigenen Ladedialog (sond_treeviewfm_lazy.c).
 2. Container (ZIP, PDF, E-Mail) als Quelle und Ziel: Öffnen von Containereinträgen samt
    Entsperren vorab ist da (Stufe 1, getestet). Kopieren, Verschieben und Einfügen mit
    Containern (Quellen und Ziele ZIP/PDF, Passwort-Vorprüfung tvfm/sond_tvfm_preflight.c,
    Einfügen am Ort der Zielzeile, PDF ohne Anhänge wird Container, letzter Anhang entfernt
    macht die PDF wieder zum Blatt, Mimeparts rücken nach) ist da (Stufe 3, getestet).
    E-Mail als Ziel bleibt gesperrt, s. #247.
 3. "Dauerhaft entsperren" (Passwortschutz von ZIP/PDF entfernen): der Kern ist jetzt
    GTK-frei (tvfm/sond_tvfm_unlock.c) und im Client eingebunden (Menü im Dokumentenfenster,
    Passwortdialog sond_client/sond_password_dialog.c, Nachlauf nach dem Index-Job).
    Menüpunkt im GTK4-Baum selbst (gtk4/sond_treeviewfm_unlock.c, auch für Einträge in
    Containern; Schreibprüfung über sond_treeviewfm_set_write_check, Vfunc file_part_busy)
    getestet; das Dokumentenfenster nutzt ihn statt eines eigenen Menüpunkts.
 4. Gleichwertigkeit der Basis mit der GTK3-Fassung (gebaut, ungetestet): Vfunc get_eingang
    (Spalte "Eingang" nur, wenn eine Unterklasse es setzt; sortierbar, ohne Angabe gilt die
    Änderungszeit) und sond_treeviewfm_eingang_refresh(); sond_treeviewfm_filepart_visible()
    (mit/ohne Aufklappen); sond_treeviewfm_search() ohne Dialog; Wartecursor beim Lesen der
    Kinder von Containern (ohne Pumpen, ob die Sanduhr erscheint, hängt vom Fenstersystem ab);
    Hooks text_from_section, deter_background (Hervorhebung per CSS-Klasse "sond-hit") und
    text_edited (Umbenennen, Standard fm_default_text_edited). Nicht übernommen: klassenweite
    Zwischenablage über mehrere Bäume (GTK4: nur dieselbe Ansicht); öffentliche Hilfen
    item_fs_path/file_part_indexable (sond_tvfm_item_fs_path ist in tvfm/ öffentlich). Den Inhalt
    (Zettel, Cache, Menü Eingang, Abschnitte) liefert eine Unterklasse, s. #259.
 5. Öffnen einer nicht geladenen SeaDrive-Datei mit Fortschrittsdialog und Abbrechen und
    Vorab-Laden beim Kopieren: gebaut (gtk4/sond_seadrive_load_dialog.c auf dem GTK-freien
    Lade-Auftrag in seadrive/sond_seadrive_core.c, tvfm/sond_tvfm_load.c), getestet.
    Passwörter vorab abfragen: erledigt mit Punkt 2 (tvfm/sond_tvfm_preflight.c).
 Der Drop-Ziel-Teil (Anbinden im Inhaltsbaum von zond) gehört nicht ins Dateibaum-Widget,
 s. #259; die Quellseite (Nutzlast beim Ziehen: GPtrArray aus SondTVFMItem, Strg unter
 Windows nur über GetAsyncKeyState lesbar) ist fertig und getestet.
```

### #253 Absturz beim Schließen eines Projekts (nicht reproduziert)

Status: beobachten

```text
 Einmal SIGSEGV beim Projektschließen in zond_dbase_finalize (sqlite3_close, 
 zond_dbase.c) bei einem Projekt im SeaDrive-Laufwerk, kurz nach dem Umbau des
 Watchers (sond_seadrive_watcher). Danach nicht mehr aufgetreten; ein Stresstest des
 Watchers allein (300 Durchläufe, Stopp während des Scans) lief sauber. Ungeklärt, ob
 der Watcher beteiligt war. Tritt es wieder auf: vollständige Stack-Frames sichern und
 prüfen, ob es auch bei einem Projekt außerhalb von SeaDrive (ohne Watcher) passiert.

 Zweite Beobachtung (16.11.2026): SIGSEGV in sqlite3_close der Store-Datenbank
 (project_close -> project_clear_dbase_zond -> zond_dbase_finalize), Projekt auf einem
 SMB-Share (nicht SeaDrive, also kein Watcher). Es war das erste Projekt, das in dieser
 Sitzung geöffnet wurde, davor war ein anderes Projekt geschlossen worden. Nicht
 reproduzierbar. Gemeinsam mit dem ersten Fall: Store liegt auf einem Netzlaufwerk.
 Beim nächsten Auftreten festhalten: Netzlaufwerk ja/nein, wievieltes Schließen in der
 Sitzung, Modulname der obersten Frames (sqlite-DLL?), vorher ZIP-/Mail-Aktionen.

 Dritte und vierte Beobachtung (20.11.2026): zweimal derselbe Stack (libsqlite3-0.dll,
 zond_dbase_finalize -> dbase_zond_free -> project_close), Projekt Test.ZND auf der LOKALEN
 Platte (kein Netzlaufwerk, kein SeaDrive). Beide Male nach Dateioperationen im Baum (Verschieben
 mit Index) und Schließen per Fensterkreuz. Gleiches Szenario danach mehrfach sauber (auch mit
 Diagnose), im Test (Projektkopie, work und Index angehängt, Verschieben, Commit, Speichern,
 Schließen) nicht reproduzierbar. Also kein Netzlaufwerk-Problem und kein Watcher.
 Debug-Builds geben beim Schließen jeder Verbindung "[DIAG]"-Zeilen auf stderr aus
 (angehängte Schemas, offene Statements, autocommit, "schliesse"/"geschlossen"): bei einem
 Absturz die letzten Zeilen sichern - fehlt "geschlossen", war es sqlite3_close.
```

### #259 GTK4-zond: Inhalt für den Dateibaum (Eingang, Anbinden)

Status: zurückgestellt (erst mit der Umstellung von zond auf GTK4)

```text
 Was nicht ins Dateibaum-Widget gehört, sondern in die zond-Unterklasse und den Inhaltsbaum
 eines GTK4-zond:
 1. Unterklasse zond_treeviewfm (GTK4): setzt get_eingang (Cache sond_eingang_cache,
    Zettel sond_eingang_zettel), fs_entry_deleted/_renamed/_moved (zond_eingang_*),
    Menü "Eingang" (Setzen, Entfernen, Neu einlesen), die Signale before-move, before-insert,
    before-delete, after (Transaktion mit work-/store-DB und Index, Prüfung beider DBs),
    Vfunc file_part_busy (PDF im Viewer geöffnet), has_sections/load_sections/delete_section,
    open_item mit dem Viewer, Index-Kontext.
 2. Drop-Ziel im Inhaltsbaum (BAUM_INHALT): nimmt die Nutzlast GPtrArray aus SondTVFMItem an
    und legt Anbindungen an. Gehört zu zond_treeview/zond_tree_store (noch GTK3).
 3. GTK-freies Modul src/znd/sond_projekt_pflege (Verweise in store/work, Index in der
    Transaktion, Mimepart-Renummerierung, Anhang-Adressen, Struktur, Verschieben, Kopieren,
    Löschen, Transaktionsrahmen tx_begin/tx_ende mit Revert und Fehlerdatei): aus zond_treeviewfm.c
    herausgelöst, die Abläufe before_delete/_move/_insert und after rufen sie nur noch auf (Anzeige,
    Projekt schließen und Programmende bleiben in der Anwendung). Die Nachführung der
    Eingangsangaben (Umbenennen, Verschieben, Kopieren, Löschen) liegt GTK-frei in
    src/sond_eingang_pflege; zond_eingang.c behält Auswahl, Dialog und Meldungen. Die Zugriffe
    auf Anbindungen und Abschnitte (Text, ID, Kinder, Löschen, Volltreffer) laufen über
    sond_projekt_pflege_section_*/_sections/_anbindung_volltreffer auf der Primärdatenbank
    (dbase_zond_primary(): work in zond, store im Client); zond_treeviewfm.c hält nur noch die
    Anzeige. Die Pflegelogik ist damit aus zond_treeviewfm.c herausgelöst. Der Client
    (sond_dokumente.c) hält die Projektdatei lesend (zond_dbase_new_readonly, Neuöffnen bei
    Änderung) und führt Löschen, Verschieben und Kopieren im Dateibaum über sond_projekt_pflege
    nach (Projektdatei kurz schreibend, Index in derselben Transaktion; angebundene Einträge
    sind nicht löschbar; hat zond das Projekt offen, wird verweigert; ohne Projektdatei bleibt es
    beim Index). Die GTK4-Klasse SondProjektTreeviewFM (src/gtk4, unter SondTreeviewFM) kapselt
    das für Client und später ein GTK4-zond: Anbindungen und Abschnitte im Baum (Anzeige,
    Umbenennen, Löschen, Auflösen), Schreibpfad über sond_projekt_pflege (Hooks permit,
    write_begin/_end, critical, tx_done), read_only, Überschreibschutz beim Verschieben, Eingang
    (Spalte, Menü Setzen/Entfernen/Neu einlesen, Dialog sond_eingang_dialog, Nachführen bei
    Umbenennen/Verschieben/Löschen; Nachlesen neuer Zettel per Signal root-entries-added des
    Wurzel-Monitors, Konvention: Importe nur auf der Wurzel-Ebene). zond und Dokumentenfenster
    einer Akte laufen nie gleichzeitig: zond im Akten-Modus (--akte-projekt, eigene Instanz,
    Schließen beendet zond), der Client verfolgt die zond-PID (sond_zond_prozess, State-Datei)
    und sperrt gegenseitig. Die Takte für Projekt-DB und Index im Dokumentenfenster bleiben
    (Änderungen kommen per Synchronisierung). Offen: Index-Menü im Client, Öffnen von
    Abschnitten mit Seitensprung (zond-Viewer im Client), echter Schreibschutz in zond.
    Renderer: das Rendern (sond_render_doc.[ch], ohne Widgets) ist vom Viewer-Fenster
    (sond_renderer.c) getrennt; das Fenster läuft in GTK3 und GTK4 aus einer Quelle über die
    Wrapper in sond_gtk_compat.[ch]. sond_renderer_open_file_part() (vorher sond_file_part_open)
    öffnet Text, Bild, DOCX/ODT und E-Mail im eigenen Viewer (sond_renderer_can_render), sonst
    extern; sond_file_part_open_external liegt wieder in sond_fileparts.c. Der GTK4-Baum nutzt das:
    "Öffnen" intern, "Öffnen mit" extern. Inhaltsbäume, PDF-Viewer, Headerbar und Export für
    GTK4 sind noch ungeplant.
```

### #260 Inhalts- und Auswertungsbaum: GTK4-Baum und Notizen auf GTK-freier Logik

Status: Phase 1 erledigt; Phasen 2 bis 5 zurückgestellt (erst mit der Umstellung von zond auf GTK4)

```text
 Ziel: Inhalts-/Auswertungsbaum und Notizen später als GTK4-Widgets in einem GTK4-zond.
 Phase 1 (erledigt): Die Baum-Logik ist aus src/zond/zond_treeview.c GTK-frei nach
 src/zond/zond_knoten.[ch] gelöst (Arbeit auf node_id und Anzeige-ID statt GtkTreeIter);
 zond_treeview.c führt nur noch die Anzeige nach. Im Modul: Lesen (get_children ebenenweise,
 get_root), Anker prüfen, neuer Punkt, Verschieben, Löschprüfung, extern verlinkt, Auflösen
 (Anbindung entfernen), Kopieren in die Auswertung (vorbereiten, anlegen), neuer Link,
 Anbinden (Datei anlegen, untergeordnete Teile auflösen, Anker einfügen). Bei Verschieben und
 Löschen behoben: Strukturpunkt mit als Copy angebundenem Kind wurde samt Kind gelöscht, jetzt
 bleibt er stehen. Die Rekursion beim Anbinden (Container, Passwörter, SeaDrive, Info-Fenster,
 Abbrechen) und die Zulässigkeitsregeln des Einfügens (aktiver Baum, Clipboard) hängen am
 Zustand der Oberfläche und bleiben in zond_treeview.c. Die Notizen brauchen nichts: Lesen und
 Schreiben sind zond_dbase_get_text/_update_text (GTK-frei, in znd/).
 Offen (noch nicht angegangen): Phase 2 Knotenmodell GTK4 (ZondNode, create_children über
 zond_knoten, Links über eine Registry node_id), Phase 3 GTK4-Baum auf SondTreeview (Menü,
 Drag-and-Drop, Clipboard, Hooks), Phase 4 Notizen-Widget (GtkTextView mit Pin und Sprung),
 Phase 5 Testfenster. Engpass für ein GTK4-zond in einem Prozess bleibt der PDF-Viewer (GTK3,
 rund 7500 Zeilen mit modalen Dialogen).
 Ort: zond/, nicht znd/ (der Client zeigt diese Bäume nicht; wer so tief einsteigen will,
 öffnet zond).
```

### #262 Viewer als einbettbares Widget

Status: zurückgestellt (Vorarbeit: viewer_einrichten_fenster ist in Hilfsfunktionen zerlegt)

```text
 Rest aus #265: Annot-Helfer (annot_deep_copy, annot_free) von zond_pdf_document.c in ein eigenes
 Modul, die Typedefs aus viewer.h, PdfViewer in Zustand und Widgets teilen, Verzeichnis 40viewer
 aus zond/ heraus.
 Ziel: Der Viewer soll wahlweise in ein Toplevel-Fenster oder anderswo eingebettet werden
 können. Heute ist pv->vf ein GtkWindow und zugleich Kern und Hülle.
 Hindernisse:
 1. pv->vf wird an rund 45 Stellen als Elternfenster benutzt (display_message, Dialoge,
    Dateiauswahl, Druckdialog, info_window_open, gtk_window_present, gtk_application_add_window).
    Als Widget: Toplevel über gtk_widget_get_toplevel, bzw. ein Helfer viewer_parent(pv).
 2. Die Kopfzeile ist die Titelleiste des Fensters (gtk_window_set_titlebar), Seiten- und
    Suchfeld liegen darin. Im Widget: eigene Leiste, die der Embedder auch in seine
    Titelleiste setzen kann.
 3. Tastenkürzel (Strg+C, Strg+X) hängen an der AccelGroup des Fensters; im Widget über
    Controller/Shortcuts.
 4. Cursor über pv->gdk_window (GDK-Fenster des Toplevels, viewer_mouse.c, viewer_ui.c);
    besser vom Layout (GTK3: gtk_widget_get_window(layout), GTK4: gtk_widget_set_cursor).
 5. Fenstersignale delete-event (Schließen mit Speichern) und motion-notify-event gehören in
    die Hülle bzw. auf das Layout; der Kern bietet stattdessen "darf geschlossen werden?"
    (viewer_save_and_close ist vorhanden, muss nur vom Fenster gelöst werden).
 6. Lebenszyklus: gtk_widget_destroy(pv->vf) in viewer.c und der Weak-Pointer args->vf in
    viewer.c hängen am Fenster; besser an der Session.
 Schritte: A Hauptbereich als Container liefern, Fenster nur Hülle; C Kopfzeile als Leiste des
 Widgets, Kürzel über Controller, Cursor vom Layout, Schließen als Anfrage nach außen;
 B pv->vf in pv->widget (Kern) und pv->fenster (optional) teilen, Elternfenster über Helfer;
 D (Callbacks von arr_pv auf Signale, Stufe 2 in #257) ist erledigt (HISTORY.md). Empfehlung:
 A und C, dann B.
 GTK4: GtkLayout, show_all und Container-API fehlen dort; die Zeichenfläche müsste ersetzt
 werden (der GTK4-Viewer im Client macht das bereits eigenständig). Siehe auch die Idee
 viewer_host GTK-frei.
 Verzeichnis: Der Viewer (zond/40viewer) baut auch als eigenständige Anwendung (make viewer) und
 hängt nur an zond/zond_pdf_document.[ch] und znd/, nicht an zond selbst. Beim Schritt B
 zond_pdf_document und 40viewer zusammen aus zond/ herauslösen (z. B. nach src/viewer/);
 viewer_host bleibt in zond/ als zond-Seite der Schnittstelle. Dann Makefile (Ziel viewer und
 zond) und rund 40 Includes anpassen. Früher lohnt der Umzug nicht, solange zond_pdf_document
 in zond/ bleibt.
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
