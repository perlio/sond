# TODO

Offene Punkte für zond. Erledigtes steht in [HISTORY.md](HISTORY.md), Hinweise zum Bauen in [BUILD.md](BUILD.md).
Neue Einträge werden ab #200 nummeriert.

Die folgenden Abschnitte sind unverändert aus src/zond/ToDo.c übernommen (Stand 03.10.2026).

## Allgemeine Liste

```text
 ToDo:

- Rows mit Text Farbe
- Copy_Auswertung wenn root dann Verweis auf root?
- Wenn in BAUM_INHALT Section angebunden, copy_auswertung öffnet ganze Datei
- Beim Kopieren von ZIP-Dateien (und anderen Containern) ins Filesystem:
  verbotene Sonderzeichen im Dateinamen escapen
- Durchsuchen des Dateisystems (BAUM_FS) - Anforderungen noch offen

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
```

## Architektur-Plan: Atomarität store/work (Phase 2 offen)

Phase 1 und Punkt 8 sind umgesetzt, als Kontext des Plans hier belassen.

```text
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
```

## SeaDrive-Hydrierungsfehler: UX-Umgang (zurückgestellt)

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

## NULL-Prüfung von stvfm_item bei Dummy-Zeilen (nur notiert)

Aus dem Abschnitt "BAUM_FS: hängenbleibende Dummy-Zeile nach fehlgeschlagenem Expand" (HISTORY.md, 18.09.2026).

```text
 Nicht angegangen (nur notiert): beim Durchsuchen des Codes fielen weitere
 Aufrufstellen auf (u.a. Doppelklick-Handler ~Zeile 2690/3198, Selektions-
 Verarbeitung ~Zeile 2580/2909), die stvfm_item nach gtk_tree_model_get()
 ebenfalls ohne NULL-Prüfung entreffen bzw. weiterreichen - potentiell
 riskant, falls eine Dummy-Zeile je direkt angeklickt werden sollte. Durch
 obigen Fix jetzt nur noch für ein sehr kurzes Zeitfenster (während des
 Ladens) statt dauerhaft sichtbar, also entschärft, aber nicht
 grundsätzlich ausgeschlossen - bei Bedarf defensiv nachrüsten. Nicht
 durch Kompilieren/Testen verifiziert.
```

## ZIP-Dateinamen mit unter Windows unzulässigen Zeichen (zurückgestellt)

Aus "Regressions-Fund (18.09.2026): Doppelklick auf unhydrierte Datei" (HISTORY.md); zweimal notiert, beide Fassungen übernommen.

```text
 Zurückgestellt (Nutzer-Entscheidung 18.09.2026): Sonderfall ZIP-
 Archivinhalte - ZIP erlaubt Dateinamen/Zeichen, die im Windows-
 Dateisystem gar nicht erst anlegbar wären (z.B. bei "Verzeichnis aus ZIP
 kopieren" ins Dateisystem, s. 16.09.2026 oben) - dafür ggf. eigene,
 separate Betrachtung nötig, wenn das konkret auftritt.

 Später (vom Nutzer explizit zurückgestellt): Sonderfall ZIP-
 Archivinhalte - Zip erlaubt Dateinamen/Zeichen, die im Windows-
 Dateisystem gar nicht erst anlegbar wären (z.B. bei "Verzeichnis aus ZIP
 kopieren" ins Dateisystem, s. 16.09.2026 oben) - dafür ggf. eigene,
 separate Betrachtung nötig.
```

## Busy-/Fortschrittsanzeige beim Aufklappen in BAUM_FS (zurückgestellt)

Aus "Performance ZIP-Anbinden: verbleibende Silent-Freeze-Lücke" (HISTORY.md, 16.09.2026).

```text
 - Bewusst zurückgestellt (kein Teil dieses Fixes): eine Busy-/
   Fortschritts-Cursor-Anzeige (z.B. Uhrglas, später ggf. ein sich
   füllender Kreis) beim interaktiven Aufklappen von BAUM_FS-
   Verzeichnissen (sond_treeviewfm_expand_dummy()) - der Mechanismus
   (SondTVFMProgress) ist dafür vorbereitet, aber noch nicht an dieser
   Stelle verdrahtet.
```

## Löschen in BAUM_INHALT: verbleibende Kosten (nicht behoben)

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

## Ideen #166-#170

Aus "Offene Punkte (21.09.2026, Nutzer-Sammlung)".

```text
 #166 sond_treeviewfm bekommt Dateien, die "von außen" (außerhalb der
 App) auf Root-Ebene eingefügt werden, nicht mit - Baum aktualisiert sich
 nicht automatisch. Zwei Ansätze: (a) Dateisystem-Watcher (GFileMonitor)
 auf das Root-Verzeichnis, analog zum bereits vorhandenen SeaDrive-
 Watcher-Mechanismus, der bei Änderungen die Kinder-Liste des
 betroffenen DIR-Knotens invalidiert/neu lädt; (b) einfacher manueller
 "Aktualisieren"-Menüpunkt, der für den aktuell selektierten (oder den
 Root-)Knoten die Kinder neu einliest, ohne Hintergrundprozess. (b) ist
 deutlich einfacher umzusetzen, verlangt aber eine bewusste Nutzeraktion;
 (a) ist komfortabler, aber mehr Aufwand und Fehlerfläche (Symmetrie zu
 den bekannten SeaDrive-Watcher-Bugs dieser Session zu bedenken).

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

 #168 Exportfunktion: aktueller Zustand laut Nutzer unbefriedigend -
 noch nicht untersucht, welche Datei/Funktion das konkret betrifft; das
 wäre der erste Schritt vor einem Redesign.

 #169 Projekt-Teilexport: markierte Punkte eines Baums (z.B. BAUM_INHALT
 oder BAUM_AUSWERTUNG) samt zugehöriger Dateien als eigenständiges,
 kleineres Projekt exportieren. Spiegelbildlich zu #167 (Import) - beide
 bräuchten im Kern dieselbe Fähigkeit, einen Teilbaum samt abhängiger
 file_parts konsistent zu extrahieren (einmal Richtung "rein", einmal
 Richtung "raus" in eine neue zond_dbase mit eigenem Root-Verzeichnis,
 in das die betroffenen Dateien kopiert würden). Gemeinsame Infrastruktur
 für #167/#169 naheliegend.

 #170 PDFs aus Auszug/markierten Punkten erzeugen: der bestehende
 Auszug-Mechanismus (zond_treeview_open_auszug(), document.c) fügt PDF-
 Segmente bereits zu einer gemeinsamen ANSICHT zusammen (mehrere
 DisplayedDocument in einer Kette) - für eine echte Export-PDF-Datei
 bräuchte es zusätzlich einen Schreibpfad, der dieselben Segmente (via
 mupdf, das für das PDF-Handling ohnehin schon verwendet wird, s.
 zond_pdf_document.c) tatsächlich in eine neue, physische PDF-Datei
 zusammenführt statt nur anzuzeigen.
```

## zond_treeview_get_path() ohne Aufrufer

Aus "Refactoring des #180-BAUM_FS-Sprungs" (HISTORY.md, 22.09.2026).

```text
 zond_treeview_get_path() (zond_treeview.c/.h) hat dadurch aktuell keinen
 Aufrufer mehr - bewußt nicht entfernt (kein Teil dieser Anfrage, eigene
 Entscheidung wäre ggf. später sinnvoll).
```

## Offene Frage: cursor-changed nach Sprung aus dem Suchfenster

Aus #182 (unselect_all()-Nachfrage, HISTORY.md, 22.09.2026).

```text
 Offene Frage/Beobachtung für später: dieselbe Unsicherheit betrifft
 möglicherweise auch das im #181-Nachtrag beschriebene Entfallen des
 manuellen "cursor-changed"-Connect/Emit (dort mit derselben, jetzt
 widerlegten Grab-Focus-Annahme begründet) - falls Label/Textview nach
 einem Sprung aus dem Suchfenster nicht aktualisiert werden, ist das
 vermutlich dieselbe Ursache und müsste analog behoben werden.
```

## #192 GLib-GIO-CRITICAL "GFileInfo created without standard::type"

```text
 #192 offen (01.10.2026): GLib-GIO-CRITICAL "GFileInfo created without
 standard::type" (g_file_info_get_file_type) tritt häufig in einem
 Projekt mit angebundener Section auf. Kein eigener Aufruf von
 g_file_info_get_file_type() im Code - Auslöser in GLib/GTK. Nächster
 Schritt: Stacktrace per G_DEBUG=fatal-criticals (Eclipse-Debug-
 Konfiguration, Environment).
 Seit 1.1.3 nicht mehr beobachtet, Ursache unklar. Zusammenhang mit #190
 (leerer Auszug) unwahrscheinlich - der leere Auszug griff auf nichts zu.
 Bei erneutem Auftreten im Debug-Build mit Konsole prüfen (Release-Build
 mit -mwindows zeigt keine Konsole).
 Vermutung (unbelegt): GtkFileChooserDialog (choose_file(), misc.c) beim
 Einlesen eines SeaDrive-Ordners - GTK3 liest dort intern den Dateityp,
 der bei nicht hydrierten Platzhaltern fehlen kann. Passt zum
 gleichzeitigen Auftreten (gleicher Zeitstempel), nicht aber zu "ganz
 oft" ohne offenen Dialog.
 Nebenbei behoben: choose_file() las bei NULL von
 gtk_file_chooser_get_filename() (nicht lokale Auswahl) in der
 "\"->"/"-Schleife einen NULL-Zeiger.

 #193 Eingebettete Dateien mit gleichem Namen (02.10.2026, Nutzer-Hinweis).
 Eingebettete Dateien werden über ihren Dateinamen (/UF, sonst /F, s.
```
