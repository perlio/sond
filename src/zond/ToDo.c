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

 Ältere Bugfix-Historie (Code-Reviews 23.08.-29.08.2026, insgesamt vier
 Runden, alle Funde behoben bzw. geprüft/bewusst nicht behoben) wurde nach
 dem Commit vom 29.08.2026 aus dieser Datei entfernt, um sie schlank zu
 halten - Details zu jedem einzelnen Fix stehen dauerhaft in der jeweiligen
 Commit-Message bzw. im Diff (git log). Dasselbe Prinzip gilt seit
 12.09.2026 auch für den unten folgenden Abschnitt zur SeaDrive-/Index-
 Historie (06.-11.09.2026): vollständig umgesetzte Punkte sind auf kurze,
 datierte Zusammenfassungen eingedampft, die mechanische Detailbeschreibung
 lebt jetzt in den Doc-Kommentaren der jeweiligen Header (sond_index.h,
 sond_icon_util.h, sond_treeviewfm.h, zond_treeview.h, general.h,
 sond_text_extract.h) bzw. im Diff/git log - nur genuin noch offene Punkte
 und Entscheidungen/Verwerfungen, die sich nicht aus dem Code selbst
 ergeben, wurden ausführlicher belassen.

 Architektur-Plan: Atomarität store/work bei den Dual-Write-Stellen
 (24.08.2026 aufgesetzt; Phase 1 - Punkte 1.-5. - am 02.09.2026 UMGESETZT,
 s. Doc-Kommentare in project.c bei dbase_zond_begin()/_commit()/
 _rollback() und vor dbase_zond_update_section_schema(); Phase 2 - Punkte
 6./7., plus die separat notierten Punkte 8./9. - weiterhin OFFEN,
 zurückgestellt. 12.09.2026 richtiggestellt, nachdem im Code verifiziert:
 vorher stand hier fälschlich "insgesamt noch nicht umgesetzt"):

 Betroffene Stellen (Stand 24.08.2026, ergänzt 02.09.2026, per grep
 verifiziert):
 - viewer_save.c, viewer_save_dirty_dds() (~Zeile 604-647): dbase_zond_begin/
   commit/rollback um dbase_zond_update_sections() (Seiten löschen/einfügen).
 - zond_treeviewfm.c, zond_treeviewfm_before_move()/_after() (~Zeile 215-369):
   dbase_zond_begin/commit/rollback um dbase_zond_update_path() und
   mehrfach dbase_zond_update_gmessage_index() (Datei/Verzeichnis umbenennen/
   verschieben, inkl. GMessage-Sonderfälle). ZUSÄTZLICH dort eine dritte,
   unabhängige Transaktion auf index_ctx->db (FTS-Suchindex, eigene
   sqlite3-Verbindung, eigenes rohes sqlite3_exec("BEGIN/COMMIT/ROLLBACK")),
   die bislang (Phase 2, s.u.) nicht mit der store/work-Transaktion
   verklammert ist.
 - zond_treeviewfm.c, zond_treeviewfm_before_delete() (~Zeile 236-260):
   dual_write dort BEDINGT (nur wenn from_gmessage) - dbase_zond_begin/
   commit/rollback um dbase_zond_update_gmessage_index() beim Löschen eines
   Elements aus einer GMessage/E-Mail-Anhangsstruktur (nachfolgende
   Geschwister-Indizes müssen in beiden DBs neu nummeriert werden). Beim
   normalen Löschen (nicht aus GMessage) reiner Single-Write auf work,
   nicht betroffen.

 Problem (Ausgangslage vor Phase 1, für Phase 2 - index_ctx - unverändert
 relevant): store und work sind zwei unabhängige sqlite3-Verbindungen; die
 obigen Stellen schrieben sequenziell in beide (dbase_zond_begin/commit/
 rollback, project.c). Schlägt der zweite Commit nach erfolgreichem ersten
 fehl, oder das Rollback selbst, entsteht potenziell ein inkonsistenter
 Zustand zwischen store und work - keine echte Atomarität über beide
 Dateien. Für index_ctx (separate dritte Transaktion, s.o.) besteht dieses
 Problem unverändert fort, bis Phase 2 (6./7.) umgesetzt ist.

 Plan (abschichtbar in zwei Phasen, s. Begründung in 6.b unten,
 Frage/Antwort 03.09.2026):

 Phase 1 - Punkte 1.-5.: store/work atomar machen, unabhängig von
 index_ctx, in sich abgeschlossen umsetzbar. UMGESETZT 02.09.2026.
 Phase 2 - Punkte 6./7.: index_ctx-Anbindung (Entscheidung s.u.), kann
 zeitlich beliebig später erfolgen, ohne Phase 1 nochmal anzufassen. OFFEN.

 Geprüfte und verworfene Alternative (03.09.2026): komplett auf EINE
 Connection für store+work umstellen (work als "main", store nur noch
 als angehängtes Schema, oder umgekehrt), statt wie umgesetzt work's
 eigene Connection für die ~80 Einzel-DB-Funktionen unangetastet zu
 lassen und nur für die paar Dual-Use-Funktionen zusätzlich anzuhängen.
 Verworfen, aus mehreren Gründen:
 - Es sind nicht nur die 3 _update-Funktionen (Punkt 3), die beide DBs
   brauchen - mind. 3 weitere Stellen fragen lesend BEIDE Schemata
   unabhängig voneinander ab, mit unterschiedlicher Behandlung je
   nachdem wo der Treffer liegt: zond_treeviewfm.c Zeile 159/180 (vor
   Löschen: erst work, dann store geprüft, bei Treffer in store eigene
   Fehlermeldung "bitte zuerst speichern") und seiten.c Zeile 937
   (seiten_anbindung_int, dieselbe Zwei-Pass-Prüfung). "Die meisten nur
   auf work umstellen" trifft also nicht zu.
 - Hauptgrund: alle ~80 zond_dbase_*-Funktionen arbeiten mit fest
   verdrahteten UNQUALIFIZIERTEN SQL-Strings ("... FROM knoten",
   "UPDATE knoten SET ..."). Bei einer gemeinsamen Connection löst sich
   ein unqualifizierter Tabellenname immer gegen "main" auf - jede
   dieser ~80 Funktionen müsste also schemabewusst gemacht werden, nicht
   nur umbenannt. Risiko: eine übersehene Stelle liest/schreibt still
   das falsche Schema (stiller Datenfehler statt Absturz).
 - sqlite3_update_hook() ist bewusst nur auf works eigener Connection
   registriert, damit Dual-Write-Zugriffe (über die an store angehängte
   work-Verbindung) den "changed"-Hook NICHT auslösen (s. Punkt 2). Bei
   einer gemeinsamen Connection fiele diese Unterscheidung weg (Hook
   bekäme für Single- wie Dual-Write auf work gleichermaßen zDb="work"
   gemeldet) - bräuchte ein zusätzliches Flag zur Unterscheidung. Dieser
   Punkt war für die Entscheidung ausschlaggebend.
 - zond_dbase_backup() (kompletter Kopiervorgang store<->work bei
   Öffnen/Speichern, project.c) nutzt die SQLite-Online-Backup-API
   zwischen zwei unabhängigen Connections. Ob das zwischen zwei Schemata
   EINER Connection ebenso funktioniert, ist in der offiziellen SQLite-
   Doku (sqlite.org/backup.html) nicht dokumentiert - weder bestätigt
   noch ausgeschlossen, wäre also ungetestetes Neuland.
 Ergebnis: technisch nicht unmöglich, aber Umfang und Risiko (praktisch
 alle ~80 Funktionen anfassen, stille Fehlrouting-Gefahr) stehen in
 keinem guten Verhältnis zum Nutzen (eine Connection weniger). Bei der
 umgesetzten, chirurgischen Lösung (work-Connection unangetastet, ATTACH
 nur für die Dual-Use-Funktionen) geblieben.

 Punkte 1.-5. (Phase 1, UMGESETZT 02.09.2026 - Mechanik/Details jetzt in
 den Doc-Kommentaren der genannten Funktionen, nicht mehr hier ausgeführt):
 1. journal_mode/synchronous-Sicherung vor jedem Öffnen des Projekts UND
    vor jeder Dual-Write-Transaktion (zond_dbase_check_journal_settings(),
    aufgerufen aus dbase_zond_begin() für BEIDE Dateien) - lehnt die
    Operation mit klarer Fehlermeldung ab, wenn journal_mode auf
    WAL/MEMORY/OFF oder synchronous auf OFF steht (verhindert sonst den
    für ATTACH-Transaktionen nötigen SQLite-Super-Journal-Mechanismus,
    s. sqlite.org/atomiccommit.html).
 2. work wird beim Öffnen des Projekts (project_create_dbase_zond()) per
    "ATTACH DATABASE ... AS work;" zusätzlich als zweites Schema an die
    store-Connection gehängt - work behält daneben unverändert seine
    eigene Connection für alle ~80 Einzel-DB-Funktionen. Nebeneffekt:
    sqlite3_update_hook() (nur auf works eigener Connection registriert)
    feuert für Dual-Writes über die attachte Verbindung nicht mehr -
    die frühere Sicherungs-/Rücksetzungs-Logik für "changed" in
    viewer_save_dirty_dds()/zond_treeviewfm_before_move()/_after() wurde
    dadurch überflüssig und entfernt.
 3. dbase_zond_update_sections()/update_path()/update_gmessage_index()
    (project.c) schreiben jetzt schemaqualifiziert ("work.tabelle" /
    unqualifiziert für store) auf der store-mit-attachtem-work-
    Verbindung, innerhalb einer Transaktion - statt zweimal dieselbe
    Einzel-DB-Funktion auf getrennten ZondDBase-Objekten aufzurufen.
 4. dbase_zond_begin/commit/rollback (project.c) laufen jetzt nur noch
    mit einem einzigen BEGIN/COMMIT/ROLLBACK auf der einen
    store-mit-work-Verbindung, statt einer Schleife über zwei Objekte.
 5. Die Rollback-Fehlerbehandlung in dbase_zond_rollback() entsprechend
    vereinfacht (nur noch ein lokaler error_int für den einen
    Rollback-Aufruf, an ein schon gesetztes *error angehängt statt
    zwischen zwei error_int gemergt - direktes Durchreichen auf *error
    bewusst vermieden, s. Kommentar dort, würde sonst den eigentlichen
    Fehlergrund überschreiben können).

 6. Entscheidung index_ctx (02.09.2026 getroffen, Umsetzung = Phase 2,
    weiterhin OFFEN, zwei getrennte Fragen):

    a) Index-DB dauerhaft in die Projekt-DB integrieren (store/work)?
       NEIN. Indizierung (neue Dateien einlesen, Embeddings berechnen)
       läuft auf einem eigenen Hintergrund-Thread (headerbar.c Zeile 228,
       g_thread_new("ocr-doc", do_index_thread, ...)) mit eigener
       SQLite-Connection - eine dauerhafte Verschmelzung mit store/work
       würde diese Trennung aufbrechen. Außerdem ist der Index ein
       abgeleitetes Artefakt (kann im Zweifel neu aufgebaut werden),
       anders als store/work (Primärdaten) - eine geringere
       Fehlertoleranz-Anforderung, die durch Verschmelzung verloren ginge
       (aktuell lässt z.B. ein fehlendes Embedding-Modell die Indizierung
       bewusst nicht scheitern, s. sond_index_ctx_new()).

    b) index_ctx NUR für die kurze Dual-Write-Transaktion (Move/Delete-
       Coverage-Invalidierung) per ATTACH mit in die store+work-Connection
       aufnehmen (drittes Schema), ansonsten bleibt index_ctx's eigene
       Connection für den Hintergrund-Thread unangetastet? JA, und zwar
       als dynamisches ATTACH/DETACH lokal in
       zond_treeviewfm_before_move()/_before_delete() (ATTACH unmittelbar
       vor, DETACH unmittelbar nach der jeweiligen Transaktion) - NICHT
       als dauerhaftes ATTACH beim Verbindungsaufbau wie work in 2.
       (index_ctx braucht weiterhin seine eigene dauerhafte Connection für
       den Hintergrund-Thread, s. 6.a). FTS5-Tabellen sind normale
       Shadow-Tables und vom Super-Journal-Mechanismus mit abgedeckt, kein
       grundsätzliches Hindernis.
       Abschichtbar (Frage/Antwort 03.09.2026): so umgesetzt betrifft
       6.b) NUR den bestehenden separaten Transaktionsblock auf
       index_ctx->db in zond_treeviewfm.c (Stand 12.09.2026 unverändert:
       zwei rohe sqlite3_exec(index_ctx->db, "BEGIN;") in
       zond_treeviewfm_before_move()/_before_insert()), NICHT
       dbase_zond_begin/commit/rollback (4.) und NICHT die
       SQL-Umschreibung in 3. - deshalb konnte store/work-Atomarität
       (1.-5.) unabhängig als Phase 1 vorgezogen werden. In der
       Zwischenzeit (Phase 1 umgesetzt, Phase 2 noch offen) bleibt
       index_ctx wie bisher eine separate, eigene Transaktion neben der
       jetzt schon atomaren store+work-Transaktion - und das in 7.
       beschriebene Cross-Thread-Risiko besteht unverändert fort, bis
       6.b) umgesetzt ist.
       Voraussetzung für 6.b) selbst: SQLITE_BUSY behandeln - läuft der
       Hintergrund-Thread gerade eine offene Schreibtransaktion auf
       index_ctx's eigener Connection, bekommt der ATTACH-Schreibversuch
       der UI-Transaktion SQLITE_BUSY (zwei getrennte Connections auf
       dieselbe Datei, siehe 7.) - busy_timeout setzen und/oder Retry,
       sonst klare Fehlermeldung an den Anwender ("Indizierung läuft,
       bitte kurz warten").

 7. Separat notiert, unabhängig von 6.b) beim Nachdenken darüber gefunden
    (weiterhin offenes Risiko, per Grep am 12.09.2026 erneut bestätigt):
    zond->wctx (inkl. wctx->index_ctx, EINE feste SQLite-Connection) wird
    beim Start der Indizierung 1:1 an den Hintergrund-Thread durchgereicht
    (headerbar.c Zeile 222: td->wctx = zond->wctx, kurz vor g_thread_new(...)).
    Dieselbe Connection wird aber auch synchron von der UI aus benutzt:
    zond_treeviewfm_before_move()/_before_delete() schreiben per rohem
    sqlite3_exec(priv->zond->wctx->index_ctx->db, "BEGIN/COMMIT/ROLLBACK")
    direkt auf dieselbe Connection. D.h. schon HEUTE (unabhängig von 6.b)
    könnte ein Datei-Move/-Delete während laufender Hintergrund-
    Indizierung dieselbe Connection von zwei Threads aus ansprechen.
    SQLite serialisiert Zugriffe auf eine Connection zwar intern (kein
    Crash), aber falls der Hintergrund-Thread mitten in einer offenen
    Transaktion mit Lücken zwischen den Statements steckt (Embedding-
    Berechnung kann pro Chunk dauern), könnte sich das UI-BEGIN/COMMIT in
    die noch offene Hintergrund-Transaktion einmischen - unklare
    Reihenfolge, ein ROLLBACK der einen Seite könnte Arbeit der anderen
    mit wegwerfen. Mit 6.b) umgesetzt würde dieses bestehende Problem
    tendenziell sogar entschärft (UI-Schreibzugriffe liefen dann über
    eine eigene, angehängte Connection statt über die geteilte
    index_ctx-Connection - nur noch normale dateibasierte SQLite-Sperren,
    klar behandelbar mit busy_timeout, statt unklarem Cross-Thread-
    Interleaving). Bis 6.b) umgesetzt ist, bleibt es ein offenes Risiko.

 8. UMGESETZT (12.09.2026): zond_treeviewfm.c, zond_treeviewfm_after() -
    das bisherige exit(EXIT_FAILURE) bei fehlgeschlagenem
    dbase_zond_commit() (dual_write-Zweig) ist ersetzt durch:
    - Bei physischer Umbenennung/Verschiebung (pending_move_path_old/
      _new, gesetzt in before_move()): Revert-Versuch per sond_rename()
      mit vertauschten Pfaden - deckt sowohl reinen Rename- als auch
      Move(Kopieren+Löschen)-Fall ab, da das Dateisystem danach gleich
      aussieht. Erfolg -> normaler Fehlerdialog ("bitte erneut
      versuchen"), kein exit, normaler Weiterbetrieb.
    - Schlägt der Revert fehl (oder war keiner möglich, z.B. reine
      GMessage-Index-Renumerierung ohne physische Aktion): neue Funktion
      write_commit_failure_report() schreibt eine für den Anwender
      lesbare Klartextdatei (ZOND_FEHLER_<Zeitstempel>.txt) ins
      Projektverzeichnis mit altem/neuem Pfad und beiden Fehlermeldungen
      - einziger dauerhafter Anhaltspunkt, da project_close() die
      Arbeitskopie am Ende aufräumt. Kritischer Fehlerdialog verweist auf
      diese Datei.
    - project_close() wird in einer Schleife aufgerufen, bis sie 0
      zurückgibt, dann erst exit(EXIT_FAILURE). project_close() selbst
      wurde dafür erweitert (project.c): schlägt project_save() darin
      fehl, wird jetzt zusätzlich gefragt "Speichern fehlgeschlagen:
      <Fehler>. Projekt trotzdem ohne Speichern schließen?" - bei "Ja"
      wird wie gewohnt weitergemacht (offene PDF-Viewer werden mit
      Speichern-Abfrage geschlossen usw.), sonst -1 wie bisher. Durch die
      Schleife hat der Anwender die Chance, ein z.B. nur kurzzeitig
      nicht erreichbares Netzlaufwerk zwischenzeitlich zu beheben und das
      Projekt doch noch zu retten, aber keine Möglichkeit, ohne Speichern
      oder ausdrückliche Bestätigung einfach in den Normalbetrieb
      zurückzukehren (Nutzervorgabe).
    Betroffene Dateien: zond_treeviewfm.c (neue Felder
    pending_move_path_old/_new, neue Funktion
    write_commit_failure_report(), umgebauter dual_write-Fehlerzweig in
    zond_treeviewfm_after()), project.c (project_close() um die
    Rückfrage bei fehlgeschlagenem Speichern erweitert).

 9. Testschritt (nach Umsetzung von Phase 2): gezielt einen Fehler mitten
    in einer Dual-Write-Operation provozieren (z.B. künstliche
    Constraint-Verletzung nur im zweiten Statement), prüfen, ob das
    Rollback wirklich beide Schemata (store und work) zurücksetzt.
    Zusätzlich (aus 6.b)/7.): gezielt eine Dual-Write-Operation auslösen,
    während der Hintergrund-Indizierungs-Thread läuft, prüfen, ob
    SQLITE_BUSY sauber behandelt wird statt eines Absturzes oder stillen
    Fehlers. Speziell zu Punkt 8 (noch zu testen): Revert-Erfolgsfall
    (Umbenennen rückgängig, kein exit), Revert-Fehlschlagsfall
    (Fehlerdatei wird geschrieben, project_close()-Schleife greift), und
    die neue Rückfrage in project_close() bei fehlgeschlagenem
    project_save() (inkl. Fall, dass der Anwender zunächst ablehnt und
    es bei erneuter Gelegenheit doch klappt).

 SeaDrive-/Index-Historie 06.-11.09.2026 (Punkte vollständig umgesetzt,
 auf kurze Zusammenfassungen eingedampft - Details s. jeweilige Header-
 Doc-Kommentare bzw. Commit-Historie/git log, Prinzip s. Hinweis oben):

 - Performance-Untersuchung großer SeaDrive-Projekte (06.09.2026,
   abgeschlossen): project_load_trees() 140s -> 2,62s bei sehr großen
   Projekten (68.817 Dateien). Root Cause: fehlender zusammengesetzter
   Index auf knoten(parent_ID, older_sibling_ID) - Fix in
   zond_dbase_ensure_indexes() (zond_dbase.c).

 - SeaDrive-Verzeichnis-Coverage-Badge (06.09.2026 geplant, 08.09.2026
   komplett umgesetzt): Ordner-Icon zeigt rekursiv den Hydrierungsstatus
   des Teilbaums an (unten rechts, analog zum Index-Status-Badge unten
   links). Datenstrukturen/Funktionen (seadrive_dir_counts,
   SondSeadriveDirStatus, watcher_count_pending_down()) dokumentiert in
   sond_icon_util.h/sond_treeviewfm.h. Dabei zwei eigenständige
   Watcher-Bugs behoben (fehlender FILE_NOTIFY_CHANGE_FILE_NAME-Filter,
   fehlendes Buffer-Overflow-Resync-Handling).

 - Beim Testen gefundene, unabhängige Bugs (08.-10.09.2026, alle behoben):
   "Ordner ohne Icon" (kaputter icon-name-Direktpfad für Ordner ohne
   Badge, jetzt einheitlich über sond_icon_util_render_with_overlays());
   "Ordner fälschlich grün/Dateien fälschlich violett" (Overlay wurde in
   ein vom Icon-Theme intern gecachtes, geteiltes GdkPixbuf gezeichnet -
   Fix: gdk_pixbuf_copy() vor dem Hineinkomponieren); Absturz bei
   Expansion eines .eml auf oberster Ebene (g_return_val_if_fail lehnte
   die reguläre NULL-Konvention ab; fehlender NULL-Check auf
   error->message in sond_treeviewfm_row_expanded()).

 - Redesign SeaDrive-Badges Datei+Ordner (10.09.2026, umgesetzt): der
   obige Cache-Bug-Fund deckte einen echten Semantik-Bug auf - "offline"
   zählte bisher nur PINNED+nicht-hydriert, nicht auch nie gepinnte
   Dateien. Neues 3-Zustands-Modell je Datei (PENDING/OFFLINE/PINNED) und
   4-Zustands-Modell je Ordner (NONE/FULL_OFFLINE/FULL_HYDRATED_PINNED/
   MIXED), dokumentiert in sond_icon_util.h.

 - Konsolidierung seadrive_file_badges + Erweiterung auf ZondTreeview
   (11.09.2026, umgesetzt): die bisherigen zwei bool-Ground-Truth-Sets
   durch eine Hashtable seadrive_file_badges ersetzt (Pfad ->
   SondSeadriveBadge), dient jetzt sowohl dem Datei-Badge (O(1) statt
   GetFileAttributesW pro Renderzeile) als auch den Ordner-Coverage-
   Deltas - s. sond_treeviewfm.h (_set_file_badges()/_get_file_badge()/
   _update_file_badge()). Auf Nutzerfrage direkt erweitert auf
   ZondTreeview (BAUM_INHALT/BAUM_AUSWERTUNG): neues SeaDrive-Datei-Badge
   unten rechts, s. zond_treeview_get_seadrive_badge() (zond_treeview.c).

 - SeaDrive-Menü neu geordnet + gruppiert + ausgegraut (11.09.2026,
   Nutzer-Vorschlag/-Feedback, umgesetzt): nur noch "Auswahl" in
   Kontextmenüs, "Gesamtes Projekt" nur im Hauptmenü - Parallele zur
   Indexsuche/zum Index-Menü (s.u.). Auslöser: die alte "Gesamtes
   Verzeichnis"-Option im BAUM_FS-Kontextmenü wirkte tatsächlich schon
   immer projektglobal (apply_pin_state_to_root()), gehörte semantisch
   also nicht ins Kontextmenü. Anschließend zu einem eigenen Untermenü
   "SeaDrive" gruppiert und ausgegraut, wenn kein SeaDrive-Projekt offen
   ist (project_set_widgets_sensitive()). Neue Funktionen dazu in
   sond_treeviewfm_seadrive.h/zond_treeview.h/headerbar.h dokumentiert.

 - Index-Coverage-Bug beim Kopieren/Löschen (11.09.2026, Nutzer-Fund,
   behoben). Symptom: Ordner blieb nach Hineinkopieren einer nicht
   indizierten Datei fälschlich "komplett indiziert" (grün). Drei
   verschachtelte Bugs (per Diagnose-Logging gefunden, inzwischen wieder
   entfernt): (1) echtes Kopieren emittierte anders als Verschieben gar
   kein Signal, das die Ziel-Coverage hätte auflösen können - Fix: Signal
   "before-insert" jetzt auch beim Kopieren emittiert, neuer Handler
   zond_treeviewfm_before_insert(). (2) sond_index_ctx_
   coverage_invalidate() löste Geschwister-Einträge mit einem
   projektrelativen statt absoluten Pfad auf (g_dir_open() schlug still
   fehl) und baute deren Keys Windows-typisch mit "\" statt der überall
   sonst verwendeten "/"-Konvention - Fix: root_dir-Parameter ergänzt
   (analog coverage_try_collapse()), Keys jetzt konsistent mit "/".
   (3) nach einem Löschen wurde nie erneut geprüft, ob das
   Elternverzeichnis wieder vollständig abgedeckt ist - Fix:
   pending_delete_path in ZondTreeviewFMPrivate, zond_treeviewfm_after()
   stößt bei Erfolg coverage_try_collapse() an.

 - Index-Menü: Parallelstruktur zu SeaDrive (11.09.2026, Nutzerwunsch,
   umgesetzt). "Index erstellen"/"Index durchsuchen" strukturell an das
   SeaDrive-Menü angeglichen (Untermenü "Index" mit "Erstellen"/
   "Durchsuchen", je "Gesamtes Projekt"/"Auswahl"; Kontextmenüs nur
   "Auswahl"). Neue öffentliche Funktion zond_index_erstellen_
   activate_fuer_baum() (headerbar.h) analog zond_indexsuche_
   activate_fuer_baum().

 - SeaDrive-Hydrierung beim Abdeckungs-Check der Indexsuche (11.09.2026,
   Nutzer-Fund, behoben). check_coverage_one() (zond_indexsuche.c)
   öffnete bisher jede PDF ohne eigenen Coverage-Eintrag ("ganze Datei"-
   Fall) nur, um per pdf_count_pages() die Gesamtseitenzahl für die
   Lücken-Anzeige zu bekommen - zog bei SeaDrive-Platzhaltern volle
   Hydrierung nach sich. Nutzer-Entscheidung: Gesamtzahl dafür
   verzichtbar, reiner pages-Tabellen-Zugriff (DB, kein Dateizugriff)
   reicht. Seit der später ergänzten file_pagecount-Tabelle (s.u.) ist
   die Gesamtzahl in den meisten Fällen wieder verfügbar, ebenfalls ohne
   Dateizugriff - s. check_coverage_one()/SondIndexCoverageGap
   (zond_indexsuche.c) für den aktuellen Stand.

 - "Index erstellen": Invalid argument (11.09.2026, Nutzer-Fund). Kein
   Bug - erwartetes Verhalten bei nicht erreichbarem SeaDrive/Seafile-
   Server (mehr Dateien galten dank der obigen Coverage-Fixes zurecht
   als "nicht abgedeckt" und wurden entsprechend gelesen/hydriert).
   Dabei ein echter, unabhängiger Bug gefunden und behoben:
   coverage_invalidate()/coverage_try_collapse() (sond_index.c) nutzten
   g_dir_open() ohne Long-Path-Präfix, anders als der Rest des Codes
   (sond_dir_open() & Co.) - umgestellt.

 Zwei im Zuge der Indexsuche-Untersuchung gefundene, aber NICHT behobene
 Hydrierungsquellen (11.09.2026, weiterhin OFFEN; betreffen nicht speziell
 die Indexsuche, sondern jeden vollständigen BAUM_FS-Scan):
 (1) sond_tvfm_item_load_fs_dir() (sond_treeviewfm.c) liest beim
     Einlesen eines Verzeichnisses für jede nicht als SeaDrive-
     Platzhalter erkannte Datei die ersten 2 KB zur MIME-Typ-Erkennung
     (sond_file_part_create() -> sond_file_part_read_bytes_internal()).
     Betrifft jeden vollen Tree-Scan (z.B. "Gesamtes Projektverzeichnis"
     bei Index erstellen/durchsuchen), nicht nur die Indexsuche.
 (2) Die SeaDrive-Platzhalter-Erkennung selbst (GetFileAttributesW +
     FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) kann im Einzelfall fehlschlagen
     oder ungenau sein - dann greift die 2-KB-Lese-Weiche aus (1) nicht.

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

 "Index löschen" + Unterseitig-Sperre + file_pagecount (11.09.2026,
 Nutzerwunsch/-Fund, umgesetzt - Details/Mechanik s. Doc-Kommentare
 sond_index.h, general.h, zond_treeviewfm.h/zond_treeview.h,
 sond_text_extract.h; hier nur Anlass, Entscheidungen und Verweise, die
 sich nicht aus dem Code selbst ergeben):

 - "Index löschen": neue Funktion, parallel zu "Index erstellen"/"Index
   durchsuchen" (Hauptmenü: Gesamtes Projekt/Auswahl; Kontextmenüs
   BAUM_FS + ZondTreeview: nur Auswahl). Löscht NUR Index-Daten (chunks/
   pages/coverage in .sond_index.db) - weder die Dateien selbst noch
   Anbindungen (dbase_zond, komplett separate Datenbank) werden
   angerührt. Kernfunktionen: sond_index_ctx_delete_index()/
   _delete_all() (sond_index.h). Vom Nutzer aufgeworfene und per
   Rückfrage geklärte Frage: eine Anbindung speichert ihre Position
   eigenständig (Datei + Seite/Offset), nicht als Verweis auf einen
   Index-Eintrag - wird durch "Index löschen" also nicht ungültig,
   verliert lediglich ihren Coverage-Badge/ihre Durchsuchbarkeit bis zur
   Neuindizierung. Erwünschtes Verhalten, kein Bug.

 - Unterseitige Anbindungen gesperrt: Nutzer-Einwand nach obigem
   Feature - Indizierung/Coverage arbeitet nur seitenweise, eine
   unterseitige Anbindung (z.B. "Seite 1, untere Hälfte") teilt sich
   beim Löschen ihren Seiten-Index-Eintrag mit einer ggf. zweiten
   Anbindung auf derselben Seite ("Seite 1 komplett") - deren Index
   würde sonst mitgelöscht. Erster Lösungsvorschlag (vor dem Löschen
   alle Anbindungen der Datei per zond_dbase_get_arr_sections()
   gegenprüfen) vom Nutzer verworfen ("Kappes") - zu komplex, löst
   außerdem nicht den allgemeineren Fall zweier sich überschneidender,
   aber beide seitenweise ausgerichteter Anbindungen (z.B. "Seite 1" und
   "Seite 1-3" - bleibt als vom Nutzer bewusst hingenommenes
   Restrisiko). Stattdessen einfachere, vom Nutzer vorgegebene Lösung:
   unterseitige Anbindungen dürfen für Index erstellen/löschen (Auswahl)
   gar nicht erst ausgewählt werden, Fehlermeldung statt stiller
   Verarbeitung - anbindung_ist_unterseitig() (general.h),
   reject_unterseitig-Parameter in den get_fileparts-Funktionen.

 - file_pagecount: Anlass war die Nutzer-Erwartung, dass
   coverage_invalidate() eine kollabierte Datei-Coverage beim Löschen
   eines Teilbereichs wieder auf einzelne Seiten herunterbricht - ging
   bisher nicht, weil die Gesamtseitenzahl nirgends coalescing-
   unabhängig gespeichert war (und ohne Öffnen der Datei, SeaDrive-
   Hydrierung vermeiden, nicht ermittelbar ist). Nutzer-Entscheidung:
   "Ich denke, wir müßten das machen. Und daran denken, daß, wenn Seiten
   eingefügt oder gelöscht werden, die Gesamtzahl angepaßt wird." - neue
   Tabelle file_pagecount(filename, total_pages), s.
   sond_index_ctx_set/get/clear_page_count() (sond_index.h). Aktualisiert
   bei jeder vollständigen Indizierung (sond_index()) sowie beim
   PDF-Speichern mit Seiten-Journal (viewer_update_index_for_save(),
   viewer_save.c - vor dem physischen Speichern, wie bestätigt).
   Aufgefallener Zählfehler dabei vermieden: die Gesamtzahl kommt aus
   sond_text_extract_pdf()'s out_n_pages (pdf_count_pages()), NICHT aus
   der Segment-Anzahl - Seiten ohne extrahierbaren Text liefern kein
   Segment, zählen aber mit.

 - Indexsuche-Abdeckungs-Check "X von Y Seiten" dank file_pagecount:
   Nachtrag zum obigen Feature - da die Gesamtseitenzahl jetzt oft
   bekannt ist, zeigt check_coverage_one() (zond_indexsuche.c) für
   "ganze Datei, keine Coverage, aber schon einzelne Seiten indiziert"
   wieder "X von Y Seiten fehlen" statt nur "nur X Seiten indiziert"
   ohne Gesamtzahl - weiterhin ohne Dateizugriff (rein file_pagecount +
   pages-Tabelle). Ist file_pagecount unbekannt, bleibt es beim
   bisherigen Verhalten.

 Index-Status-Badge: Grau statt Orange für PARTIAL (12.09.2026,
 Nutzer-Feedback, umgesetzt). Orange wirkte wie ein Warn-/Unfertig-
 Signal statt eines reinen Ist-Zustands ("teilweise indiziert" ist kein
 Fehler). sond_icon_util_status_badge_pixbuf() nutzt für
 SOND_INDEX_STATUS_PARTIAL jetzt dieselbe Grau-Farbe wie
 SEADRIVE_DIR_STATUS_MIXED. SeaDrive-PENDING (Datei gepinnt, noch nicht
 heruntergeladen) bleibt bewusst Orange - beide Farben waren bis dahin
 geteilt (Kommentar "wie INDEX_STATUS_PARTIAL"), sind es jetzt nicht
 mehr. Stale gewordener Doc-Kommentar in sond_icon_util.h (Zeile ~40,
 nannte noch Orange für PARTIAL) mitkorrigiert.

 */
