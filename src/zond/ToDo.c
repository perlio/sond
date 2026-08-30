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

 */
