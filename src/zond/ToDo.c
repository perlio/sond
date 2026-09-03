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
 Commit-Message bzw. im Diff (git log).

 Architektur-Plan (24.08.2026, noch nicht umgesetzt - erstmal zurückgestellt):
 Atomarität store/work bei den Dual-Write-Stellen

 Betroffene Stellen (Stand 24.08.2026, ergänzt 02.09.2026, per grep
 verifiziert):
 - viewer_save.c, viewer_save_dirty_dds() (~Zeile 604-647): dbase_zond_begin/
   commit/rollback um dbase_zond_update_sections() (Seiten löschen/einfügen).
 - zond_treeviewfm.c, zond_treeviewfm_before_move()/_after() (~Zeile 215-369):
   dbase_zond_begin/commit/rollback um dbase_zond_update_path() und
   mehrfach dbase_zond_update_gmessage_index() (Datei/Verzeichnis umbenennen/
   verschieben, inkl. GMessage-SonderfälleAch). ZUSÄTZLICH dort eine dritte,
   unabhängige Transaktion auf index_ctx->db (FTS-Suchindex, eigene
   sqlite3-Verbindung, eigenes rohes sqlite3_exec("BEGIN/COMMIT/ROLLBACK")),
   die bislang nicht mit der store/work-Transaktion verklammert ist.
 - zond_treeviewfm.c, zond_treeviewfm_before_delete() (~Zeile 236-260):
   dual_write dort BEDINGT (nur wenn from_gmessage) - dbase_zond_begin/
   commit/rollback um dbase_zond_update_gmessage_index() beim Löschen eines
   Elements aus einer GMessage/E-Mail-Anhangsstruktur (nachfolgende
   Geschwister-Indizes müssen in beiden DBs neu nummeriert werden). Beim
   normalen Löschen (nicht aus GMessage) reiner Single-Write auf work,
   nicht betroffen. Bei der ursprünglichen Bestandsaufnahme vom 24.08.2026
   übersehen, am 02.09.2026 nachgetragen.

 Problem: store und work sind zwei unabhängige sqlite3-Verbindungen; die
 obigen Stellen schreiben sequenziell in beide (dbase_zond_begin/commit/
 rollback, project.c). Schlägt der zweite Commit nach erfolgreichem ersten
 fehl, oder das Rollback selbst, entsteht potenziell ein inkonsistenter
 Zustand zwischen store und work - keine echte Atomarität über beide Dateien.

 Plan (abschichtbar in zwei Phasen, s. Begruendung in 6.b unten,
 Frage/Antwort 03.09.2026):

 Phase 1 - Punkte 1.-5.: store/work atomar machen, unabhaengig von
 index_ctx, in sich abgeschlossen umsetzbar.
 Phase 2 - Punkte 6./7.: index_ctx-Anbindung (Entscheidung s.u.), kann
 zeitlich beliebig spaeter erfolgen, ohne Phase 1 nochmal anzufassen.

 Geprüfte und verworfene Alternative (03.09.2026): komplett auf EINE
 Connection für store+work umstellen (work als "main", store nur noch
 als angehängtes Schema, oder umgekehrt), statt wie geplant work's
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
 bestehenden, chirurgischen Lösung (work-Connection unangetastet, ATTACH
 nur für die Dual-Use-Funktionen) bleiben.

 1. journal_mode/synchronous-Sicherung (eigenständig, zuerst umsetzbar,
    unabhängig von 2.-5.; Voraussetzungen am 02.09.2026 anhand der
    offiziellen SQLite-Doku verifiziert, Zitate s.u.): vor jedem
    Öffnen des Projekts (project_create_dbase_zond()) UND unmittelbar vor
    jeder der o.g. Dual-Write-Transaktionen für BEIDE Dateien zwei Werte
    per "PRAGMA journal_mode;" bzw. "PRAGMA synchronous;" abfragen (über
    die jeweils schon offene Verbindung - beides sind Dateieigenschaften,
    nicht verbindungsgebunden):

    - journal_mode muss DELETE, TRUNCATE oder PERSIST sein (NICHT WAL,
      NICHT MEMORY, NICHT OFF). Ist er das nicht (z.B. WAL, weil extern
      z.B. mit DB Browser for SQLite umgestellt), Rückwechsel versuchen
      ("PRAGMA journal_mode=DELETE;") UND den zurückgelieferten Wert
      prüfen (ein gescheiterter Wechsel wirft keinen Fehler, sondern
      liefert stillschweigend den alten Modus zurück - sqlite.org/
      pragma.html).
    - synchronous darf nicht OFF sein. Ist es das, Rückwechsel versuchen
      ("PRAGMA synchronous=FULL;", oder zumindest NORMAL).

    Bleibt journal_mode bei WAL/MEMORY/OFF oder synchronous bei OFF,
    Operation NICHT ausführen, klare Fehlermeldung an den Anwender
    ("Datenbank wird von einem anderen Programm verwendet"). Grund (Zitat
    sqlite.org/atomiccommit.html): ATTACH-Transaktionen über mehrere
    Dateien sind nur atomar, wenn dabei eine "Super-Journal"-Datei
    angelegt wird - "if the database files have other settings that
    compromise integrity across a power-loss event (such as PRAGMA
    synchronous=OFF or PRAGMA journal_mode=MEMORY) then the creation of
    the super-journal is omitted, as an optimization." journal_mode=OFF
    deaktiviert das Journal ganz, ist also erst recht nicht abgedeckt.
    Für WAL gilt separat (Zitat sqlite.org/wal.html, Abschnitt
    "Disadvantages"): "Transactions that involve changes against multiple
    ATTACHed databases are atomic for each individual database, but are
    not atomic across all databases as a set." - dort gibt es den
    Super-Journal-Mechanismus prinzipiell nicht, unabhängig von
    synchronous. Alle drei Ausschlüsse (WAL, MEMORY, OFF bei journal_mode;
    OFF bei synchronous) sind also nötig, nicht nur der WAL-Fall. Und:
    der Modus kann jederzeit von außen (auch bei offener eigener
    Verbindung, da SQLite außerhalb aktiver Statements kein Lock hält)
    unbemerkt umgestellt werden - daher die Prüfung unmittelbar vor jeder
    Transaktion, nicht nur beim Öffnen.

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

 5. Rückbau der aktuellen Rollback-Fehlerbehandlung (UMGESETZT
    02.09.2026, dabei korrigiert - s.u.): der kürzlich gebaute Error-Merge
    in dbase_zond_rollback() (zwei separate error_int für store- und
    work-Rollback, zusammengeführt via add_string(), s. Eintrag oben
    "viewer_save.c, viewer_save_dirty_dds()...") wird durch 4. teilweise
    überflüssig - es gibt nur noch einen einzigen ROLLBACK-Aufruf, der
    scheitern kann, daher keine Schleife/kein Vergleich zwischen ZWEI
    Rollback-Fehlern mehr nötig.
    KORREKTUR bei der Umsetzung (02.09.2026): "komplett entfernen zugunsten
    eines normalen einzelnen GError-Fehlerpfads" war zu weitgehend gedacht
    - ein direkter Durchreich auf den übergebenen error-Parameter hätte
    einen Bug (zurück-)gebracht: an der Rollback-Aufrufstelle (s.
    dbase_zond_commit()) ist *error i.d.R. schon gesetzt (Grund des
    Rollbacks, z.B. der gescheiterte Commit). Schlägt das ROLLBACK-
    Statement selbst fehl (selten), würde ein direktes *error=... diesen
    eigentlichen Fehler überschreiben UND die alte GError leaken. Daher
    beibehalten, nur vereinfacht: EIN lokaler error_int für den einen
    Rollback-Aufruf, bei Fehler an ein schon gesetztes *error angehängt
    (nicht überschrieben) statt wie vorher zwischen zwei error_int
    gemergt.

 6. Entscheidung index_ctx (02.09.2026, vorher offene Frage - jetzt
    entschieden, zwei getrennte Fragen):

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
       index_ctx->db in zond_treeviewfm.c, NICHT dbase_zond_begin/commit/
       rollback (4.) und NICHT die SQL-Umschreibung in 3. - store/work-
       Atomarität (1.-5.) kann daher als abgeschlossene Phase 1
       vorgezogen werden, 6./7. als Phase 2 beliebig später folgen, ohne
       1.-5. noch einmal anzufassen. In der Zwischenzeit (Phase 1
       umgesetzt, Phase 2 noch offen) bleibt index_ctx wie heute eine
       separate, eigene Transaktion neben der dann schon atomaren
       store+work-Transaktion - und das in 7. beschriebene
       Cross-Thread-Risiko besteht unverändert fort, bis 6.b) umgesetzt
       ist.
       Voraussetzung für 6.b) selbst: SQLITE_BUSY behandeln - läuft der
       Hintergrund-Thread gerade eine offene Schreibtransaktion auf
       index_ctx's eigener Connection, bekommt der ATTACH-Schreibversuch
       der UI-Transaktion SQLITE_BUSY (zwei getrennte Connections auf
       dieselbe Datei, siehe 7.) - busy_timeout setzen und/oder Retry,
       sonst klare Fehlermeldung an den Anwender ("Indizierung läuft,
       bitte kurz warten").

 7. Separat notiert, unabhängig von 6.b) beim Nachdenken darüber gefunden:
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

 8. Separat notiert, nicht Teil dieser Atomaritäts-Umstellung, aber im
    selben Bereich entdeckt: zond_treeviewfm.c, zond_treeviewfm_after()
    (~Zeile 351-356) - exit(EXIT_FAILURE) bei fehlgeschlagenem
    dbase_zond_commit(), unabhängig von der genauen Fehlerursache (auch
    bei einem gewöhnlichen ersten-Commit-Fehler, nicht nur bei echter
    store/work-Inkonsistenz). Kein Error-Dialog, keine Chance, sonstige
    ungesicherte Änderungen der Sitzung zu retten. Ggf. eigenständig zu
    behandeln.

 9. Testschritt (nach Umsetzung): gezielt einen Fehler mitten in einer
    Dual-Write-Operation provozieren (z.B. künstliche Constraint-
    Verletzung nur im zweiten Statement), prüfen, ob das Rollback wirklich
    beide Schemata (store und work) zurücksetzt. Zusätzlich (aus 6.b)/7.):
    gezielt eine Dual-Write-Operation auslösen, während der
    Hintergrund-Indizierungs-Thread läuft, prüfen, ob SQLITE_BUSY sauber
    behandelt wird statt eines Absturzes oder stillen Fehlers.

 */
