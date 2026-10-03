# HISTORY

Erledigte Punkte von zond, chronologisch (Ältestes oben). Offenes steht in [TODO.md](TODO.md).
Unverändert aus src/zond/ToDo.c übernommen (Stand 03.10.2026); aus dem Text herausgelöste offene Reste sind in TODO.md mit Verweis hierher vermerkt.

```text
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

 Zwei im Zuge der Indexsuche-Untersuchung gefundene Hydrierungsquellen
 (11.09.2026):
 (1) sond_tvfm_item_load_fs_dir() (sond_treeviewfm.c) liest beim
     Einlesen eines Verzeichnisses für jede nicht als SeaDrive-
     Platzhalter erkannte Datei die ersten 2 KB zur MIME-Typ-Erkennung
     (sond_file_part_create() -> sond_file_part_read_bytes_internal()).
     Für BAUM_FS bei "Index erstellen/durchsuchen/löschen" (Gesamtes
     Projekt UND Auswahl) BEHOBEN, s. Eintrag 12.-14.09.2026 unten -
     zond_treeviewfm_item_get_fileparts() nutzt dafür jetzt einen von
     load_fs_dir() unabhängigen, reinen readdir-Scanner. load_fs_dir()
     selbst bleibt unverändert (Nutzer-Entscheidung: der interaktive
     Aufklapp-Mechanismus des Baums darf ruhig hydrieren, das ist beim
     gezielten Hineinnavigieren erwartbar) - betroffen war nur der
     bisherige Umweg über genau diesen Mechanismus beim Sammeln der zu
     (de-)indizierenden Punkte. Für BAUM_INHALT/BAUM_AUSWERTUNG (Auswahl
     dort) weiterhin OFFEN - anderer Mechanismus (Anbindung statt
     Verzeichnis-Scan), s. Eintrag 15.09.2026 unten.
 (2) Die SeaDrive-Platzhalter-Erkennung selbst (GetFileAttributesW +
     FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) kann im Einzelfall fehlschlagen
     oder ungenau sein - dann greift die 2-KB-Lese-Weiche aus (1) nicht.
     Für den jetzt behobenen BAUM_FS-Indizierungsweg ohne Belang (der
     neue Scanner liest dort gar nicht mehr, unabhängig von dieser
     Erkennung).


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

 SeaDrive-Massenhydrierung bei "Index durchsuchen" (12.-15.09.2026,
 Nutzer-Fund, für BAUM_FS behoben - Details/Mechanik in den jeweiligen
 Doc-Kommentaren, hier nur Anlass, Entscheidungen und der Stand):

 - Anlass: bei einem teilweise indizierten SeaDrive-Projekt lud "Index
   durchsuchen" ohne Ende Dateien herunter, die gar nicht
   indizierungsfähig sind. Nutzer-Entscheidung (verschärft im Lauf der
   Diskussion): das Durchsuchen des Index darf UNTER KEINEN UMSTÄNDEN
   ein Öffnen/Hydrieren einer Datei erfordern - auch nicht für Dateien
   innerhalb von ZIP/PDF-Einbettungen/E-Mail-Anhängen. Der interaktive
   Aufklapp-Mechanismus von BAUM_FS (sond_tvfm_item_load_fs_dir() &
   Co.) bleibt davon ausdrücklich unberührt - "wenn ich in eine Datei
   einsteige, muss geladen werden, ist klar".

 - zond_treeviewfm_item_get_fileparts() (zond_treeviewfm.c) sammelt für
   "wirkliche" (nicht in einem Container liegende) Dateisystem-Äste
   jetzt über einen eigenen readdir-Scanner (neue Funktion
   zond_treeviewfm_item_get_fileparts_readdir()) statt über den
   bisherigen Weg via sond_tvfm_item_load_children()/
   sond_file_part_create() (2-KB-Inhalts-Sniffing). Nutzt nur
   sond_dir_open()/sond_stat() (Metadaten) und
   sond_file_part_create_leaf() mit rein endungsbasiertem MIME-Typ
   (mime_from_extension()) - kein Dateizugriff. Gilt für "Gesamtes
   Projekt" UND ordnerbasierte "Auswahl" gleichermaßen, für Index
   Erstellen/Durchsuchen/Löschen (dieselbe Sammelfunktion). Ein frisch
   entdeckter Container (ZIP/PDF mit Einbettungen/E-Mail) wird dabei
   NIE aufgeschlüsselt, sondern immer als ein einziger opaker Filepart
   eingetragen - das war beim bisherigen Weg für diesen Fall ohnehin
   nie anders (verifiziert, keine Verhaltensänderung für "Index
   erstellen": die tatsächliche Rekursion in Container-Inhalte
   passiert unverändert erst downstream in sond_process_file.c, s.
   nächster Punkt).

 - Klargestellt (Nutzer-Nachfrage, per Code verifiziert statt vermutet):
   die vollständige, beliebig tief verschachtelte Rekursion in
   Container-Inhalte bei "Index erstellen" findet in sond_process_file.c
   statt, NICHT bei der Fileparts-Sammlung - process_zip_for_ocr() geht
   dort alle ZIP-Einträge durch, pdf_walk_embedded_files() alle
   embedded files eines PDF, gmessage_process_part() rekursiv den
   ganzen MIME-Baum einer E-Mail, jeweils mit rekursivem Aufruf von
   sond_process_file_do_rec() pro Eintrag (Pfad-Konvention "//"). Beide
   Befunde (Sammlung sammelt Container nur opak ein / Verarbeitung
   schlüsselt sie vollständig auf) widersprechen sich nicht, sie
   betreffen verschiedene Pipeline-Stufen.

 - container_entrycount(filename, total_entries) - neue Tabelle
   (sond_index.h/.c), analog file_pagecount, aber für die Anzahl
   direkter Container-Einträge statt Seiten. Grund: check_coverage_one()
   (zond_indexsuche.c) muss für einen NICHT vollständig abgedeckten ZIP-
   Container trotzdem sagen können, ob/wie viel darin schon (über eine
   frühere gezielte Auswahl) indiziert wurde, ohne den Container zu
   öffnen. Bewusst nur EINE Ebene (Nutzer-Vorgabe: "für die
   coverage-Prüfung reicht die Aussage 'xyz.zip nicht erfaßt', egal was
   sich genau darin befindet") - kein Anspruch auf eine rekursive
   Gesamtzahl über mehrere Container-Ebenen hinweg. Population: ZIP in
   process_zip_for_ocr() (zip_get_num_entries(), immer, unabhängig
   davon ob dabei etwas verändert wurde). Keine mtime/Größe nötig (wie
   file_pagecount): externe Änderungen finden laut bestehender Absprache
   nur auf Projekt-Root-Ebene statt. sond_index_ctx_count_nested_
   indexed() zählt dazu passend nur DIREKTE Kinder (ein "//"-Segment,
   tiefer verschachtelte Treffer zählen als Beleg für ihr direktes
   Elternsegment, nicht extra).

   Zunächst analog auch für PDF (process_pdf_for_ocr(), Zähler in
   ProcessPdfData) und E-Mail (process_gmessage_for_ocr(),
   g_mime_multipart_get_count() des Wurzel-Teils) mitgebaut, dann aber
   wieder zurückgebaut (15.09.2026, Nutzerfrage/-Entscheidung): beim
   Umsetzen aufgefallen, dass check_coverage_one() den
   container_entrycount-Zweig nur für ZIP (application/zip - weder
   is_pdf noch von sond_index_mime_type_supported() als "ganze Datei"
   indizierbar erkannt) je betritt. PDFs laufen immer über die
   is_pdf-Zweige (eigene Seiten + alle embedded files werden bei "Index
   erstellen" in einem Rutsch verarbeitet, s.o.), E-Mails sind über
   message/rfc822 direkt unterstützt und bekommen bei vollständiger
   Verarbeitung immer einen eigenen pages-Eintrag unter ihrem eigenen
   Dateinamen (Header + nicht-Attachment-Body-Text, s.
   build_gmessage_text() in sond_text_extract.c) - beide brauchen den
   container_entrycount-Zweig daher nie, die PDF-/E-Mail-Population wäre
   dauerhaft toter Code gewesen. container_entrycount betrifft damit nur
   ZIP.

 - check_coverage_one() (zond_indexsuche.c) zusätzlich angepasst: PDF-
   Erkennung jetzt auch über die Dateiendung (nicht nur
   SOND_IS_FILE_PART_PDF()), da die readdir-Scanner-Fileparts immer
   LEAF sind. Für ZIP-Dateien ohne bekannten container_entrycount (noch
   nie als Ganzes verarbeitet) wird jetzt "nicht erfaßt" gemeldet
   (missing=total=1, wie eine normale nie indizierte Datei) statt
   stillschweigend gar nicht als Lücke aufzutauchen - vorher wären
   Container, von denen bislang nur ein gezielt ausgewählter Eintrag
   indiziert wurde, fälschlich gar nicht als Lücke erschienen.

 - BAUM_INHALT/BAUM_AUSWERTUNG (15.09.2026, Nutzerfrage, behoben):
   sammeln ihre Fileparts für "Index erstellen/durchsuchen/löschen
   (Auswahl)" nicht über einen Verzeichnis-Scan, sondern über die
   Anbindung jedes Baum-Knotens
   (zond_treeview_get_selected_fileparts_foreach(), zond_treeview.c) -
   der Datei-Teil der Anbindung ging dabei bisher durch
   sond_file_part_from_filepart() (sond_fileparts.c), die für JEDES
   "//"-Segment sond_file_part_create() aufruft - und die liest
   tatsächlich 2048 Bytes zur MIME-Erkennung. Dieser Weg war also NICHT
   hydrierungsfrei, unabhängig vom obigen BAUM_FS-Fix. Fix: neue
   Funktion sond_file_part_from_filepart_leaf() (sond_fileparts.c) -
   baut dieselbe verschachtelte Eltern-Kind-Kette wie
   sond_file_part_from_filepart() (wird für "Index erstellen" bei
   Anbindungen INNERHALB eines Containers gebraucht, damit
   sond_file_part_get_bytes() den Eintrag später korrekt über die
   Eltern-Kette extrahieren kann - anders als beim BAUM_FS-Scanner, der
   Container nie aufschlüsselt), aber mit sond_file_part_create_leaf()
   (Endung statt Inhalt) statt sond_file_part_create() pro Segment -
   inklusive derselben sond_file_part_is_open()-Vorabsuche wie im
   Original, sonst würde die Identitäts-basierte Vereinigung mehrerer
   Anbindungen auf dieselbe Datei (Z. 3385ff.) unterlaufen. Nur an
   dieser einen Stelle eingesetzt; sond_file_part_from_filepart() selbst
   unangetastet gelassen - wird an anderen Stellen (Datei öffnen im
   Viewer: seiten.c/stand_alone.c; PDF-Stapelfunktionen:
   headerbar.c:selection_abfragen_pdf(); zond_treeview.c:
   get_filepart_from_iter()) zu Recht weiterhin mit echter
   Inhaltserkennung gebraucht. Als Nebeneffekt jetzt auch für
   zond_treeview_seadrive_apply_to_selection() (Pin/Unpin-Menü)
   hydrierungsfrei, da diese denselben Sammelweg nutzt.

 Performance "Index durchsuchen" bei großen Projektverzeichnissen
 (15.09.2026, Nutzer-Fund, für "Gesamtes Projekt" behoben):

 - Anlass: bei einem großen, größtenteils schon indizierten
   Projektverzeichnis dauerte allein der Abgleich mit dem Bestand
   mehrere Minuten - der bisherige Weg legte für JEDE Datei im Projekt
   ein SondFilePart an und fragte die DB einzeln ab, auch innerhalb
   längst vollständig abgedeckter Ordner. Dazu kam eine UX-Klage: bei
   einem schlecht abgedeckten Projekt wurden ggf. hunderte einzelne
   Dateien als Lücke aufgelistet, ohne dass das dem Nutzer weiterhalf.

 - Neuer Scanner scan_coverage_gaps_fs() (zond_indexsuche.c), ersetzt
   für "Gesamtes Projekt" den bisherigen Weg über
   zond_treeviewfm_get_fileparts()+check_coverage(). Nutzt die
   schon vorhandene sond_index_ctx_get_dir_status() (coverage_get() +
   eine LIKE-Existenzprüfung auf pages/coverage, rein DB-seitig) pro
   Verzeichnis-Ebene: FULL -> ganzer Ast übersprungen, gar nicht erst
   per readdir hineingelesen; NONE -> ganzer Ast als EINE Lücke
   gemeldet (Nutzer-Vorgabe: nur der Pfad, keine Dateizahl - eine
   Zählung würde wieder ein volles Listing erfordern); PARTIAL -> eine
   Ebene tiefer readdir'en und dieselbe Prüfung je Kind wiederholen,
   bis die Mischgrenze gefunden ist - erst dort werden einzelne Dateien
   weiterhin per check_coverage_one() geprüft. Die oberste Ebene
   (Projektverzeichnis selbst) wird nie pauschal geprüft, sondern immer
   direkt aufgeklappt - Coverage wird nie über die oberste Ebene hinaus
   zusammengefasst (s. coverage_try_collapse()), ein Eintrag fürs ganze
   Projekt existiert also nie.

 - SondIndexCoverageGap um dir_path erweitert (Verzeichnis-Gap, sfp
   bleibt NULL). Die eigentliche Aufschlüsselung eines gemeldeten
   Verzeichnis-Asts in einzelne Fileparts passiert dabei bewusst NICHT
   beim Scannen/Anzeigen, sondern erst verzögert, wenn der Nutzer im
   Lücken-Dialog "jetzt nachindizieren" wählt (neue gemeinsame Funktion
   handle_coverage_gaps(), löst zond_treeviewfm_item_get_fileparts_
   readdir() dafür extra aus) - dafür wurde diese bisher datei-lokale
   Funktion (zond_treeviewfm.c) exponiert (zond_treeviewfm.h).

 - Dieselbe Verzeichnis-Kurzschluss-Logik für "Index erstellen (Gesamtes
   Projekt)" nachgezogen (16.09.2026, Nutzerwunsch/-bestätigung "Ja",
   Task #100 - s. eigenen Abschnitt weiter unten für Details).

 GLib-CRITICAL "g_date_time_unref: assertion 'datetime->ref_count > 0'
 failed" (15.09.2026, Nutzer-Log-Fund, behoben):

 - Ursache: build_gmessage_text() (sond_text_extract.c) rief auf dem
   Rückgabewert von g_mime_message_get_date() ein g_date_time_unref()
   auf - dieser ist aber laut GMime-API (transfer none) eine von der
   Message gehaltene, geliehene Referenz. Der zusätzliche unref senkte
   deren Referenzzähler vorzeitig auf 0; der eigentliche Crash/die
   CRITICAL-Meldung trat erst später beim Aufräumen der Message selbst
   auf. Fix: unref-Aufruf entfernt, Fundstelle kommentiert. Einzige
   Fundstelle im ganzen Code (per grep verifiziert).

 ZIP-Anbinden (BAUM_FS -> BAUM_INHALT) bei großen Archiven praktisch
 endlos (15./16.09.2026, Nutzer-Fund, TEILWEISE behoben - Ursache des
 eigentlichen Hängers noch nicht gefunden, s. unten):

 - Anlass: Anbinden eines ZIP-Archivs mit mehreren tausend Einträgen
   hing sich scheinbar auf, das Info-Fenster zeigte nichts an. Ursache
   keine echte Endlosschleife, sondern eine mit der Archivgröße
   explodierende Laufzeit: sfp_zip_list_dir() (früher in
   sond_treeviewfm.c) durchlief bei JEDEM Aufruf - also für JEDEN
   Verzeichnisknoten im ZIP - erneut ALLE zip_get_num_entries()
   Einträge, UND sond_tvfm_item_create() rief für jeden neu angelegten
   ZIP-Verzeichnisknoten zusätzlich noch einmal denselben Vollscan nur
   zur has_children-Bestimmung auf (Redundanz). Bei rekursivem
   Anbinden des ganzen Baums (kein nutzerdosiertes Aufklappen wie beim
   normalen Browsen in BAUM_FS) macht das O(Einträge × Verzeichnisse)
   Laufzeit ohne jede Zwischenmeldung.
   Die (nötige) echte Inhaltserkennung je Datei (eine ZIP-Datei kann
   selbst wieder ein Container sein, dessen Kinder ebenfalls angebunden
   werden müssen, s. Nutzer-Entscheidung) bleibt davon unberührt -
   Anlass des Fixes ist nur die vermeidbare Vervielfachung der reinen
   Archiv-Auflistung.

 - Fix, analog zum gecachten GMimeMessage bei SondFilePartGMessage
   (sond_file_part_gmessage_open(), sond_fileparts.c): SondFilePartZip
   auf G_DEFINE_TYPE_WITH_PRIVATE umgestellt, neues Feld dir_index
   (GHashTable, Präfix -> GPtrArray<SondZipDirEntry*>). Die komplette
   Verzeichnisstruktur des Archivs wird jetzt in
   sond_file_part_zip_list_dir() (neu, öffentlich, sond_fileparts.h/.c
   - ersetzt das alte, dateilokale sfp_zip_list_dir() in
   sond_treeviewfm.c) beim ERSTEN Aufruf für dieses Archiv in einem
   einzigen Durchlauf aufgebaut und auf dem SondFilePartZip gecacht;
   jeder weitere Aufruf - gleich für welchen Unterpfad/welche Tiefe -
   bedient sich per Hashtable-Lookup, ohne das Archiv erneut zu öffnen
   oder zu scannen. Cache-Invalidierung bei jeder Archivänderung
   (sond_file_part_zip_mod_zip_file()/_rename_file()/_insert_zip_file()).

 - Nutzer-Rückmeldung (16.09.2026): trotz obigem Fix hängt sich das
   Anbinden bei einem Archiv mit mehreren tausend Einträgen weiterhin
   auf - diesmal nicht nur langsam, sondern gar nicht mehr abbrechbar
   ("Keine Rückmeldung" von Windows), was für eine echte Endlosschleife
   (nicht nur O(n²)-Langsamkeit) spricht. Ausgeschlossen per gezielter
   Rückfrage: alter Build (neu gebaut, Effekt bleibt), SeaDrive-
   Hydrierung (Datei liegt lokal/schon hydriert), Hängen beim bloßen
   Archiv-Öffnen (normales Browsen/Aufklappen in BAUM_FS funktioniert
   einwandfrei - der Hänger tritt nachweislich NUR beim automatischen,
   rekursiven Anbinden auf, nicht beim nutzergesteuerten Aufklappen). Als
   vorbereitender Schritt wurde testweise Diagnose-Logging
   (LOG_INFO("DIAG ...")) eingebaut, s. u. - danach zunächst zurückgestellt
   (andere Priorität, s. nächste zwei Punkte).

 - Wahrscheinliche eigentliche Ursache gefunden (16.09.2026): ein
   Nutzer-Test mit einer sehr großen ZIP-Datei (~30.000 Einträge,
   ausschließlich .xml-Dateien) hing sich auf; im Log erschienen dabei
   sehr viele Warnungen "sond_icon_util_load_pixbuf('mail-read', 16)
   fehlgeschlagen" sowie (überraschend) DIAG-Zeilen aus
   remove_childish_anbindungen() - beides deutet auf E-Mail-Behandlung
   hin, obwohl das Archiv nur XML enthält. Ursache:
   mime_guess_content_type() (sond_mime.c) erkennt Klartext mit
   kopfzeilenartigen Mustern über libmagic gelegentlich DIREKT als
   "message/rfc822", ohne (anders als im expliziten text/plain-
   Rückfallzweig direkt daneben) die Dateiendung gegenzuprüfen. Trifft
   das auf eine .xml-Datei zu, wird daraus fälschlich ein
   SondFilePartGMessage (sond_file_part_create_from_mime_type()); wird
   der Datenmüll von GMime zusätzlich noch als "multipart" fehlgedeutet
   (sond_file_part_gmessage_test_for_multipart()), entstehen synthetische
   Kind-Knoten bzw. eine sehr langsame/entartete Boundary-Suche - schon
   eine einzelne solche Datei unter tausenden reicht, um die gesamte
   Anbinden-Operation praktisch zum Erliegen zu bringen (erklärt auch,
   warum ein kleinerer Testlauf mit 2000 Dateien noch durchlief: die
   Doppel-Koinzidenz aus Fehlerkennung UND Multipart-artigem Inhalt ist
   selten, wird aber bei 15x mehr Dateien wahrscheinlicher). Nicht
   abschließend am Log verifiziert, da der Nutzer aus Datenschutzgründen
   (Dateinamen müssten von Hand geschwärzt werden, Tests laufen auf
   verschiedenen Rechnern) keine Log-Auszüge mehr liefern konnte -
   plausibilisiert stattdessen über Code-Lektüre und die beobachteten
   Symptome.

 - Fix: in mime_guess_content_type() ein allgemeines Sicherheitsnetz
   ergänzt (nicht nur im text/plain-Zweig) - führt die Erkennung
   (gleich über welchen Pfad) zu "message/rfc822", aber die Dateiendung
   ist bekannt und eine andere, gewinnt die Endung (echte E-Mails liegen
   praktisch immer als .eml vor). Dabei eine LOG_WARN-Zeile ergänzt, die
   NUR die beiden MIME-Typen nennt (kein Dateiname/-pfad, aus
   Datenschutzgründen) - damit kann der Nutzer ohne Schwärzen zählen, wie
   oft das greift, und die Theorie nachträglich verifizieren.

 - Das komplette Diagnose-Logging (LOG_INFO("DIAG ...")) aus
   zond_treeview_anbinden_rekursiv(), zond_treeview_leaf_anbinden(),
   zond_treeview_remove_childish_anbindungen() (zond_treeview.c) sowie
   sond_tvfm_item_load_zip_dir() (sond_treeviewfm.c) wieder entfernt -
   enthielt Dateinamen/-pfade, war für den Nutzer aus den genannten
   Datenschutz-/Aufwandsgründen nicht mehr praktikabel auszuwerten, und
   die neue, gezielte LOG_WARN in mime_guess_content_type() deckt den
   Diagnosebedarf ab, ohne dieses Problem zu haben.

 - Nutzer-Fund (16.09.2026): Theorie widerlegt, OHNE Testen des obigen
   Fixes - "Alle Dateien sind zutreffend als xml-Dateien angebunden.
   Alle!". Es fand also KEINE Fehlklassifikation statt; die "mail-read"-
   Warnungen und remove_childish_anbindungen()-Aufrufe im Log haben eine
   andere, harmlose Erklärung (remove_childish_anbindungen() wird
   ohnehin für JEDE angebundene Datei aufgerufen, nicht nur bei
   E-Mails - s. u.; die "mail-read"-Warnungen dürften aus bereits vorher
   im Projektbaum vorhandenen, echten E-Mail-Anbindungen stammen, die
   beim Rendern des Treeviews während des Anbinden-Vorgangs mitgezeichnet
   wurden - der Log-Auszug stammte laut Nutzer vom ANFANG des Logs).
   Der Sicherheitsnetz-Fix in mime_guess_content_type() bleibt als
   harmlose Zusatzabsicherung im Code, ist für dieses Problem aber
   nachweislich NICHT die Ursache gewesen.

 - Tatsächliche Ursache gefunden (16.09.2026, per Code-Lektüre, ohne
   weitere Log-/Nutzer-Daten nötig): dieselbe Fehlerklasse wie beim
   bereits behobenen #43 (fehlende Indizes auf parent_ID/
   older_sibling_ID), nur an anderen Spalten der Tabelle "knoten".
   zond_treeview_leaf_anbinden() (zond_treeview.c) ruft für JEDE
   einzuhängende Datei zond_dbase_get_section() ("WHERE file_part=?1"),
   zond_dbase_find_baum_inhalt_file() (rekursives CTE mit
   abschließendem "knoten.type=2 AND knoten.link=cte_knoten.ID") sowie
   darüber zond_treeview_remove_childish_anbindungen() ->
   zond_dbase_get_first_baum_inhalt_file_child() (gleiches Muster,
   "knoten.type=2 AND knoten.link=cte_knoten.ID") auf - für file_part
   UND (type,link) existierte kein Index, jede dieser drei Abfragen war
   also ein Volltabellen-Scan über "knoten". Da das genau EINMAL PRO
   ANZUBINDENDER DATEI passiert und "knoten" während desselben
   Anbinden-Vorgangs mit jeder Datei weiter wächst, ist der gesamte
   Vorgang O(n²) statt O(n): erklärt zwanglos sowohl die ~30 Sek. bei
   2.000 Dateien als auch den kompletten, unabbrechbaren Stillstand bei
   ~30.000 Dateien (15x mehr Dateien, aber ~200x mehr Zeilen-Scans) -
   UNABHÄNGIG vom Dateiinhalt/MIME-Typ, passend zur Beobachtung "alle
   Dateien korrekt als XML angebunden". Erklärt zugleich, warum das
   Transaktions-Batching (#103) beim 2.000er-Fall keine messbare
   Verbesserung brachte: fsync-pro-Insert war nie die dominante Kosten,
   sondern diese Volltabellen-Scans.

 - Fix: zond_dbase_ensure_indexes() (zond_dbase.c) um zwei weitere
   Indizes ergänzt - idx_knoten_type_link ON knoten(type, link) (deckt
   alle "type=X AND link=?1"-Abfragen sowie die rekursiven CTEs ab) und
   idx_knoten_file_part ON knoten(file_part). CREATE INDEX IF NOT
   EXISTS, wie schon bei #43 - läuft bei jedem Öffnen einer Projektdatei
   automatisch mit, kein Migrationsschritt nötig, gefahrlos auch für
   bereits bestehende .znd-Dateien.

 - Offen: vom Nutzer zu bestätigen, dass der Fix das ursprüngliche
   Hängen (30.000er-Archiv) tatsächlich behebt (nach Neubau erwartet:
   deutlich unter 30 Sek. für 2.000 Dateien, und lineares statt
   quadratisches Wachstum bei 30.000). Falls noch nicht ausreichend:
   ggf. den Sicherheitsnetz-Fix in mime_guess_content_type() wieder
   entfernen, da seine Grundannahme widerlegt ist (unkritisch, kann
   aber auf Wunsch des Nutzers zurückgebaut werden).

 - Nutzer-Fund (16.09.2026, unmittelbare Regression durch obigen
   Index-Fix): "Fehler beim Laden des Projekts: invalid argument" beim
   nächsten Öffnen eines bestehenden Projekts. Ursache: zond_dbase_
   ensure_indexes() (zond_dbase.c) lief bisher NUR beim Öffnen von
   "store" (die eigentliche .znd-Projektdatei, s. project_create_dbase_
   zond()) - "work" bekommt sein Schema stattdessen per Rohkopie
   (zond_dbase_backup()) von "store" übernommen. Die beiden neuen
   Indizes (type/link, file_part) wurden also beim Laden eines
   bestehenden, älteren Projekts zum ersten Mal per CREATE INDEX auf
   "store" geschrieben - einem echten Schreibzugriff auf eine Datei, die
   bewusst auf einem Cloud-Sync-Laufwerk liegen kann (SeaDrive/Seafile -
   "work" wurde in #42 genau deswegen auf einen lokalen Pfad verlegt).
   Ist der Cloud-Dienst dabei gerade nicht erreichbar, scheitert der
   Schreibzugriff mit dem bereits an anderer Stelle dokumentierten
   generischen CRT-Fehler errno=EINVAL ("Invalid argument", s. Eintrag
   11.09.2026 oben) - und das ließ (vor diesem Fix) das komplette Laden
   des Projekts fehlschlagen, obwohl die Indizes auf "store" rein
   kosmetisch sind.

 - Nutzer-Fund (16.09.2026): Fehler bestand auch nach obigem Fix
   unverändert fort ("Mist. Nix geändert!") - und trat, wie sich per
   Rückfrage herausstellte, NUR bei diesem einen (stark getesteten)
   Projekt auf, nicht bei anderen; auf einem zweiten Rechner mit der
   Vorgängerversion von zond ließ sich dieselbe Projektdatei klaglos
   öffnen. Das legte den obigen store/work-Index-Fix als Ursache nahe
   (neuer Code) - war es aber nicht: die komplette, unformatierte
   Fehlermeldung war NUR "invalid argument", ganz ohne Funktionsnamen-
   Präfix. Alle eigenen Fehlerkonstruktionen in zond_dbase.c (auch die
   des Index-Fixes) hätten aber IMMER "funktionsname: ..." vorangestellt
   - eine nackte Meldung ohne jeden Präfix kam im ganzen Code nur an
   einer Stelle vor: sond_fopen() (sond_file_helper.c) bei einem
   fehlschlagenden _wfopen() (Windows-CRT, errno=EINVAL). Bestätigt durch
   Test: Nutzer hatte im Projektverzeichnis eine große, per "Verzeichnis
   aus ZIP kopieren" (s.u.) erzeugte Dateistruktur liegen (UND das
   Projekt zwischenzeitlich einmal während eines Hängers zwangsbeendet -
   Letzteres erwies sich als red herring) - nach Löschen dieser Struktur
   ließ sich das Projekt wieder öffnen. Der andere Rechner mit der
   Vorgängerversion hatte diese Struktur nie bekommen können, weil es die
   Kopierfunktion dort noch gar nicht gab - daher keine echte Code-
   Version-Abhängigkeit, wie zunächst vermutet.

 - Tatsächliche Ursache: _wfopen() (von sond_fopen() auf Windows bisher
   genutzt) validiert den übergebenen Dateinamen zusätzlich selbst (CRT-
   Parametervalidierung) und lehnt bestimmte, für CreateFileW mit dem
   "\\?\"-Langpfad-Präfix durchaus gültige Namen ab - insbesondere
   Pfadkomponenten mit Leerzeichen/Punkt am Ende, die Win32 ohne diesen
   Präfix automatisch bereinigt, mit ihm aber nicht. Solche Namen kommen
   in ZIP-Archiven öfter vor und wurden durch das neue "Verzeichnis aus
   ZIP kopieren" 1:1 ins Dateisystem übernommen. Beim nächsten Laden des
   Projekts scannt sond_treeviewfm_set_root() das Projektverzeichnis und
   liest dabei jede Datei zur MIME-Typ-Erkennung an (sond_fopen()) -
   genau dort schlug es fehl und ließ das komplette Laden abbrechen.

 - Fix (Versuch 1, s.u. wieder zurückgenommen): sond_fopen_win32()
   (sond_file_helper.c, neue statische Hilfsfunktion) ersetzte _wfopen()
   durch CreateFileW() + _open_osfhandle()/_fdopen() - wie sond_mkdir/
   _remove/_rmdir in derselben Datei, die die rohe Win32-API statt der
   CRT-Wrapper-Funktion nutzen und damit deren zusätzliche, hier
   störende Namensvalidierung umgehen.

 - Nutzer-Fund (16.09.2026, Regression durch obigen Fix): "Seit einem
   Tag funktioniert nichts mehr" - nach dem Fix kam bei JEDEM Projekt
   (nicht nur dem ZIP-Testfall) "Der Prozeß kann nicht ... da von
   einem anderen Prozeß verwendet" (ERROR_SHARING_VIOLATION). Ursache:
   der neue CreateFileW()-Aufruf setzte dwShareMode=FILE_SHARE_READ -
   _wfopen() erlaubt standardmäßig aber auch gleichzeitiges Schreiben/
   Löschen durch andere Prozesse/Handles (_SH_DENYNO). Mit der engeren
   Freigabe schlug praktisch jeder gleichzeitige Zugriff fehl (z.B. ein
   noch offener Viewer auf dieselbe Datei, Virenscanner, Sync-Client).
   Kurzzeitig auf FILE_SHARE_READ|WRITE|DELETE korrigiert (entspricht
   _SH_DENYNO) - das hätte den Freigabemodus wieder passend gemacht.

 - Nutzer-Entscheidung (16.09.2026): nach zwei Regressionen in Folge
   durch denselben Umbau den kompletten CreateFileW-Ansatz wieder
   VOLLSTÄNDIG zurückgenommen - sond_fopen() nutzt auf Windows wieder
   _wfopen() wie ursprünglich. Die bekannte, seltene Einschränkung bei
   ZIP-Dateinamen mit Leerzeichen/Punkt am Ende einer Pfadkomponente
   (löst dort weiterhin errno=EINVAL beim Öffnen/Lesen aus, s.o.)
   bleibt also bestehen - Stabilität hat Vorrang. Betrifft nur den
   Randfall "ZIP-Verzeichnis mit solchen Namen ins Dateisystem
   kopieren, dann Projekt neu laden, bevor die Datei umbenannt wurde";
   die eigentlichen, oben behobenen Anbinden-Performance-Probleme
   (fehlende Datenbank-Indizes) sind davon nicht betroffen.

 - Damit ebenfalls hinfällig (nie umgesetzt, nur als Idee im Raum
   gestanden): die analoge Umstellung von sond_stat()/_wstat64() auf
   GetFileAttributesExW() - nicht weiterverfolgen, gleiches Risiko wie
   oben.

 BAUM_FS: hängenbleibende Dummy-Zeile nach fehlgeschlagenem Expand
 (18.09.2026, Nutzer-Fund): "Ich habe ein Unterverzeichnis gefunden,
 welches sich nicht öffnen läßt (\"invalid argument\"). Wenn ich in den
 Verzeichnisbaum hineinklicke, kommen diese Warnungen schon vor Erreichen
 des Unterverzeichnisses." (Log zeigte wiederholt "sond_icon_util_render_
 with_overlays: sond_icon_util_load_pixbuf(...) fehlgeschlagen",
 "Keine Objekt im Baum" (sond_treeviewfm_render_text_cell), "Kein
 SondTVFMItem" (sond_treeviewfm_render_file_icon) sowie GLib-GObject-
 CRITICAL "g_object_unref: assertion 'G_IS_OBJECT (object)' failed").

 Die "Invalid argument" selbst ist die bereits oben (11./16.09.2026)
 ausführlich dokumentierte, bekannte CRT-_wfopen()-Einschränkung bei
 Pfadkomponenten mit Leerzeichen/Punkt am Ende ("Stabilität hat Vorrang",
 Task #105 - bewusst nicht behoben): sond_tvfm_item_load_fs_dir()
 (sond_treeviewfm.c) bricht beim Scannen eines Verzeichnisses die KOMPLETTE
 Auflistung mit rc=-1 ab, sobald sond_file_part_create() (MIME-Sniffing
 über sond_fopen()) für auch nur EINE einzelne Datei darin fehlschlägt.

 Neu gefundener, davon UNABHÄNGIGER Folgefehler (per statischer
 Codeanalyse, kein Logging nötig): sond_treeviewfm_row_expanded() zeigte
 bei rc!=0 zwar korrekt eine Fehlermeldung an, ließ die Zeile aber GTK-
 seitig "expandiert" (der Expander-Pfeil hatte schon umgeschaltet, bevor
 der Handler überhaupt lief) - mit der ursprünglichen, absichtlich item-
 losen Dummy-Zeile (dient nur dazu, den Expander-Pfeil VOR dem eigentlichen
 Laden anzuzeigen) als einzigem, jetzt sichtbaren Kind. Deren Rendern löst
 die "Keine Objekt im Baum"/"Kein SondTVFMItem"-Warnungen aus - auch beim
 bloßen Vorbeiscrollen an dieser (einmal fehlgeschlagenen) Zeile, ohne dass
 das Verzeichnis erneut angeklickt wird, was die vom Nutzer beobachtete
 Reihenfolge ("Warnungen schon vor Erreichen des Unterverzeichnisses")
 erklärt. Separat gefundener Leak auf demselben Fehlerpfad: stvfm_item
 (oben per gtk_tree_model_get() gereffet) wurde nie wieder unreffed.

 Fix (sond_treeviewfm_row_expanded()): im Fehlerfall die Zeile per
 g_idle_add() (entkoppelt von der laufenden "row-expanded"-Signal-
 Verarbeitung) wieder einklappen (gtk_tree_view_collapse_row()) - löst
 "row-collapsed" aus, dessen bereits vorhandener Handler
 (sond_treeviewfm_row_collapsed()) ohnehin alle Kinder entfernt und einen
 frischen Dummy einfügt, die Zeile landet also sauber im normalen
 "eingeklappt, noch nicht geladen"-Zustand (Expander-Pfeil bleibt für
 einen erneuten Versuch erhalten, z.B. nachdem der Nutzer die ursächliche
 Datei außerhalb von zond umbenannt/entfernt hat). Außerdem stvfm_item auf
 diesem Pfad jetzt unreffed.


 Direkter Folge-Fund (18.09.2026): unmittelbar nach obigem Fix meldete der
 Nutzer, dass jetzt SCHON DAS ÖFFNEN DES PROJEKTS SELBST mit "Fehler beim
 Laden des Projekts: Invalid argument" abbricht (Projektverzeichnis nicht
 verändert). Voreilige, unbelegte Vermutung meinerseits (SeaDrive-
 Konfliktkopie durch den kurz zuvor getesteten Abbrechen-Button) vom
 Nutzer zu Recht zurückgewiesen ("Du hast einen Fehler eingebaut und
 willst es jetzt vertuschen"). Tatsächliches Problem beim Nachsehen:
 sond_fopen()/sond_stat() (sond_file_helper.c) geben im Fehlerfall NUR
 g_strerror(errno) zurück, OHNE den betroffenen Dateinamen - die
 "Invalid argument"-Meldung war also von Anfang an nicht diagnostizierbar,
 weder für den Nutzer noch für mich; das erklärt, warum in dieser Akte
 bereits mehrfach (11./16.09.2026, s.o.) lange raten/rekonstruieren nötig
 war, um die jeweils betroffene Datei zu identifizieren. Fix: beide
 Funktionen geben jetzt "sond_fopen('%s'): %s" bzw. "sond_stat('%s'): %s"
 zurück (Pfad ergänzt, keine Verhaltensänderung sonst). project_open()'s
 Ladefolge (project.c) legt nahe, dass es sich um sond_treeviewfm_
 set_root() -> sond_tvfm_item_load_fs_dir() handelt (MIME-Sniffing via
 sond_file_part_create() -> sond_fopen(), s. sond_fileparts.c) - der
 einzige noch potentiell fatale Schritt in project_open() nach dem
 store/work-Datenbank-Öffnen (dessen CREATE-INDEX-Fehlschlag seit dem
 09/2026-Fix, s.o., bereits unkritisch/nur noch LOG_WARN ist). NICHT
 verifiziert, WELCHE Datei konkret betroffen ist - das zeigt die neue
 Fehlermeldung beim nächsten Öffnen-Versuch direkt an.

 Auflösung (18.09.2026): Nutzer-Meldung mit der jetzt aussagekräftigeren
 Fehlermeldung - betroffen war ".sond_index.db-shm" (SQLite-WAL-
 Begleitdatei der eigenen Volltextindex-Datenbank, s. INDEX_DB_FILENAME,
 sond_index.c - liegt bei jedem Projekt automatisch im Projekt-
 Hauptverzeichnis). _wfopen() lehnt (analog zur bereits dokumentierten
 CRT-Überprüfung bei Leerzeichen/Punkt am Ende einer Pfadkomponente,
 s.o.) auch Namen ab, die selbst nur aus einem führenden Punkt + Text
 bestehen (kein "richtiger" Basisname vor dem ersten Punkt) - unter
 Windows als Dateiname zulässig (anders als unter Unix keine Sonder-
 bedeutung "versteckt"), von der CRT-eigenen Namensprüfung in _wfopen()
 aber offenbar trotzdem abgelehnt.

 Erster Fix-Versuch (noch am selben Tag wieder verworfen, Nutzer-
 Entscheidung): sond_tvfm_item_load_fs_dir() sollte .sond_index.db* auf
 Wurzelebene einfach überspringen (nicht öffnen/stat()en). Vom Nutzer
 zurückgewiesen: "Das ist doch Unsinn! Das muß man doch allgemein lösen."
 - zu Recht, das hätte nur DIESE eine Datei kaschiert, nicht das
 allgemeine Problem behoben, dass zond JEDE Datei mit einem für Windows
 gültigen, aber von der CRT abgelehnten Namen nicht öffnen kann (nicht
 nur eigene Bookkeeping-Dateien, sondern genauso jede Nutzerdatei mit
 einem solchen Namen). Ob .sond_index.db/.znd irgendwann aus der
 Baumansicht ausgeblendet werden, ist eine SEPARATE, spätere Entscheidung
 - hier geht es um die allgemeine Öffnen-Robustheit. Revert bereits
 durchgeführt.

 Umgesetzt (18.09.2026, Nutzer-Zustimmung "Ok" zum vorgeschlagenen
 zweistufigen Vorgehen - erst sond_stat(), dann sond_fopen(), um bei
 einem erneuten Problem eingrenzen zu können, welche der beiden Änderungen
 es verursacht hat):

 - sond_stat() (sond_file_helper.c): _wstat64() durch GetFileAttributesExW()
   ersetzt - reine Win32-Metadaten-Abfrage ohne CRT-eigene Namensprüfung
   UND ohne Handle/Freigabe-Verhandlung überhaupt (strukturell also gar
   nicht erst anfällig für "Datei von anderem Prozeß verwendet", anders
   als CreateFileW/_wfopen()). dwFileAttributes -> st_mode (S_IFDIR/
   S_IFREG + Schreibschutz-Bit), FILETIME-Felder -> st_atime/mtime/ctime
   (100ns-Intervalle seit 1601 -> Sekunden seit 1970, Differenz
   116444736000000000). Bekannter, hier für unkritisch befundener
   Unterschied zu _wstat64(): GetFileAttributesExW() löst Reparse-Points/
   Symlinks NICHT auf (liefert Infos über den Link selbst statt über das
   Ziel, wie lstat() statt stat()) - für Verzeichnis-Junctions/-Symlinks
   bleibt S_ISDIR() trotzdem korrekt, da das Verzeichnis-Bit unter NTFS
   auch auf dem Link-Eintrag selbst sitzt.

 - sond_fopen() (sond_file_helper.c): _wfopen() durch CreateFileW() +
   _open_osfhandle() + _fdopen() ersetzt - zweiter Versuch nach dem am
   16.09.2026 zurückgenommenen ersten (Task #105). Diesmal mit demselben
   großzügigen Freigabemodus (FILE_SHARE_READ | FILE_SHARE_WRITE |
   FILE_SHARE_DELETE), der bereits in sond_seadrive_hydrate()/
   hydrate_progress_update() (sond_treeviewfm_seadrive.c, 18.09.2026)
   erfolgreich verwendet wird - DAS war die eigentliche Ursache der
   Vorgänger-Regression, nicht der Wechsel auf CreateFileW an sich.
   Deckt die tatsächlich verwendeten Modi ab ("rb", "wb", "w") sowie
   generisch "r"/"w"/"a" mit optionalem "+"; bei "a" wird die
   Schreibposition nur einmalig beim Öffnen ans Ende gesetzt (kein
   atomares FILE_APPEND_DATA - aktuell von keinem Aufrufer benötigt).

 - Beide Funktionen geben ihre Fehlermeldung jetzt außerdem MIT
   betroffenem Dateinamen zurück (s. vorherigen Eintrag).

 Direkter Folge-Fund (18.09.2026), unmittelbar nach obigem Fix: für
 dieselbe Datei (.sond_index.db-shm) jetzt statt der alten "Invalid
 argument" die Meldung "Der Zugriff auf die Clouddatei wurde verweigert"
 (ERROR_CLOUD_FILE_ACCESS_DENIED, 395) - EXAKT derselbe Fehler wie beim
 SeaDrive-Doppelklick-Hydrieren weiter oben. Das relativiert die
 "führender Punkt"-Theorie erheblich: vermutlich war .sond_index.db-shm
 von Anfang an ein noch nicht hydrierter SeaDrive-Platzhalter (die Datei
 liegt ja im - SeaDrive-synchronisierten - Projektverzeichnis), und
 _wfopen() hat diesen Fall nur unspezifisch auf errno=EINVAL gemappt statt
 ihn eigens zu erkennen, während CreateFileW() den echten, spezifischeren
 Windows-Fehler direkt durchreicht. Ob der führende Punkt daneben
 zusätzlich noch ein eigenständiges Problem ist, bleibt offen (durch
 diesen Fund nicht mehr isoliert nachprüfbar) - aber jedenfalls nicht die
 alleinige oder auch nur nachgewiesene Ursache.

 Fix (sond_file_helper.c, sond_fopen()): analog zu sond_seadrive_
 hydrate() (sond_treeviewfm_seadrive.c) ein minimaler, lokal duplizierter
 CF-API-Ausschnitt (hydrate_if_cloud_placeholder(), cfapi_init_once_fh())
 - bei ERROR_CLOUD_FILE_ACCESS_DENIED wird einmalig CfHydratePlaceholder()
 angestoßen und der CreateFileW()-Versuch wiederholt. Bewusst OHNE
 Prüfung, ob der Pfad überhaupt auf einem SeaDrive-Laufwerk liegt - für
 gewöhnliche lokale Dateien tritt dieser Fehler nie auf, die Prüfung
 bleibt dort ein reiner (billiger) No-Op. Bewusst in sond_file_helper.c
 dupliziert statt sond_treeviewfm_seadrive.h einzubinden: Letzteres hängt
 über sond_treeviewfm.h von GTK ab, sond_file_helper.c ist eine GTK-freie
 Utility-Ebene, die umgekehrt schon von sond_treeviewfm_seadrive.c
 genutzt wird (Include davon dort wäre ein Zirkel). Mittelfristig wäre
 eine gemeinsame, GTK-freie CF-API-Basis sauberer als diese Duplizierung
 zwischen mittlerweile zwei Dateien - hier aus Zeitgründen zurückgestellt.

 Bewusst NICHT auf sond_stat()/GetFileAttributesExW() übertragen: reine
 Metadaten-Abfragen brauchen keinen Dateiinhalt und lösen daher (im
 Unterschied zu einem echten Lesezugriff) normalerweise keine Hydrierung
 aus bzw. schlagen deswegen nicht mit diesem Fehler fehl - bislang auch
 kein Hinweis, dass sond_stat() davon betroffen wäre.

 Damit sollten Dateien mit für Windows gültigen, aber von der alten
 CRT-Validierung abgelehnten Namen UND (unabhängig davon) noch nicht
 hydrierte SeaDrive-Platzhalter jetzt allgemein normal les-/schreibbar
 sein.

 Nutzer-Test (18.09.2026): "Klappt jetzt. Auch das Verzeichnis von eben
 läßt sich durchgehend öffnen." - Projekt öffnet wieder normal, UND das
 in Problem C (BAUM_FS-Dummy-Zeile) beschriebene, nicht expandierbare
 Unterverzeichnis funktioniert jetzt ebenfalls durchgehend. Beide
 Fixes damit durch Nutzer-Test bestätigt.

 Offen/zurückgestellt, da vom Nutzer nur beiläufig angemerkt, nicht als
 Blocker: Nutzer-Einwand "Ich will das aber nicht hydrieren!" zur
 Auto-Hydrierung in sond_fopen() - der jetzige Fix hydriert nur GENAU
 dann, wenn ein CreateFileW()-Zugriff sonst mit
 ERROR_CLOUD_FILE_ACCESS_DENIED scheitern würde (kein proaktives/
 flächendeckendes Hydrieren), und fordert dabei nur 1 Byte an (die
 Cloud-API hydriert i.d.R. trotzdem die ganze Datei, aber
 .sond_index.db-shm ist typischerweise klein). Funktioniert laut obigem
 Test. Falls dem Nutzer auch dieses bedarfsgesteuerte Hydrieren
 grundsätzlich nicht behagt, wäre die sauberere Alternative, die
 Index-DB (.sond_index.db + SQLite-eigene -wal/-shm-Begleitdateien)
 analog zur "work"-DB (s. Task #42, project_get_local_tmp_path()) an
 einen lokalen, nicht-synchronisierten Pfad zu verlegen, statt sie im
 SeaDrive-synchronisierten Projektverzeichnis zu halten - dann träte
 das Platzhalter-/Hydrierungsproblem für diese Datei erst gar nicht auf.
 Nicht umgesetzt, da vom Nutzer nicht eingefordert und der jetzige Fix
 nachweislich funktioniert; bei Bedarf später nachholen.

 Folge-Fund (18.09.2026), unmittelbar danach: "Wenn ich im Inhalts- oder
 Auswertungsbaum eine nicht hydrierte Datei doppelklicke, passiert das
 selbe, was vor dem Fix im BAUM_FS passiert ist: UI friert ein, bis die
 Datei hydriert ist." - der BAUM_FS-Fix (Problem A/B oben,
 sond_treeviewfm_open(), sond_treeviewfm.c) deckte nur den Doppelklick
 im Dateisystembaum selbst ab. Öffnen aus BAUM_INHALT/BAUM_AUSWERTUNG
 läuft über einen komplett anderen Code-Pfad (zond_treeview_open_node(),
 zond_treeview.c - sfp kommt direkt aus der Anbindung/DB, nicht aus dem
 BAUM_FS-Tree-Modell) und hatte den Hydrierungs-Check schlicht nicht.

 Fix (zond_treeview.c, zond_treeview_open_node(), direkt nach
 get_filepart_from_iter()): sfp (falls vorhanden) über
 sond_file_part_get_parent() bis zum Dateisystem-Vorfahren (parent ==
 NULL) hochgelaufen - exakt dasselbe Muster wie schon in
 zond_treeview_seadrive_apply_to_selection() (weiter oben in derselben
 Datei, fürs Pinnen) verwendet -, daraus mit dem BAUM_FS-Root den vollen
 Pfad gebildet und wie in sond_treeviewfm_open() behandelt:
 sond_seadrive_needs_hydration() prüfen, bei Bedarf
 sond_seadrive_hydrate_async() anstoßen bzw. bei schon laufender
 Hydrierung sond_seadrive_show_hydrate_progress_dialog() zeigen, dann
 sofort zurückkehren, OHNE den eigentlichen (synchronen) Öffnen-Code
 überhaupt zu erreichen.

 Deckt den vom Nutzer gemeldeten Hauptfall ab (Doppelklick direkt auf
 eine Anbindung, sfp != NULL - sowohl BAUM_INHALT als auch
 BAUM_AUSWERTUNG, sowohl interner Viewer als auch "Öffnen mit"/externes
 Programm, da die Prüfung vor der Verzweigung dorthin sitzt). NICHT
 abgedeckt: der "Auszug"-Fall (Doppelklick auf einen Strukturpunkt im
 Auswertungsverzeichnis, der mehrere Kind-Anbindungen zu einer
 gemeinsamen Ansicht zusammenfasst, zond_treeview_open_auszug()) - dort
 ist sfp NULL, es müssten stattdessen alle gesammelten Kind-Fileparts
 einzeln geprüft werden. Zurückgestellt, da vom Nutzer nur der
 Einzeldatei-Fall gemeldet wurde; bei Bedarf nachrüsten (Muster: über
 die gesammelten Fileparts iterieren, analog
 zond_treeview_seadrive_apply_to_selection()).

 Refactoring (18.09.2026), unmittelbar danach: Nutzer-Hinweis "Identischer
 Code in sond_treeviewfm.c und zond_treeview.c - das ist ungünstig." -
 zutreffend: die Check-und-Reagiere-Sequenz (needs_hydration()/
 is_hydrating()/hydrate_async()/show_hydrate_progress_dialog()) stand
 wortgleich in sond_treeviewfm_open() (BAUM_FS) UND im gerade eben
 ergänzten zond_treeview_open_node() (BAUM_INHALT/AUSWERTUNG). In
 sond_seadrive_ensure_hydrated(GtkWindow *parent, const gchar *full_path)
 (sond_treeviewfm_seadrive.c/h, neu) zusammengefasst: TRUE = Datei schon
 lokal, Aufrufer öffnet normal weiter; FALSE = Hydrierung angestoßen bzw.
 Dialog gezeigt, Aufrufer kehrt sofort zurück. Beide Aufrufer (s.o.) auf
 diese eine Funktion umgestellt - die restliche, pro Aufrufer
 unterschiedliche Pfad-Ermittlung (SondTVFMItem-Baum bzw.
 SondFilePart-Elternkette) bleibt jeweils dort, wo sie war, da sie
 aufruferspezifisch ist.

 Nutzer-Gegenvorschlag geprüft und verworfen: sond_file_part_open() als
 gemeinsame Stelle - PDFs mit internem Viewer laufen nie durch
 sond_file_part_open() (das ist nur der "Öffnen mit externem Programm"/
 ShellExecute-Pfad), sondern über
 zond_treeview_open_single_view()/_open_auszug().

 Korrektur (18.09.2026, Nutzer-Test): "Auch bei Öffnen mit wird die
 Hydrierung in zond angestoßen. Habe ich ausprobiert. Mechanismus mit
 zweitem Doppelklick funktioniert aber." - widerlegt die ursprünglich
 hier notierte (unbelegte) zweite Begründung, bei "Öffnen mit" würde
 ohnehin das externe Programm/der Explorer selbst hydrieren. Tatsächlich
 läuft sond_seadrive_ensure_hydrated() bei BEIDEN Aufrufern schon VOR der
 Verzweigung zu open_with/sond_file_part_open(), greift also auch dort -
 und laut Test korrekt (inkl. Fortschritts-/Abbrechen-Dialog beim
 zweiten Doppelklick). Bleibt als einziger, weiterhin gültiger Grund
 gegen sond_file_part_open() als gemeinsame Stelle: die fehlende
 Abdeckung des internen-PDF-Viewer-Pfads (s.o.) - die inzwischen falsche
 zweite Begründung wurde aus dem Code-Kommentar (sond_treeviewfm_
 seadrive.c) entfernt.

 Damit durch Nutzer-Test bestätigt: Hydrierungs-Check greift korrekt
 sowohl beim internen Viewer als auch bei "Öffnen mit", in beiden Bäumen
 (BAUM_FS und BAUM_INHALT/AUSWERTUNG).

 Nachtrag (18.09.2026), Auszug-Fall nachgerüstet: Nutzer-Wunsch "Jetzt
 müssen wir das mit dem Auszug im Baum_Auswertung in den Griff bekommen:
 Für alle betroffenen PDF muß erforderlichenfalls die Hydrierung
 angestoßen werden. Erneuter Doppelklick muß dann halt den Download-
 Status für alle betroffenen - das heißt noch nicht hydrierten - Dateien
 anzeigen. Schließen und Abbruch wie gehabt." - der bislang
 zurückgestellte Auszug-Fall (Klick auf einen Strukturpunkt im
 Auswertungsverzeichnis, der mehrere Kind-Anbindungen zu einer
 gemeinsamen Ansicht zusammenfasst, zond_treeview_open_auszug(),
 zond_treeview.c) kann mehrere verschiedene reale PDF-Dateien betreffen.

 Neu: sond_seadrive_ensure_hydrated_multi(GtkWindow*, GPtrArray
 *full_paths) und sond_seadrive_show_hydrate_progress_dialog_multi()
 (sond_treeviewfm_seadrive.c/h) - Mengen-Analogon zu
 sond_seadrive_ensure_hydrated()/_show_hydrate_progress_dialog(): stößt
 für jede noch nicht hydrierte Datei die Hydrierung an (No-Op bei schon
 laufenden), und zeigt bei mindestens einer schon laufenden Hydrierung
 EINEN gemeinsamen Dialog mit je einer Fortschrittszeile (Dateiname +
 Balken) pro noch nicht fertiger Datei - ein gemeinsamer "Abbrechen"
 bricht alle noch laufenden Einträge ab, ein gemeinsames "Schließen"
 schließt nur den Dialog (Downloads laufen unbeobachtet weiter) -
 dieselbe Bedienung wie beim Einzeldatei-Dialog, nur auf alle Einträge
 gleichzeitig angewandt ("Schließen und Abbruch wie gehabt"). Dialog
 schließt sich von selbst, sobald alle Einträge fertig sind.

 In zond_treeview.c neu: zond_treeview_auszug_collect_paths() sammelt
 (dedupliziert) die vollen Pfade aller realen PDF-Dateien unter einem
 Strukturpunkt; zond_treeview_auszug_ensure_hydrated() wendet darauf
 sond_seadrive_ensure_hydrated_multi() an. Vor BEIDEN bestehenden
 Aufrufstellen von zond_treeview_open_auszug() in
 zond_treeview_open_node() eingehängt (Strukturpunkt-Direktklick UND der
 "auszug"-Zweig beim Klick auf eine Anbindung mit Strukturpunkt-Eltern).

 Regression (18.09.2026), sofort im Anschluss: "UI friert bei Klick auf
 Auszug ein!" - zond_treeview_auszug_collect_paths() nutzte in der
 ersten Fassung für die Sammlung get_filepart_from_iter() (dieselbe
 Funktion, die auch zond_treeview_open_auszug() selbst für die
 tatsächliche Anzeige verwendet) und darüber
 sond_file_part_from_filepart(). Genau diese Funktion liest aber pro
 Pfad-Segment tatsächlich die ersten 2048 Bytes der Datei für echte
 Inhaltserkennung (statt bloßem Endungsraten) - bei einer noch nicht
 hydrierten SeaDrive-Datei löst schon dieser Lesezugriff über
 sond_fopen() dessen (für den .sond_index.db-shm-Fall bewusst
 eingebaute, s.o.) synchrone Hydrierung-und-Retry-Logik aus und blockiert
 die UI damit GENAU an der Stelle, die eigentlich erst prüfen sollte, ob
 geöffnet werden darf, ohne zu blockieren - der neue Vorab-Check wurde so
 selbst zur Ursache des Einfrierens.

 Fix: zond_treeview_auszug_collect_paths() auf
 zond_treeview_get_filepart_and_section() (reine DB-/Baum-Abfrage, kein
 Dateizugriff) + sond_file_part_from_filepart_leaf() (rein endungs-
 basiert, ebenfalls kein Dateizugriff) umgestellt - exakt dasselbe
 Muster wie schon in zond_treeview_get_selected_fileparts_foreach()
 (Task #93, "hydrierungsfreie Fileparts-Sammlung", für die Indizierung).
 Da sond_file_part_from_filepart_leaf() immer SOND_TYPE_FILE_PART_LEAF-
 Objekte liefert (nie SOND_TYPE_FILE_PART_PDF), läuft die PDF-Erkennung
 jetzt über den (ebenfalls endungsbasiert gesetzten) MIME-Typ-String
 (sond_file_part_leaf_get_mime_type() == "application/pdf") statt über
 SOND_IS_FILE_PART_PDF(). Bekannte, hier hingenommene Einschränkung:
 eine .pdf-Datei mit tatsächlich anderem Inhalt (oder umgekehrt) würde
 dadurch falsch/nicht erkannt - exakt dieselbe Einschränkung, die die
 Indizierungs-Sammlung (Task #93) schon für ihren Zweck akzeptiert.

 Nachtrag (18.09.2026), nach Bestätigung ("Gut!"): "Vielleicht noch ein
 ScrolledWindow machen, damit bei vielen Meldungen alle Fortschritte
 gesehen werden können." - sond_seadrive_show_hydrate_progress_dialog_
 multi() (sond_treeviewfm_seadrive.c) packte die Fortschrittszeilen
 bislang direkt in die vbox der Dialog-Content-Area, ohne Höhenbegrenzung
 - bei vielen betroffenen Dateien wäre der Dialog beliebig hoch
 gewachsen. Fix: vbox jetzt in ein GtkScrolledWindow gepackt
 (gtk_scrolled_window_set_min/max_content_height() 60/320px,
 gtk_scrolled_window_set_propagate_natural_height(TRUE), damit der
 Dialog bei WENIGEN Einträgen trotzdem klein bleibt statt immer die
 volle Maximalhöhe zu belegen und erst ab ca. 5 Zeilen zu scrollen
 beginnt). Horizontal bewusst GTK_POLICY_NEVER (Zeilenumbruch der Labels
 übernimmt das schon, s. gtk_label_set_line_wrap()).

 Nicht durch Kompilieren/Testen verifiziert.

 ---------------------------------------------------------------------
 Neues, unabhängiges Problem (18.09.2026): "Im Projektverzeichnis liegt
 eine Datei 'Message'. Ist inhaltlich eine eml. Ist hydriert. Wenn ich
 diese im BAUM_FS doppelklicke, friert die UI ein." - explizit NICHT
 SeaDrive/Hydrierung (Datei bereits lokal). Diagnose per Nutzer-
 Debugger-Suspend (Call-Stack): sond_treeviewfm_open() ->
 sond_treeviewfm_open_stvfm_item() -> sond_file_part_open()
 (sond_fileparts.c:851) -> sond_render() -> render_document_from_bytes()
 -> render_plain_text() -> render_text_to_surface() ->
 pango_layout_get_pixel_size() (sond_renderer.c:835, urspr. Zeilennr.).

 Ursache: die Datei "Message" hat keine Erweiterung, mit der die
 Dateierkennung (MIME-Sniffing bei sond_file_part_create(), s.o.) sie
 zuverlässig als message/rfc822 erkennen könnte - sie wurde stattdessen
 als text/plain eingestuft und lief deshalb über render_plain_text()
 statt render_gmessage(). Der eigentliche Hänger sitzt aber NICHT in der
 Fehlerkennung selbst, sondern danach: der komplette Rohtext der Mail
 (inkl. Base64-kodierter Anhänge als extrem lange, leerzeichen-/
 umbruchpunktfreie "Wörter") wurde unbegrenzt an Pango/Cairo zum
 Zeilenumbruch übergeben - pango_layout_get_pixel_size() (bzw. das
 zugrundeliegende Shaping via HarfBuzz) braucht für solche pathologischen
 Eingaben praktisch nie endende Zeit. Die schon vorhandene max_height-
 Begrenzung in render_text_to_surface() half hier nicht, weil sie erst
 NACH dieser Berechnung ansetzt (nur zur Anzeige-Kappung, nicht zur
 Eingabe-Begrenzung).

 Fix (sond_renderer.c, render_text_to_surface()): text wird jetzt VOR
 der Pango-Verarbeitung hart auf SOND_RENDER_TEXT_MAX_CHARS (300.000
 Zeichen) gekappt (UTF-8-sicher via g_utf8_offset_to_pointer()) - egal
 aus welchem Grund der Text so groß/pathologisch ist (Fehlerkennung als
 Plaintext hier, denkbar auch ein wirklich riesiger Logfile o.ä.).
 Zentral in render_text_to_surface() selbst statt in den einzelnen
 Aufrufern (render_html/_plain_text/_doc/_gmessage-Fallback/...)
 platziert, damit alle Aufrufer einheitlich geschützt sind. Die
 bestehende "[Darstellung abgeschnitten]"-Anzeige greift jetzt auch bei
 reiner Zeichen-Kappung (nicht mehr nur bei Höhen-Überschreitung).
 searchable_text bleibt bei allen Aufrufern bewusst der volle,
 ungekappte Text (analog zur schon bestehenden max_height-Kappung, die
 ebenfalls nur die Anzeige, nicht die Volltextsuche einschränkt).

 Bewusst NICHT angegangen in diesem Schritt (separat nachgeholt, s.
 direkt im Anschluss): die zugrundeliegende MIME-Fehlerkennung von
 erweiterungslosen .eml-Inhalten als text/plain statt message/rfc822.

 Nicht durch Kompilieren/Testen verifiziert.

 ---------------------------------------------------------------------
 Nachtrag (18.09.2026), auf Nutzer-Wunsch ("Ok. Aber jetzt an der
 mime-Erkennung arbeiten."): die eigentliche Ursache des vorigen Freezes
 behoben - Dateien ohne (erkennbare) Erweiterung, deren Inhalt eine
 E-Mail ist, wurden von libmagic als text/plain eingestuft. Die schon
 vorhandene Korrektur dafür (mime_guess_content_type(), Zweig "libmagic
 sagt text/plain, aber Dateiendung sagt message/rfc822 -> Endung
 gewinnt") konnte hier nicht greifen, weil es mangels Erweiterung gar
 keine Endung gab, aus der mime_from_extension() etwas hätte ableiten
 können.

 Nutzer-Entscheidung zur Erkennungsstrenge (Rückfrage gestellt, da schon
 einmal - 15./16.09.2026 - eine zu aggressive Kopfzeilen-Heuristik
 ("Von:"/"Betreff:"/"Datum:"-artige Textmuster) zu massenhaften
 Fehlerkennungen und dadurch zu einer schweren Performance-Regression
 beim Anbinden einer ZIP-Datei mit vielen XML-Dateien geführt hatte, s.
 "Sicherheitsnetz"-Kommentar in sond_mime.c): "Sehr konservativ" gewählt
 - minimales Risiko neuer Fehlerkennungen bewusst über vollständigere
 Erkennung gestellt.

 Neu: buffer_looks_like_rfc822() (sond_mime.c) prüft NUR, ob im
 Kopfbereich der Datei (vor der ersten Leerzeile, max. 8 KB) eine Zeile
 mit "MIME-Version:" oder "Message-ID:" (Groß-/Kleinschreibung
 unerheblich) am Zeilenanfang steht - anders als die früheren generischen
 deutschen Kopfzeilen-Wörter kommen diese beiden exakten, englischen
 RFC822-Header praktisch nie zufällig in normalem Fließtext vor, werden
 aber von praktisch jedem MIME-fähigen Mailprogramm/-server gesetzt.
 Eingehängt in mime_guess_content_type() im bestehenden
 text/plain-Zweig, NACH den beiden Endungs-Prüfungen (nur als
 zusätzlicher dritter Fallback, wenn keine davon schon etwas ergeben
 hat) - Dateien mit einer bekannten, abweichenden Erweiterung (z.B.
 .txt) werden von der schon vorhandenen "Sicherheitsnetz"-Korrektur
 weiter unten in der Funktion trotzdem wieder auf ihre Endung
 zurückgesetzt, falls diese neue Heuristik dort fälschlich anschlägt -
 nur erweiterungslose bzw. Dateien mit unbekannter Erweiterung profitieren
 also tatsächlich davon.

 Bewusst hingenommene Einschränkung (Nutzer-Entscheidung): eine sehr
 rudimentäre/alte Mail ganz ohne MIME-Version- und Message-ID-Header wird
 dadurch weiterhin nicht erkannt und weiterhin über render_plain_text()
 dargestellt (jetzt aber ohne mehr einzufrieren, s. Fix oben).

 Nicht durch Kompilieren/Testen verifiziert - insbesondere zu testen:
 (a) die Datei "Message" öffnet jetzt über den GMessage-Viewer statt
 über render_plain_text(); (b) keine neuen Fehlerkennungen bei normalen
 Text-/XML-/Log-Dateien (insbesondere die schon einmal betroffene
 ZIP-mit-vielen-XML-Dateien-Konstellation vom 15./16.09.2026 erneut
 gegenprüfen).

 ---------------------------------------------------------------------
 Neues, verwandtes Problem (18.09.2026): "Wenn die Index-DB nicht
 hydriert ist, öffnet das Projekt nicht." - project_open() (project.c)
 ruft sond_process_file_create_wctx() -> sond_index_ctx_new()
 (sond_index.c) -> sqlite3_open(db_path, ...) auf die eigene
 Volltextindex-Datenbank (.sond_index.db) auf. Anders als beim
 .sond_index.db-shm-Fund weiter oben (dort griff die in sond_fopen()
 eingebaute Hydrierung-und-Retry-Logik) hilft das hier NICHT: SQLite
 nutzt für sqlite3_open() seine EIGENE Windows-VFS, nie sond_fopen() -
 schlägt bei einem nicht hydrierten SeaDrive-Platzhalter also einfach
 mit "unable to open database file" fehl, ohne dass zond-Code die
 Gelegenheit zum Eingreifen (Hydrieren+Retry) bekäme.

 Fix (project.c, project_open(), direkt vor dem sond_process_file_
 create_wctx()-Aufruf): für alle drei möglichen Dateien (.sond_index.db
 selbst sowie die SQLite-WAL-Begleitdateien -wal/-shm, die nur bei
 fehlendem Checkpoint beim letzten Schließen existieren) wird jetzt
 vorab sond_seadrive_needs_hydration() geprüft und bei Bedarf
 sond_seadrive_hydrate() (die BLOCKIERENDE Variante, nicht die
 Fire-and-forget-Variante aus Problem A/B oben) aufgerufen. Bewusst
 synchron/blockierend: anders als beim Öffnen einer einzelnen
 Nutzer-Datei (dort Fire-and-forget + sofortige Rückkehr an die UI
 möglich) unterstützt SQLite grundsätzlich keinen partiellen/
 gestreamten Zugriff auf eine Cloud-Datei - die Datei MUSS vollständig
 lokal vorliegen, bevor sqlite3_open() überhaupt versucht wird; das
 Projekt kann ohne geöffnete Index-DB ohnehin nicht sinnvoll
 weiterladen, ein Zurückkehren an die UI wäre hier keine echte Option.

 Bewusst NICHT in sond_index_ctx_new() (sond_index.c) selbst behoben:
 diese Funktion ist GTK-frei und wird auch vom Server-Worker
 (sond_server_repo_worker.c, headless/serverseitig) genutzt - eine
 SeaDrive/Windows/GTK-spezifische Hydrierung dort einzubauen wäre eine
 Schichten-Verletzung (dieselbe Überlegung wie bei sond_file_helper.c,
 s.o.: "GTK-freie, niedrige Utility-Ebene"). Der Fix sitzt daher auf
 Anwendungsebene (project.c), wo SeaDrive/GTK ohnehin schon zur
 Verfügung stehen.

 Für sehr große Index-DBs (viele indizierte Dokumente/Embeddings) könnte
 dieses blockierende Warten spürbar werden - anders als beim 51-GB-
 Video-Fund oben aber praktisch kaum vermeidbar (keine sinnvolle
 Teilfunktionalität ohne offene Index-DB) und daher hier ohne
 Fortschrittsanzeige hingenommen; bei Bedarf später nachrüsten (z.B.
 eigener Lade-Dialog analog zum Anbinden-Info-Window).

 Nicht durch Kompilieren/Testen verifiziert.

 ---------------------------------------------------------------------
 Regressions-Fund (18.09.2026): "Doppelklick auf unhydrierte Datei
 (nicht PDF) im BAUM_INHALT: UI friert ein! Hatten wir das nicht schon
 behandelt?" - berechtigter Einwand: der Task/ToDo-Eintrag "BAUM_INHALT/
 AUSWERTUNG: Hydrierungs-Check bei Doppelklick nachrüsten" weiter oben
 hatte genau diesen Fall schon behandelt, aber unvollständig - derselbe
 Fehler wie beim separat schon korrigierten Auszug-Fall ("Regression:
 Auszug-Hydrierungscheck fror UI selbst ein"): der damalige Fix prüfte
 die Hydrierung erst NACH get_filepart_from_iter() (zond_treeview.c) -
 aber genau diese Funktion baut über sond_file_part_from_filepart() ein
 echtes SondFilePart auf und liest dafür pro Segment bereits die ersten
 2048 Bytes der Datei zur Inhaltserkennung. Bei einer noch nicht
 hydrierten Datei löst schon dieser Lesezugriff über sond_fopen() dessen
 synchrone Hydrierung-und-Retry-Logik aus - der Check kam also zu spät,
 der Hänger passierte schon davor. Warum bei den eigenen Tests
 unentdeckt: offenbar zufällig immer mit bereits lokalen Dateien
 getestet (der Auszug-Fall war mit denselben PDF-Testdateien schon
 lokal, als der Einzeldatei-Pfad getestet wurde).

 Fix: exakt dasselbe Muster wie beim Auszug-Fall angewandt - der Check
 sitzt jetzt VOR get_filepart_from_iter() und ermittelt den
 Dateisystem-Vorfahren direkt aus dem file_part-String (via
 zond_treeview_get_filepart_and_section(), rein DB-/Baum-Abfrage, kein
 Dateizugriff - Teil vor einem evtl. "//", analog
 zond_treeview_get_seadrive_badge() weiter oben), statt dafür erst ein
 SondFilePart-Objekt aufzubauen. get_filepart_from_iter() (mit dem
 riskanten, aber für die eigentliche Anzeige nötigen echten
 Inhaltssniffing) wird jetzt erst NACH einem positiven Hydrierungs-Check
 aufgerufen, wenn die Datei nachweislich schon lokal ist.

 Damit sollte dieselbe Fehlerklasse jetzt an allen drei betroffenen
 Stellen behoben sein: BAUM_FS (sond_treeviewfm_open(), nutzte von
 Anfang an SondTVFMItem-Metadaten statt Inhaltssniffing, war nie
 betroffen), Auszug-Fall (zond_treeview_auszug_collect_paths()) und jetzt
 auch der Einzeldatei-Fall (zond_treeview_open_node()) - alle drei
 ermitteln den zu prüfenden Pfad jetzt konsequent OHNE Dateizugriff,
 bevor überhaupt irgendein Lesezugriff versucht wird.

 Nicht durch Kompilieren/Testen verifiziert.


 Performance "Anbinden" - weiterer Fehlversuch und Rückbau (16.09.2026):

 - Nutzer-Fund: "1000 Dateien dauern eine Minute anzubinden" - trotz der
   oben behobenen fehlenden Datenbank-Indizes eher noch schlechter als
   die ursprünglich beobachteten 2000 Dateien/~30 Sek. Das schließt die
   Datenbank-Indizes als (alleinige) Erklärung für die Dauer aus.

 - Vermutung (per Code-Lektüre, nicht mit Nutzer abgestimmt VOR der
   Umsetzung umgesetzt - das war der Fehler, s.u.): mime_guess_content_
   type() (sond_mime.c) ruft bei JEDEM Aufruf (also einmal pro Datei)
   magic_open()+magic_load() neu auf - lädt/kompiliert damit die
   komplette libmagic-Signaturdatenbank für jede einzelne Datei neu.
   Versuchsweise auf ein pro Thread gecachtes magic_t-Handle (GPrivate)
   umgestellt, um das zu vermeiden, mit der Begründung, dass die Funktion
   sowohl vom GTK-Hauptthread als auch von den Indizier-Worker-Threads
   aus aufgerufen wird.

 - Nutzer-Fund: SIGSEGV in libmagic-1.dll unmittelbar nach dieser
   Änderung. Vermutlich ein Thread-Safety-Problem INNERHALB von libmagic
   selbst (bekanntes Problem einiger libmagic-Versionen bei
   gleichzeitiger Nutzung, auch mit getrennten Handles pro Thread).

 - Nutzer-Entscheidung: Änderung komplett zurückgenommen - zurück zu
   magic_open()/magic_load()/magic_close() bei jedem Aufruf. Stabilität
   hat Vorrang. Die vermutete Ursache der Anbinden-Dauer (magic_load()
   pro Datei) bleibt damit weiterhin ungeklärt/unbehoben und OFFEN -
   nicht weiter angefasst, bis das Vorgehen mit dem Nutzer VORHER
   abgestimmt ist (nicht wie hier: erst umsetzen, dann erklären).

 - Ausdrücklicher Nutzer-Hinweis (16.09.2026): "Erst Vorschläge machen.
   Das ergibt keinen Sinn!" - künftig bei Änderungen an dieser Art von
   sensiblen/heißen Pfaden ERST den Vorschlag samt Begründung machen und
   auf Bestätigung warten, DANN erst die Änderung im Code vornehmen -
   nicht umgekehrt. Gilt über diesen konkreten Fall hinaus.

 - Nutzer-Einwand (16.09.2026, korrekt, per Code-Lektüre bestätigt): ein
   Mutex sei überflüssig, weil eine echte Kollision zwischen Haupt- und
   Hintergrundthread schon architektonisch ausgeschlossen sei. Geprüft
   und bestätigt: die "4 Threads" bei sond_process_file_create_wctx()
   sind ein Tesseract-OCR-Pool (sond_ocr.c) für die Texterkennung auf
   Seitenebene - mime_guess_content_type() wird davon NIE aufgerufen. Es
   gibt nur EINEN Indizier-Hintergrundthread ("ocr-doc", g_thread_new()
   in headerbar.c), der sequenziell arbeitet, UND dieser läuft nur,
   während das modale Info-Window das Hauptfenster sperrt - laut Code-
   Kommentar bei cb_info_window_delete_event() (misc.c) schließt dieses
   Fenster nachweislich erst, wenn der Hintergrundthread wirklich fertig
   ist. Der GTK-Hauptthread (Anbinden) kann also nie währenddessen
   ebenfalls mime_guess_content_type() aufrufen.

 - Nutzer-Vorschlag (16.09.2026, aufgegriffen): magic_t nicht lazy beim
   ersten Aufruf laden (Risiko: zwei Threads könnten zufällig gleichzeitig
   zum ALLERERSTEN Mal laden - vermutlich die eigentliche Ursache des
   früheren SIGSEGV, unabhängig von der oben widerlegten Nutzungs-
   Kollision), sondern explizit und einmalig an einer Stelle, an der
   garantiert nur der Hauptthread läuft und noch kein Hintergrundthread
   existiert.

 - Umgesetzt: mime_guess_content_type_init() (sond_mime.c/.h, neu) lädt
   ein datei-statisches magic_t-Handle (g_magic) einmalig und idempotent;
   mime_guess_content_type() nutzt es fortan für alle Aufrufe (Anbinden
   UND Indizieren gleichermaßen - beide Aufrufer profitieren, nicht nur
   einer), kein magic_close() mehr pro Aufruf. Fällt auf das alte
   Verhalten (Open/Load/Close pro Aufruf) zurück, falls aus irgendeinem
   Grund ohne vorherige Initialisierung aufgerufen (defensiv, sollte im
   Normalbetrieb nie eintreten). Aufruf von mime_guess_content_type_
   init() in project_open() (project.c), unmittelbar VOR sond_process_
   file_create_wctx() (also vor dem Start des Indizier-Hintergrundthreads,
   noch auf dem Hauptthread) - ein Fehlschlagen dort ist unkritisch (nur
   LOG_WARN, kein Abbruch des Projekt-Ladens), weil mime_guess_content_
   type() in dem Fall einfach auf den langsameren Fallback zurückfällt.
   KEIN Mutex, KEIN GPrivate - bewusst so einfach wie möglich, da echte
   Nebenläufigkeit architektonisch ausgeschlossen ist (s.o.).

 - Offen: vom Nutzer zu bestätigen (erst mit wenigen Dateien anbinden,
   dann eine kleine Indizierung, dann erst wieder hochskalieren) - falls
   es erneut abstürzt, war die Ursache vermutlich NICHT die Nebenläufigkeit
   (die ist jetzt so oder so ausgeschlossen), sondern ein von Nebenläufigkeit
   unabhängiger Bug in dieser libmagic-Version bei bloßer Wiederverwendung
   desselben Handles über viele Aufrufe hinweg.

 - Nutzer-Rückmeldung: 1000 Dateien anbinden jetzt ~10 Sek. (vorher ~60
   Sek.) - deutliche Verbesserung, kein Absturz.

 UI-Feedback beim Anbinden fehlte komplett (16.09.2026, Nutzer-Fund,
 behoben):

 - Nutzer-Fund: bei einem Test mit >30.000 Dateien "tut sich nichts" im
   Info-Window, und der Abbrechen-Button sei "sinnlos". Ursache (per
   Code-Lektüre bestätigt, keine neue Regression - bestand schon immer):
   zond_treeview_leaf_anbinden()/zond_treeview_anbinden_rekursiv()
   (zond_treeview.c) laufen komplett synchron im Hauptthread, OHNE
   jemals gtk_main_iteration()/gtk_events_pending() aufzurufen. GTK
   zeichnet die per info_window_set_message() eingefügten Zeilen aber
   erst, wenn die Hauptschleife wieder erreicht wird - bei dieser
   synchronen Funktion also erst, wenn ALLES fertig ist. Aus demselben
   Grund wird auch das "clicked"-Signal des Abbrechen-Buttons nie
   verarbeitet (das läuft über gtk_dialog_response() -> das "response"-
   Signal -> cb_info_window_response(), misc.c, die info_window->cancel
   setzt) - der Button war dadurch faktisch wirkungslos, obwohl die
   Abbruch-Prüfung selbst (*(info_window->cancel) am Anfang von
   zond_treeview_anbinden_rekursiv()) längst vorhanden war.

 - Fix: in zond_treeview_leaf_anbinden() (nach jeder Datei) und im
   DIR-Zweig von zond_treeview_anbinden_rekursiv() (nach jedem
   eingefügten Verzeichnis) jeweils direkt nach der info_window_set_
   message()-Zeile ein "while (gtk_events_pending()) gtk_main_
   iteration();" ergänzt. Zeichnet damit laufend mit UND macht den
   Abbrechen-Button erstmals tatsächlich wirksam - die eigentliche
   Abbruch-Logik (Prüfung von *(info_window->cancel)) war bereits
   vorhanden und musste nicht geändert werden.

 - Fix: zond_dbase_ensure_indexes() öffentlich zugänglich gemacht
   (zond_dbase_ensure_performance_indexes(), zond_dbase.h) und in
   project_create_dbase_zond() (project.c) zusätzlich direkt NACH dem
   Backup explizit auf "work" aufgerufen - dort laufen alle
   performance-kritischen Abfragen, unabhängig vom Zustand/der
   Erreichbarkeit von "store". Ein Fehlschlagen auf "work" (lokal,
   unerwarteter echter I/O-Fehler) bleibt fatal. Der ursprüngliche
   Aufruf auf "store" (zond_dbase_open()) ist dagegen NICHT mehr fatal -
   nur noch LOG_WARN bei Fehlschlag; die Indizes auf "store" werden
   ohnehin beim nächsten erfolgreichen project_save() automatisch
   nachgezogen (zond_dbase_backup() kopiert das komplette Schema von
   "work" nach "store").

 Kopieren eines Verzeichnisses aus einem Container (ZIP) in das
 Filesystem (16.09.2026, Nutzeranfrage, umgesetzt):

 - copy_dir_across_sfps() (sond_treeviewfm.c) war bislang nur ein Stub
   ("noch nicht implementiert") - aufgerufen von sond_tvfm_item_copy()
   immer dann, wenn beim Kopieren eines Verzeichnis-Knotens mindestens
   eine Seite (Quelle oder Ziel) einen SondFilePart trägt (also nicht
   reines Dateisystem-zu-Dateisystem, dafür sorgt weiterhin
   sond_copy_r()).

 - Neue Funktion copy_container_dir_to_fs() implementiert die Richtung
   Container -> Dateisystem rekursiv: legt das Zielverzeichnis per
   sond_mkdir() an, holt die Kinder generisch über
   sond_tvfm_item_load_children() (funktioniert unverändert für ZIP,
   und strukturell auch für PDF-Ordner/GMessage-Multipart) und
   unterscheidet je Kind zwei Fälle:
   1. Echtes Unterverzeichnis IM SELBEN Archiv (Kriterium: Kind trägt
      denselben SondFilePart wie der Quellknoten, nur mit anderem
      path_or_section - so legt sond_tvfm_item_load_zip_dir() ZIP-
      Unterverzeichnisse an) -> rekursiver Aufruf.
   2. Alles andere (normale Datei ODER eine eingebettete Datei, die
      selbst wieder ein Container ist, z.B. eine verschachtelte .zip/
      .pdf) -> wird als GANZE Datei kopiert (sond_file_part_copy() mit
      sfp_dst=NULL, wie beim schon vorhandenen einzelnen Datei-Kopieren
      aus einem Container ins Filesystem). Bewusste Nutzer-Entscheidung:
      eingebettete Container werden NICHT in ihre interne Struktur
      (PageTree, Anhänge) aufgelöst, sondern 1:1 als normale Datei
      übernommen - der Nutzer erwartet beim Herauskopieren reale
      Dateien, keine synthetische App-Ansicht.

 - Sonderfall abgefangen (nicht Teil der Anfrage, aber sonst stiller
   Fehler): der PDF-"PageTree"- bzw. GMessage-"Message"-Pseudo-Knoten
   (steht für den Inhalt der Container-Datei selbst, trägt denselben
   SondFilePart wie deren "Ordner", ist aber vom Typ LEAF statt DIR,
   s. sond_tvfm_item_create()) würde ohne Sonderbehandlung fälschlich
   noch einmal als eigene Datei "PageTree"/"Message" kopiert - eine
   Duplizierung der ganzen PDF/E-Mail-Datei unter falschem Namen. Bei
   ZIP kommt das nie vor (ZIP-Items sind immer DIR). Für PDF/GMessage
   bricht copy_container_dir_to_fs() diesen Fall jetzt klar mit
   Fehlermeldung ab, statt ein falsches Ergebnis zu erzeugen - echtes
   Kopieren eines PDF-/E-Mail-"Ordners" ins Filesystem war nicht Teil
   der Anfrage und bleibt offen.

 - Nutzer-Entscheidung: die umgekehrte Richtung (Dateisystem-Verzeichnis
   in einen Container wie ZIP hineinkopieren) bleibt bewusst
   unimplementiert (weiterhin Fehlermeldung in copy_dir_across_sfps()).

 Performance Anbinden: fehlendes Transaktions-Batching (16.09.2026,
 Nutzer-Messung 2000 Dateien ~30s, behoben; Nutzer-Nachfrage 16.09.2026:
 nicht ZIP-spezifisch, s.u.):

 - Ursache: zond_treeview_clipboard_anbinden() (zond_treeview.c) rief
   sond_treeview_clipboard_foreach() bislang ohne umschließende
   Transaktion auf. Jeder zond_dbase_insert_node()-Aufruf (Filepart-
   Registrierung + Anker-Knoten, oft mehrere pro Datei) läuft intern über
   ein eigenes SAVEPOINT/RELEASE - ohne äußere Transaktion committet
   jedes RELEASE für sich. Diese DB erzwingt bewusst journal_mode in
   {DELETE, TRUNCATE, PERSIST} (nicht WAL) und synchronous=FULL (nötig
   für atomare Mehrdatei-Transaktionen per ATTACH, s.
   zond_dbase_check_journal_settings()) - jeder dieser Commits löst also
   einen echten fsync() aus. Bei mehreren tausend Dateien macht allein
   das den Löwenanteil der Laufzeit aus.

 - Nutzer-Nachfrage (berechtigt, aufgegriffen): der Fund/die Messung
   stammte aus einem Test mit einem ZIP-Archiv, deshalb zunächst als
   "ZIP-Anbinden" bezeichnet/dokumentiert (auch im Titel von Task #103).
   Tatsächlich ist der Fix aber gar nicht ZIP-spezifisch: zond_treeview_
   clipboard_anbinden() ist die EINZIGE Stelle, die beim Einfügen aus der
   Zwischenablage in zond_treeview_anbinden_rekursiv() verzweigt -
   aufgerufen von zond_treeview_paste_clipboard() immer dann, wenn die
   Quelle BAUM_FS ist, unabhängig davon, ob die ausgewählten Punkte
   normale Dateien/Verzeichnisse im Dateisystem oder ein ZIP-Archiv sind.
   Es gibt auch keinen zweiten Weg ins Anbinden (kein Drag&Drop, kein
   separater Code-Pfad - per grep verifiziert). Die Transaktion liegt
   eine Ebene über dieser Verzweigung, um die komplette Operation herum -
   normales Anbinden vieler Dateien/Ordner aus dem Dateisystem (ganz ohne
   ZIP) profitiert also seit demselben Fix genauso davon. Titel/Überschrift
   entsprechend von "ZIP-Anbinden" auf "Anbinden" korrigiert.

 - Fix: die ganze Anbinden-Operation (der komplette
   sond_treeview_clipboard_foreach()-Durchlauf) jetzt in EIN
   zond_dbase_begin()/zond_dbase_commit() eingepackt - analog zum
   bestehenden Muster in zond_treeview_clipboard_kopieren_foreach() (nur
   dort pro Top-Level-Element statt für den ganzen Durchlauf). Nur noch
   ein fsync für die komplette Operation. Nutzer-Abbruch (rc==1 aus
   sond_treeview_clipboard_foreach()) committet bewusst trotzdem - die
   bis dahin eingefügten Knoten sollen wie im bisherigen (nicht-
   transaktionalen) Verhalten erhalten bleiben; nur ein echter DB-Fehler
   (rc==-1, kommt praktisch nie vor, da zond_treeview_anbinden_rekursiv()
   Fehler pro Knoten selbst abfängt und weitermacht) löst ein Rollback
   aus.

 Performance ZIP-Anbinden: verbleibende Silent-Freeze-Lücke bei sehr
 großen Archiven + SondTVFMProgress-Mechanismus (16.09.2026,
 Nutzer-Fund, behoben):

 - Nutzer-Fund: auch nach dem UI-Pumping-Fix oben (s. "UI-Feedback beim
   Anbinden fehlte komplett") blieb bei einem Test mit >30.000 ZIP-
   Einträgen eine lange stille Pause: "Es erscheint: Verzeichnis
   eingefügt: xyz.zip. Dann wieder nichts." Ursache (per Code-Lektüre):
   sond_tvfm_item_load_zip_dir() (sond_treeviewfm.c) liest und erzeugt
   ALLE Einträge eines ZIP-Verzeichnisses in einer einzigen Schleife,
   BEVOR sond_tvfm_item_load_children() überhaupt zurückkehrt - pro
   Nicht-Verzeichnis-Eintrag ein sond_file_part_create()-Aufruf
   (MIME-Sniffing). Bei 30.000 Einträgen und wenigen ms pro Eintrag
   ergibt das mehrere Minuten, in denen weder gtk_main_iteration()
   läuft noch der Abbrechen-Button reagieren kann - das UI-Pumping an
   den beiden Anbinden-Stellen in zond_treeview.c greift hier nicht,
   weil es NACH dieser Schleife liegt (die Schleife selbst ist der
   Engpass, nicht die Rekursion darüber).

 - Nutzer-Anregung (aufgegriffen): denselben cancel/log_func-Gedanken
   wie bei SondProcessFileCtx (sond_process_file.h, dort schreibend -
   OCR + Indizierung) auch für sond_tvfm_item_load_children()/die vier
   load_*_dir-Funktionen einführen, u.a. weil auch interaktives
   Aufklappen großer Verzeichnisse (BAUM_FS) spürbar dauern kann.

 - Vorab geklärt (Nutzer-Nachfrage, bestätigt): sond_process_file ist
   lesend UND schreibend (OCR verändert PDFs, Indizierung schreibt in
   die DB) - dessen wctx->cancel wird deshalb nur an sicheren
   Checkpoints geprüft (Anfang von sond_process_file_do_rec()), nie
   mitten in einer Operation. sond_tvfm_item_load_*_dir() ist dagegen
   REIN LESEND (baut nur In-Memory-Objekte, keine Disk-/DB-Schreiben) -
   Abbruch ist dort daher an praktisch jeder Stelle unkritisch möglich,
   ohne Rollback-Sorgen. Deshalb: gleiche STRUKTUR (kleiner Kontext mit
   cancel-Zeiger + Progress-Callback), aber bewusst getrennte, dem
   jeweiligen Risiko angemessene Abbruch-LOGIK - kein gemeinsamer
   Implementierungscode über das Interface-Muster hinaus.

 - Fix: neuer Typ SondTVFMProgress (sond_treeviewfm.h: gint *cancel,
   progress_func(gpointer, gchar const*), progress_func_data). Als
   zusätzlicher, NULL-barer Parameter durch sond_tvfm_item_load_
   children() und alle vier load_fs_dir/load_zip_dir/load_pdf_dir/
   load_gmessage_dir-Funktionen durchgeschleift (Dispatcher +
   Signaturen in sond_treeviewfm.c). Tatsächlich ausgewertet wird er
   bisher nur in load_zip_dir(): alle 200 Einträge wird bei
   vorhandenem progress progress_func() aufgerufen und *cancel
   geprüft - bei Abbruch bricht nur die Schleife ab (bereits geladene
   Kinder bleiben erhalten, rc bleibt 0, kein Fehler); die Rekursion
   in zond_treeview_anbinden_rekursiv() bricht dann an ihrer
   gewohnten Prüfstelle (*(info_window->cancel) am Funktionsanfang)
   ohnehin ab. load_fs_dir/load_pdf_dir/load_gmessage_dir nehmen den
   Parameter zwar entgegen (einheitliche Signatur), werten ihn aber
   (noch) nicht aus - dort ist kein vergleichbarer Engpass bekannt.

 - Alle bestehenden Aufrufer außer dem Anbinden-Pfad übergeben
   weiterhin NULL (unverändertes Verhalten). Der Anbinden-Aufruf in
   zond_treeview_anbinden_rekursiv() (DIR-Zweig, zond_treeview.c)
   bekommt einen echten Kontext: cancel zeigt auf info_window->cancel
   (dasselbe Flag, das der Abbrechen-Button schon setzt), progress_func
   (neuer statischer Helper zond_treeview_anbinden_progress()) pumpt
   nur die GTK-Events (macht damit auch während des Einlesens eines
   großen ZIP-Verzeichnisses den Abbrechen-Button wirksam) und zeigt
   bei übergebenem Text zusätzlich eine InfoWindow-Meldung an.


 Performance ZIP-Anbinden: Archiv wurde pro Datei komplett neu geöffnet
 (16.09.2026, Nutzer-Fund nach obigem Fix, behoben):

 - Nutzer-Fund: trotz des SondTVFMProgress-Mechanismus oben blieb beim
   Anbinden großer ZIPs eine Pause von ca. 30 Sek. NACH jedem einzelnen
   eingefügten ZIP-Unterverzeichnis, bevor die "Anbindung Datei"-Zeilen
   seiner Kinder erscheinen - unabhängig von der Anzahl der Einträge in
   diesem Unterverzeichnis (auch bei nur wenigen Dateien).

 - Ursache (per Code-Lektüre): sond_file_part_create() (sond_fileparts.c)
   liest für den MIME-Sniff pro Datei nur die ersten 2048 Bytes, aber
   sond_file_part_zip_open_archive() öffnete dafür bei JEDEM Aufruf das
   GESAMTE ZIP-Archiv neu von der Platte (sond_fopen() + zip_source_
   filep_create() + zip_open_from_source()) - das parst das komplette
   Central Directory des Archivs neu - und verwarf den Handle danach
   sofort wieder (zip_discard()). Bei einem Unterverzeichnis mit z.B. 6
   Dateien wurde das ganze (u.U. sehr große, ggf. auf SeaDrive liegende)
   Archiv also 6x komplett neu geöffnet und geparst, nur um 6x 2048 Bytes
   zu lesen. sond_file_part_zip_list_dir() cachte den Verzeichnis-Index
   bereits pro Objekt (dir_index) - genau dieses Cache-Muster fehlte beim
   eigentlichen Lesen der Dateiinhalte.

 - Nutzer-Nachfrage (berechtigt): wo ein analoger Mechanismus schon
   existiert, statt neu zu erfinden - Antwort: genau der dir_index-Cache
   in SondFilePartZipPrivate.

 - Fix: SondFilePartZipPrivate um ein zweites Feld cached_archive
   (zip_t*) erweitert, exakt nach demselben Lifecycle-Muster wie
   dir_index - lazy aufgebaut, invalidiert in
   sond_file_part_zip_invalidate_dir_index() (bei jeder Archiv-Änderung
   durch rename/insert/delete im ZIP - dort ohnehin schon aufgerufen),
   geschlossen in sond_file_part_zip_finalize() (läuft über dieselbe
   invalidate-Funktion). sond_file_part_zip_open_archive() liefert für
   den read-only/Filesystem-Fall (!sfp_parent && !writeable - der
   häufigste beim Anbinden) ab dem zweiten Aufruf direkt den gecachten
   Handle statt neu zu öffnen. Neue Funktion
   sond_file_part_zip_release_archive() als Gegenstück: lässt den
   gecachten Handle unangetastet, verwirft einen nicht-gecachten
   (verschachtelter Fall) wie bisher sofort - ersetzt an den drei
   betroffenen Aufrufstellen (read_bytes_internal, list_dir,
   test_for_files) das bisherige direkte zip_discard(). Der writeable-
   Fall (rename/insert/delete im ZIP) ist unverändert - der lädt ohnehin
   je Schreibvorgang frisch in den Speicher.

 Performance Löschen (BAUM_INHALT): fehlendes Transaktions-Batching
 (16.09.2026, Nutzer-Fund, behoben):

 - Nutzer-Fund: Löschen von 5 ausgewählten Knoten mit zusammen mehreren
   hundert Unterknoten in BAUM_INHALT dauerte >20 Sek.

 - Ursache: identisches Muster wie beim Anbinden (s.o.) - zond_
   treeview_action_loeschen() (zond_treeview.c) rief sond_treeview_
   selection_foreach()/zond_treeview_selection_loeschen_foreach() bisher
   ohne umschließende Transaktion auf. Jeder zond_dbase_remove_node()-
   Aufruf (einer pro gelöschtem Knoten, dazu noch mehrere begleitende
   Lese-Abfragen wie zond_dbase_get_node()/_get_baum_auswertung_copy()/
   _is_file_part_copied()/_get_baum_inhalt_file_from_file_part())
   committet einzeln (SAVEPOINT/RELEASE) - bei erzwungenem
   synchronous=FULL ein echter fsync() PRO gelöschtem Knoten.

 - Fix: die ganze Lösch-Operation in zond_treeview_action_loeschen() in
   EIN zond_dbase_begin()/zond_dbase_commit() eingepackt - analog zum
   Anbinden-Fix. Nutzer-Entscheidung (bewusst abweichend vom Anbinden-
   Muster, das bei Abbruch/Fehler trotzdem committet): bei rc == -1
   (echter DB-Fehler, kommt praktisch nie vor) hier ein echtes ROLLBACK -
   kein halb gelöschter Baum bei einem echten DB-Fehler. rc == 2 (Löschen
   abgebrochen, weil noch ein Link auf einen der Knoten besteht) committet
   weiterhin die bis dahin bereits gelöschten Geschwister-Knoten - das
   entspricht dem bisherigen (nicht-transaktionalen) Verhalten und wurde
   bewusst nicht geändert.

 - Nutzer-Rückmeldung: Löschen geht "etwas schneller", dauert aber bei
   mehreren hundert Unterknoten immer noch ein paar Sekunden. Nutzer-
   Vermutung (per Nachfrage geprüft, NICHT bestätigt): GNode-Traversierung/
   -Freigabe beim Löschen. Per Code-Lektüre widerlegt: node_free()
   (zond_tree_store.c) gibt pro Knoten nur ein kleines RowData/Data-Struct
   frei und entfernt einen Hashtable-Eintrag (ht_node_id) - beides O(1),
   nicht die Bremse.


 Performance BAUM_FS-Aufklappen: redundanter ZIP-dir_index-Aufbau beim
 bloßen Auflisten (16.09.2026, Nutzer-Fund, behoben):

 - Nutzer-Fund: Aufklappen eines (vollständig hydrierten, also kein
   SeaDrive-Netzwerkeffekt) Verzeichnisses mit 5 ZIP- und 3 CSV-Dateien
   dauerte ~10 Sek.

 - Ursache: sond_tvfm_item_create() (sond_treeviewfm.c) behandelte den
   ZIP-Fall anders als PDF/GMessage - dort wird für has_children die
   schon vom Erzeugen des SondFilePart her vorhandene, billige
   sond_file_part_get_has_children()-Flag wiederverwendet, im ZIP-Zweig
   wurde stattdessen IMMER sond_tvfm_item_load_zip_dir(stvfm_item, NULL,
   NULL, NULL) aufgerufen. Das ruft über sond_file_part_zip_list_dir()
   beim allerersten Zugriff auf ein Archiv sfp_zip_build_dir_index() auf
   - einen kompletten Durchlauf über JEDEN Eintrag im GESAMTEN Archiv
   (Pfad-Zerlegung, Hashtable-Aufbau für alle Verzeichnisebenen) - nur um
   festzustellen, ob überhaupt ein Eintrag existiert. Das passierte für
   jede ZIP-Datei bereits beim bloßen AUFLISTEN des sie enthaltenden
   Verzeichnisses, nicht erst beim Hineinklicken. Bei großen Archiven
   (mehrere Tausend Einträge) macht allein das bei mehreren ZIP-Dateien
   in einem Verzeichnis mehrere Sekunden aus.

 - Fix: für path_or_section == NULL (das ZIP-File selbst, noch nicht
   hineinexpandiert) die von sond_file_part_zip_test_for_files() (läuft
   schon in sond_file_part_create_from_mime_type() beim Erzeugen des
   SondFilePart, prüft nur zip_get_num_entries() auf dem gecachten
   Archiv-Handle, OHNE die Einträge zu parsen) gesetzte has_children-
   Flag wiederverwenden - analog PDF/GMessage. Für bereits expandierte
   ZIP-Unterverzeichnisse (path_or_section != NULL) bleibt
   load_zip_dir() unverändert, da dort dir_index durchs Expandieren
   ohnehin schon gecacht und der Aufruf billig ist.

 Performance InfoWindow: ein GtkLabel-Widget PRO Nachricht (16.09.2026,
 Nutzer-Nachfrage - "ist das Schreiben einer Zeile pro Datei ins
 InfoWindow selbst ein Performance-Killer?" -, behoben):

 - Nutzer-Nachfrage (berechtigt, bestätigt): info_window_set_message()
   (misc.c) legte bei JEDEM Aufruf ein neues GtkLabel an und packte es
   per gtk_box_pack_start() in info_window->content - eine GtkBox, die
   seit info_window_open() nie geleert wird. Bei z.B. 30.000 Dateien
   beim Anbinden landen am Ende 30.000 einzelne Label-Widgets in einer
   Box innerhalb eines GtkScrolledWindow.

 - Ursache: GtkBox muss bei jeder Kind-Änderung ihre Größenanforderung
   neu berechnen - O(Anzahl Kinder). Normalerweise bündelt GTK mehrere
   queue_resize()-Aufrufe bis zum nächsten Main-Loop-Durchlauf. Der
   UI-Pumping-Fix von zuvor (gtk_main_iteration() nach jeder einzelnen
   Anbinden-Nachricht, s.o. "UI-Feedback beim Anbinden fehlte komplett")
   verhindert genau dieses Bündeln: jede einzelne Nachricht erzwingt
   sofort einen vollständigen Resize/Redraw über die inzwischen
   angewachsene Box. In Summe O(n²) statt O(n) für n Nachrichten - der
   UI-Pumping-Fix hat also das Sichtbarkeits-/Abbrechen-Problem gelöst,
   dabei aber vermutlich diesen Kostenfaktor erst richtig scharf gemacht.

 - Fix: InfoWindow (misc.h) von GtkBox+GtkLabel-pro-Zeile (Felder
   content/last_inserted_widget) auf ein einzelnes GtkTextView mit
   GtkTextBuffer umgestellt (neue Felder text_view/end_mark). Text an
   einen GtkTextBuffer anhängen bleibt auch bei sehr vielen Zeilen
   günstig (dafür gebaut), im Unterschied zu vielen einzelnen
   Kind-Widgets in einer Box. Auto-Scroll-ans-Ende läuft jetzt über
   einen GtkTextMark mit left_gravity=FALSE (bleibt automatisch am
   Textende) + gtk_text_view_scroll_mark_onscreen() - robuster und
   einfacher als die alte size-allocate-Einmal-Verbindung/Trennung.
   Betrifft nur misc.c/misc.h - alle Aufrufer (project.c, headerbar.c,
   seiten.c, zond_chat.c, zond_update.c, zond_treeview.c) unverändert,
   da Funktionssignaturen gleich geblieben sind (content/
   last_inserted_widget wurden von keiner anderen Datei gelesen).

 Performance BAUM_FS Auf-/Zuklappen bei ZIP-Dateien: Archiv-Öffnen schon
 beim bloßen Erkennen (16.09.2026, Nutzer-Rückmeldung nach Task #116 -
 "sind sie nicht [weg]. Auch das Zuklappen... dauert lange" -, behoben):

 - Nutzer-Rückmeldung: der Fix aus #116 (redundanten dir_index-Aufbau
   vermeiden) brachte keine spürbare Besserung; zusätzlich dauerte auch
   das ZUKLAPPEN des Verzeichnisses lange - unerwartet, da Zuklappen
   normalerweise nur schon geladene Zeilen ausblendet/verwirft, nichts
   neu lädt.

 - Tatsächliche Ursache (tiefer als #116): sond_file_part_create_from_
   mime_type() ruft für JEDE neu entdeckte ZIP-Datei (schon beim bloßen
   Auflisten eines Verzeichnisses, nicht erst beim Aufklappen der ZIP
   selbst) sond_file_part_test_for_children() ->
   sond_file_part_zip_test_for_files() auf, die bisher das Archiv via
   sond_file_part_zip_open_archive()/zip_open_from_source() (libzip)
   öffnete, nur um zip_get_num_entries() abzufragen. Das PROBLEM: schon
   das bloße ÖFFNEN eines ZIP-Archivs mit libzip parst IMMER dessen
   komplettes Central Directory - das ist die eigentliche teure
   Operation, nicht erst der (in #116 vermiedene) zusätzliche
   dir_index-Aufbau. Jedes Auflisten eines ZIP-Dateien enthaltenden
   Verzeichnisses öffnete also alle enthaltenen Archive komplett neu.
   Erklärt auch das Zuklappen: sond_treeviewfm_row_collapsed()
   (sond_treeviewfm.c) zerstört beim Einklappen alle Kind-Zeilen inkl.
   der zugehörigen SondFilePartZip-Objekte (zip_discard() auf die beim
   Öffnen aufgebaute interne libzip-Struktur) - bei großen Archiven mit
   vielen Tausend Einträgen ist auch dieses Freigeben nicht kostenlos.
   Jedes erneute Aufklappen fing wieder bei Null an (Objekte wurden beim
   Zuklappen ja zerstört, nicht wiederverwendet).

 - Fix: sond_file_part_zip_test_for_files() ersatzlos entfernt (war
   danach unbenutzt). sond_file_part_test_for_children() setzt für ZIP
   jetzt direkt has_children = TRUE, OHNE das Archiv zu öffnen - Nutzer-
   Entscheidung: leere ZIP-Dateien sind selten und harmlos, wenn sie
   fälschlich mit einem Aufklapp-Pfeil gezeigt werden, der dann eine
   leere Liste offenbart. Echtes Öffnen des Archivs passiert jetzt nur
   noch dort, wo wirklich gebraucht: beim tatsächlichen Aufklappen DER
   ZIP-DATEI SELBST (sond_tvfm_item_load_zip_dir()) oder beim Anbinden.
   Damit sind Auf-/Zuklappen eines Verzeichnisses mit ZIP-Dateien jetzt
   unabhängig von deren Größe genauso schnell wie mit normalen Dateien.

 Bug (nicht Performance) beim Kopieren eines Verzeichnisses aus einem
 ZIP-Archiv ins Dateisystem (16.09.2026, Nutzer-Fund, behoben):

 - Nutzer-Fund: "Das Kopieren von Verzeichnissen aus zips funktioniert
   zwar, aber im BAUM_FS wird ein Verzeichnis-Item angezeigt, das, wenn
   man es öffnet, einen Fehler anzeigt" - genauer Fehlertext "Zeile
   konnte nicht expandiert werden / No such file or directory". Wichtiger
   Befund des Nutzers: auf der Platte ist das kopierte Verzeichnis
   fehlerfrei vorhanden, und nach Schließen+Neuöffnen des Projekts lässt
   sich dasselbe Verzeichnis anstandslos aufklappen. Fehler betraf laut
   Nutzer die oberste, gerade eingefügte Ebene selbst (nicht erst einen
   Unterordner darin).

 - Ursache (per Code-Lektüre, ohne Testen bestätigt - kein Zugriff auf
   funktionierendes make zond in dieser Session): das eigentliche Kopieren
   (copy_dir_across_sfps()/copy_container_dir_to_fs(), Task #102) legt die
   Dateien/Verzeichnisse korrekt im Dateisystem an - das ist NICHT die
   fehlerhafte Stelle. Der Fehler steckt in
   sond_treeviewfm_paste_clipboard_foreach(): nach erfolgreichem Kopieren
   baut diese Funktion generisch ein neues SondTVFMItem für die Anzeige,
   indem sie den GType des sond_file_part der QUELLE klont:

     if (stvfm_item_priv->sond_file_part) {
         sfp_new = g_object_new(G_OBJECT_TYPE(stvfm_item_priv->sond_file_part), NULL);
         if (!stvfm_item_priv->path_or_section)
             sond_file_part_set_path(sfp_new, path_new);
         ...
         sond_file_part_set_parent(sfp_new, stvfm_item_parent_priv->sond_file_part);
     }

   Ein Verzeichnis INNERHALB eines ZIP-Archivs hat aber gar keine eigene
   sond_file_part-Identität - es teilt sich die des umschließenden
   Archivs, nur path_or_section unterscheidet den Unterpfad (s.
   sond_tvfm_item_load_zip_dir(): Verzeichnis-Einträge bekommen das
   sond_file_part des Eltern-Containers). Bei so einem Verzeichnis ist
   path_or_section gesetzt, also läuft sond_file_part_set_path(sfp_new,
   ...) NICHT (die Bedingung ist !path_or_section) - sfp_new bleibt ohne
   Pfad. Das Ziel-Parent (ein normaler Ordner im Dateisystem) hat selbst
   kein sond_file_part, also wird auch sfp_new's Parent auf NULL gesetzt.
   Ergebnis: sfp_new ist eine neue, orphane SondFilePartZip-Instanz ohne
   Pfad und ohne Eltern-Archiv.

   Beim ersten Kinder-Check (in sond_tvfm_item_create(), ZIP-Zweig) sowie
   beim späteren echten Aufklappen versucht diese Attrappe trotzdem, sich
   selbst als ZIP-Datei zu öffnen: sond_file_part_zip_open_archive()
   baut mit sfp_parent==NULL und !writeable den Pfad
   "Projektwurzel/" + sond_file_part_get_path(sfp_new). Da
   get_path(sfp_new) NULL liefert, bricht g_strconcat() dort ab (NULL
   markiert für g_strconcat() das Varargs-Ende) - es bleibt nur
   "Projektwurzel/" übrig, und sond_fopen() versucht, das
   Projektwurzelverzeichnis selbst mit "rb" zu öffnen. Das schlägt fehl -
   exakt mit "No such file or directory".

   Warum kein Fehler schon beim Einfügen sichtbar wurde: der ZIP-Zweig in
   sond_tvfm_item_create() wertete den Rückgabewert von
   sond_tvfm_item_load_zip_dir() bisher per "? TRUE : FALSE" aus - auch
   der Fehlercode -1 zählte damit fälschlich als "hat Kinder", weshalb
   trotz gescheitertem Öffnen ein Aufklapp-Pfeil samt Dummy-Kind gesetzt
   wurde. Der Fehler zeigte sich dadurch erst beim tatsächlichen Klick
   zum Aufklappen (mit echtem GError), nicht schon beim Einfügen (dort
   wird error=NULL übergeben, der Fehler also verschluckt).

   Warum nach Projekt-Neustart kein Problem: beim Neuladen entsteht das
   Verzeichnis-Item ganz normal durch echtes Verzeichnis-Listing des
   Dateisystems (sond_file_part bleibt NULL) - der komplette
   Klon-Mechanismus oben kommt dabei gar nicht zum Zug.

 - Fix: in sond_treeviewfm_paste_clipboard_foreach() wird sfp_new NICHT
   mehr geklont, wenn die Quelle ein Verzeichnis-Marker ist
   (path_or_section gesetzt) UND das Ziel-Parent kein eigenes
   sond_file_part hat (Kopie ins echte Dateisystem). sfp_new bleibt dann
   NULL - der schon vorhandene, dafür ausgelegte Zweig in
   sond_tvfm_item_create() (sond_file_part == NULL -> echtes
   Dateisystem-Verzeichnis, has_children per sond_tvfm_item_load_fs_dir()
   auf dem tatsächlich existierenden Pfad ermittelt) greift dann korrekt -
   exakt der Code, der auch beim Projekt-Neustart funktioniert. Alle
   anderen Fälle (einzelne Dateien; Kopien innerhalb von ZIP/PDF/GMessage,
   wo Ziel-Parent ein eigenes sond_file_part hat) bleiben unverändert.
   Zusätzlich der oben beschriebene "-1 zählt als TRUE"-Fehler in
   sond_tvfm_item_create() behoben (nur Rückgabewert 1 zählt jetzt als
   "hat Kinder").

 - Nutzer-Rückmeldung (16.09.2026) nach Build: "super. Kopieren klappt." -
   Fix bestätigt.

 SeaDrive-Statusanzeige zeigt dauerhaft "✓" trotz ausstehender Uploads
 (16.09.2026, Nutzer-Fund im Anschluss an obigen Kopier-Fix, behoben):

 - Nutzer-Fund: nach dem Kopieren von Dateien aus einem ZIP-Archiv ins
   Dateisystem (SeaDrive-synchronisiertes Projektverzeichnis) zeigte die
   SeaDrive-Statuszeile durchgehend ein Häkchen ("SeaDrive: ✓", s.
   cb_seadrive_status_app_window() in app_window.c - Haken erscheint bei
   pending_down==0 && pending_up==0), obwohl die frisch kopierten Dateien
   noch zum Server hochgeladen werden mussten.

 - Erste Theorie (verworfen): ein Puffer-Overflow von
   ReadDirectoryChangesW (32-KB-Puffer) während des Bulk-Kopierens könnte
   einen Resync auslösen, der pending_up nicht neu ermittelt
   (watcher_count_pending_down() prüft nur PINNED/OFFLINE-Attribute,
   nie den In-Sync-Status). Von Nutzer widerlegt: derselbe Effekt trat
   auch bei nur ~20 kopierten Dateien auf - dafür reicht der Puffer
   bei Weitem.

 - Tatsächliche Ursache: watcher_check_in_sync() (sond_treeviewfm_
   seadrive.c) - wird bei JEDEM einzelnen ADDED/MODIFIED-Watcher-Event
   aufgerufen, um per CfGetPlaceholderInfo() (Cloud Files API) den
   echten Sync-Status einer Datei abzufragen - wertete jede Art von
   Unsicherheit (CfGetPlaceholderInfo-Funktionszeiger fehlt, Datei nicht
   öffenbar, CfGetPlaceholderInfo() selbst schlägt fehl) per Fallback als
   "im Zweifel: in sync" (TRUE). Eine gerade erst per normalem
   CreateFile()/fwrite() (statt über die Cloud-Files-Platzhalter-APIs)
   neu angelegte Datei wird von CfGetPlaceholderInfo() vermutlich (noch)
   nicht als Cloud-Datei erkannt, der Aufruf schlägt fehl - der
   optimistische Fallback verschleierte dadurch dauerhaft (nicht nur
   kurz nach dem Anlegen, da kein späteres Ereignis den Fehler
   korrigierte), dass die Datei noch hochgeladen werden musste.

 - Fix: alle drei Fallback-Stellen in watcher_check_in_sync() von TRUE
   auf FALSE gedreht - im Zweifel gilt eine Datei jetzt als NICHT
   synchron (Upload ausstehend), nicht mehr als synchron. Für einen
   Indikator, der vor "Daten sind noch nicht gesichert" warnen soll, ist
   das die richtige Default-Richtung: ein fälschliches "noch nicht
   synchron" ist höchstens ein optisches Ärgernis, ein fälschliches
   "alles synchron" verschleiert ein echtes Datenverlust-Risiko.

 - Nutzer-Rückmeldung (16.09.2026) nach Build: "scheint zu passen. Meldet
   72 zum Hochladen, springt dann nach ca. 5 Sek auf Häkchen. Explorer
   zeigt zu diesem Zeitpunkt, dass schon hochgeladen." - Fix bestätigt,
   Anzeige deckt sich mit dem tatsächlichen SeaDrive/Explorer-Status.

 Verzeichnis-Kurzschluss auch bei "Index erstellen (Gesamtes Projekt)"
 (16.09.2026, Nutzerwunsch/-bestätigung, Task #100, umgesetzt):

 - Anlass: Nachfrage, ob sich derselbe Verzeichnis-Kurzschluss wie bei
   "Index durchsuchen" (scan_coverage_gaps_fs(), s.o.) nicht auch bei
   "Index erstellen" lohnt - bislang durchlief "Gesamtes Projekt" dort
   immer den vollen readdir-Weg (zond_treeviewfm_item_get_fileparts_
   readdir()), auch für längst vollständig indizierte Äste.

 - Hindernis, warum das nicht einfach derselbe Kurzschluss war: bei
   "Index erstellen" wird der OCR-Modus (kein OCR / prüfen / erzwingen,
   ask_ocr_mode()) bisher immer ERST NACH der Fileparts-Sammlung
   abgefragt (zond_index_erstellen_ht(), headerbar.c). Bei "erzwingen"
   darf aber kein Ast übersprungen werden, auch wenn er schon vollständig
   abgedeckt ist. Ein Kurzschluss ohne Kenntnis des Modus zum Zeitpunkt
   der Sammlung wäre also bei "erzwingen" falsch gewesen.

 - Umsetzung:
   1. zond_index_erstellen_ht() (headerbar.c) in einen öffentlichen Teil
      (fragt wie bisher den OCR-Modus ab, für Auswahl/Lücken-
      Aufschlüsselung - Reihenfolge dort unkritisch) und einen neuen
      internen Kern zond_index_erstellen_ht_mit_modus() aufgeteilt, der
      einen schon bekannten Modus direkt entgegennimmt statt erneut zu
      fragen.
   2. do_index_erstellen_gesamt() (headerbar.c, "Gesamtes Projekt") ruft
      jetzt ask_ocr_mode() VOR der Sammlung auf und übergibt das Ergebnis
      direkt an zond_index_erstellen_ht_mit_modus() - EIN Dialog wie
      bisher, nur früher im Ablauf.
   3. zond_treeviewfm_get_fileparts()/zond_treeviewfm_item_get_fileparts()/
      zond_treeviewfm_item_get_fileparts_readdir() (zond_treeviewfm.c/.h)
      um einen neuen Parameter skip_fully_covered erweitert und
      durchgereicht. In _readdir() (dem tatsächlichen Rekursions-Kern):
      bei skip_fully_covered und rel_dir != NULL wird zuerst
      sond_index_ctx_get_dir_status() abgefragt - bei
      SOND_INDEX_STATUS_FULL wird der Ast gar nicht erst per readdir
      geöffnet (return 0, keine Fileparts). Für rel_dir == NULL
      (Projektwurzel) nie geprüft, analog scan_coverage_gaps_fs() -
      Coverage wird nie über die oberste Ebene hinaus zusammengefasst.
   4. do_index_erstellen_gesamt() übergibt skip_fully_covered als
      (ocr_mode != SOND_OCR_MODE_FORCE) - bei "erzwingen" also FALSE,
      unverändertes (vollständiges) Verhalten. Alle anderen Aufrufer
      (Auswahl erstellen/löschen, Indexsuche-Auswahl, Lücken-
      Aufschlüsselung in handle_coverage_gaps()) übergeben weiterhin
      FALSE - für sie ändert sich nichts.

 - Nicht durch Kompilieren/Testen verifiziert (kein Zugriff auf make zond
   in dieser Session) - Bestätigung durch den Nutzer nach dem nächsten
   Build steht noch aus.

 Index-Badges fehlen bei eingebetteten MIME-Parts (E-Mail) + Architektur-
 Refactor get_section_page_range -> get_section_index_status
 (16.09.2026, Nutzerfund + Nutzer-Entscheidung, Task #122, umgesetzt):

 - Nutzerfund: "die Index-badges scheinen bei emls nicht zu
   funktionieren." Nach Rückfrage (Symptom: "Kein Badge, obwohl
   indiziert") und Nutzer-Revision "es werden nur bei verschiedenen
   mime-parts, insbesondere text/html etc., keine badges gemalt" -
   betraf also nicht die Eml-Datei selbst, sondern einzelne eingebettete
   MIME-Teile (z.B. eine HTML-Alternative) innerhalb einer E-Mail.

 - Ursache: sond_treeviewfm_get_index_status() (sond_treeviewfm.c)
   entschied "ist dieser Dateityp überhaupt indizierbar?" bisher über
   mime_from_extension(coverage_path) - also über die (aus dem
   internen Pfad-String geratene) Dateiendung. Eingebettete MIME-Parts
   einer E-Mail haben aber oft keinen aussagekräftigen Dateinamen/keine
   Endung, obwohl ihr echter, per Content-Sniffing ermittelter Mime-Typ
   (mime_guess_content_type() in sond_file_part_create(), gespeichert
   via sond_file_part_leaf_set_mime_type()) längst auf dem
   SondFilePartLeaf steht - und genau dieser gespeicherte Typ ist es
   auch, nach dem sond_index() beim tatsächlichen Indizieren
   dispatcht. Die Endungs-Ratelogik hier lief also am tatsächlich
   verwendeten Typ vorbei und lieferte für genau indizierte Teile
   fälschlich SOND_INDEX_STATUS_NONE.

 - Zusätzlicher Architektur-Einwand des Nutzers (unabhängig vom obigen
   Bug, an sond_treeviewfm_get_index_status()): die vfunc
   get_section_page_range() lieferte für einen LEAF_SECTION-Knoten
   (Anbindung) nur zwei Ints (von_seite/bis_seite), die diese generische
   BASISKLASSE dann selbst als PDF-artigen Seitenbereich an
   sond_index_ctx_get_file_status() weiterreichte. Zitat: "Das ist im
   Falle von zond_treeviewfm (zufällig) so, muß aber nicht sein. Die
   vfunc sollte daher vielleicht SondIndexStatus zurückgeben." - "Section
   = Seitenbereich" ist eine zond/PDF-spezifische Annahme (bei zond
   zufällig immer zutreffend), die die generische Basisklasse nicht
   voraussetzen darf; eine andere Unterklasse könnte "Section" z.B. als
   Zeitausschnitt (Audio/Video) verstehen.

 - Fix/Umsetzung (beides in einem Aufwasch, da dieselbe Funktion
   betroffen war):
   1. sond_treeviewfm.h: vfunc get_section_page_range(SondTVFMItem*,
      gint*, gint*) -> gboolean ersetzt durch
      get_section_index_status(SondTVFMItem*, SondIndexCtx*) ->
      SondIndexStatus. Dafür #include "sond_index.h" ergänzt (keine
      zirkuläre Abhängigkeit: sond_index.h inkludiert nur
      glib/sqlite3/mupdf).
   2. sond_treeviewfm.c: sond_treeviewfm_get_index_status() - der
      komplette LEAF_SECTION-Zweig (Coverage-Pfad ermitteln,
      Mime-Check, alte vfunc für von_seite/bis_seite aufrufen,
      sond_index_ctx_get_file_status() selbst aufrufen) durch eine
      einfache Delegation an die neue vfunc ersetzt - die Basisklasse
      reicht nur noch stvfm_item und index_ctx durch und gibt das
      Ergebnis direkt zurück. Für den regulären (Nicht-Section)
      Datei-Zweig: Mime-Type-Ermittlung umgestellt auf
      sond_file_part_leaf_get_mime_type() (wenn SondFilePartLeaf),
      mime_from_extension() nur noch als Fallback für Nicht-Leaf-Typen;
      zusätzlich SOND_IS_FILE_PART_GMESSAGE()-Bypass analog zur
      bestehenden SOND_IS_FILE_PART_PDF()-Ausnahme ergänzt (beides
      generische, in sond_fileparts.h definierte Typen - keine
      zond-Spezifika, daher unproblematisch in der Basisklasse).
   3. zond_treeviewfm.c: zond_treeviewfm_get_section_page_range() zu
      zond_treeviewfm_get_section_index_status() umgebaut - übernimmt
      jetzt zusätzlich die (von der Basisklasse entfernte) Mime-Check-
      Logik für die zugrundeliegende Datei der Section (Leaf-Mime-Typ
      bevorzugt, PDF/GMessage-Bypass) sowie den abschließenden
      sond_index_ctx_get_file_status()-Aufruf mit dem per
      anbindung_parse_file_section() ermittelten Seitenbereich; liefert
      den fertigen SondIndexStatus direkt. class_init-Verdrahtung
      entsprechend angepasst.

 - Nicht durch Kompilieren/Testen verifiziert (kein Zugriff auf make zond
   in dieser Session) - Bestätigung durch den Nutzer nach dem nächsten
   Build (insbesondere: Badges bei E-Mail-MIME-Parts wie text/html jetzt
   korrekt) steht noch aus.

 Diskussion: Coverage-Invalidate-Bug bei E-Mail-Mimeparts, Attachment-
 Anzeige im Baum, Header-Indizierung (16./17.09.2026, Nutzerfund +
 ausführliche Design-Diskussion):

 - Nutzerfund: Löschen des Index für einen einzelnen Mimepart einer E-Mail
   ließ die Badges für ALLE Mimeparts (einschl. der Message selbst)
   verschwinden. Ursache: sond_index_ctx_coverage_invalidate()
   (sond_index.c) sucht einen abdeckenden Vorfahren rein über
   strrchr(path, '/') - bei einem Container-Pfad wie "mail.eml//0" landet
   das (durch die zwei direkt aufeinanderfolgenden Slashes) nach zwei
   Abschneide-Schritten zufällig exakt bei "mail.eml" - dem eigenen,
   kollabierten Coverage-Eintrag der ganzen Mail (entstanden, weil
   sond_index() beim Indizieren der ganzen Datei am Ende IMMER
   coverage_mark(ctx, filename, ...) mit filename = der ganzen Datei
   aufruft, s.u.). Der Eintrag wird korrekt gelöscht, aber die
   anschließende Geschwister-Neueintragung (Fall 2) versucht ein echtes
   Verzeichnis-Listing (sond_dir_open()) auf "mail.eml" - schlägt fehl
   (ist eine Datei, kein Verzeichnis), Geschwister werden NICHT neu
   eingetragen. Derselbe Mechanismus (Ahnen-Walk über "//") sorgt
   umgekehrt dafür, dass ein Attachment-Mimepart, dessen Inhalt NIE
   indiziert wird (s.u.), trotzdem fälschlich als "vollständig indiziert"
   angezeigt wird, sobald die Mail als Ganzes einen Coverage-Eintrag hat.

 - Was beim Indizieren einer .eml (sond_index(), mime_type
   "message/rfc822" -> sond_text_extract_gmessage(), sond_text_extract.c)
   tatsächlich passiert: Header (Von/An/CC/BCC/Betreff/Datum) UND
   rekursiv alle NICHT als "attachment" disponierten (Content-Disposition)
   text- und image-Mimeparts (MIME-Typ "text/..." bzw. "image/...",
   HTML zu Klartext konvertiert) werden zu
   EINEM zusammenhängenden Textsegment zusammengefasst und unter dem
   Coverage-Pfad der ganzen Datei ("mail.eml") abgelegt - keine
   Aufteilung nach Mimepart. Bilder werden zwar gesammelt
   (sond_text_extract_gmessage_images()), aber NUR vom Renderer für die
   Anzeige genutzt, nicht von sond_index() - Inline-Bilder werden aktuell
   nie per OCR erfasst. Echte Attachments (jeder Art) werden nie
   erfasst - auch nicht, wenn sie durchsuchbaren Text enthalten (z.B. ein
   Attachment-PDF) -, außer der Nutzer wählt den einzelnen Mimepart-Knoten
   im Baum gezielt für "Index erstellen (Auswahl)" aus.

 - Diskutiertes (noch nicht umgesetztes) Redesign: "mail.eml" (ohne
   Suffix) als Coverage-Pfad für "Header UND alle Mimeparts vollständig"
   reservieren (nur per generalisiertem coverage_try_collapse() erreicht,
   wenn wirklich jedes Kind - Message + jeder Mimepart - einen eigenen
   Coverage-Eintrag hat); "mail.eml//header" für "nur der Header ist
   indiziert" (Message-Knoten bleibt dabei ein ganz normales
   SOND_TVFM_ITEM_TYPE_LEAF - kein Sonderfall in
   zond_treeviewfm_item_get_fileparts() nötig, da anbindungsseitig ohnehin
   schon eindeutig: eine Anbindung mit filepart=="mail.eml" ohne jedes
   Suffix kann laut get_path_from_stvfm_item() nur durch Anbinden des
   Message-Knotens entstehen, da der oberste eml-DIR-Knoten selbst nie
   direkt anbindbar ist). coverage_try_collapse()/coverage_invalidate()
   müssten dafür einen GMessage-bewussten Zweig bekommen (echte Kinder
   per GMime aufzählen statt sond_dir_open()), was nebenbei auch den
   Invalidate-Bug oben sauber löst. Größerer Umbau, noch nicht
   angegangen - nur Design festgehalten.

 - Umgesetzt aus der Diskussion (17.09.2026, Nutzer-Entscheidung "Ja,
   aber ohne die Öffnen/Indizieren-Anzeige - das lassen wir so, kann man
   in der Doku drauf hinweisen"): Attachment/Inline im Baum sichtbar
   unterscheidbar gemacht, unabhängig vom obigen (noch offenen) Coverage-
   Redesign.
   1. SondFilePart (Basisklasse, sond_fileparts.c/.h): neues generisches
      Attribut is_attachment (wie path/parent) + Getter/Setter
      sond_file_part_get/set_is_attachment(). Auf der Basisklasse, nicht
      auf SondFilePartLeaf, weil ein Attachment je nach Inhalt zu jedem
      SondFilePart-Subtyp werden kann (PDF/ZIP/GMessage/Leaf).
   2. sond_tvfm_item_load_gmessage_dir() (sond_treeviewfm.c): Content-
      Disposition jetzt einheitlich (vorher nur im GMimeMessagePart-Zweig
      für den Dateinamen) gelesen und bei "attachment" auf dem neu
      erzeugten sfp_child per sond_file_part_set_is_attachment() vermerkt.
   3. sond_icon_util.h/.c: SondIconCorner um TOP_LEFT/TOP_RIGHT erweitert
      (vorher nur die beiden unteren Ecken, jetzt für SeaDrive+Index+
      Attachment gleichzeitig gebraucht); sond_icon_util_render_with_
      overlays() Compositing entsprechend generalisiert (2x2-Ecken statt
      nur links/rechts unten). Neue Funktion
      sond_icon_util_attachment_badge_pixbuf() - lädt "mail-attachment-
      symbolic" aus dem Icon-Theme (anders als die Status-Badges bewusst
      ein Symbol statt eines Farbkreises, da hier keine mehrwertige
      Zustandsskala, sondern eine binäre Eigenschaft angezeigt wird).
   4. sond_treeviewfm_render_file_icon(): drittes Overlay (Attachment,
      oben rechts) ergänzt, geprüft unabhängig vom Baum-Item-Typ (DIR
      oder LEAF) direkt über sond_file_part_get_is_attachment() auf
      stvfm_item_priv->sond_file_part - erfasst damit auch als Attachment
      eingebettete Container (ZIP/PDF/verschachtelte E-Mail).
   - Bewusst NICHT umgesetzt (Nutzer-Entscheidung): keine UI-Kennzeichnung
     dafür, dass "Öffnen/Öffnen mit" des Message-Knotens die GANZE E-Mail
     öffnet, während "Indizieren" (nach obigem, noch offenem Redesign) nur
     die Kopfzeilen abdecken würde - bleibt unkommentiert in der
     Anwendung, nur hier in der Doku festgehalten.
   - Nicht durch Kompilieren/Testen verifiziert (kein Zugriff auf make
     zond in dieser Session) - Bestätigung durch den Nutzer nach dem
     nächsten Build steht noch aus.

 Coverage-/Indizierungs-Redesign für E-Mails, schrittweise Umsetzung
 (17.09.2026, Nutzer-Entscheidung "Wir wollen das angehen!" /
 "Nein, ok nacheinander." - explizit EIN Schritt nach dem anderen,
 jeweils dokumentiert, bevor der nächste beginnt):

 Geplante Schritte (s.o. Diskussion für das Zielbild):
   1. sond_text_extract.c/.h: reine Header-Extraktion als eigene Funktion.
   2. Message-Knoten-Auswahl von "ganze Datei" unterscheiden, unnötige
      OCR-Vorverarbeitung dafür überspringen.
   3. sond_index()-Dispatch: bei Message-Knoten Header-Extraktion +
      Coverage-Pfad "x.eml//header" statt "x.eml" verwenden.
   4. coverage_try_collapse()/coverage_invalidate() GMessage-bewusst
      machen (container_entrycount statt sond_dir_open() - behebt
      nebenbei den Invalidate-Bug oben).
   5. zond_treeviewfm_item_get_fileparts_readdir(): "Gesamtes Projekt"
      soll .eml-Dateien in Message+Mimeparts auflösen statt als einen
      opaken Leaf zu behandeln.
   6. sond_treeviewfm_get_index_status(): Badge-Berechnung für E-Mails
      auf die neue Aggregat-Logik umstellen.

 Schritt 1 (abgeschlossen): sond_text_extract.c/.h - build_gmessage_text()
 in einen neuen, reinen Header-Baustein build_gmessage_header_text() und
 den unverändert bleibenden Rest (Trennlinie + Body-Sammlung) aufgeteilt;
 neue öffentliche Funktion sond_text_extract_gmessage_header() (nur
 Header, kein Body) für die künftige gezielte Message-Knoten-Indizierung
 (Schritt 3) ergänzt. Bestehende sond_text_extract_gmessage() (Anzeige +
 bisherige Indizierung) unverändert im Verhalten.

 Schritt 2 (abgeschlossen): Message-Knoten von "ganze Datei" unterscheiden.
   1. SondPageRange (sond_process_file.h) um gboolean gmessage_header_only
      erweitert (statt von/bis zu überladen oder einen neuen Typ
      einzuführen - dasselbe Muster wie is_attachment als eigenständiges
      Attribut). Neuer Konstruktor sond_page_range_new_gmessage_header()
      (von=bis=-1, gmessage_header_only=TRUE).
   2. zond_treeviewfm_item_get_fileparts() (zond_treeviewfm.c): neuer
      else-if-Zweig erkennt den Message-Knoten eindeutig über
      type==SOND_TVFM_ITEM_TYPE_LEAF && !path_or_section &&
      SOND_IS_FILE_PART_GMESSAGE(sond_file_part) (der "//message"-Marker
      wird beim Item-Erzeugen sofort zu NULL, s. sond_tvfm_item_create() -
      ein LEAF mit GMessage-sfp und ohne path_or_section kann nur der
      Message-Knoten sein, nie ein numerisch adressierter Mimepart) und
      trägt für diesen Fall sond_page_range_new_gmessage_header() statt
      NULL/eines Seitenbereichs ein.
   3. Das neue Flag durchgereicht: sond_process_fileparts() liest
      range->gmessage_header_only und übergibt es an sond_process_file()
      -> sond_process_file_do_rec() (beide Signaturen um den Parameter
      gmessage_header_only erweitert, ebenso der einzige externe Aufrufer
      sond_server_repo_worker.c mit FALSE). Dort: process_gmessage_for_ocr()
      wird bei gmessage_header_only übersprungen - das OCRen/Bearbeiten
      eingebetteter Inhalte (Bilder, PDF-Attachments) wäre reine
      Verschwendung, wenn ohnehin nur der Header indiziert werden soll
      (Schritt 3). sond_index() selbst bekommt das Flag in diesem Schritt
      BEWUSST NOCH NICHT übergeben - der Aufruf bleibt unverändert, sodass
      bei Auswahl des Message-Knotens vorerst weiterhin die ganze Mail
      (ohne die nun übersprungene OCR-Vorverarbeitung) unter dem
      Coverage-Pfad der ganzen Datei indiziert wird. Die eigentliche
      Umstellung auf Header-only-Extraktion + eigenen Coverage-Pfad
      "x.eml//header" ist Schritt 3.
   - sond_process_file.h: gboolean-Typedef ergänzt (fehlte bisher unter
     den dortigen minimalen Typedefs für gchar/guchar/gint/gsize/gpointer -
     identisch zu glibs eigenem typedef int gboolean, daher unproblematisch
     bei gemeinsamer Übersetzungseinheit mit glib.h, wie schon bei den
     bestehenden Typedefs dort).
   - Bei der Signaturerweiterung von sond_process_file_do_rec() zunächst
     vier interne, rekursive Aufrufstellen übersehen (Compiler-Fehler
     "too few arguments", vom Nutzer beim eigenen Build gemeldet):
     process_zip_for_ocr() (ZIP-Eintrag-Rekursion), gmessage_process_part()
     (eingebettete Nachricht), ein weiterer Mimepart-Rekursionszweig
     sowie process_emb_file() (in PDF eingebettete Datei). Alle vier
     verarbeiten stets verschachtelte/eingebettete Inhalte, nie den
     obersten Message-Knoten selbst - dort jeweils FALSE ergänzt.
   - Nicht durch Kompilieren/Testen verifiziert (kein Zugriff auf make
     zond in dieser Session) - Bestätigung durch den Nutzer nach dem
     nächsten Build steht noch aus.

 Schritt 3 (abgeschlossen): sond_index()-Dispatch für Header-only.
   1. sond_index() (sond_index.c/.h) um Parameter gboolean
      gmessage_header_only erweitert (einziger externer Aufrufer:
      sond_process_file.c). Nur wirksam bei mime_type "message/rfc822".
   2. Intern: is_header_only = gmessage_header_only &&
      mime_type=="message/rfc822"; davon abgeleitet ein g_autofree
      header_path = "<filename>//header" und idx_filename = is_header_only
      ? header_path : filename. idx_filename wird ab da konsequent überall
      verwendet, wo bisher filename für DB-Operationen stand:
      sond_index_ctx_should_process_page(), sond_index_ctx_clear_page(),
      db_insert_chunk(), sond_index_page_set(),
      sond_index_ctx_coverage_mark(), sond_index_ctx_set_page_count() (dort
      ohnehin nur für PDF relevant, bei Mails nie erreicht) sowie die
      zugehörigen Log-Meldungen. filename selbst bleibt unverändert (wird
      für die Extraktion/den ursprünglichen Dateinamen weiter gebraucht).
   3. Segment-Extraktion: bei is_header_only
      sond_text_extract_gmessage_header() statt
      sond_text_extract_gmessage() (Schritt 1).
   4. sond_index_ctx_coverage_mark() wurde geprüft: löscht/inserted nur den
      übergebenen path selbst plus "path/%"/"path//%"-Kinder, rührt KEINEN
      Vorfahren an - unproblematisch für einen "x.eml//header"-Pfad, keine
      Berührung mit dem bekannten Ahnen-Walk-Bug (der sitzt in
      coverage_get()/coverage_invalidate(), s.o., Schritt 4).
   5. sond_process_fileparts() (sond_process_file.c): die beiden
      coverage_get()-Aufrufe (Vorab-Kurzschluss vor dem Öffnen der Datei,
      Nach-Prüfung fürs Coalescing) fragten bisher immer file_part (den
      Pfad der ganzen Datei) ab - bei gmessage_header_only jetzt stattdessen
      coverage_key = "file_part//header", damit sie denselben Pfad sehen,
      den sond_index() tatsächlich beschreibt. coverage_try_collapse()
      wird bei gmessage_header_only bewusst NICHT aufgerufen - das ist
      Schritt 4 (GMessage-bewusstes Collapse), vorher würde der bestehende
      sond_dir_open()-basierte Mechanismus nur denselben "//"-Ahnen-Walk-Bug
      treffen, den das Redesign beheben soll.
   - Auswirkung für den Nutzer: Wählt er jetzt den "Message"-Knoten einer
     E-Mail gezielt für "Index erstellen (Auswahl)" aus, wird NUR der
     Header (Von/An/CC/BCC/Betreff/Datum) indiziert und unter
     "x.eml//header" abgedeckt - die einzelnen Mimeparts (und "x.eml" ohne
     Suffix als "alles vollständig") sind davon unberührt. Das finale
     Zusammenspiel ("x.eml" = Header UND alle Mimeparts vollständig, per
     Collapse) folgt in Schritt 4.
   - Nicht durch Kompilieren/Testen verifiziert (kein Zugriff auf make
     zond in dieser Session) - Bestätigung durch den Nutzer nach dem
     nächsten Build steht noch aus.

 Schritt 4 (abgeschlossen): GMessage-bewusstes Collapse/Invalidate.

 - Vorfrage an den Nutzer geklärt (17.09.2026): container_entrycount für
   E-Mails war am 15.09.2026 (Task #94) bewusst als toter Code
   zurückgebaut worden, weil es damals keinen Verwendungszweck hatte. Mit
   der Header/Mimepart-Trennung (Schritt 2/3) gibt es den jetzt - Nutzer
   hat der Wiedereinführung zugestimmt. Zweite Entscheidung: Attachments
   ZÄHLEN beim Collapse mit ("x.eml" wird nur dann komplett, wenn wirklich
   jeder Mimepart - auch Attachments - einzeln indiziert wurde) - in der
   Praxis wird das Collapse damit fast nur bei bewusster, vollständiger
   Einzelauswahl aller Mimeparts einer Mail erreicht, nicht beiläufig
   durch "Gesamtes Projekt" (das weiterhin nur EINEN Blob für Header+
   Inline-Text erzeugt, s.u.).

 1. container_entrycount-Population für E-Mails NEU (sond_index.c):
    gmessage_count_root_entries(buf, size) - öffnet die Mail aus dem
    bereits im Speicher vorliegenden Puffer (kein zusätzlicher
    Dateizugriff, SeaDrive-unbedenklich) und liefert die Anzahl direkter
    Wurzel-Mimeparts (g_mime_multipart_get_count() bei Multipart-Root,
    sonst 1). In sond_index() wird darüber IMMER (unabhängig von
    gmessage_header_only, da der Puffer so oder so vorliegt)
    container_entrycount(filename) = n_mimeparts + 1 (der "+1" ist der
    virtuelle Header-Slot) aufgefrischt.

 2. Neue interne Bausteine (sond_index.c, statisch):
    - coverage_get_exact(ctx, path): wie sond_index_ctx_coverage_get(),
      aber OHNE Ahnen-Walk - nur der exakte Pfad selbst.
    - gmessage_find_last_boundary(path): letztes "//"-Vorkommen.
    - is_gmessage_child_segment(segment): TRUE nur für "header" oder eine
      reine Ziffernfolge - grenzt E-Mail-Kinder sauber von anderen
      "//"-Containern (z.B. ZIP-interne Pfade mit echten Dateinamen) und
      von tiefer verschachtelten Multiparts (z.B. "0/1") ab, für die
      diese Runde bewusst KEIN Collapse/Invalidate anbietet (Segment mit
      "/" oder mit Nicht-Ziffern -> Funktion liefert FALSE -> bisheriges,
      unverändertes Verhalten greift).
    - gmessage_container_child_keys(ctx, container): "container//header"
      + "container//0" .. "container//(N-1)" aus container_entrycount,
      NULL wenn unbekannt.

 3. sond_index_ctx_coverage_try_collapse(): zusätzlicher Zweig VOR der
    bisherigen "/"-basierten sond_dir_open()-Logik - liegt current an
    einer erkannten E-Mail-Grenze, werden die erwarteten Kind-Schlüssel
    statt eines Verzeichnis-Listings geprüft (coverage_get_exact() pro
    Kind, Mindestmodus wie bisher); sind alle abgedeckt, wird die E-Mail
    zu einem Eintrag zusammengefasst und current auf die .eml-Datei
    selbst gesetzt (die geht danach normal über den bestehenden
    "/"-Zweig weiter nach oben). Kein erkannter GMessage-Kindpfad ->
    unverändertes Verhalten (fällt auf sond_dir_open() zurück, das für
    einen Container ohnehin fehlschlägt - keine Verschlechterung
    gegenüber vorher).

 4. sond_index_ctx_coverage_invalidate(): die Fall-2-Geschwister-
    Rekonstruktion (bisher: g_strsplit(rest, "/", -1), IMMER
    sond_dir_open()) durch einen eigenen Tokenizer ersetzt, der pro
    Segment auch den ORIGINALEN Trenner ("/" vs. "//") mitführt. Pro
    Ebene: bei "//" + erkanntem E-Mail-Kindsegment werden die erwarteten
    Geschwister-Schlüssel (wie oben) außer dem gerade invalidierten neu
    eingetragen (container_entrycount-basiert, kein Dateizugriff); sonst
    unverändert sond_dir_open()-Listing. Das behebt den ursprünglichen
    Bug direkt: Löschen des Index für einen einzelnen Mimepart (z.B.
    "mail.eml//0") fand den kollabierten "mail.eml"-Eintrag als
    abdeckenden Vorfahren, löschte ihn, und die Geschwister-Rekonstruktion
    scheiterte lautlos an sond_dir_open("mail.eml") (Datei, kein
    Verzeichnis) - jetzt werden stattdessen "mail.eml//header" und alle
    ÜBRIGEN "mail.eml//N" korrekt mit dem alten Modus neu eingetragen,
    nur der invalidierte Mimepart selbst verliert seinen Badge. Nebeneffekt
    behoben: dieselbe Rekonstruktion baute bisher (rein hypothetisch, weil
    sond_dir_open() ohnehin nie erfolgreich war) Geschwister-Pfade IMMER
    mit einfachem "/" statt korrekt "//" - jetzt trennerkorrekt.

 5. sond_process_file.c: die in Schritt 3 bewusst gesetzte Sperre
    "kein coverage_try_collapse() bei gmessage_header_only" wieder
    aufgehoben - der neue GMessage-Zweig macht das jetzt sicher.

 - Bekannte, bewusste Einschränkungen (dokumentiert im Code):
   tiefer verschachtelte Multiparts (z.B. "x.eml//0/1") und "//"-Grenzen
   anderer Container (ZIP) nehmen weiterhin NICHT am Collapse/Invalidate
   teil - exakt dieselbe Einschränkung wie vorher, nur nicht mehr
   fälschlich mit der E-Mail-Logik vermischt.
 - Nicht durch Kompilieren/Testen verifiziert (kein Zugriff auf make
   zond in dieser Session) - Bestätigung durch den Nutzer nach dem
   nächsten Build steht noch aus, insbesondere: (a) Badges bleiben beim
   Löschen des Index eines einzelnen Mimeparts für die übrigen
   Mimeparts/die Message erhalten, (b) nach Einzelindizierung von Header
   UND jedem Mimepart (inkl. Attachments) kollabiert "x.eml" zu einem
   grünen Gesamt-Badge.

 KORREKTUR einer eigenen Fehlannahme (17.09.2026, beim Planen von Schritt 5
 entdeckt): in der Diskussion vom 16./17.09.2026 wurde angenommen (und vom
 Nutzer auf Nachfrage bestätigt), "echte Attachments werden nie erfasst -
 auch nicht, wenn sie durchsuchbaren Text enthalten". Das stimmte nur für
 sond_text_extract_gmessage() (den kombinierten Header+Inline-Blob) - es
 gibt aber eine ZWEITE, unabhängige Rekursion: process_gmessage_for_ocr()
 -> gmessage_process_part() (sond_process_file.c, schon lange vorhanden)
 geht JEDES MIME-Leaf durch, UNABHÄNGIG von dessen Content-Disposition,
 und ruft dafür sond_process_file_do_rec() mit Dateiname
 "eml_filename//internal_path" auf - das mündet am Ende ganz normal in
 sond_index(), welches den jeweiligen Mimepart (PDF, Text, DOCX, ...)
 gemäß seines eigenen MIME-Typs indiziert. Ein Attachment-PDF WIRD also
 bereits heute durchsucht, sofern sein MIME-Typ unterstützt ist - nur
 eben unter dem Pfad "x.eml//N", nicht als Teil des kombinierten
 "x.eml"-Blobs. Für flache Multipart-Strukturen (kein verschachteltes
 Multipart) entspricht "N" dabei genau dem Index, den auch
 container_entrycount/gmessage_container_child_keys() erwarten (Schritt
 4) - purer Zufall keineswegs, sondern weil beide Mechanismen dieselbe
 "//"+Index-Konvention (sond_file_part_get_filepart()) verwenden.

 Schritt 5 (abgeschlossen, dadurch viel kleiner als ursprünglich geplant):
 einziges fehlendes Puzzlestück für ein vollständiges Kind-Set (Header +
 jeder Mimepart) bei einem GANZ NORMALEN "Gesamtes Projekt"/Ganze-Datei-
 Lauf war der Header selbst - der wird von keiner der beiden Rekursionen
 erzeugt. Fix (sond_index.c, im coverage-Coalescing-Block, nach
 coverage_mark(idx_filename,...) und dem file_pagecount-Block): bei einer
 normalen (nicht schon header-only) message/rfc822-Indizierung ruft
 sond_index() sich selbst rekursiv mit gmessage_header_only=TRUE auf (das
 dortige is_header_only verhindert eine weitere Rekursionsebene) - erzeugt
 "filename//header" über exakt denselben Weg wie Schritt 3. KEINE Änderung
 an zond_treeviewfm_item_get_fileparts_readdir() nötig (ursprünglicher
 Plan verworfen) - die Fileparts-Sammlung bleibt unverändert ein opaker
 Leaf pro Datei, die Aufschlüsselung passiert wie schon vorher
 ausschließlich downstream in sond_process_file.c.
 - Praktische Folge: bei einer E-Mail mit flacher Multipart-Struktur
   (kein multipart-in-multipart, z.B. kein "HTML+Text-Alternative neben
   Attachments") kollabiert "x.eml" nach einem normalen "Gesamtes
   Projekt"-Lauf jetzt automatisch zu einem einzigen grünen Eintrag -
   inklusive Attachments, wie vom Nutzer gefordert ("Wenn die gesamte eml
   indiziert wird, sollen natürlich auch die attachments indiziert
   werden!" - was, wie oben festgestellt, für unterstützte MIME-Typen
   bereits vorher der Fall war, nur ohne die Header-Ergänzung nie zum
   Collapse führte). Bei verschachtelten Multiparts bleibt es (bewusst,
   s. Schritt 4) bei Einzel-Badges ohne automatisches Collapse - kein
   Rückschritt, nur (weiterhin) keine Optimierung für diesen Fall.
 - Nicht durch Kompilieren/Testen verifiziert (kein Zugriff auf make
   zond in dieser Session) - Bestätigung durch den Nutzer nach dem
   nächsten Build steht noch aus.

 Schritt 6 (abgeschlossen): Badge-Anzeige.
 - Analyse: sond_treeviewfm_get_coverage_path() liefert für ein LEAF-Item
   einfach sond_file_part_get_filepart(sond_file_part). Für einen
   Mimepart-Kind-Knoten ist das schon automatisch "mail.eml//N" (eigener
   sfp mit eigenem path-Feld) - für den Message-Knoten dagegen bewusst
   das BARE "mail.eml" (teilt sich denselben sfp mit der ganzen eml, kein
   eigenes Pfadsegment, s. Schritt 2). sond_index_ctx_get_dir_status()
   (Badge des eml-DIR-Knotens) nutzt SQL LIKE 'path/%' - das matcht per
   SQL-Semantik automatisch auch "mail.eml//header" und "mail.eml//N"
   (ein "/" gefolgt von IRGENDETWAS, auch einem weiteren "/") - Mimepart-
   und DIR-Badges brauchten deshalb KEINE Änderung, nur der Message-
   Knoten selbst.
 - Fix (sond_treeviewfm.c, sond_treeviewfm_get_index_status()): neuer
   Zweig VOR der bestehenden PDF/GMessage-Sonderbehandlung, der den
   Message-Knoten exakt wie in zond_treeviewfm_item_get_fileparts()
   erkennt (LEAF, kein path_or_section, GMessage-sfp). Statt nur
   coverage_path ("mail.eml") wird ZUSÄTZLICH "mail.eml//header"
   abgefragt; der Badge zeigt den jeweils besseren der beiden Status
   (MAX von NONE/PARTIAL/FULL) - FULL, wenn ENTWEDER der Header gezielt
   indiziert ist ODER die ganze Mail als Block/per Collapse unter
   "mail.eml" selbst abgedeckt ist.
 - Nicht durch Kompilieren/Testen verifiziert (kein Zugriff auf make
   zond in dieser Session) - Bestätigung durch den Nutzer nach dem
   nächsten Build steht noch aus.

 Damit ist das E-Mail-Coverage-Redesign (alle 6 geplanten Schritte) aus
 Nutzersicht abgeschlossen - Gesamtverifikation (Build + manuelles
 Durchspielen: Message einzeln indizieren, einzelne Mimeparts einzeln
 indizieren/löschen, Collapse bei vollständiger Abdeckung, Badges in
 allen drei Knotentypen) steht noch aus.

 Regressions-Bugfix (18.09.2026): nach dem Build meldete der Nutzer "Index
 löschen für einen mimepart löscht auch message und wohl auch die anderen
 mimeparts". Ursache ausschließlich per statischer Codeanalyse gefunden
 (auf Nutzerwunsch "Lieber erst weiter analysieren" - kein Diagnose-
 Logging eingebaut):

 zond_index_loeschen_ht() (headerbar.c) baute - anders als
 zond_index_erstellen_ht()/sond_process_fileparts() (sond_process_file.c,
 dortiges "coverage_key") - den an sond_index_ctx_delete_index()
 übergebenen Pfad NICHT gmessage_header_only-bewusst, sondern übergab
 immer den nackten sond_file_part_get_filepart()-Rückgabewert. Für den
 Message-Knoten (teilt sich denselben sfp mit der ganzen .eml, s. Schritt
 2/6 oben) ist das der nackte Dateiname "mail.eml", NICHT "mail.eml//header".
 "Index löschen" für den Message-Knoten landete dadurch in
 sond_index_ctx_delete_index() im "ganze Datei"-Zweig für "mail.eml"
 SELBST. Hat "mail.eml" (aus dem ursprünglichen Ganze-Datei-Indizierlauf)
 einen eigenen, direkten coverage-Eintrag, greift in
 sond_index_ctx_coverage_invalidate() Fall 1 (Vorfahre == path selbst) -
 der Eintrag wird dort einfach gelöscht, OHNE die
 Geschwister-Rekonstruktion aus Fall 2 (die nur greift, wenn path selbst
 NICHT der eigene coverage-Träger ist, sondern erst ein Vorfahre
 abdeckend gefunden wird). Ergebnis: Message- UND alle Mimepart-Badges
 verschwinden - exakt das gemeldete Symptom. Für einen wirklich
 nummerierten Mimepart-Knoten (eigener sfp, eigenes path-Feld, z.B. "0")
 dagegen bereits vorher korrekt: dessen coverage-Pfad hat nach einem
 Ganze-Datei-Lauf keinen eigenen direkten Eintrag mehr (von
 coverage_mark("mail.eml",...) beim Coalescing automatisch mitgelöscht,
 s. Schritt 4/5), landet beim Invalidieren also in Fall 2 und durchläuft
 die (mehrfach durchgerechnete, korrekte) GMessage-bewusste
 Geschwister-Rekonstruktion.

 Fix (headerbar.c, zond_index_loeschen_ht()): analog coverage_key in
 sond_process_fileparts() wird bei range->gmessage_header_only jetzt
 "%s//header" statt des nackten file_part an sond_index_ctx_delete_index()
 übergeben - derselbe Pfad, unter dem sond_index() den Header tatsächlich
 abgelegt/abgedeckt hat.
 - Vom Nutzer bestätigt: behebt den gemeldeten Bug ("scheint zu klappen").

 Separate Regression (18.09.2026), NICHT durch das E-Mail-Coverage-
 Redesign verursacht: Doppelklick-Hydrierung von SeaDrive-Platzhaltern
 (BAUM_FS) funktionierte nach einem Windows-Update plötzlich gar nicht
 mehr - für ALLE Dateitypen, übersteht Neustart und kompletten Neu-Build
 von zond. Ausführliche Fehlersuche (siehe auch die vielen ausgeschlossenen
 Hypothesen unten, damit sie nicht erneut geprüft werden):
 - Ausgeschlossen: Laufzeit-Zustand (Neustart hilft nicht), stale .o durch
   inkrementellen Build (clean rebuild hilft nicht), SeaDrive-Client selbst
   (Explorer hydriert dieselbe Datei problemlos), jede der 7 Code-Änderungen
   dieser Sitzung (alle nachweislich auf message/rfc822-Dispatch oder die
   Index-erstellen/löschen-Menüaktionen beschränkt, keine berührt
   sond_treeviewfm_open()/sond_file_part_create()/Item-Typ-Bestimmung).
 - Diagnose-Logging in sond_treeviewfm_open() (mit Nutzer-Zustimmung
   eingebaut, s.u. wieder entfernt) zeigte: CreateFileW(GENERIC_READ) auf
   den SeaDrive-Platzhalter schlägt mit GetLastError()=395
   (ERROR_CLOUD_FILE_ACCESS_DENIED) fehl - der alte Code hatte dafür einen
   rohen "kurz reinlesen"-Trick verwendet (CreateFileW+ReadFile 1 Byte)
   statt der dafür vorgesehenen Cloud-Files-API.
 - Recherche: Windows 11 KB5124008 (08.09.2026) war laut Presseberichten
   (Windows Latest u.a.) ein ungewöhnlich umfangreiches Update mit
   zahlreichen, scheinbar unzusammenhängenden Kollateralschäden (Explorer.
   exe-Abstürze, File History, RDS, sogar Claude Cowork selbst über eine
   Plan9-Filesystem-Änderung) - passt zeitlich zum vom Nutzer bestätigten
   Windows-Update. Die Windows-Dokumentation zu ERROR_CLOUD_FILE_ACCESS_
   DENIED bestätigt: der Fehler tritt typischerweise auf, wenn eine
   Anwendung eine Cloud-Datei mit gewöhnlichem Lesezugriff statt über die
   Cloud-Filter-API zu hydrieren versucht - exakt das alte Verhalten. Das
   Notfall-Update KB5129195 (15.09.2026) behebt dies laut Nutzer NICHT.
 - Fix: sond_seadrive_hydrate() (neu, sond_treeviewfm_seadrive.c/.h) ersetzt
   den alten CreateFileW(GENERIC_READ)+ReadFile()-Trick durch die
   offizielle CfHydratePlaceholder()-API (dynamisch aus cldapi.dll geladen,
   wie der Rest der Datei - kein cfapi.h im verwendeten MinGW-Toolchain,
   s. Kommentar bei cfapi_init_once()). Öffnet das Handle nur mit
   FILE_READ_ATTRIBUTES (statt GENERIC_READ) und hydriert dann gezielt 1
   Byte über CfHydratePlaceholder() - reicht laut Doku, um den Provider
   zum Download zu bewegen, ohne synchron auf die ganze Datei zu warten.
   sond_treeviewfm_open() ruft jetzt diese Funktion auf; bei Fehlschlag
   fällt der Code (anders als vorher) auf den normalen Öffnen-Weg zurück,
   statt kommentarlos nichts zu tun. Das testweise eingebaute Diagnose-
   Logging wurde wieder entfernt.
 - Vom Nutzer bestätigt: behebt den gemeldeten Bug ("klappt jetzt!").

 Folgeproblem (18.09.2026), vom Nutzer direkt im Anschluss an obige
 Bestätigung gemeldet: sond_seadrive_hydrate() (s.o.) ruft
 CfHydratePlaceholder() synchron im GTK-Hauptthread auf. Der Doku-Hinweis
 oben ("hydriert gezielt 1 Byte ... ohne synchron auf die ganze Datei zu
 warten") erwies sich als falsch: der Nutzer klickte versehentlich eine
 >51-GB-Datei doppelt an, das Programm fror daraufhin mehrere Minuten
 komplett ein, ohne Cursor-Rückmeldung und ohne Abbrechen-Möglichkeit
 (vermutlich lädt der SeaDrive-Provider unabhängig von der angeforderten
 Länge grundsätzlich die ganze Datei, bevor der Aufruf zurückkehrt).

 Ursprünglich vorgeschlagen: das im Projekt etablierte Info-Fenster+
 GThread+Polling-Muster (analog zond_index_erstellen_ht_mit_modus() in
 headerbar.c) mit Abbrechen-Button. Nutzer-Entscheidung dagegen, deutlich
 einfacher: kein Warten und kein Info-Fenster nötig, wenn der Download
 ohnehin im Hintergrund weiterläuft - es soll immer sofort an die UI
 zurückgegeben werden; einzige Anforderung: ein erneuter Doppelklick auf
 dieselbe, noch herunterladende Datei darf keinen zweiten, redundanten
 Hydrier-Versuch auslösen.

 Umsetzung (sond_treeviewfm_seadrive.c/.h):
 - sond_seadrive_needs_hydration(full_path): neuer, schneller, nicht-
   blockierender Vorab-Check (nur GetFileAttributesW). Von
   sond_seadrive_hydrate() (Fast-Path für schon lokale Dateien) und von
   sond_treeviewfm_open() genutzt, um zu entscheiden, ob überhaupt
   hydriert werden muss, ohne dafür einen Thread zu starten.
 - sond_seadrive_hydrate_async(full_path): neu, Fire-and-forget. Prüft
   unter einem Mutex eine GHashTable aktuell laufender Pfade
   (g_hydrating_paths) - ist full_path bereits enthalten, sofortiger
   No-Op-Rückkehr (erfüllt die Nutzer-Anforderung "erneuter Doppelklick
   ... nichts mehr bewirkt"). Sonst: Pfad einfügen, GThread starten
   (g_thread_new(), sofort g_thread_unref() - nicht gejoined), der
   Thread ruft das bisherige, weiterhin synchrone
   sond_seadrive_hydrate() auf, loggt Fehler per LOG_WARN und entfernt
   den Pfad wieder aus der Menge.
 - sond_treeviewfm_open() (sond_treeviewfm.c): ruft jetzt erst
   sond_seadrive_needs_hydration() - bei FALSE (schon lokal) fällt der
   Code direkt auf den normalen Öffnen-Weg durch (kein unnötiger
   Thread-Start). Bei TRUE: sond_seadrive_hydrate_async() anstoßen und
   IMMER sofort return 0 (kein synchrones Prüfen von Erfolg/Fehlschlag
   mehr möglich, da fire-and-forget - Fehler landen nur noch im Log,
   nicht mehr im Rückgabewert dieser Funktion).
 - Der oben zitierte, jetzt widerlegte Doku-Kommentar bei
   sond_seadrive_hydrate() wurde um eine Korrektur (18.09.2026) ergänzt.
 - Nicht durch Kompilieren/Testen verifiziert (kein Zugriff auf make
   zond in dieser Session) - Bestätigung durch den Nutzer nach dem
   nächsten Build steht noch aus. Insbesondere zu testen: (a) Doppelklick
   auf große Platzhalter-Datei gibt UI sofort frei, Download läuft im
   Hintergrund weiter und Datei-Badge aktualisiert sich nach Abschluss
   wie gehabt über den SeaDrive-Watcher; (b) Doppelklick auf bereits
   lokale Datei öffnet weiterhin sofort ganz normal.

 Ergänzung (18.09.2026), unmittelbar im Anschluss: Nutzerwunsch, (b) oben
 ("wiederholter Doppelklick auf dieselbe, noch herunterladende Datei löst
 keinen zweiten Download aus") um Sichtbarkeit/Kontrolle zu erweitern -
 Zitat: "Vielleicht bei erneutem Klick auf Datei, deren Hydration schon
 gestartet wurde: Fenster mit Mitteilung, daß Download im Gange, schon
 x/y Bytes heruntergeladen und Abbruchmöglichkeit. Damit nicht der Server
 verstopft wird." D.h. statt eines stillen No-Ops bei erneutem
 Doppelklick jetzt ein Dialog mit Fortschritt und echtem Abbrechen-
 Versuch (nicht nur "UI-Warten abbrechen", das ja mit dem Fire-and-
 forget-Design von vornherein entfällt - hier geht es um den tatsächlich
 laufenden Download).

 Umsetzung (sond_treeviewfm_seadrive.c/.h):
 - HydratingEntry (Wert in g_hydrating_paths, ersetzt den bisherigen
   Dummy-Wert): enthält jetzt ein per DuplicateHandle(GetCurrentThread(),
   ..., THREAD_TERMINATE, ...) erzeugtes Thread-Handle des jeweiligen
   Hydrier-Threads, sobald dieser läuft.
 - sond_seadrive_is_hydrating(full_path): einfache Abfrage, ob für
   full_path aktuell ein Eintrag existiert.
 - sond_seadrive_hydrate_cancel(full_path): ruft CancelSynchronousIo()
   auf das Thread-Handle auf (bricht den dort blockierenden
   CfHydratePlaceholder()-Aufruf ab) - oder setzt nur cancel_requested,
   falls der Thread sein Handle noch nicht eingetragen hat (Race
   unmittelbar nach dem Start), woraufhin der Thread den Download gar
   nicht erst beginnt. WICHTIG (Doku-Kommentar an der Funktion):
   CancelSynchronousIo() ist für CfHydratePlaceholder() nicht offiziell
   dokumentiert/garantiert - ob der SeaDrive-Minifilter/-Dienst den
   Abbruch tatsächlich zeitnah beachtet und den Download serverseitig
   stoppt, ist unsicher. Es ist aber der einzige als Konsument (nicht als
   Sync-Provider) verfügbare Mechanismus, ohne CfHydratePlaceholder()
   selbst auf OVERLAPPED umzustellen.
 - sond_seadrive_show_hydrate_progress_dialog(parent, full_path): neuer
   GtkDialog (angelehnt an das bestehende InfoWindow-Muster in misc.c,
   aber eigenständig, da die Fortschrittsquelle hier eine externe
   Pollschleife statt eines vom Aufrufer selbst gefütterten Fortschritts
   ist). Fortschritt: Dateigröße einmalig per GetFileSizeEx(), danach
   alle 300ms per g_timeout_add() OnDiskDataSize über
   CfGetPlaceholderInfo(CF_PLACEHOLDER_INFO_STANDARD) abgefragt (Struct-
   Layout aus der offiziellen cfapi.h recherchiert und wie der Rest der
   Datei manuell dupliziert). "Abbrechen"-Button ruft
   sond_seadrive_hydrate_cancel() auf und deaktiviert sich selbst; das
   Fenster schließt sich von selbst, sobald sond_seadrive_is_hydrating()
   FALSE liefert (Hydrierung zu Ende - egal ob Erfolg, Fehler oder
   Abbruch). Schließen über das X bricht NICHT ab, der Download läuft
   dann unbeobachtet im Hintergrund weiter (Timeout wird beim "destroy"-
   Signal sauber entfernt).
 - sond_treeviewfm_open(): unterscheidet jetzt bei needs_hydration==TRUE
   zusätzlich per sond_seadrive_is_hydrating() zwischen "neu anstoßen"
   (erster Doppelklick) und "Fortschritt/Abbrechen-Dialog zeigen"
   (wiederholter Doppelklick).
 - Nicht durch Kompilieren/Testen verifiziert. Zusätzlich zu (a)/(b) oben
   zu testen: (d) wiederholter Doppelklick auf eine noch laufende
   Hydrierung zeigt den Fortschrittsdialog mit plausibel wachsendem
   Balken; (e) Abbrechen-Button beendet den Download tatsächlich (Badge
   bleibt PENDING/Platzhalter, kein Wechsel zu hydriert) - falls nicht:
   s.o., CancelSynchronousIo() ist für diesen Anwendungsfall nicht
   offiziell garantiert, ggf. Rückfall auf OVERLAPPED-basierten Ansatz
   nötig; (f) Schließen des Dialogfensters per X lässt den Download im
   Hintergrund unangetastet weiterlaufen.

 Ergänzung (18.09.2026), unmittelbar im Anschluss: Nutzer-Feedback - ohne
 einen expliziten "Schließen"-Button war für den Fall "Fortschritt
 ansehen, aber NICHT abbrechen wollen" nur das X am Fensterrand nutzbar,
 was nicht offensichtlich ist. Ergänzt: zweiter Button "Schließen" links
 vom "Abbrechen"-Button (gtk_dialog_add_button() mit GTK_RESPONSE_NONE,
 "clicked" per g_signal_connect_swapped direkt auf gtk_widget_destroy()
 gemappt) - verhält sich wie das X (kein Cancel, Download läuft im
 Hintergrund weiter), macht diese Option aber explizit sichtbar.

 Nutzer-Test (18.09.2026): Abbrechen funktioniert ("klappt gut") -
 CancelSynchronousIo() auf das Hydrier-Thread-Handle bricht
 CfHydratePlaceholder() also tatsächlich zuverlässig ab, die
 Unsicherheit aus dem Doc-Kommentar an sond_seadrive_hydrate_cancel()
 hat sich damit nicht bewahrheitet. Fortschrittsanzeige dagegen zeigte
 durchgehend 0%, obwohl laut Windows-Explorer aktiv heruntergeladen
 wurde. Ursache (statische Analyse): cfapi.h deklariert
 CF_PLACEHOLDER_STANDARD_INFO.FileIdentity als BYTE[1] - nur ein
 Platzhalter für ein tatsächlich variabel langes, vom Aufrufer selbst
 groß genug zu allozierendes Feld (SeaDrivePlaceholderBasicInfo oben im
 selben File hat dieses Problem für CF_PLACEHOLDER_INFO_BASIC schon immer
 richtig gelöst: dort schon immer 256 statt 1 Byte). Mit nur 1 Byte
 Puffer liefert CfGetPlaceholderInfo() bei einer nicht-winzigen Identity
 (bei SeaDrive offenbar der Normalfall) HRESULT_MORE_DATA - ein
 FAILURE-HRESULT trotz des Namens (Severity-Bit gesetzt) -,
 SUCCEEDED(hr) schlägt fehl, hydrate_progress_update() bricht VOR dem
 Auswerten von OnDiskDataSize ab, Progress-Bar bleibt auf ihrem
 Default-Wert 0%. Fix: FileIdentity in SeaDrivePlaceholderStandardInfo
 (sond_treeviewfm_seadrive.c) ebenfalls auf 256 Byte vergrößert;
 zusätzlich ein einmaliges (nicht alle 300ms wiederholtes) Diagnose-
 LOG_WARN samt HRESULT und ReturnedLength ergänzt, falls 256 Byte
 wider Erwarten immer noch nicht reichen sollten. Nicht durch
 Kompilieren/Testen verifiziert.

 Neuer Nutzer-Fund (18.09.2026): "Wenn ich eine eml als Immer verfügbar
 markiere, dann wird auch Message mit grünem badge versehen, aber nicht
 die mime-parts". Ursache in sond_treeviewfm_render_file_icon()
 (sond_treeviewfm.c, BAUM_FS-Icon-Rendering): der SeaDrive-Badge-Zweig
 verlangte für LEAF- wie für DIR-Zeilen bisher explizit
 !sond_file_part_get_parent(...), also ein Top-Level-Objekt OHNE Parent -
 Mime-Parts einer E-Mail (Anhänge/Inline-Teile als eigene LEAF-Kindzeilen
 mit der .eml als sond_file_part-Parent) fielen dadurch grundsätzlich aus
 der Badge-Berechnung heraus (ebenso beträfe es ZIP-Einträge oder
 PDF-Seiten als eigene Zeilen). Der Pin-/Hydrierungsstatus gehört aber
 zur realen Datei im Dateisystem als janzem, nicht zum einzelnen
 (virtuellen) Teil - alle Kinder EINER realen Datei müssen also dasselbe
 Badge zeigen wie die Datei selbst.

 Fix: die Top-Level-Bedingung entfernt, stattdessen läuft der Code jetzt
 (wie schon zond_treeview_get_seadrive_badge() bzw.
 sond_file_part_get_filepart() es für BAUM_INHALT/AUSWERTUNG bzw. den
 "//"-Filepart-String tun) über sond_file_part_get_parent() zum obersten
 Vorfahren hoch und verwendet dessen Pfad für den full_path-/
 Hashtable-Lookup (sond_treeviewfm_seadrive_get_file_badge()). Bei einem
 Top-Level-Objekt ohne Parent läuft die Schleife einfach nicht, das
 bisherige Verhalten für "normale" Dateien bleibt also unverändert. Die
 PDF-mit-Pagetree-Ausnahme (eigene Zeile für den Container selbst, kein
 Badge, da dort keine echte Einzeldatei angezeigt wird) bleibt bestehen.
 Der bisher zusätzliche !stvfm_item_priv->path_or_section-Check im
 DIR-Zweig bleibt ebenfalls bestehen (schließt reine Abschnitts-/
 Header-Pseudo-Knoten weiterhin aus), nur die Parent-Bedingung wurde dort
 entsprechend entfernt.

 Nicht durch Kompilieren/Testen verifiziert.

 Direkter Folge-Fund (18.09.2026), derselbe Tag: "Anwahl von 'Immer
 offline verfügbar' wirkt nur bei Message, nicht bei den mimeparts" -
 dieselbe Fehlerklasse wie beim Badge oben, jetzt aber bei der
 eigentlichen Pin-Aktion selbst (BAUM_FS-Kontextmenü, sond_treeviewfm_
 seadrive.c). stvfm_item_get_full_path() lieferte für ein SondFilePart
 mit Parent (Mime-Part/ZIP-Eintrag/PDF-Seite) bisher dessen EIGENEN,
 ggf. rein internen/synthetischen sond_file_part_get_path()-Wert statt
 des echten Dateisystempfads - root+"/"+dieser-Pfad ergab damit einen
 nicht-existenten Pfad, sond_seadrive_set_pin_state() schlug für solche
 Zeilen wirkungslos fehl (nur LOG_WARN, keine sichtbare Fehlermeldung).
 Bei "Message" selbst (sfp ohne Parent) war der eigene Pfad zufällig
 schon der richtige, deshalb funktionierte es nur dort.

 Fix: stvfm_item_get_full_path() läuft jetzt, genau wie zuvor schon
 zond_treeview_seadrive_apply_to_selection() (BAUM_INHALT/AUSWERTUNG,
 zond_treeview.c - dort war der Fix bereits korrekt vorhanden) und der
 SeaDrive-Badge-Fix von oben, zum obersten sond_file_part-Vorfahren
 hoch und verwendet dessen Pfad. Die "Skip embedded entries"-Prüfung in
 apply_pin_state_to_item() (sfp != NULL && path_or_section != NULL)
 bleibt unverändert bestehen - sie betrifft reine Abschnitts-/
 Bereichs-Pseudo-Knoten (z.B. Seitenbereich), nicht Mime-Parts, und war
 nicht die Ursache.

 Nicht durch Kompilieren/Testen verifiziert.

 Weiterer Folge-Fund (18.09.2026), unmittelbar im Anschluss: "Die
 (virtuellen) Verzeichnisse in einem Container (zip-Verzeichnis,
 multipart) werden nicht mit badge markiert." Dieselbe Ursache wie bei
 den beiden Funden oben, diesmal im DIR-Zweig von
 sond_treeviewfm_render_file_icon() selbst: dort blieb bisher zusätzlich
 zur (jetzt entfernten) Parent-Bedingung noch ein
 !stvfm_item_priv->path_or_section-Check bestehen, in der irrigen
 Annahme, path_or_section markiere nur reine Abschnitts-/Header-Pseudo-
 Knoten. Tatsächlich wird path_or_section aber auch für ein bereits
 aufgeklapptes ZIP-Unterverzeichnis bzw. ein Multipart-Verzeichnis einer
 E-Mail gesetzt (interner Pfad/Kennung innerhalb des Containers,
 sond_tvfm_item_create()) - das sond_file_part ist dabei dasselbe Objekt
 wie beim Container-Top-Level-Item (die ganze .zip/.eml). Auch diese
 virtuellen Verzeichniszeilen sind also nur eine andere Ansicht EINER
 realen Datei und müssen deren Badge zeigen.

 Fix: die path_or_section-Bedingung im DIR-Zweig ebenfalls entfernt -
 bei vorhandenem sond_file_part wird jetzt unabhängig von
 path_or_section zum obersten Vorfahren hochgelaufen. Der schon
 bestehende Mechanismus darunter (live GetFileAttributesW auf full_path,
 da Ordner nicht in seadrive_file_badges geführt werden) funktioniert
 dafür unverändert, da full_path jetzt korrekt auf die reale Container-
 Datei zeigt statt leer zu bleiben - GetFileAttributesW liefert dann
 schlicht die Attribute dieser Datei (kein Verzeichnis, aber das ist für
 die reine Pinned/Offline-Auswertung unerheblich).

 Nicht durch Kompilieren/Testen verifiziert.

 Neuer, unabhängiger Nutzer-Fund (18.09.2026): "Schließen des Projekts
 bei SeaDrive-Projekten dauert sehr lange (20 Sek.)". Ursache:
 project_close() -> sond_treeviewfm_set_root(BAUM_FS, NULL) ->
 sond_treeviewfm_seadrive_stop_watcher() setzte das Stop-Flag für den
 Watcher-Thread und wartete dann per g_thread_join() SYNCHRON im GTK-
 Hauptthread auf dessen Ende. Der Watcher-Thread selbst reagiert zwar
 reaktionsschnell auf das Flag (500ms-Timeout in der Warteschleife bzw.
 sofortiger Check in der Rescan-Rekursion, watcher_count_pending_down())
 - das eigentliche Ende des Threads verzögert sich aber durch dessen
 Aufräumcode: CancelIo(hDir) auf das noch ausstehende, per
 ReadDirectoryChangesW gestartete OVERLAPPED-Directory-Watch stößt den
 Abbruch nur AN, das anschließende CloseHandle(hDir) wartet laut
 Windows-I/O-Modell auf den tatsächlichen Abschluss dieser ausstehenden
 I/O - und SeaDrives Cloud-Filtertreiber braucht dafür offenbar
 regelmäßig um die 20 Sekunden (vermutlich ein interner Timeout).

 Fix: sond_treeviewfm_seadrive_stop_watcher_async() (neu,
 sond_treeviewfm.c/.h) - setzt das Stop-Flag, merkt sich das GThread-
 Handle, setzt stvfm_priv->seadrive_watcher_thread schon jetzt (nicht
 erst nach dem Join) auf NULL und delegiert das eigentliche
 g_thread_join() an einen neu gestarteten, kurzlebigen "Reaper"-Thread
 (per g_thread_unref() sofort "fire-and-forget" freigegeben - das
 dokumentierte GLib-Muster dafür). Der GTK-Hauptthread kehrt damit
 sofort zurück, das eigentliche CancelIo/CloseHandle-Warten passiert im
 Hintergrund, unbemerkt vom Nutzer. Sicherheitsüberlegung: der Watcher-
 Thread fasst nach dem Setzen des Stop-Flags keine stvfm-Daten mehr an
 (nur noch eigene lokale Handles/Kopien), das Auslagern ist also
 unproblematisch - ABER NUR, solange stvfm selbst danach am Leben
 bleibt. Deshalb zwei Varianten: sond_treeviewfm_seadrive_stop_watcher()
 (unverändert blockierend) bleibt für sond_treeviewfm_finalize() bestehen
 (dort wird direkt im Anschluss der private Instanz-Speicher freigegeben
 - ein im Hintergrund noch laufender Watcher-Thread wäre dort ein
 Use-after-free-Risiko), die neue nicht-blockierende Variante wird nur
 in sond_treeviewfm_set_root() verwendet (BAUM_FS-Widget bleibt über die
 Projekt-Lebensdauer hinaus bestehen).

 Nicht durch Kompilieren/Testen verifiziert.

 Korrektur (18.09.2026), Nutzer-Test: "Aber es ändert nichts." - Fix
 half nicht. Auf Bitte per Eclipse/gdb "Suspend" während des Hängers
 einen Call-Stack geholt: der zeigte den Hänger NICHT im Watcher-Thread-
 Join, sondern weiterhin in sond_treeviewfm_set_root() selbst, konkret in
 g_hash_table_remove_all(stvfm_priv->seadrive_file_badges) (Zeile 3904
 vor diesem Fix). Der Watcher-Thread-Fix oben war also unnötig (schadet
 aber nicht) - die eigentliche Ursache war die ganze Zeit diese Zeile.
 Erklärung: bei einem großen SeaDrive-Projekt hat praktisch jede noch
 nicht heruntergeladene (OFFLINE-)Datei einen eigenen Eintrag in
 seadrive_file_badges - bei vielen Zehn- oder Hunderttausend Dateien im
 Projekt entsprechend viele Einträge, die remove_all() einzeln (mit je
 einem g_free() auf den Key-String) synchron im GTK-Hauptthread
 abarbeiten musste. Betraf im Prinzip auch die drei anderen SeaDrive-
 Hashtables (seadrive_not_in_sync, seadrive_pending_down_paths,
 seadrive_dir_counts), dort aber typischerweise mit deutlich weniger
 Einträgen.

 Fix: neuer Typ SeadriveOldTables + statische Funktion
 seadrive_old_tables_reap() (beide direkt vor sond_treeviewfm_set_root()
 in sond_treeviewfm.c). In set_root() werden die vier Hashtable-Zeiger
 jetzt nur noch aus stvfm_priv "gestohlen" (Felder sofort auf NULL
 gesetzt statt sie zu leeren) und die eigentliche Zerstörung
 (g_hash_table_destroy() auf alle vier) an einen kurzlebigen Hintergrund-
 Thread abgegeben (g_thread_new()+g_thread_unref(), "fire and forget",
 analog zum Watcher-Reaper). Die vier Tabellen enthalten ausschließlich
 Strings/Zahlen ohne Rückverweis auf stvfm - ihre Zerstörung ist deshalb
 unabhängig vom weiteren Leben des stvfm-Objekts sicher, die Watcher-
 Einschränkung "nur außerhalb finalize()" gilt hier NICHT.

 Nicht durch Kompilieren/Testen verifiziert - diesmal bitte per Call-
 Stack (oder einfach durch Nachmessen der Schließzeit) verifizieren, ob
 damit tatsächlich behoben, BEVOR weitere Stellen vermutet werden.

 Folge-Fund (18.09.2026), Nutzer-Test des Close-Fixes: Schließen selbst
 jetzt schnell ("Sehr gut!!"), ABER: "der Zeitverlust ist der gleiche,
 bis Öffnen das Verzeichnis anzeigt." Per Eclipse/gdb-Suspend lokalisiert
 - diesmal lag der Hänger NICHT in sond/zond-eigenem Code, sondern in
 my_dialog_run() -> choose_file() -> filename_oeffnen() -> project_load()
 - also im GTK-Dateiauswahldialog (GtkFileChooserDialog) selbst, der
 aufgeht, BEVOR der Nutzer überhaupt eine neue Projektdatei ausgewählt
 hat. Ursache: choose_file() rief bisher immer ohne expliziten
 Startpfad auf (path==NULL) und fiel intern auf g_get_current_dir()
 zurück - das Arbeitsverzeichnis des Prozesses war zu diesem Zeitpunkt
 aber noch auf das AKTUELL (bzw. gerade eben) geöffnete SeaDrive-
 Projektverzeichnis gesetzt (g_chdir() in sond_treeviewfm_set_root(),
 root!=NULL-Zweig - project_close() setzt das nirgends zurück, und
 project_load() ruft filename_oeffnen() ohnehin VOR project_open()/
 project_close() auf). GtkFileChooserDialog musste also erst das
 komplette, potentiell riesige alte Fallakten-Verzeichnis einlesen, um
 seine eigene Dateiliste zu füllen - derselbe "Cloud-Filtertreiber pro
 Datei langsam"-Effekt wie beim SeaDrive-Scan, diesmal aber in GTKs
 eigenem Dialog statt in unserem Code und deshalb dort nicht direkt
 beschleunigbar.

 Fix: filename_oeffnen() (misc.c/.h) um einen neuen Parameter
 start_path erweitert, an choose_file() durchgereicht. Die beiden
 anderen, unkritischen Aufrufer (seiten.c: Datei zum Einfügen/Merge
 auswählen; stand_alone.c: PDF im Stand-alone-Viewer öffnen) übergeben
 weiterhin NULL (unverändertes Verhalten). project_load() (project.c)
 ermittelt jetzt vor dem Dialogaufruf per g_path_get_dirname() das
 ELTERNverzeichnis von zond->project_dir (des noch geöffneten, alten
 Projekts - project_close() läuft ja erst später in project_open()) und
 übergibt das als Startordner - typischerweise nur eine Handvoll
 Fallakten-Ordner statt deren komplettem, riesigem Inhalt. Fehlt
 project_dir (erstes Öffnen einer frischen Session), bleibt es beim
 alten Verhalten (NULL -> g_get_current_dir()).

 Nicht durch Kompilieren/Testen verifiziert.

 Refactoring (18.09.2026, Nutzer-Fund): "sond_treeviewfm.c und
 sond_treeviewfm_seadrive.c sind riesen Trümmer! Kann man das besser
 aufteilen? Und in _treeviewfm.c sind auch Funktionen, die in
 sond_treeviewfm_seadrive gehören; das Modul sollte auch eher
 sond_seadrive.c heißen." Auf Nachfrage (Umfang: nur umbenennen? auch
 verschieben? zusätzlich sond_treeviewfm.c selbst weiter aufteilen?)
 Nutzer-Entscheidung: umbenennen + SeaDrive-Code verschieben, aber
 sond_treeviewfm.c selbst NICHT weiter aufsplitten.

 Ursache dafür, dass die SeaDrive-Backend-Logik (Ground-Truth-
 Hashtables für Badges/Coverage, Watcher-Start/Stop) bisher zwangsläufig
 in sond_treeviewfm.c stehen musste, obwohl sie inhaltlich zu SeaDrive
 gehört: G_DEFINE_TYPE_WITH_PRIVATE() erzeugt nur einen STATISCHEN,
 ausschließlich in der eigenen Übersetzungseinheit sichtbaren Accessor
 (sond_treeviewfm_get_instance_private()/sond_tvfm_item_get_instance_
 private()) auf die private Instanzstruktur - Code in einer anderen .c-
 Datei kann diese Structs also nicht direkt anfassen.

 Lösung: neuer "Freund"-Header sond_treeviewfm_private.h macht beide
 privaten Structs (SondTreeviewFMPrivate, SondTVFMItemPrivate) sowie je
 einen normalen (nicht-statischen) Freund-Accessor bekannt
 (sond_treeviewfm_get_priv(), sond_tvfm_item_get_priv()), implementiert
 in sond_treeviewfm.c direkt hinter den beiden G_DEFINE_TYPE_WITH_
 PRIVATE()-Aufrufen als 1:1-Durchreicher an den jeweiligen Makro-
 Accessor. Der Header ist bewusst NICHT Teil der öffentlichen API (kein
 Include in sond_treeviewfm.h) - nur sond_treeviewfm.c und sond_
 seadrive.c binden ihn ein.

 Damit ließen sich ca. 570 Zeilen (17 Funktionen/Structs: die Ordner-/
 Datei-Badge-Verwaltung, die SeaDrive-Statuszähler, Item-Hydrierung/-
 Dehydrierung im Baum, sowie Watcher-Start/-Stop/-Stop-Async mitsamt dem
 ausführlichen Task-#148-Kommentar zum Reaper-Pattern) unverändert aus
 sond_treeviewfm.c nach sond_seadrive.c (vormals sond_treeviewfm_
 seadrive.c) verschieben - einzige nötige Textänderung darin: sond_
 treeviewfm_get_instance_private( -> sond_treeviewfm_get_priv( und
 sond_tvfm_item_get_instance_private( -> sond_tvfm_item_get_priv(
 (jeweils rein mechanisch, keine Verhaltensänderung). Die zugehörigen
 Deklarationen wurden aus dem #ifdef _WIN32-Block von sond_treeviewfm.h
 nach sond_seadrive.h verschoben (inkl. des SondSeadriveDirCounts-Typs);
 sond_treeviewfm.h braucht dadurch auch sond_icon_util.h nicht mehr
 direkt einzubinden.

 Zusätzlich neue Funktion sond_seadrive_reset_ground_truth(SondTreeviewFM*)
 in sond_seadrive.c: fasst den früheren SeadriveOldTables/seadrive_old_
 tables_reap()-Mechanismus (Task #148: die vier Ground-Truth-Hashtables
 beim Projekt-Wechsel/-Schließen "stehlen" statt synchron zu leeren) samt
 des dazugehörigen Inline-Blocks in sond_treeviewfm_set_root() zu einer
 sauber benannten, öffentlichen Funktion zusammen - sond_treeviewfm_
 set_root() ruft jetzt nur noch diese eine Funktion auf. Verhalten
 unverändert, nur die Zuständigkeitsgrenze zwischen den beiden Modulen
 verbessert.

 Umbenennung: sond_treeviewfm_seadrive.c/.h -> sond_seadrive.c/.h
 (Makefile Zeile 41 sowie alle 6 einbindenden Dateien - ToDo.c selbst,
 project.c, zond_treeview.c, headerbar.c, app_window.c - angepasst;
 Kommentar-Erwähnungen des alten Dateinamens an anderer Stelle wie
 gehabt belassen, soweit sie sich auf den Stand zum jeweiligen
 Zeitpunkt beziehen).

 sond_treeviewfm.c dadurch von 4639 auf ca. 3940 Zeilen geschrumpft,
 sond_seadrive.c (vormals sond_treeviewfm_seadrive.c, 2101 Zeilen) auf
 ca. 2790 Zeilen gewachsen - reine Verschiebung, keine
 Funktionalitätsänderung.

 Nicht durch Kompilieren/Testen verifiziert.

 Refactoring (19.09.2026, Nutzer-Fund im Anschluss an obiges SeaDrive-
 Refactoring): "Und wie wäre es, wenn man stvfm_item aus sond_treeviewfm
 herausnimmt?" SondTVFMItem (das GObject-Derivat für EINEN Knoten im
 Baum: Datei/Verzeichnis/Section, Erzeugen/Kinder laden/Umbenennen/
 Kopieren/Verschieben/Löschen) steckte komplett in sond_treeviewfm.h/.c,
 obwohl es inhaltlich ein eigenständiges Modell ist.

 Auf Nachfrage (Umfang: nur verschieben, oder zusätzlich die zahlreichen
 direkten SondTVFMItemPrivate-Feldzugriffe in sond_treeviewfm.c auf
 saubere Getter/Setter umstellen?) zunächst Nutzer-Entscheidung
 "verschieben + Zugriffe aufräumen". Beim Durcharbeiten der kompletten
 3939 Zeilen von sond_treeviewfm.c zeigte sich aber: die Kopier-/
 Verschiebe-/Einfüge-/Lösch-Kaskaden (mehrere Items gleichzeitig, über
 Dateisystem- und ZIP-Grenzen hinweg) pfriemeln an gut einem Dutzend
 Stellen direkt in SondTVFMItemPrivate herum - eine saubere Getter-/
 Setter-Kapselung hätte dort mehrere neue, ungetestete Setter
 (set_item_type, set_has_children, set_path_or_section,
 set_display_name) in genau diesem riskanten, dateisystemverändernden
 Code nötig gemacht, ohne dass ich das kompilieren/testen kann. Dieser
 konkrete Befund wurde dem Nutzer vorgelegt; Entscheidung daraufhin:
 "Nur verschieben, kein Cleanup" - reine mechanische Verschiebung wie
 beim SeaDrive-Refactoring oben, keine Verhaltensänderung, keine neuen
 Setter/Getter außer dem für den Dateisplit technisch Nötigen.

 Umsetzung: neue Dateien sond_tvfm_item.h/.c. Öffentliche API (Typ-
 Deklaration, Getter, sond_tvfm_item_create(), SondTVFMProgress,
 sond_tvfm_item_load_children()) jetzt in sond_tvfm_item.h; sond_
 treeviewfm.h inkludiert diesen Header (statt der Typ-Deklaration
 selbst), transparent für alle bisherigen Includer. Das SondTVFMItemPrivate-
 Struct sowie der Freund-Accessor sond_tvfm_item_get_priv() bleiben wie
 gehabt in sond_treeviewfm_private.h (Definition jetzt in sond_tvfm_
 item.c statt sond_treeviewfm.c) - dadurch behält sond_treeviewfm.c
 exakt wie vorher direkten Zugriff auf die Item-Privatfelder, nur der
 Accessor-Funktionsname hat sich geändert (sond_tvfm_item_get_instance_
 private( -> sond_tvfm_item_get_priv(, rein mechanisch).

 Sechs Funktionen, die vor dem Refactoring file-static in sond_
 treeviewfm.c waren (bzw. "delete_item" hießen), werden sowohl von der
 jetzt in sond_tvfm_item.c lebenden Item-Logik selbst als auch von im
 Baum-Code verbliebenen Aufrufern (Rename/Kontextmenü-Löschen/
 Umbenennen-Handler/Fileparts-Sammlung) gebraucht: sond_tvfm_item_get_
 basename(), _rename(), _copy(), _move(), _delete() (umbenannt von
 delete_item(), da nicht mehr file-static), _get_fileparts(). Diese
 sechs wurden bewusst NICHT in die öffentliche sond_tvfm_item.h
 aufgenommen, sondern als "modul-interne Freund-API" in sond_
 treeviewfm_private.h ergänzt - das entspricht genau der vorherigen
 Sichtbarkeit (file-static), nur jetzt auf zwei Übersetzungseinheiten
 verteilt statt einer.

 sond_treeviewfm.c dadurch von 3939 auf 2728 Zeilen geschrumpft, sond_
 tvfm_item.c (neu) ca. 750 Zeilen. Verschiebung per sed anhand
 exakter Zeilenbereiche (statt Retippen großer Blöcke), anschließend
 durchgehend verifiziert (Klammernbilanz, #ifdef/#else/#endif-Paarung,
 keine verwaisten Referenzen auf jetzt in sond_tvfm_item.c lebende
 Funktionen, alle Aufrufstellen der sechs neu exportierten Funktionen
 einzeln nachgeprüft) - reine Verschiebung, keine
 Funktionalitätsänderung.

 Nicht durch Kompilieren/Testen verifiziert.

 Bug (19.09.2026, Nutzer-Fund nach obigem stvfm_item-Refactoring, GTK-
 Warning): "Failed to set text '<small><tt>.../AdV Arrestanordnung -
 A&F GmbH.pdf</tt></small>' from markup due to error parsing markup:
 ... Sie haben ein &-Zeichen benutzt, ohne eine Entität beginnen zu
 wollen". Ursache: zond_treeview_query_tooltip() (zond_treeview.c) baut
 den Tooltip-Text per g_strdup_printf("<small><tt>%s</tt></small>", ...)
 aus dem rohen Dateipfad (file_part) bzw. Anbindungstext (anb_string)
 zusammen und übergibt das direkt an gtk_tooltip_set_markup() - ohne
 Escaping wirft Pango bei Sonderzeichen wie '&', '<', '>' im
 Dateinamen einen Parse-Fehler und der Tooltip bleibt leer. Auch die
 Fehlermeldung (error->message) im selben Zweig war ungeescaped. Analoges
 Muster in zond_treeview_render_node_text() gefunden: das Label eines
 Link-Knotens (zond_tree_store_is_link()) wird ebenso ungeescaped in
 "<i>%s</i>" eingesetzt - Link-Knoten können vom Nutzer umbenannt werden
 (zond_treeview_text_edited()), also ebenfalls potentiell betroffen.

 Fix: an allen vier Stellen (file_part, anb_string, error->message in
 zond_treeview_query_tooltip(); label in zond_treeview_render_node_text())
 g_markup_escape_text() vor dem Einsetzen in die Markup-Strings
 eingefügt. Restliche g_strdup_printf(..."<..."...)-Aufrufe im Projekt
 geprüft - keine weiteren Fundstellen mit
 Nutzertext/Dateipfad in Markup.

 Nicht durch Kompilieren/Testen verifiziert.

 Bug (19.09.2026, Nutzer-Review von sond_treeviewfm_open(), Zeile
 1428f.): die SeaDrive-Hydrierungsprüfung vor dem Öffnen einer Datei aus
 BAUM_FS lief nur für SOND_TVFM_ITEM_TYPE_LEAF-Knoten, deren sond_file_part
 vom Typ SondFilePartLeaf war UND keinen Parent hatte
 (SOND_IS_FILE_PART_LEAF(...) && !sond_file_part_get_parent(...)).
 Nutzer-Fund: eine LEAF_SECTION (Anbindung/Section, z.B. Seitenbereich
 innerhalb eines PDF oder Mimepart einer E-Mail) behält denselben
 sond_file_part wie der übergeordnete LEAF-Zustand - bei einer PDF-/
 GMessage-Section bleibt das also ein SondFilePartPDF/-GMessage
 (Container-Typ, s. sond_tvfm_item_create() in sond_tvfm_item.c), womit
 SOND_IS_FILE_PART_LEAF() fehlschlägt und die komplette Prüfung
 übersprungen wird - ein Klick auf eine Seite eines großen, noch nicht
 hydrierten PDF in BAUM_FS ging also direkt in
 document_new_displayed_document() (mupdf-Rohzugriff) statt vorher
 sond_seadrive_ensure_hydrated() anzustoßen.

 Auf Nachfrage, warum überhaupt nach Typ unterschieden wird: der
 !get_parent()-Teil war NICHT redundant, sondern notwendig - bei
 verschachtelten sond_file_parts (ZIP-Eintrag, PDF-Embedded-File,
 GMessage-Mimepart) enthält das path-Feld nur den container-internen
 Bezeichner, nicht den echten projektrelativen Plattenpfad (s.
 sond_file_part_do_create(), das path unverändert vom jeweiligen
 Erzeuger übernimmt) - ein naives g_strconcat(root, "/", path) wäre für
 solche Parts falsch. Der SOND_IS_FILE_PART_LEAF()-Teil dagegen war die
 eigentliche Fehlerquelle: nicht der Typ des sond_file_part entscheidet,
 ob eine Hydrierungsprüfung nötig ist, sondern einzig, ob es einen
 Parent hat oder nicht (DIR-Knoten sind ohnehin schon oben
 ausgeschlossen - alles andere fußt letztlich in genau einer echten
 Datei).

 Fix: SOND_IS_FILE_PART_LEAF()-Unterscheidung entfernt. Stattdessen wird
 immer (für jeden Nicht-DIR-Knoten) vom eigenen sond_file_part über
 sond_file_part_get_parent() zum obersten Vorfahren hochgelaufen -
 dessen path ist garantiert der echte projektrelative Pfad, unabhängig
 davon, ob der ursprüngliche Knoten LEAF oder LEAF_SECTION war und
 unabhängig vom konkreten Container-Typ (Leaf/PDF/ZIP/GMessage). Die
 analoge Stelle in zond_treeview.c (BAUM_INHALT/AUSWERTUNG, Task
 #137/139) war von diesem Bug nicht betroffen - dort wird der
 Dateisystem-Pfad ohnehin per String-Split am "//"-Trenner aus dem
 file_part-String bestimmt, unabhängig vom SondFilePart-Typ.

 Nicht durch Kompilieren/Testen verifiziert.

 Bug (19.09.2026, Nutzer-Review von zond_treeview_open_node(),
 zond_treeview.c): "ensure_ wird auch geprüft, wenn ich im
 BAUM_AUSWERTUNG auf eine COPY klicke, obwohl ja später nochmal in
 multi alles geprüft und angestoßen wird." Der Einzeldatei-
 Hydrierungscheck ganz oben in der Funktion lief bisher IMMER unbedingt
 für iter_target - unabhängig davon, ob der Klick am Ende in den
 Einzel- oder den Auszug-Pfad (mehrere Geschwister-Anbindungen zu einer
 gemeinsamen Ansicht zusammengefasst) mündet. Traf der Klick auf eine
 Anbindung, deren Anzeige-Elternknoten ein Strukturpunkt ist (auszug==
 TRUE, ermittelt erst weiter unten in der Funktion), wurde trotzdem
 schon vorher eine Einzeldatei-Hydrierung für GENAU diese eine Datei
 angestoßen - und bei noch nicht lokaler Datei sofort mit return 0
 abgebrochen, BEVOR überhaupt geprüft wurde, ob gleich sowieso der
 Auszug-Pfad mit sond_seadrive_ensure_hydrated_multi() für ALLE
 Geschwister greift. Der Nutzer musste dadurch ggf. mehrfach klicken
 (einmal pro noch nicht hydriertem Geschwister), statt gleich eine
 gebündelte Abfrage für alle betroffenen Dateien zu bekommen.

 Fix: die Auszug-Entscheidung (ist der Anzeige-Elternknoten - bei
 direktem Strukturpunkt-Treffer: der Zielknoten selbst - ein
 Strukturpunkt?) wird jetzt GANZ OBEN in der Funktion vorgezogen, rein
 per zond_treeview_get_filepart_and_section() (reine DB-Abfrage, kein
 Dateizugriff - dieselbe Funktion, die der bestehende Einzel-Check
 schon nutzte), OHNE die dafür weiter unten verwendete teure
 get_filepart_from_iter()/SondFilePart-Variante zu brauchen. Je nach
 Ergebnis läuft jetzt genau EINMAL entweder der Einzel- oder der Multi-
 Hydrierungscheck. Die beiden vormals an ihrer ursprünglichen Stelle
 (im !sfp-Zweig bzw. im auszug-Unterzweig) redundant gewordenen Aufrufe
 von zond_treeview_auszug_ensure_hydrated() wurden ersatzlos entfernt -
 die dortige spätere Neuberechnung von auszug/iter_parent per
 get_filepart_from_iter() bleibt unverändert bestehen (jetzt gefahrlos,
 weil die betroffenen Dateien zu diesem Zeitpunkt bereits nachweislich
 hydriert sind) und entscheidet weiterhin, ob zond_treeview_open_auszug()
 oder zond_treeview_open_single_view() aufgerufen wird.

 Nicht durch Kompilieren/Testen verifiziert.

 Refactoring (19.09.2026, Nutzer-Fund): "sond_seadrive_ensure_hydrated
 und _multi enthalten viel doppelten Code. Kann man _ensure_hydrated
 nicht als _multi mit arr->len==1 verstehen?" Zutreffend: der komplette
 Einzeldatei-Fortschrittsdialog (HydrateProgressUi-Struct,
 hydrate_progress_update(), hydrate_progress_tick(),
 cb_hydrate_progress_dialog_destroy(),
 cb_hydrate_progress_abbrechen_clicked(),
 sond_seadrive_show_hydrate_progress_dialog(), ca. 140 Zeilen) war eine
 strukturelle 1:1-Dopplung der Multi-Variante (HydrateProgressEntryMulti/
 HydrateProgressUiMulti und Umfeld) - nur für genau einen statt beliebig
 viele Pfade. sond_seadrive_show_hydrate_progress_dialog() wurde
 außerdem nirgends sonst im Projekt direkt aufgerufen (nur von
 sond_seadrive_ensure_hydrated() selbst, geprüft per grep).

 Fix: kompletter Einzeldatei-Dialog-Code entfernt.
 sond_seadrive_ensure_hydrated() ist jetzt nur noch ein dünner Wrapper,
 der full_path in einen einelementigen GPtrArray packt und an
 sond_seadrive_ensure_hydrated_multi() delegiert. Header (sond_
 seadrive.h) angepasst: Deklaration + Doc-Kommentar von
 sond_seadrive_show_hydrate_progress_dialog() sowie dessen Linux-Stub
 entfernt, Doc-Kommentar von sond_seadrive_ensure_hydrated() aktualisiert.
 SeaDrivePlaceholderStandardInfo/CF_PLACEHOLDER_INFO_STANDARD (von der
 Multi-Variante weiterhin gebraucht) unverändert stehen gelassen.

 Einzige sichtbare Verhaltensänderung: der Dialog bei einem erneuten
 Doppelklick auf eine einzelne, noch hydrierende Datei zeigt jetzt
 denselben Rahmen wie der Auszug-Fall (Titel "Download läuft", darunter
 EINE Zeile mit Dateiname + Fortschrittsbalken statt des Satzes
 "Download läuft bereits: <Name>") - inhaltlich identisch, nur ohne den
 einleitenden Satz.

 Nicht durch Kompilieren/Testen verifiziert.

 Bug (19.09.2026, Nutzer-Fund: "wenn ich das Projekt schließe und neu
 lade, spinnen die links" + Test-ZND-Datei): zond_tree_store_load_node()
 (zond_tree_store.c) erzeugte beim Nachladen (Aufklappen) eines Link-
 Knotens unter bestimmten Umständen einen Link auf SICH SELBST, was
 beim Aufklappen zu endloser Selbst-Verschachtelung führte ("a.pdf" ->
 "a.pdf" -> "a.pdf" -> ... ohne Ende). Konkret betroffen: ein Link A
 (z.B. ein head-link auf eine Anbindung/Datei in BAUM_INHALT), dessen
 Ziel Kinder hat, war selbst noch nie aufgeklappt worden und trug daher
 nur seinen initialen Dummy-Platzhalter als Kind. Wird nun - an anderer
 Stelle im Baum - ein Knoten gespiegelt, der SEINERSEITS ein Kind hat,
 das Link A ist (Fall "Kind ist selbst link-head", Zeile ~916), entsteht
 dabei ein weiterer Link B mit target=A (bewusst so: Link-Ketten im
 Quell-Teilbaum werden strukturgleich nachgebildet statt aufgelöst, s.
 Kommentar bei zond_tree_store_insert_link_at_pos()). Klappt man B auf,
 ruft zond_tree_store_load_link() zond_tree_store_load_node() mit
 node_parent_target=A auf - und A hat ja nur den einen Dummy als Kind.
 Der beim Dummy-Fall (Zeile ~926) vorgesehene Code
 (zond_tree_store_insert_link_at_pos(node_parent_target, ...)) fügte
 dabei fälschlich einen Link auf A SELBST ein (statt, wie der
 dortige - bereits vorher vorhandene, aber nicht umgesetzte - Kommentar
 es correct beschreibt, auf das ZIEL von A). Der neu entstandene
 Knoten zeigt dadurch wieder auf A, hat wieder nur dessen (weiterhin
 ungeladenen) Dummy als Kind - jedes weitere Aufklappen wiederholt exakt
 denselben Fall: unendliche Verschachtelung, ohne dass je die echten
 Kinder von A's Ziel (im Testfall: die Anbindungen S.1-S.1/S.2-S.2 von
 a.pdf) erreicht werden.

 Per SQLite-Analyse der vom Nutzer übergebenen Test.ZND (Tabelle
 knoten) rekonstruiert: Knoten 22 (BAUM_AUSWERTUNG_LINK, link=3=a.pdf-
 file_part) liegt als Kind unter Knoten 19 (BAUM_AUSWERTUNG_COPY von
 a.pdf). An anderer Stelle (Knoten 12/24 bzw. 23) wird Knoten 13 (der
 Strukturpunkt, unter dem 19 liegt) gespiegelt - dabei entsteht der
 oben beschriebene Link B auf Knoten 22 (=A). Aufklappen von B erzeugte
 die endlose a.pdf-Verschachtelung aus dem Screenshot.

 Fix: im Dummy-Fall wird node_parent_target jetzt vor dem Einfügen per
 while-Schleife über ->target vollständig aufgelöst (wie an anderen
 Stellen im File, z.B. zond_tree_store_insert()/_insert_link()), und
 erst das Ergebnis (im Testfall: das echte file_part a.pdf, Knoten 3)
 als Link-Ziel verwendet - genau wie es der schon vorher vorhandene
 Kommentar an der Stelle ("dann Kind von Ziel von Ziel als Link
 einfügen") beschrieb, aber der Code bisher nicht tat.

 Nicht durch Kompilieren/Testen verifiziert (Testdatei liegt vor, aber
 kein Build/Testlauf durch mich möglich - bitte mit der Test.ZND
 gegenprüfen).

 Feature (19./20.09.2026, Nutzer-Vorgabe, "Link-Klettern"): in
 BAUM_AUSWERTUNG können mehrere Dateien/Dateiteile untereinander
 angeordnet sein, so daß sie beim Öffnen als ein Gesamt-PDF angezeigt
 werden (Auszug, s. zond_treeview_open_auszug()). Eine davon kann dabei
 ein Link sein, der seinerseits die Anbindungen/Unterabschnitte seines
 Ziels in BAUM_INHALT spiegelt. Bisher bestimmte beim Klick auf einen
 solchen gespiegelten Unterabschnitt einfach dessen unmittelbarer
 Anzeige-Elternknoten iter_parent für den Auszug - das kann aber selbst
 wieder nur ein weiterer gespiegelter Zwischen-Link sein, so daß der
 Auszug an der falschen (zu tief verschachtelten) Stelle ansetzte.

 Nutzer-Vorgabe: die Klickposition bestimmt weiterhin, an welcher
 Stelle geöffnet wird, aber WAS geöffnet wird (iter_parent), bestimmt
 der oberste Punkt des Links auf diese Datei. Präzisiert: es macht
 einen Unterschied, ob Link oder Copy - nur bei einem Link auf eine
 Anbindung in BAUM_INHALT wird dessen Kette hochgeklettert, bis der
 Link aufhört (nächster Anzeige-Elternknoten ist selbst kein Link mehr)
 oder die Anbindung in BAUM_INHALT ihre oberste Ebene (reine Datei,
 kein section mehr) erreicht hat. Copy-Knoten nehmen daran nicht teil.

 Umsetzung: neue Hilfsfunktion zond_treeview_climb_link_chain()
 (zond_treeview.c, vor zond_treeview_open_node()) - rein DB-basiert
 (zond_treeview_get_filepart_and_section(), gtk_tree_model_iter_parent(),
 zond_tree_store_is_link()), kein Dateizugriff. Eingebaut an zwei
 Stellen in zond_treeview_open_node(): (1) in der günstigen
 Hydrierungs-Vorprüfung (Task #154) - identische Klettersequenz, damit
 die Vorhersage Einzel- vs. Multi-Hydrierung mit dem tatsächlich
 später ermittelten iter_parent übereinstimmt; (2) in der eigentlichen
 Auszug/Einzelansicht-Entscheidung ("Ziel ist Anbindung", Strg nicht
 gedrückt) - hat der Anzeige-Elternknoten selbst Inhalt (sfp_parent)
 UND ist er ein Link, wird zuerst geklettert und dann der oberste
 erreichte Link so behandelt, als sei ER direkt angeklickt worden
 (dessen eigener Anzeige-Elternknoten wird iter_parent, sofern der
 keinen eigenen sfp trägt).

 Nicht durch Kompilieren/Testen verifiziert.

 Bug (20.09.2026, Nutzer-Fund): Struktur "Strukturpunkt A -> Kind ist
 head-link B auf einen ANDEREN Strukturpunkt C -> dessen Kind ist Copy D
 einer Anbindung in BAUM_INHALT -> dessen Kind ist Link E auf dieselbe
 Anbindung". Klick auf die gespiegelte Kopie von E (unter B/mirror-of-D)
 tat gar nichts; Klick auf E direkt (unter C/D) öffnete korrekt.

 Ursache: zond_treeview_climb_link_chain() (s.o.) prüfte pro Ebene nur
 "hat die AKTUELLE Anbindung schon die oberste Ebene (section==NULL)
 erreicht", nicht aber, ob der NÄCHSTE Vorfahre überhaupt noch eine
 Datei referenziert. Der Vorfahre von mirror-of-D ist mirror-of-B - ein
 Link, aber auf einen STRUKTURPUNKT (C), nicht auf eine Datei - dessen
 file_part ist ebenfalls NULL, was fälschlich wie "oberste Ebene
 erreicht" behandelt wurde. Die Kette kletterte deshalb bis zu B hoch;
 iter_parent wurde anschließend Strukturpunkt A, dessen einziges Kind
 (B) selbst keine Datei ist und in zond_treeview_open_auszug() beim
 Aufbau übersprungen wird (if (!sfp) continue;) - es blieben keine
 Dokumente übrig, dd blieb NULL, der Klick blieb sichtbar wirkungslos.

 Fix: vor jedem Klettern zu einem Vorfahren wird jetzt zusätzlich
 dessen eigenes file_part geprüft - ist es NULL (Vorfahre referenziert
 gar keine Datei, z.B. Link auf einen anderen Strukturpunkt statt auf
 eine Anbindung), wird NICHT mehr dorthin geklettert, die Kette bleibt
 auf der letzten echten Datei-Ebene stehen.

 Bekannte, noch offene Nebenfrage (nicht Teil dieses Fixes):
 zond_treeview_open_auszug() vergleicht die Klickposition (iter_pos)
 nur gegen die DIREKTEN Kinder von iter_parent (Zeiger-Gleichheit). Ist
 iter_parent jetzt (durch das Klettern) weiter oben angesiedelt als
 vorher, kann iter_pos mehrere Ebenen tiefer liegen (wie E unter
 mirror-of-D unter B) und wird beim Positionsabgleich nie gefunden -
 das Dokument öffnet dann zwar (Hauptbug behoben), aber ggf. an der
 falschen Stelle statt exakt an der angeklickten Anbindung. Bereits vor
 dem Link-Klettern-Feature so vorhanden (z.B. beim ursprünglichen
 Punkt4/Punkt5-Beispiel), durch das Klettern aber häufiger relevant.
 Müsste ggf. auf Abstammungs-Prüfung (gtk_tree_path_is_ancestor()) statt
 exakter Gleichheit umgestellt werden - noch nicht umgesetzt, mit dem
 Nutzer noch nicht abschließend besprochen.

 Nicht durch Kompilieren/Testen verifiziert.

 Bug (20.09.2026, Nutzer-Fund, Folgefund zum vorigen Punkt): "Seiten-
 anzeige (1/x) bleibt zunächst leer, Seite wird auch nicht angezeigt,
 erst nach Scrollen." Genau die oben als offen vermerkte Nebenfrage
 schlug jetzt konkret zu: in zond_treeview_open_auszug() verglich der
 Positionsabgleich iter_pos (Klickposition) per exakter Zeiger-
 Gleichheit (iter_pos->user_data == iter_tmp.user_data) NUR gegen die
 DIREKTEN Kinder von iter_parent. Nach dem Link-Klettern kann iter_pos
 aber mehrere Ebenen tiefer liegen als iter_tmp (z.B. ein gespiegelter
 Unterabschnitt unter einem Link-Kind) - die Gleichheit traf nie zu,
 found blieb FALSE, und die Schleife akkumulierte pdf_pos->seite immer
 weiter (als läge iter_pos hinter dem gesamten Auszug). Der Viewer
 versuchte danach, auf eine Seite HINTER dem ganzen Dokument zu
 scrollen - zeigte deshalb zunächst nichts an, bis manuelles Scrollen
 den sichtbaren Bereich neu berechnete und wieder eine gültige Seite
 fand.

 Fix: Positionsabgleich nutzt jetzt GtkTreePath-Vergleich
 (gtk_tree_path_compare()/gtk_tree_path_is_ancestor()) statt exakter
 Gleichheit - iter_tmp gilt als Treffer, wenn iter_pos IHM ENTSPRICHT
 ODER IRGENDWO IN SEINEM TEILBAUM liegt. Die Position springt dann an
 den (akkumulierten) Anfang von iter_tmp - im konkreten Fall (Link auf
 dieselbe Anbindung wie das gefundene Kind) korrekt, weil beide
 dieselbe Anbindung referenzieren; bei einer abweichenden, genaueren
 Unterposition innerhalb eines gefundenen Kindes würde weiterhin nur
 dessen Anfang angesprungen, nicht die exakte Unterposition - dafür
 müsste iter_pos' eigene Anbindung zusätzlich ausgewertet werden
 (bisher nicht nötig, da nicht aufgetreten).

 Nicht durch Kompilieren/Testen verifiziert.

 Überarbeitung (20.09.2026, Nutzer-Vorgabe): der bisherige Kletter-Test
 in zond_treeview_climb_link_chain() ("section==NULL" = oberste Ebene
 erreicht) war zu PDF-Anbindungs-spezifisch und wurde nach dem Bugfund
 mit dem Link-auf-Strukturpunkt-Fall (s.o.) durch einen zusätzlichen
 Sonderfall geflickt. Nutzer gab die eigentliche, allgemeingültige
 Bedingung präzise vor: "Wenn iter_click (1) ein Link ist, dessen (2)
 Ziel in BAUM_INHALT liegt und (3) als node_id vom type 5 [FILE_PART]
 ist, (4) das aber nicht unmittelbar als type 2 [BAUM_INHALT_FILE]
 angebunden ist, dann: eine Etage höher und gleiche Prüfung."

 Fix: zond_treeview_climb_link_chain() komplett auf diese vier
 Bedingungen umgestellt, pro Ebene neu geprüft (keine Sonderfall-Flicken
 mehr nötig). Bedingungen 2+4 werden über die bereits vorhandene
 zond_dbase_find_baum_inhalt_file() ermittelt (dieselbe Funktion, die
 auch zond_treeview_get_root() nutzt): kein baum_inhalt_file gefunden ->
 Bedingung 2 scheitert (Ziel liegt nicht/nicht mehr in BAUM_INHALT, z.B.
 Link auf Strukturpunkt); gefundenes id_file_part entspricht bereits der
 aktuellen node_id -> Bedingung 4 scheitert (schon unmittelbar
 angebunden, oberste Ebene). Vorteil gegenüber dem alten section-Test:
 funktioniert jetzt auch korrekt für nicht-PDF-Anbindungs-Container
 (ZIP-Eintrag, eingebettete PDF, GMessage-Mimepart), die keine "section"
 im Sinne einer Seiten-Anbindung haben, aber trotzdem nicht unmittelbar
 angebundene, verschachtelte file_part-Knoten sind.

 Nicht durch Kompilieren/Testen verifiziert.

 Komplett-Neufassung (20.09.2026, Nutzer-Vorgabe): auch die um die vier
 Bedingungen erweiterte zond_treeview_climb_link_chain() (s.o.) deckte
 nicht alle Fälle ab - insbesondere den direkten Klick auf einen
 Strukturpunkt im Auswertungsverzeichnis (kein Link, aber trotzdem
 Ausgangspunkt für einen Auszug) sowie den Klick auf eine BAUM_AUSWERTUNG_
 COPY (eigene, DB-Ebenen-Referenz auf eine Anbindung, kein GNode-Link).
 Beide Fälle wurden bisher durch getrennten, teils dupliziertern Code vor
 bzw. neben dem Klettern behandelt (Zweige "!sfp" und "Ziel ist
 Anbindung" in zond_treeview_open_node()). Nutzer gab nach mehreren
 Rückfragen und Gegenbeispielen (unter anderem node16: einfacher Link auf
 eine ganze PDF-Datei, ohne Klettern; Punkt4/Punkt5: zweistufiger Fall,
 Link auf Section verschachtelt in Link auf ganze Datei; sowie ein
 vierstufiger Fall Strukturpunkt A -> Link B auf Strukturpunkt C -> Copy D
 einer Anbindung -> Link E auf dieselbe Anbindung wie D) die
 abschließende, einheitliche Formulierung als Pseudocode vor:

   do {
       iter_target = target(iter_click);
       if (iter_target -> Strukturpunkt):
           iter_parent = iter_click; break;
       else if (iter_target -> COPY):
           iter_parent = parent(iter_click); break;
       else {
           iter_click = parent(iter_click);
           if (iter_target -> PDF/PDF-Section && !node_id(iter_target)
                   ist unmittelbar als file_link angebunden):
               continue;
           else
               break;
       }
   }
   (iter_parent = iter_click, sofern die Schleife nicht schon vorher per
   break mit eigenem iter_parent verlassen wurde)

 Wichtig dabei (eigener Irrtum, vom Nutzer korrigiert): trifft iter_target
 direkt beim allerersten Durchlauf (iter_click == ursprünglicher Klick)
 auf einen Strukturpunkt, ist iter_parent NICHT der Strukturpunkt (das
 Ziel/target) selbst, sondern iter_click - also die tatsächlich
 angeklickte Position im Auswertungsverzeichnis. Das ist entscheidend,
 weil iter_parent als Vorfahre der Klickposition INNERHALB des
 Auswertungsverzeichnis-eigenen Baums an zond_treeview_open_auszug()
 übergeben wird; das aufgelöste target liegt dagegen im BAUM_INHALT-Baum
 und wäre dort fehl am Platz.

 Fix: zond_treeview_climb_link_chain() vollständig ersetzt durch
 zond_treeview_determine_iter_parent() (zond_treeview.c, vor
 zond_treeview_open_node()) exakt nach obigem Pseudocode. Implementiert
 als for(;;)-Schleife: pro Durchlauf zond_tree_store_get_iter_target() +
 zond_tree_store_get_node_id() auf iter_click anwenden, dann per
 zond_dbase_get_type_and_link() den DB-Typ des aufgelösten Ziels
 bestimmen. BAUM_STRUKT -> iter_parent = iter_click (s. Korrektur oben).
 BAUM_AUSWERTUNG_COPY -> iter_parent = parent(iter_click) (oder iter_click
 selbst, falls kein Elternknoten vorhanden - Top-Level-Fall). Sonst
 (FILE_PART/VIRT_PDF): bei fehlendem Elternknoten ebenfalls abbrechen mit
 iter_parent = iter_click; bei vorhandenem Elternknoten und Typ FILE_PART
 zusätzlich per zond_dbase_find_baum_inhalt_file() prüfen, ob das Ziel
 (a) überhaupt in BAUM_INHALT verankert ist und (b) nicht schon
 unmittelbar als file_link angebunden ist (Bedingungen 2+4 der vorigen
 Fassung, s.o.) - nur dann eine Ebene hochklettern (iter_click = Eltern)
 und weiterprüfen; andernfalls iter_parent = Elternknoten von iter_click
 (Fall "Ziel ist Anbindung, kein Klettern nötig").

 Beide bisherigen Aufrufstellen in zond_treeview_open_node() auf die neue
 Funktion umgestellt: der vorgezogene #ifdef _WIN32-Check (Task #154)
 sowie die "echte" Ermittlung weiter unten, wo jetzt die vormals
 getrennten Zweige "!sfp" (direkter Strukturpunkt-Treffer) und "Ziel ist
 Anbindung" zu einem einzigen Zweig zusammengefasst sind: sfp wird (falls
 vorhanden) freigegeben, dann zond_treeview_determine_iter_parent() ab dem
 ROHEN Klick-iter aufgerufen (nicht ab iter_target - die Funktion löst
 target() selbst pro Durchlauf auf), und zond_treeview_open_auszug() mit
 diesem iter_parent aufgerufen; iter_pos wird dabei NULL übergeben, wenn
 iter_parent (Zeiger-)identisch mit dem ursprünglichen iter ist (direkter
 Strukturpunkt-Treffer, kein Klettern), sonst iter selbst (Positions-
 Treffer wie bisher). Der Strg-Override (erzwungene Einzelansicht) bleibt
 als eigener, davor geprüfter Zweig erhalten, greift jetzt aber nur noch,
 wenn das Ziel selbst Inhalt trägt (sfp != NULL) - ein Klick auf einen
 reinen Strukturpunkt kennt kein "nur diese eine Anbindung" und läuft
 deshalb auch bei gedrücktem Strg immer über den Auszug-Zweig.

 Nicht durch Kompilieren/Testen verifiziert.

 Nutzer bestätigt (20.09.2026, nach Build/Test): "Scheint zu klappen."
 Anschließender Fund (20.09.2026, "ungünstig, zweimal iter_parent
 ermitteln"): trotz der Zusammenlegung oben wurde iter_parent
 tatsächlich weiterhin ZWEIMAL ermittelt - einmal im vorgezogenen
 #ifdef _WIN32-Hydrierungscheck (Task #154), einmal in der "echten"
 Weiche weiter unten. Beide Aufrufstellen prüften zufällig dieselbe,
 vom rohen Klick-iter abhängige Bedingung, liefen aber unabhängig
 voneinander.

 Fix: iter_parent (zusammen mit dem dafür nötigen file_part_target und
 einem neuen Flag have_iter_parent) wird jetzt GANZ AM ANFANG von
 zond_treeview_open_node() genau einmal ermittelt (neue lokale
 Variablen auf Funktionsebene, direkt nach dem Bestimmen von
 baum/baum_click/iter_target, in einem eigenen Block noch vor dem
 #ifdef _WIN32). Der #ifdef _WIN32-Hydrierungscheck verwendet danach
 nur noch have_iter_parent/iter_parent/file_part_target, ohne
 zond_treeview_get_filepart_and_section() oder
 zond_treeview_determine_iter_parent() erneut aufzurufen. file_part_target
 wird direkt nach dem (jetzt außerhalb des #ifdef liegenden, also auch
 auf Nicht-Windows gültigen) #endif einmalig freigegeben und auf NULL
 gesetzt; alle vorzeitigen return-Pfade innerhalb des #ifdef-Blocks
 geben es vorher explizit frei. Die "echte" Ermittlung weiter unten
 verwendet ebenfalls direkt iter_parent/have_iter_parent statt eines
 zweiten Aufrufs - mit einem defensiven (im Normalfall nie greifenden)
 Nachhol-Aufruf für den Fall, dass have_iter_parent wider Erwarten
 FALSE ist (file_part_target und das später ermittelte "echte" sfp
 werten denselben Knoten aus und müssen bzgl. NULL/nicht-NULL
 übereinstimmen, s. dortigen Kommentar).

 Voraussetzung für die Wiederverwendung über den Hydrierungscheck
 hinweg: zwischen den beiden Verwendungsstellen findet keine
 Strukturänderung am BAUM_AUSWERTUNG-GNode-Baum statt - die
 SeaDrive-Hydrierung wirkt nur auf das Dateisystem/BAUM_FS, nie auf den
 Auswertungsverzeichnis-Baum selbst -, iter_parent bleibt also über
 beide Verwendungsstellen hinweg gültig.

 Nicht durch Kompilieren/Testen verifiziert.

 "Pos-Problem" (20.09.2026, Nutzer-Vorgabe, Fortsetzung des obigen
 "Seitenanzeige bleibt zunächst leer"-Fixes in zond_treeview_open_auszug()):
 lag iter_pos (die Klickposition) nicht unmittelbar unter iter_parent,
 sondern verschachtelt unterhalb eines direkten Kindes iter_tmp (Link-
 Klettern, s.o.), sprang die Position bisher pauschal an den Anfang (bzw.
 bei Alt ans Ende) von iter_tmp - unabhängig davon, WO innerhalb von
 iter_tmp iter_pos tatsächlich lag. Nutzer-Vorgabe (Message "iter_click
 ist nicht unmittelbares Kind von iter_parent: Vorfahre unmittelbar
 unter iter_parent ergibt Anbindung; iter_click Position innerhalb
 dieser Anbindung"): die grobe Anbindung von iter_tmp liefert nur den
 Bezugsrahmen (welches PDF-Dokument bzw. welcher Seitenbereich als
 Ganzes angezeigt wird), die tatsächliche Position soll aber die exakte
 Unterposition von iter_pos innerhalb dieses Bezugsrahmens sein.

 Fix, zweiteilig:

 1. document.c/h: neue Funktion document_get_pos_in_anbindung() ergänzt -
 kapselt dieselbe Berechnung wie das bestehende (weiterhin static)
 get_pdf_pos() (Position von anbindung_node innerhalb von anbindung_ges,
 inkl. Bereinigung um gelöschte Seiten), aber OHNE ein neues
 DisplayedDocument anzulegen (kein zpdfd_part wird dauerhaft gehalten -
 zpdfd_part_peek()/zpdfd_part_drop() unmittelbar gepaart). Gebraucht, weil
 für iter_tmp bereits ein DisplayedDocument (dd_tmp) samt anbindung_ges
 existiert und nur noch die Unterposition von iter_pos INNERHALB
 desselben Bezugsrahmens gebraucht wird, ohne das Dokument ein zweites
 Mal "richtig" zu öffnen.

 2. zond_treeview_open_auszug(): die bisherige is_match-Prüfung (iter_tmp
 == iter_pos ODER Vorfahre von iter_pos, per GtkTreePath) wird jetzt in
 is_self (exakte Übereinstimmung) und den allgemeineren is_match
 aufgeteilt. Nur im is_self-Fall bleibt das bisherige Verhalten
 (Anfang/Ende von iter_tmp) unverändert bestehen - das war schon vorher
 exakt richtig, weil iter_tmp dort selbst die geklickte Anbindung ist.
 Im verschachtelten Fall (is_match && !is_self) wird zusätzlich per
 get_filepart_from_iter() auf iter_pos dessen eigene Anbindung
 (anbindung_click) ermittelt und deren Position innerhalb von
 iter_tmps anbindung_ges per document_get_pos_in_anbindung() berechnet;
 das Ergebnis (pos_click) wird statt der bisherigen 0/Ende-Annahme auf
 pdf_pos addiert. Voraussetzung: anbindung_click liegt in derselben
 Datei wie iter_tmp (Normalfall bei verschachtelten Links/Copies
 unterhalb von iter_tmp - sie setzen denselben gespiegelten Teilbaum
 EINER Datei fort) - zur Absicherung per sond_file_part_get_path()-
 Stringvergleich geprüft (g_strcmp0); weicht die Datei ab oder liefert
 get_filepart_from_iter() für iter_pos gar kein sfp (Flag "precise"
 bleibt FALSE), greift ersatzweise unverändert die alte, grobe
 Anfang/Ende-von-iter_tmp-Lösung - kein Rückschritt gegenüber vorher,
 nur eine Verbesserung für den Normalfall.

 Nebenbei aufgeräumt: g_object_unref(sfp) für iter_tmps eigenes
 SondFilePart lief bisher SOFORT nach document_new_displayed_document(),
 noch vor der Positionsermittlung - musste ans Ende der Schleifeniteration
 verschoben werden, weil document_get_pos_in_anbindung() weiterhin
 dasselbe sfp (für iter_tmps Datei) braucht. Alle Fehlerpfade dazwischen
 geben sfp jetzt explizit vor jedem return frei statt sich auf den
 (dadurch nicht mehr erreichten) ursprünglichen einzelnen unref-Aufruf zu
 verlassen.

 Nicht durch Kompilieren/Testen verifiziert.

 Nutzer-Fund (20.09.2026, direkt im Anschluss): "document_get_pos_in_
 anbindung ruft doch auch zpdfd_part_peek auf. Das öffnet ein Objekt."
 Berechtigter Einwand - die erste Fassung nahm ein SondFilePartPDF*
 entgegen und peekte sich darüber selbst ein (im Nicht-Trefferfall sogar
 neu angelegtes) ZPDFDPart, obwohl der Aufrufer
 (zond_treeview_open_auszug()) das passende, gerade erst von
 document_new_displayed_document() für iter_tmp gelieferte
 ZondPdfDocument (dd_tmp->zpdfd_part->zond_pdf_document) in diesem Moment
 längst besitzt - der Peek/Drop-Umweg war unnötig und stand im
 Widerspruch zum eigenen Kommentar ("ohne das Dokument ein zweites Mal zu
 öffnen").

 Fix: document_get_pos_in_anbindung() (document.c/h) auf einen reinen,
 nichts öffnenden Wrapper um get_pdf_pos() umgestellt - nimmt jetzt
 direkt ein ZondPdfDocument* entgegen (kein SondFilePartPDF*, kein
 GError** mehr nötig, kann nicht scheitern) und reicht nur noch durch.
 zond_treeview_open_auszug() übergibt dafür
 dd_tmp->zpdfd_part->zond_pdf_document direkt (ZPDFDPart-Struct-Zugriff,
 da zond_pdf_document.h mit der vollen Struct-Definition bereits
 eingebunden ist). was_opened kann dabei NICHT mehr erst an dieser Stelle
 per zond_pdf_document_is_open() ermittelt werden - das Dokument ist zu
 diesem Zeitpunkt ja immer schon offen (durch den eigenen
 document_new_displayed_document()-Aufruf für iter_tmp) -, sondern muss
 vom Aufrufer VORHER festgehalten werden: neue lokale Variable
 was_opened_tmp, per zond_pdf_document_is_open(sfp) unmittelbar VOR dem
 document_new_displayed_document()-Aufruf gesetzt (exakt der Zeitpunkt,
 zu dem document_new_displayed_document() intern denselben Wert für
 seine EIGENE get_pdf_pos()-Berechnung ermittelt).

 document.h: neuer Vorwärts-Typedef "typedef struct _ZondPdfDocument
 ZondPdfDocument;" ergänzt (analog general.h/project.h/zond_init.h/
 zond_treeview.h), da ZondPdfDocument jetzt Teil der öffentlichen
 Signatur ist.

 Nicht durch Kompilieren/Testen verifiziert.

 Nutzer-Fund (20.09.2026, direkt im Anschluss): "Warum dann get_pdf_pos
 nicht public machen?" - berechtigt: document_get_pos_in_anbindung()
 reichte nach obiger Korrektur nur noch 1:1 an get_pdf_pos() durch, ohne
 jeden eigenen Mehrwert - eine Indirektionsebene ohne Zweck.

 Fix: document_get_pos_in_anbindung() ersatzlos entfernt, get_pdf_pos()
 stattdessen selbst nicht mehr static und in document.h deklariert (samt
 dortigem Doc-Kommentar zu zpdfd/was_opened). zond_treeview_open_auszug()
 ruft jetzt direkt get_pdf_pos() auf. Kein Umbenennen, keine zusätzliche
 Funktion - schlicht der bestehende, schon vorher korrekte Rechenkern
 direkt sichtbar gemacht.

 Nicht durch Kompilieren/Testen verifiziert.


 Bug (21.09.2026, Nutzer-Fund): "Wenn aus dem BAUM_FS der Punkt
 'Message', also eine eml, angebunden wird, lautet die Beschriftung im
 BAUM_INHALT ebenfalls 'Message'. Das ist nicht gut. Stattdessen sollte
 der Dateiname genommen werden, wie wenn das DIR angebunden wird. Ebenso
 beim Pagetree in einer PDF."

 Ursache (sond_tvfm_item.c, sond_tvfm_item_create()): für die beiden
 synthetischen Marker-Kindknoten einer PDF ("PageTree", path_or_section
 == "//") bzw. eines .eml (GMESSAGE, "Message", path_or_section ==
 "//message") wurde der display_name hart auf die literalen Strings
 "PageTree"/"Message" gesetzt - weil die generische Basename-Ermittlung
 (sond_tvfm_item_get_basename(), die zuerst versucht wird) für diese
 Marker-Pfade nur Datenmüll liefert: strrchr('/') auf "//" bzw.
 "//message" liefert einen leeren String bzw. "message", nicht den
 echten Dateinamen. Der beim Anbinden als node_text übernommene
 display_name (zond_treeview_leaf_anbinden() -> sond_tvfm_item_
 get_display_name() -> zond_treeview_insert_file_part_in_db() ->
 zond_dbase_create_file_root()) war dadurch für beide Knotentypen
 IMMER derselbe generische Platzhalter, unabhängig von der tatsächlichen
 Datei - anders als beim Anbinden eines DIR oder einer echten Datei, wo
 path_or_section bzw. sond_file_part_get_path() den echten (Datei-)Namen
 liefert.

 Fix: neuer statischer Helfer sond_tvfm_item_basename_of_sfp_dup()
 (sond_tvfm_item.c, vor sond_tvfm_item_get_basename()) liefert den
 echten Basename der Datei, zu der der Marker-Knoten gehört - via
 sond_file_part_get_path() DES SondFilePart selbst (der PDF- bzw.
 .eml-Datei, dieselbe Quelle, die auch beim Anbinden eines "normalen"
 Knotens ohne path_or_section verwendet würde), nicht von
 path_or_section. In beiden betroffenen Zweigen (PDF-PageTree- und
 GMESSAGE-Message-Branch in sond_tvfm_item_create()) wird jetzt statt
 des hartcodierten Strings dieser Helfer aufgerufen, mit Fallback auf
 den alten Platzhalter nur für den (nicht erwarteten) Fall, dass
 sond_file_part_get_path() NULL liefert.

 Zusätzlich entfernt: in sond_tvfm_item_load_gmessage_dir() gab es eine
 REDUNDANTE zweite Display-Name-Zuweisung (wieder auf den hartcodierten
 "Message"-Platzhalter) direkt nach dem sond_tvfm_item_create()-Aufruf
 für den Message-Marker-Knoten - hätte den obigen Fix an dieser einen
 Aufrufstelle sofort wieder rückgängig gemacht. Der analoge PDF-PageTree-
 Aufruf (sond_tvfm_item_load_pdf_dir() bzw. entsprechende Funktion) hatte
 keine solche redundante zweite Zuweisung und war daher von diesem
 Zusatzfehler nicht betroffen.

 Nicht durch Kompilieren/Testen verifiziert.

 Korrektur (21.09.2026, Nutzer-Klarstellung direkt im Anschluss): "Ich
 dachte eigentlich daran, den Namen nur beim Anbinden in den BAUM_INHALT
 zu ändern. Im BAUM_FS sollten weiterhin die Bezeichnungen Pagetree bzw.
 Message stehen. Da ist ja der Dateiname direkt darüber, so daß keine
 Verwechslung möglich ist." - der obige Fix hatte den Anwendungsbereich
 zu weit gefasst: er änderte den display_name generell (also auch die
 BAUM_FS-Anzeige selbst), nicht nur den beim Anbinden nach BAUM_INHALT
 übernommenen Wert.

 Fix, enger gefasst: die display_name-Änderung in sond_tvfm_item_
 create() (beide Marker-Zweige) ZURÜCKGENOMMEN - BAUM_FS zeigt wieder
 unverändert "PageTree"/"Message". Stattdessen neues privates Flag
 is_content_root_marker in SondTVFMItemPrivate (sond_treeviewfm_
 private.h) ergänzt, das an genau den beiden Marker-Stellen (statt der
 verworfenen display_name-Änderung) gesetzt wird. Neue öffentliche
 Funktion sond_tvfm_item_get_anbinden_label() (sond_tvfm_item.h/.c):
 liefert für Knoten mit gesetztem Flag den echten Dateinamen (via des
 schon vorhandenen Helfers sond_tvfm_item_basename_of_sfp_dup(), der
 dafür unverändert weiterverwendet wird), für alle anderen Knoten eine
 Kopie von display_name (also unverändertes Verhalten) - IMMER neu
 alloziert, damit die Ownership für den Aufrufer einheitlich ist.
 zond_treeview_leaf_anbinden() (zond_treeview.c, Anbinden-Pfad für noch
 nicht in der DB vorhandene Dateien) ruft jetzt diese neue Funktion statt
 sond_tvfm_item_get_display_name() auf und gibt das Ergebnis nach dem
 zond_treeview_insert_file_part_in_db()-Aufruf wieder frei. Die vorhin
 entfernte redundante zweite Display-Name-Zuweisung in sond_tvfm_item_
 load_gmessage_dir() bleibt entfernt (reine Code-Hygiene, unabhängig von
 dieser Korrektur - setzte ohnehin nur denselben, jetzt wieder
 unveränderten Platzhalterwert ein zweites Mal).

 Nicht durch Kompilieren/Testen verifiziert.


 Offene Punkte (21.09.2026, Nutzer-Sammlung - erstmal nur notiert, nicht
 umgesetzt):

 #164 Suchen-Funktion (Popup-Entry): Nutzer-Entscheidung (22.09.2026) -
 inhaltlich bleibt es wie es ist (Popup-Suche für node_text/Kommentare/
 Dateinamen der zond_treeviews, getrennt vom Volltext-Index für
 Dateiinhalte - keine Zusammenlegung).

 Verbesserungsbedarf besteht bei der DARSTELLUNG der Treffer: Treffer in
 fileparts (Dateiinhalt/-name) und in node_texts, die denselben Knoten
 betreffen, erscheinen aktuell als mehrere getrennte Ergebniszeilen -
 sollen zu einem Ergebnis pro Treffer zusammengefasst werden.

 Skizze für ein mögliches künftiges Projekt (Nutzer-Vorschlag): eine
 Anzeige-Zeile pro angebundenem file_part, die diesen komplett
 wiedergibt - links file_part und section, dann 0 oder 1 Anbindung im
 BAUM_INHALT (mit deren node_text und Text) und anschließend 0-n Copies
 im BAUM_AUSWERTUNG (jeweils mit node_text und Text). Vollständig
 dargestellt wird die Zeile nur, wenn mindestens einer dieser Bestandteile
 tatsächlich einen Treffer enthält.

 Klargestellt (22.09.2026): ein file_part-Namenstreffer trifft technisch
 auf jede Section derselben Datei (gleicher file_part-Text, nur section
 unterschiedlich) - Nutzer-Entscheidung: eine Zeile PRO SECTION, nicht
 zusammengefasst pro Datei. Passt zur DB-Struktur (jede Section ist ein
 eigener FILE_PART-Knoten mit eigener ID/Anbindung/Copies) und zur obigen
 Skizze (file_part+section als Zeilen-Identität).

 Richtiggestellt (22.09.2026, nach zwischenzeitlichem Mißverständnis):
 eine Row wird IMMER vollständig angezeigt (0-1 Anbindung, falls
 Anbindung: 0-n Copies), unabhängig davon ob der Treffer nur aus der
 file_part-Namensspalte oder aus node_text/text stammt - keine
 "schlanke" Row ohne Anbindung.

 Feinschliff SECTION-AGGREGATION (22.09.2026, bestätigt, aber
 ZURÜCKGESTELLT - erst nach der Basisversion, kann später ergänzt
 werden): reiner Namenstreffer (nur file_part-Text, geteilt von Basis-
 Datei und allen ihren Sections) soll NICHT pro Section eine eigene Row
 erzeugen, sondern zu einer einzigen Row für die Basis-Datei (file_part
 ohne section, mit deren eigener Anbindung/Copies) zusammengefaßt
 werden - Beispiel: Datei mit Anbindung (node_text "klkklk", kein Text,
 keine Copy) und 100 Sections (je node_text "ioioi", kein Text), Suche
 nach dem Dateinamen -> nur EINE Row (die Basis-Datei). Hat dagegen
 einzelne(r) Section(s) zusätzlich einen EIGENEN inhaltlichen Treffer
 (node_text/text/Copy), bekommt diese Section trotzdem ihre eigene volle
 Row - zusätzlich zur zusammengefaßten Basis-Row. Erfordert Gruppierung
 nach dem gemeinsamen file_part-Text (nicht nur nach Knoten-ID) VOR dem
 Row-Aufbau; da diese Gruppierung rein additiv auf der in Phase 2/3
 gebauten Row-pro-Knoten-Struktur aufsetzt, kann sie ohne Redesign
 nachgerüstet werden.

 Umsetzungsplan, Basisversion (22.09.2026, mit Nutzer abgestimmt, noch
 nicht begonnen - OHNE die zurückgestellte Section-Aggregation, also
 vorerst eine Row pro getroffenem file_part-Knoten/Section):
 1) zond_dbase.c: neue Funktion zond_dbase_get_baum_auswertung_copies()
    analog zond_dbase_get_baum_auswertung_copy(), aber mit
    do{}while(SQLITE_ROW)-Schleife (Muster wie suchen_db()) statt nur
    der ersten Zeile - liefert alle Copy-node_ids zu einer Anbindung.
 2) suchen.c/suchen_db(): jeder Rohtreffer (zond_suchen, node_id) wird
    auf seine file_part-Knoten-ID aufgelöst (Knoten selbst FILE_PART:
    direkt; BAUM_INHALT_FILE: über link; BAUM_AUSWERTUNG_COPY: zwei Hops
    über link->link) - Knoten ohne Datei-Bezug (reine Strukturpunkte)
    bleiben wie bisher einzelne, einfache Zeilen. Über eine
    GHashTable<file_part_node_id, ResultRow*> entsteht pro Section genau
    eine ResultRow (mehrfache Rohtreffer zum selben Knoten werden beim
    Bauen übersprungen, da schon vorhanden).
 3) Neue Helper-Funktion: aus einer file_part-Knoten-ID file_part+section
    lesen, per zond_dbase_get_baum_inhalt_file_from_file_part() die 0/1
    Anbindung (node_text+text) und per (1) alle Copies (node_text+text)
    nachladen - immer vollständig, unabhängig von der Treffer-Quelle.
 4) misc.c/result_listbox_new() bleibt als Fenster-/Listbox-Gerüst
    unverändert; nur suchen_fuellen_row() (suchen.c) wird ersetzt durch
    eine Funktion, die pro ResultRow eine zusammengesetzte GtkBox einfügt
    (links file_part/section, rechts Anbindung + je eine Zeile pro Copy).
 5) Aktivierung (cb_lb_row_activated) und Kontextmenü
    (suchen_kopieren_listenpunkt) müssen auf Teilzeilen-Ebene (Anbindung
    bzw. einzelne Copy) umgestellt werden, da eine zusammengesetzte Zeile
    jetzt mehrere node_ids trägt statt nur einer.
 6) Verifikation ohne Compile-Möglichkeit: Klammern-/Kommentar-Balance-
    Skript nach jeder Änderung; Testfälle gedanklich durchgehen (Treffer
    in einer von mehreren Copies -> volle Zeile mit allen Copies;
    Doppeltreffer im selben Knoten -> nur eine Zeile). Eigentlicher
    Build/Test durch Nutzer.
 7) Danach optional die zurückgestellte Section-Aggregation (s.o.)
    nachrüsten: zusätzliche Gruppierung nach file_part-Text VOR Phase 2,
    Basis-Row + gesondert behandelte Sections mit eigenem Treffer.

 Umsetzung Phasen 1-4 (22.09.2026): zond_dbase_get_baum_auswertung_copies()
 ergänzt; suchen.c um ResultRow/ResultCopy/SuchenItem, suchen_resolve_
 file_part_node() (löst Rohtreffer auf file_part-Knoten auf, drei Fälle:
 FILE_PART direkt, BAUM_INHALT_FILE über link, BAUM_AUSWERTUNG_COPY zwei
 Hops - VIRT_PDF als zweite Copy-link-Alternative wird aktuell nirgends
 erzeugt und defensiv übersprungen), suchen_baue_row() und
 suchen_aggregieren() erweitert; suchen_fuellen_row() aufgeteilt in
 suchen_fuellen_row_simple() (unverändertes altes Verhalten für Knoten
 ohne Datei-Bezug) und suchen_fuellen_row_composite() (neue
 zusammengesetzte Zeile). Dabei zwei unabhängige Alt-Bugs gefunden und
 mitbehoben: (1) "root"/"baum" Object-Data-Schlüssel-Mismatch - suchen_
 fuellen_row() schrieb bisher unter "root", cb_lb_row_activated() las
 aber "baum", wodurch das Sprungziel beim Aktivieren eines Suchergebnisses
 immer fälschlich in BAUM_FS (0) statt im tatsächlichen Baum gesucht
 wurde; (2) eine lokale Variable "root" im zond_suchen==1-Zweig
 überschattete die äußere und verhinderte zusätzlich, dass der ermittelte
 Wert überhaupt nach außen drang. (3) titel (g_strconcat) in
 suchen_treeviews() wurde nie freigegeben - jetzt per g_free() nach
 Gebrauch.

 Umsetzung Phase 5 (22.09.2026, Nutzer-Vorgabe "Klick auf Spalte führt zu
 Sprung zu Knoten; ggf. muß BAUM_FS bzw. BAUM_AUSWERTUNG erst eingeblendet
 werden"): suchen_fuellen_row_composite() baut jetzt statt EINER
 zusammengesetzten GtkBox mehrere GETRENNTE Listbox-Zeilen (Kopfzeile mit
 file_part+section, inaktiv; dann je eine eigene, eingerückte Zeile für
 die Anbindung und jede Copy) - jede Teilzeile trägt so ihr EIGENES
 "baum"/"node-id" und nutzt den vorhandenen Doppelklick-/Auswahl-
 Mechanismus der Listbox unverändert (kein Event-Box-Umbau nötig). Neue
 Helper-Funktion suchen_listbox_insert_zeile() dafür.

 Die Sprunglogik selbst wurde nach suchen_springe_zu_knoten() ausgelagert
 (aus cb_lb_row_activated() heraus, jetzt auch von dort nur noch
 aufgerufen) und um das Umschalten zwischen BAUM_FS und BAUM_AUSWERTUNG
 ergänzt - beide teilen sich dieselbe Fläche (zond->hpaned, s.
 app_window.c) und werden über zond->fs_button umgeschaltet; BAUM_INHALT
 ist immer sichtbar. Gleiches Umschalt-Idiom wie schon in
 zond_treeview_jump_to_iter() (zond_treeview.c) bzw. spiegelbildlich in
 app_window.c (cb_jump_button_clicked) verwendet.

 suchen_kopieren_listenpunkt() (Kontextmenü "In Baum Auswertung
 kopieren") überspringt jetzt eine ausgewählte Kopfzeile (node-id 0)
 defensiv, statt mit node_id=0 in zond_treeview_walk_tree() zu laufen -
 der Anker bleibt für den nächsten ausgewählten Punkt unverändert.

 Phase 7 (Section-Aggregation) noch offen; Klammer- und Kommentar-Balance
 nach jeder Änderung geprüft (inkl. Prüfung auf eine Kommentaröffnung
 innerhalb eines bereits offenen Kommentars - s. Fund/Fix weiter unten,
 #165-Nachtrag). Build/Test durch Nutzer noch ausstehend.

 #165 Code-Kommentare: die in dieser Session (und wohl auch vorher)
 verwendete Form - ausführliche Herleitung mit Datumsangaben ("am xx.
 haben wir X gemacht, drei Tage später Y") - ist aufgebläht und für
 spätere Leser wenig hilfreich. Kommentare sollten künftig knapp auf was
 gemacht wird und ggf. warum reduziert werden, ohne Versionsgeschichte.
 Gilt als Stilvorgabe für neue Kommentare; ein nachträgliches Aufräumen
 bestehender Kommentare ist ein separates, potenziell sehr großes
 Vorhaben und hier nicht mitgemeint.

 Nachtrag (22.09.2026, Nutzer-Fund - Compilerfehler): der Kommentar zum
 #164-Umsetzungsstand enthielt wörtlich die beiden Blockkommentar-
 Begrenzungszeichen selbst (als Beschreibung der Balance-Prüfung gemeint)
 - genau das Problem, vor dem hier gewarnt wird: die schließende
 Zeichenfolge darin beendete den äußeren Kommentar vorzeitig, der
 Compiler brach mit einer Fehlermeldung zum unerwarteten Komma ab.
 Behoben durch Umformulierung ganz ohne diese Begrenzungszeichen im
 Kommentartext. Die bisherige Verifikation (nur Endstand der Klammer-/
 Kommentarbalance prüfen) hätte das nicht zuverlässig gefangen, wenn ein
 späterer echter Kommentar im selben Lauf die Bilanz zufällig wieder
 ausgleicht - Prüfung deshalb um einen expliziten Check ergänzt: jede
 Kommentaröffnung, die auftritt während bereits ein Kommentar offen ist,
 wird als Fehler gemeldet, unabhängig vom Endstand.


 #165 (21.09.2026, Nutzer-Vorgabe "Kommentare im Code kürzen"): dated-
 narrative Kommentare ("am xx.yy. haben wir...") in zond_treeview.c,
 sond_tvfm_item.c/.h, sond_treeviewfm_private.h und document.c/.h auf
 knappe "was + ggf. warum"-Form gekürzt, ohne Herleitungsgeschichte.
 ToDo.c selbst bleibt als dieses datierte Journal unverändert im
 bisherigen Stil. Nach jeder Änderung Klammerbalance ( { [ und
 Blockkommentar-Balance (Auf/Zu, unter Berücksichtigung von Zeilen-
 kommentaren und String-/Char-Literalen) programmatisch geprüft -
 überall ausgeglichen.

 #173 (22.09.2026, Nutzer-Fund): Bug gemeldet - eine elektronisch erzeugte
 PDF mit direkt (sichtbar) gedrucktem Text wurde beim Indizieren trotzdem
 auf jeder Seite gerendert und einer OSD-Prüfung unterzogen, die dabei
 fehlschlug. Ursache: pdf_page_has_hidden_text() (sond_pdf_helper.c)
 erkannte nur Textläufe mit Tr 3 (unsichtbar, typischerweise von früherer
 OCR) als "vorhanden" - eine normale, sichtbare Textebene lieferte
 has_hidden_text=FALSE, wodurch der Übersprungen-Zweig im Modus "prüfen"
 (sond_ocr_do_tasks(), sond_ocr.c) nie griff und die Seite trotz
 vorhandenem Text neu gerendert und OSD-geprüft wurde.

 Behoben durch Umbenennung/Erweiterung zu pdf_page_has_text() mit zwei
 Ausgabeparametern: has_text (sichtbar ODER unsichtbar - entscheidet jetzt
 über das Überspringen im Modus "prüfen") und has_hidden_text (nur Tr 3 -
 entscheidet weiterhin, ob im Modus "erzwingen" vor dem Neu-OCRen etwas zu
 entfernen ist; hat die Seite nur sichtbaren Text, gibt es nichts zu
 entfernen, sie fällt direkt durch zum Neu-OCRen). Beide Aufrufstellen
 (sond_ocr.c, zond/40viewer/seiten.c) sowie die zugehörigen Log- und
 Dialogtexte (seiten_ocr_abfrage_hidden_text()) entsprechend angepasst.
 Klammer- und Blockkommentarbalance in allen vier berührten Dateien
 (sond_pdf_helper.h/.c, sond_ocr.c, seiten.c) programmatisch geprüft -
 überall ausgeglichen.

 #174 (22.09.2026, Nutzer-Fund, direkt im Anschluß an #173): beim
 Indizieren derselben PDF trat "db_insert_chunk: step: database disk
 image is malformed" auf (SQLite SQLITE_CORRUPT). Betroffen ist
 ausschließlich .sond_index.db (Volltextindex/chunks-Tabelle,
 sond_index.c) - eine vom Fallakten-/Anbindungsbestand (dbase_zond,
 .znd) komplett separate, jederzeit neu aufbaubare Ableitung.

 Ursache: .sond_index.db liegt (anders als die "work"-DB, s. Task #42,
 project_get_local_tmp_path()) weiterhin im SeaDrive-synchronisierten
 Projektverzeichnis und lief bislang im WAL-Journal-Modus
 (sond_index_ctx_new()). WAL braucht verlässliches mmap/Byte-Range-
 Locking auf der -shm-Begleitdatei - das bietet ein Cloud-Sync-Laufwerk
 nicht zuverlässig, was zu genau dieser Art Korruption führen kann. Die
 bereits vorhandene Hydrierung-vor-dem-Öffnen (s. Eintrag weiter oben zu
 project_open()) deckt nur den Zustand beim Öffnen ab, nicht laufende
 Schreibzugriffe während der Indizierung.

 Nutzer-Entscheidung: von den zwei Alternativen (Index-DB auf lokalen,
 nicht-synchronisierten Pfad verlegen vs. WAL abschalten) wurde die
 einfachere gewählt - WAL abschalten. Umgesetzt in sond_index_ctx_new()
 (sond_index.c): PRAGMA journal_mode=DELETE statt WAL, PRAGMA
 synchronous=FULL statt NORMAL (auf einem Cloud-Laufwerk das robustere,
 wenn auch langsamere Verhalten bei klassischem Rollback-Journal). Löst
 die Ursache nicht vollständig (die Datei liegt weiterhin auf dem
 Cloud-Laufwerk), reduziert das Korruptionsrisiko aber erheblich. Die
 lokale Verlegung bliebe die sauberere, hier bewußt zurückgestellte
 Alternative, falls das Problem trotzdem wieder auftritt.

 Nutzer-Einschätzung zur zurückgestellten Alternative (22.09.2026): eine
 dauerhafte lokale Verlegung hält der Nutzer für nicht sinnvoll, da
 jedes Projekt seine eigene Index-DB hat (also kein zentraler, fester
 lokaler Pfad, sondern pro Projekt einer). Ein Hin-und-her-Kopieren bei
 Projekt-Start/-Ende wäre die einzige Alternative dazu, brächte aber
 eine eigene Fehlerquelle mit (Risiko bei einem Absturz zwischen den
 beiden Kopiervorgängen). Alternative damit nicht nur zurückgestellt,
 sondern von der Grundidee her verworfen.

 Die zum Zeitpunkt des Funds bereits kaputte .sond_index.db muß vom
 Nutzer einmalig gelöscht (samt evtl. vorhandener -wal/-shm-Dateien) und
 der Index neu erstellt werden - reiner Datenverlust einer Ableitung,
 keine Fallakten betroffen. Klammer- und Blockkommentarbalance in
 sond_index.c programmatisch geprüft - ausgeglichen. Nicht durch
 Kompilieren/Testen verifiziert.

 #175 (22.09.2026, Nutzer-Fund, Rückkehr zum #164-Ergebnisfenster):
 "Anklicken BAUM_FS und BAUM_INHALT funktioniert nicht" - beide Meldungen
 hatten dieselbe Ursache in suchen_fuellen_row_simple() (suchen.c): der
 zond_suchen==2-Zweig (Treffer im Kommentartext "text" eines Knotens ohne
 Datei-Bezug, z.B. reiner Strukturpunkt) ermittelte "baum" nie - anders
 als der zond_suchen==1-Zweig (Treffer im node_text), der dafür bereits
 korrekt zond_dbase_get_tree_root() aufrief. Die lokale Variable blieb
 beim Default 0 stehen, was zufällig BAUM_FS entspricht - ein echter
 BAUM_INHALT- oder BAUM_AUSWERTUNG-Treffer über diesen Zweig wurde also
 fälschlich als "BAUM_FS" verdrahtet. Ein Sprung nach BAUM_FS kann mit
 einer "knoten"-Tabellen-ID aber grundsätzlich nie funktionieren - BAUM_FS
 hat ein eigenes, dateisystembasiertes Baummodell ohne solche IDs
 (zond_treeview_get_path() liest dort die falsche Spalte und läuft still
 ins Leere). Fix: beide Zweige (1 und 2) ermitteln "baum" jetzt
 gleichermaßen über zond_dbase_get_tree_root(); der zond_suchen==0-Zweig
 (FilePart-Namenstreffer, seit #164 ohnehin unerreichbar, da solche
 Treffer immer schon zur aggregierten ResultRow werden) markiert sein
 Sprungziel jetzt defensiv inert (node_id=0) statt fälschlich BAUM_FS.
 Zusätzlich Verteidigungs-Guard in suchen_springe_zu_knoten(): baum==
 BAUM_FS wird jetzt explizit erkannt, geloggt und ignoriert, statt GTK
 mit einer ungültigen Spaltenabfrage ins Leere laufen zu lassen - BAUM_FS
 kann aus dieser Suche strukturell nie ein gültiges Sprungziel sein.

 Separat angesprochen: die Zusammengehörigkeit der Zeilen einer
 aggregierten ResultRow (Kopfzeile + Anbindung + Copies) ist aktuell nur
 durch Reihenfolge und Einrückung erkennbar, ohne visuelle Abgrenzung
 zwischen Gruppen, und die Kopfzeile selbst bleibt bewusst inaktiv/nicht
 klickbar. Nutzer-Entscheidung (22.09.2026): fürs Erste nur den Bug
 fixen, die optische Gruppierung (Trennlinie zwischen Gruppen, fette
 Kopfzeile, ggf. Kopfzeile klickbar zur Anbindung) zurückgestellt - bei
 Bedarf später aufgreifen.

 Klammer- und Blockkommentarbalance in suchen.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 Korrektur zu #175 (22.09.2026, unmittelbar im Anschluß, Nutzer-
 Widerspruch): "Ist leider noch überhaupt nicht das, was ich mir
 vorstelle! Für jeden Treffer komplette Zeile: filepart -
 node_text(BAUM_INHALT) - text(BAUM_INHALT) n x(node_text(BAUM_AUSWERTUNG)
 - text(BAUM_AUSWERTUNG))". Die separaten Listbox-Zeilen pro Anbindung/
 Copy (Phase 5, oben dokumentiert) waren eine Fehleinschätzung - schon die
 damalige Nutzer-Vorgabe "Klick auf Spalte führt zu Sprung zu Knoten"
 (Wortwahl "Spalte", nicht "Zeile") deutete bereits auf eine einzige Zeile
 mit mehreren klickbaren Abschnitten hin; das war beim risikoarm
 gewählten Weg über separate Zeilen (Begründung: nicht kompilierbar/
 testbar, daher der vermeintlich sicherere Weg über das ohnehin
 vorhandene Zeilen-Auswahl-/Aktivierungssystem) untergegangen.

 Neu umgesetzt: suchen_fuellen_row_composite() baut jetzt GENAU EINE
 Listbox-Zeile pro ResultRow - eine horizontale GtkBox mit einem
 (nicht-klickbaren) Label für file_part(+section), gefolgt von je einem
 flach dargestellten GtkButton ("Spalte") für die Anbindung
 (BAUM_INHALT: node_text - text) und jede Copy (BAUM_AUSWERTUNG: node_text
 - text), getrennt durch " - "-Labels. Jede Spalte trägt ihr eigenes
 "baum"/"node-id" als Objekt-Daten auf dem Button selbst (nicht auf der
 Zeile) und ruft beim "clicked"-Signal (neu: cb_suchen_spalte_clicked())
 direkt suchen_springe_zu_knoten() auf - unabhängig von den anderen
 Spalten derselben Zeile. Die Zeile selbst trägt zusätzlich "baum"/
 "node-id" der Anbindung (0/0 ohne Anbindung), damit Doppelklick auf die
 Zeile allgemein (cb_lb_row_activated(), unverändert) und "In Baum
 Auswertung kopieren" (suchen_kopieren_listenpunkt(), arbeitet auf der
 Zeilenauswahl der Listbox) weiterhin ein sinnvolles Ziel haben. Die
 jetzt ungenutzte suchen_listbox_insert_zeile() (Phase 5) wurde entfernt.

 Der oben unter #175 zurückgestellte Gruppierungs-Punkt (Trennlinie
 zwischen Gruppen, fette Kopfzeile) erledigt sich durch diese Korrektur
 größtenteils von selbst - eine Zeile pro Treffer macht die Zugehörigkeit
 schon durch die Zeilenstruktur selbst eindeutig, ohne zusätzliche Optik.

 Klammer- und Blockkommentarbalance in suchen.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 Zweite Korrektur zu #175 (22.09.2026, unmittelbar im Anschluß, Nutzer-
 Präzisierung): "Ich stelle mir eine Tabelle vor: Links filepart
 (Überschrift z.B. Dateiname), daneben node_text BAUM_INHALT (Überschrift
 z.B. Bestandsverzeichnis), in der gleichen Spalte, unter dem node_text,
 der text, falls vorhanden, Spalte daneben, wenn vorhanden, die Copies des
 Punktes im Bestandsverzeichnis, node_text und text, mehrere Copies in
 eigenen Boxen, anklickbar, untereinander. Bei Strukturpunkten bleiben die
 anderen Spalten natürlich leer, Strukturpunkte in beiden Bäumen haben ja
 keinerlei Beziehungen untereinander." Die eine-Zeile-mit-Buttons-
 hintereinander-Lösung der ersten Korrektur war noch keine echte Tabelle -
 keine Spaltenüberschriften, keine spaltenweise Ausrichtung über alle
 Zeilen hinweg, und Strukturpunkte (suchen_fuellen_row_simple()) sahen
 optisch noch anders aus als aggregierte Treffer.

 Jetzt umgesetzt als echte Tabelle mit drei Spalten - "Dateiname",
 "Bestandsverzeichnis" (BAUM_INHALT), "Auswertung" (BAUM_AUSWERTUNG):

 - misc.c, result_listbox_new(): Fenster-Aufbau um eine vbox erweitert,
   die eine neue, leere header_box (Objekt-Daten "header-box", bleibt beim
   Scrollen der Liste fest stehen, da außerhalb von scrolled_window)
   oberhalb der scrollbaren Listbox einfügt - Aufrufer füllt sie selbst.

 - suchen.c, suchen_erzeugen_ergebnisfenster(): füllt header_box mit drei
   fett dargestellten Spaltenüberschriften-Labels und legt dafür drei
   GtkSizeGroup an (sg_filepart/sg_inhalt/sg_auswertung, via
   g_object_set_data_full() lebensdauergebunden am Fenster) - jede hält
   Kopf- und Datenzelle ihrer Spalte über alle (voneinander unabhängigen)
   Zeilen-GtkBoxen hinweg gleich breit, womit trotz einer eigenständigen
   GtkBox pro Zeile eine echte Spaltenausrichtung entsteht.

 - suchen.c, neue Funktion suchen_box_knoten(): baut die "eigene Box" für
   einen einzelnen Knoten (node_text oben, text darunter falls vorhanden,
   klickbar - Sprung zu genau diesem Knoten) - ersetzt die vorige flache,
   einzeilige "Spalte" (suchen_row_add_spalte(), entfernt) durch einen
   echten zweizeiligen Button.

 - suchen.c, neue Funktion suchen_zelle_leer(): leere Platzhalterzelle für
   eine Spalte, die für eine bestimmte Zeile nicht zutrifft - trotzdem der
   sizegroup hinzugefügt, damit die Spaltenbreite erhalten bleibt.

 - suchen_fuellen_row_composite() baut jetzt drei Zellen statt einer
   Buttonkette: Spalte 1 = file_part(+section)-Label, Spalte 2 = Box der
   Anbindung (falls vorhanden, sonst leer), Spalte 3 = vertikale Box aller
   Copy-Boxen untereinander (0-n, sonst leer).

 - suchen_fuellen_row_simple() (reine Strukturpunkte) baut jetzt dieselbe
   Drei-Spalten-Struktur wie suchen_fuellen_row_composite() - Spalte 1
   bleibt immer leer (kein Datei-Bezug), und je nachdem, in welchem Baum
   der Treffer liegt, füllt sich GENAU eine der beiden anderen Spalten mit
   einer einzelnen Box - die andere bleibt leer, wie vom Nutzer
   vorgegeben. Damit vereinheitlicht: zond_suchen==1 (node_text-Treffer)
   und zond_suchen==2 (text-Treffer) zeigen jetzt dieselbe Box (node_text
   UND text, unabhängig davon, welches Feld den Treffer auslöste) - vorher
   gab es dafür unterschiedliche Textformatierungen; die zuvor hier
   behobene BAUM_FS-Verwechslung (#175, erste Korrektur) bleibt in Kraft,
   da beide Fälle weiterhin über zond_dbase_get_tree_root() aufgelöst
   werden. Die jetzt ungenutzte suchen_format_content_line() wurde
   entfernt.

 Klammer- und Blockkommentarbalance in misc.c und suchen.c programmatisch
 geprüft - ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 #176 (22.09.2026, Nutzer-Vorgabe im Anschluß an den XJustiz-Import):
 "Aber ich wünsche mir, daß in das Bestandsverzeichnis an der gewählten
 Stelle ein Strukturpunkt eingefügt wird, in den die einzelnen Dateien
 eingefügt werden. Zur Bennenung: Aus dem Nachrichtenkopf: Name des
 Produkts und Erstellungszeitpunkt."

 xjustiz_import.c: statt die Dokumente direkt an der markierten Stelle im
 Bestandsverzeichnis anzubinden, wird dort zunächst ein neuer
 ZOND_DBASE_TYPE_BAUM_STRUKT-Knoten eingefügt (icon "folder", wie an
 anderen Strukturpunkt-Einfügestellen üblich); die Dokumente hängen jetzt
 als dessen Unterpunkte (anchor_id/child auf den neuen Strukturpunkt
 umgebogen, danach unverändert die bisherige Geschwister-Anhänge-Schleife).

 Benennung aus dem Nachrichtenkopf: xjustiz_parse_nachricht() um zwei
 optionale Ausgabeparameter erweitert (*out_produktname, *out_zeitpunkt),
 gesucht wie auch sonst in dieser Datei per "//"+local-name() unabhängig
 von der genauen Verschachtelungstiefe (Recherche: erstellungszeitpunkt
 direkt unter nachrichtenkopf ist durch ein Beispieldokument belegt,
 nameDesProdukts unter grunddaten/herstellerinformation ist nur laut
 XJustiz-Konvention plausibel, nicht an einem Beispiel verifiziert - daher
 bewußt keine feste Pfadtiefe vorausgesetzt). Neue Funktion
 xjustiz_format_zeitpunkt() formatiert den rohen xs:dateTime-Wert (z.B.
 "2024-02-22T11:23:51.210+01:00") via g_date_time_new_from_iso8601() auf
 "TT.MM.JJJJ hh:mm"; schlägt das Parsen fehl, wird der Rohwert
 unverändert übernommen. Label = "<Produktname> <Zeitpunkt>", oder nur das
 vorhandene Feld, oder Fallback "XJustiz-Import", falls beide fehlen.

 Nebenbei behoben: echter Compilerfehler (-Wcomment) in
 xjustiz_import.h, Zeile 59 - drei durch Schrägstrich getrennte
 Pointer-Namen bildeten an der Trennstelle ein Kommentar-Ende-Zeichen
 gefolgt von einem Kommentar-Anfang-Zeichen; durch Kommata ersetzt. Beim
 Formulieren des neuen Doc-Kommentars zu xjustiz_parse_nachricht() ist
 dieselbe Fehlerklasse ein zweites Mal (selbst verursacht, vor dem
 Speichern durch die Klammer-/Kommentarbalance-Prüfung abgefangen)
 aufgetreten: zwei Pointer-Namen, ebenso durch Schrägstrich getrennt -
 ebenfalls durch Komma ersetzt.

 Klammer- und Blockkommentarbalance in xjustiz_import.c und .h
 programmatisch geprüft - ausgeglichen. Nicht durch Kompilieren/Testen
 verifiziert.

 Bug in der #164/#175-Ergebnistabelle (22.09.2026, Nutzer-Fund): "Der Text
 in der Spalte 'Bestandsverzeichnis' ist fast immer leer, obwohl es einen
 Treffer gibt, weil die Datei genauso heißt."

 Ursache in suchen_baue_row() (suchen.c): node_text/text der Spalte
 "Bestandsverzeichnis" wurden vom BAUM_INHALT_FILE-Anker-Knoten selbst
 gelesen (per zond_dbase_get_node(id_anbindung, ...)). Der Anker-Knoten
 trägt aber laut etablierter Konvention nie eigenes icon_name/node_text/
 text - er belegt nur die Position im knoten-Baum (s. Kommentar in
 zond_treeview_copy_node_to_baum_auswertung(), zond_treeview.c, sowie
 sämtliche zond_dbase_insert_node()-Aufrufe für
 ZOND_DBASE_TYPE_BAUM_INHALT_FILE, die icon_name/node_text/text immer als
 NULL übergeben). Angezeigt wird an allen anderen Stellen im Code deshalb
 stattdessen der verlinkte, dauerhafte file_part-Knoten - genau das hat
 suchen_baue_row() nicht getan, weshalb node_text/text dort praktisch
 immer NULL waren.

 Behoben: node_text/text werden jetzt im selben get_node()-Aufruf wie
 row->file_part/row->section vom file_part-Knoten (file_part_node_id)
 gelesen statt vom Anker-Knoten; der separate get_node(id_anbindung, ...)-
 Aufruf dafür entfällt. Die Spalte bleibt weiterhin korrekt leer, wenn gar
 keine Anbindung existiert - das entscheidet unverändert row->has_anbindung
 in suchen_fuellen_row_composite(), nicht die Textfelder selbst.

 Klammer- und Blockkommentarbalance in suchen.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 Folgebug zu #177 (22.09.2026, Nutzer-Fund, unmittelbar im Anschluß):
 "Klick auf Spalte Bestandsverzeichnis springt nur dann zum Knoten, wenn
 keine Datei angebunden."

 Ursache in suchen_fuellen_row_composite() (suchen.c): der klickbare
 Sprung-Button für die Spalte "Bestandsverzeichnis" bekam als Sprungziel
 row->anbindung_node_id - die echte ID des BAUM_INHALT_FILE-Anker-Knotens.
 Beim Anbinden wird der dazugehörige, sichtbare Tree-Store-Eintrag aber
 nicht unter dieser Anker-ID registriert, sondern unter der ID des
 verlinkten, dauerhaften file_part-Knotens (s. Kommentar "Angezeigt wird
 ID_file_part (nicht new_node_id!)" in zond_treeview_leaf_anbinden(),
 zond_treeview.c - dieselbe Aliasierung, die schon #164/#177 betraf).
 zond_treeview_get_path() sucht per node_id-Spalte im Tree-Store und fand
 mit der Anker-ID nie eine Zeile - der Sprung verpuffte lautlos. Bei
 Strukturpunkten (suchen_fuellen_row_simple()) trat das nicht auf, weil
 dort die Knoten-ID unaliasiert mit sich selbst identisch ist - daher der
 fälschliche Eindruck "nur ohne Datei funktioniert es".

 Behoben: die Box "Bestandsverzeichnis" bekommt jetzt row->file_part_
 node_id als Sprungziel statt row->anbindung_node_id. row->anbindung_
 node_id bleibt unverändert dort in Gebrauch, wo tatsächlich die echte
 Anker-ID gebraucht wird (zond_dbase_get_baum_auswertung_copies() sowie
 die "baum"/"node-id"-Objektdaten der ganzen Zeile für "In Baum Auswertung
 kopieren", das über zond_treeview_copy_node_to_baum_auswertung() anhand
 des Anker-Typs entscheidet, wie es den Unterbaum kopiert - eine
 file_part_node_id hätte dort falsches Verhalten erzeugt).

 Bewußt nicht mitbehoben: Doppelklick auf die Zeile selbst
 (cb_lb_row_activated(), separate "baum"/"node-id"-Objektdaten auf der
 Zeile) bleibt bei Anbindungen weiterhin ohne Sprungziel-Treffer - dieselbe
 Aliasierungs-Ursache, aber die Zeilen-Objektdaten dürfen wegen der
 "kopieren"-Funktion nicht auf file_part_node_id umgestellt werden. Klick
 auf die Box ist laut Nutzer-Vorgabe ohnehin der vorgesehene Sprung-Weg;
 Doppelklick auf die Zeile wäre ein eigener, kleiner Folge-Fix (getrennte
 Objektdaten je Zweck), falls der Nutzer das noch braucht.

 Klammer- und Blockkommentarbalance in suchen.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 Weiterer Folgebug zu #177/#178 (22.09.2026, Nutzer-Fund, unmittelbar im
 Anschluß): "In BAUM_AUSWERTUNG sind Copies von einer Anbindung in
 BAUM_INHALT. Diese werden auch angezeigt, aber nicht in einer Zeile mit
 der Anbindung."

 Ursache: zond_treeview_copy_node_to_baum_auswertung() (zond_treeview.c)
 setzt bei einer Copy der ANBINDUNG SELBST (nicht eines Kind-Knotens
 darunter) deren "link" auf den Wert, den die Anbindung selbst als "link"
 trägt - das ist bereits die file_part-ID direkt, nicht die Anbindung. Der
 sonst übliche Zwei-Hop-Pfad Copy->Anbindung->file_part existiert für
 diesen Fall also gar nicht, sondern nur ein Ein-Hop-Pfad Copy->file_part.

 Das betraf zwei Stellen in suchen.c, die beide nur den Zwei-Hop-Fall
 kannten:

 - suchen_resolve_file_part_node(): löste bei einer Copy der Anbindung
   selbst keinen file_part-Bezug auf (type_ziel war FILE_PART, nicht
   BAUM_INHALT_FILE) - der Rohtreffer landete dadurch als "kein
   Datei-Bezug" in einer eigenen Strukturpunkt-Zeile statt in der
   ResultRow der Anbindung aggregiert zu werden. Jetzt wird zusätzlich
   type_ziel==FILE_PART erkannt und "link" direkt als file_part_node_id
   übernommen.

 - suchen_baue_row(): fragte Copies nur über zond_dbase_get_baum_
   auswertung_copies(id_anbindung) ab (type=3 AND link=id_anbindung) -
   fand dadurch Copies der Anbindung selbst gar nicht (deren link ist
   file_part_node_id, nicht id_anbindung), auch nach obigem Fix nicht.
   Jetzt zusätzlich eine zweite Abfrage mit link=file_part_node_id, beide
   Ergebnislisten zusammengeführt in row->arr_copies.

 Klammer- und Blockkommentarbalance in suchen.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 Neues Feature zur #164-Ergebnistabelle (22.09.2026, Nutzer-Vorgabe): "Und
 jetzt noch implementieren, daß man zum Knoten im BAUM_FS springen kann.
 Entweder beim Click auf die Zeile oder auch Button einbauen."

 Umgesetzt als Button, konsistent mit den übrigen Spalten (nicht Klick auf
 die ganze Zeile): die Spalte "Dateiname" in suchen_fuellen_row_composite()
 (suchen.c) ist jetzt selbst ein klickbarer GtkButton statt eines reinen
 GtkLabel. Neue Funktion suchen_springe_zu_baum_fs(Projekt*, gchar const
 *file_part) schaltet BAUM_FS bei Bedarf sichtbar (zond->fs_button, analog
 zum bestehenden Umschalten für BAUM_AUSWERTUNG in
 suchen_springe_zu_knoten()) und sucht dann per sond_treeviewfm_file_part_
 visible(SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), NULL, file_part, TRUE,
 &iter, &error) - dieselbe, bereits in zond_indexsuche.c etablierte
 Funktion für "Datei im dateisystembasierten Baummodell finden", mit
 open=TRUE, damit dafür nötige, noch nicht aufgeklappte Verzeichnisse
 automatisch nachgeladen werden. Bei Erfolg (rc==1) sond_treeview_set_
 cursor() auf den gefundenen Iter - fokussiert und scrollt automatisch,
 wie an anderer Stelle im Code üblich (s. sond_treeviewfm_results_row_
 activated(), sond_treeviewfm.c).

 file_part wird beim Bau des Buttons als eigene, mit g_free freizugebende
 Kopie per g_object_set_data_full() auf dem Button hinterlegt (Schlüssel
 "file-part") - die ResultRow (und damit row->file_part) wird bereits kurz
 nach dem Befüllen des Ergebnisfensters wieder freigegeben (s.
 suchen_anzeigen_ergebnisse()), ein reiner Zeiger auf row->file_part wäre
 beim späteren Klick also bereits ungültig.

 Neuer Include: "../../sond_treeviewfm.h" (für SOND_TREEVIEWFM-Makro und
 sond_treeviewfm_file_part_visible()) - vorher nur transitiv über
 zond_treeview.h/sond_treeview.h erreichbar, dort aber nicht deklariert.

 Betrifft nur suchen_fuellen_row_composite() (Zeilen mit Datei-Bezug) -
 suchen_fuellen_row_simple() (reine Strukturpunkte) hat keine Spalte
 "Dateiname" und bleibt unverändert.

 Klammer- und Blockkommentarbalance in suchen.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 Nachtrag zum BAUM_FS-Sprung (22.09.2026, Nutzer-Fund, unmittelbar im
 Anschluß): "Klappt. Aber wenn ich zu BAUM_FS springe bleibt die
 Markierung in BAUM_INHALT bestehen."

 suchen_springe_zu_baum_fs() (suchen.c) hat vor dem Setzen des Cursors in
 BAUM_FS gefehlt, was suchen_springe_zu_knoten() für die anderen Bäume
 schon macht: gtk_tree_selection_unselect_all() auf den jeweils NICHT
 angesprungenen Bäumen. Jetzt ergänzt: unselect auf BAUM_INHALT und
 BAUM_AUSWERTUNG vor dem Cursor-Setzen in BAUM_FS.

 Klammer- und Blockkommentarbalance in suchen.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 Refactoring des #180-BAUM_FS-Sprungs (22.09.2026, Nutzer-Hinweis direkt
 im Anschluß): "Kanns Du da nicht die Implementierung aus jump-to-origin
 verwenden?"

 suchen_springe_zu_baum_fs() (suchen.c) rief bisher selbstgestrickt
 sond_treeviewfm_file_part_visible() + sond_treeview_set_cursor() auf.
 Es gibt dafür aber bereits eine etablierte, öffentliche Funktion für
 genau diesen Zweck: zond_treeviewfm_set_cursor_on_section() (zond_
 treeviewfm.c), die auch vom bestehenden "Sprung zur Herkunft" (zond_
 treeview_jump_to_origin(), zond_treeview.c, Menüpunkt/Action "jump",
 FILE_PART-Fall) genutzt wird. Umgestellt darauf - Vorteil gegenüber der
 vorigen Fassung: berücksichtigt zusätzlich row->section (z.B.
 Seitenbereich einer PDF-Datei), was die datei-only Fassung schlicht
 ignoriert hätte. Dafür wird jetzt auch "section" (zusätzlich zu
 "file-part") als eigene, freizugebende Kopie auf dem Button hinterlegt
 (g_object_set_data_full, nur falls vorhanden).

 Das Unselect von BAUM_INHALT/BAUM_AUSWERTUNG (Nachtrag von eben) bleibt
 zusätzlich zur wiederverwendeten Funktion bestehen - zond_treeview_jump_
 to_origin() selbst macht das nicht, braucht es aber auch nicht: dort wird
 der Sprung immer aus BAUM_INHALT/BAUM_AUSWERTUNG selbst ausgelöst
 (Menü-Action auf dem aktiven Baum), hier dagegen aus dem separaten
 Suchergebnisfenster, wo die alte Markierung sonst optisch stehen bliebe.

 Klammer- und Blockkommentarbalance in suchen.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 Refactoring des Sprungs zu BAUM_INHALT/BAUM_AUSWERTUNG (22.09.2026,
 Nutzer-Hinweis direkt im Anschluß an das BAUM_FS-Refactoring): "Und der
 Sprung zum Knoten in den anderen beiden Bäumen? Kannst Du da nicht auch
 etwas wiederverwenden?"

 suchen_springe_zu_knoten() (suchen.c) nutzte zond_treeview_get_path()
 (zond_treeview.c) für den Lookup - ein gtk_tree_model_foreach() über den
 kompletten Baummodell-Inhalt, O(n) pro Aufruf. Genau dieses Muster hatten
 die Tasks #39-41 bereits überall sonst im Code durch zond_tree_store_get_
 iter_by_node_id() ersetzt (O(1) über eine intern mitgeführte Hashtabelle,
 s. Kommentar dort) - suchen.c war seit der #164-Einführung der einzige
 verbliebene Aufrufer der alten Funktion, offenbar übersehen, weil suchen.c
 zu dem Zeitpunkt neu und unabhängig von der damaligen Migration entstand.

 Umgestellt auf denselben Lookup wie überall sonst (zond_tree_store_get_
 iter_by_node_id() + sond_treeview_expand_to_row() + sond_treeview_set_
 cursor()). Dabei fiel eine zweite Vereinfachung ab: das bisherige manuelle
 Verbinden/Emittieren/Trennen von "cursor-changed" um den Cursor-Aufruf
 (nötig, weil das Ziel-Treeview zum Sprungzeitpunkt aus dem Suchfenster
 heraus meist keinen Fokus und damit keine permanente "cursor-changed"-
 Verbindung hat, s. cb_treeview_focus_in()/cb_treeview_focus_out(),
 app_window.c) entfällt ersatzlos: sond_treeview_set_cursor() (sond_
 treeview.c) ruft selbst am Ende gtk_widget_grab_focus() auf - das löst
 über cb_treeview_focus_in() automatisch genau denselben Mechanismus aus
 (Verbinden + einmaliges erzwungenes Emittieren von "cursor-changed"),
 nur über den offiziellen, fokus-basierten Pfad statt einer eigenen
 Kopie davon. Dieselbe Funktion setzt damit jetzt auch den Sprung zu
 BAUM_FS (suchen_springe_zu_baum_fs(), s.o.) und zu BAUM_INHALT/
 BAUM_AUSWERTUNG konsistent auf denselben, bereits etablierten Bausteinen
 auf.


 Klammer- und Blockkommentarbalance in suchen.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 #182 Nachfrage/Vereinfachung (22.09.2026): "Müßte das unselect nicht durch
 die focus-in/out-Callbacks erledigt werden?" - Nutzer stellt damit die eben
 (Nachträge zu #180/#181) hinzugefügten manuellen gtk_tree_selection_
 unselect_all()-Aufrufe in suchen_springe_zu_knoten() und suchen_springe_
 zu_baum_fs() (suchen.c) in Frage.

 Erste Antwort (widerlegt, s.u.): vermutet, cb_treeview_focus_in()
 (app_window.c) erledige das bereits selbst (Vergleich baum_active/
 baum_prev + unselect_all(selection[baum_prev]) + Reselect), da sond_
 treeview_set_cursor() intern gtk_widget_grab_focus() aufruft. Die beiden
 unselect_all()-Aufrufpaare in suchen_springe_zu_knoten() und suchen_
 springe_zu_baum_fs() (suchen.c) daraufhin entfernt.

 Nutzer-Test widerlegt das ("Kein unselect"): die Markierung blieb nach
 Entfernen tatsächlich stehen. Ursache der falschen Annahme: gtk_widget_
 grab_focus() setzt zwar den internen Fokus-Widget von app_window (via
 gtk_window_set_focus()), löst "focus-in-event" bei GTK3 aber nur dann
 synchron aus, wenn app_window auch die echte Fenstermanager-/GDK-Fokus
 hat. Das Ergebnisfenster ist ein eigenständiges Top-Level-Fenster und hält
 diese zum Zeitpunkt des Klicks - app_window bekommt sie dadurch nicht
 automatisch zurück, cb_treeview_focus_in() feuert also nicht zuverlässig.
 Innerhalb desselben Fensters (z.B. zond_treeview_jump_to_origin(), von
 BAUM_INHALT/BAUM_AUSWERTUNG selbst ausgelöst) besteht dieses Problem nicht,
 da app_window dort ohnehin schon fokussiert ist - deshalb kommt dieser
 Fall ohne eigenes unselect_all aus, der Sprung aus dem separaten
 Suchergebnisfenster aber nicht.

 Beide unselect_all()-Aufrufpaare wieder eingebaut, Kommentare entsprechend
 korrigiert (Grund: fensterübergreifender Fokuswechsel, nicht redundant).


 Klammer- und Blockkommentarbalance in suchen.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 #183 Folgebug zu #182 (22.09.2026): "Wenn man in BAUM_FS gesprungen ist und
 springt in BAUM_INHALT, wird Markierung BAUM_FS nicht gelöscht."

 suchen_springe_zu_knoten() (suchen.c) räumte beim Wiedereinbau der
 unselect_all()-Aufrufe (#182-Nachtrag) nur BAUM_INHALT und BAUM_AUSWERTUNG
 auf - BAUM_FS fehlte, weil es an dieser Stelle nie Sprungziel sein kann
 (eigener Guard weiter oben in derselben Funktion) und deshalb beim
 Kopieren der Aufrufpaare aus suchen_springe_zu_baum_fs() übersehen wurde.
 Als Sprungherkunft (Markierung von einem vorherigen BAUM_FS-Sprung) kommt
 es aber sehr wohl in Betracht.

 gtk_tree_selection_unselect_all(zond->selection[BAUM_FS]) ergänzt.

 Klammer- und Blockkommentarbalance in suchen.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 #184 Nachfrage (23.09.2026): "Aber wie kann ich programmatisch ein focus-
 in-event auslösen, so daß der Callback feuert." - im Anschluß an die #182-
 Erkenntnis, daß grab_focus() beim Sprung aus dem separaten Ergebnisfenster
 kein echtes focus-in-event auf app_window auslöst (Fenstermanager-Fokus
 hat zu dem Zeitpunkt das Ergebnisfenster), was denselben Zweifel auch für
 das im #181-Nachtrag entfernte manuelle "cursor-changed"-Connect/Emit
 (Label/Textview-Update) aufwirft.

 Ein künstlich per g_signal_emit_by_name(treeview, "focus-in-event", ...)
 erzeugtes Event würde zwar cb_treeview_focus_in() (app_window.c) ausführen,
 aber GTKs eigene interne Fokus-Buchhaltung (focus_widget-Zeiger des
 GtkWindow, HAS_FOCUS-Flag) bliebe unberührt, da diese normalerweise erst
 durch das reale WM-Fokusereignis auf dem Top-Level-Fenster aktualisiert
 wird - Risiko von späterem Auseinanderlaufen zwischen zond->baum_active/
 baum_prev und GTKs tatsächlichem Fokuszustand bei einem nachfolgenden
 echten Fokuswechsel.

 Sauberer: die eigentlich benötigte Wirkung direkt aufrufen statt über ein
 Fokus-Event umzuleiten. zond_treeview_cursor_changed(ZondTreeview*,
 gpointer) (zond_treeview.c) ist bereits eine gewöhnliche öffentliche
 Funktion (im Header deklariert, kein reines Callback-Interna) und bricht
 bei bereits aktuellem Knoten selbst ab (node_id == zond->node_id_act) -
 ein zusätzlicher Aufruf ist also auch dann harmlos, wenn "cursor-changed"
 zufällig schon verbunden war und den Callback ohnehin ausgelöst hat.

 In suchen_springe_zu_knoten() (suchen.c) direkt nach sond_treeview_set_
 cursor() ergänzt: zond_treeview_cursor_changed(ZOND_TREEVIEW(zond->
 treeview[baum]), zond). Betrifft nur BAUM_INHALT/BAUM_AUSWERTUNG (BAUM_FS
 nutzt diesen Mechanismus ohnehin nicht, s. Guard in cb_treeview_focus_in()
 - "baum_active != BAUM_FS").

 Klammer- und Blockkommentarbalance in suchen.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 #185 Bug (23.09.2026, Nutzer-Fund, Indexsuche statt Popup-Suche - erst auf
 Nachfrage geklärt): "Wenn ich im Ergebnisfenster eine Zeile anklicke,
 öffnet sich - wie gewünscht - die Datei mit dem Treffer. Dazu wird aber
 außerdem der BAUM_FS eingeblendet und das Verzeichnis, in dem sich die
 angezeigte Datei befindet, geöffnet."

 zond_indexsuche_row_activated() (zond_indexsuche.c) schaltete zond->
 fs_button zwangsweise ein und rief danach sond_treeviewfm_file_part_
 visible(..., open=TRUE, ...) auf, nur um an das SondFilePart-Objekt der
 Datei zu kommen (Baum-Walk in BAUM_FS) - dafür musste BAUM_FS sichtbar und
 der Pfad darin aufgeklappt werden, obwohl das eigentliche Ziel nur "Viewer
 mit Treffer öffnen" war, nicht "zeig mir das in BAUM_FS".

 Nutzer-Nachfrage: "Warum nicht über sond_file_part_get_filepart()" -
 gemeint war die Gegenrichtung, sond_file_part_from_filepart() (String ->
 SondFilePart, sond_fileparts.h/.c), die dieselbe Objekt-Kette direkt aus
 dem file_part-String aufbaut. sond_file_part_create() darin prüft zuerst
 sond_file_part_is_open() (globale Registry bereits offener Objekte) und
 liefert bei Treffer dasselbe Objekt wie der Baum-Walk - keine
 "billigere Kopie", nur ganz ohne BAUM_FS anzufassen.

 Fix: Block "FS-Ansicht einschalten falls nötig" + sond_treeviewfm_file_
 part_visible()/SondTVFMItem-Umweg ersetzt durch direktes
 sond_file_part_from_filepart(filename, &error) (g_autoptr, da die
 Funktion anders als die vorherige geliehene Referenz eine neue Referenz
 liefert - automatische Freigabe deckt auch die beiden vorhandenen
 Early-Returns bei "Seite gelöscht" bzw. fehlgeschlagenem
 DisplayedDocument ab). BAUM_FS/fs_button werden dadurch beim Öffnen eines
 Indexsuche-Treffers nicht mehr angefasst.

 Klammer-/Kommentarbalance in zond_indexsuche.c programmatisch geprüft -
 ausgeglichen. Nicht durch Kompilieren/Testen verifiziert.

 #186 Widget "Herkunft und Verwendung" (29.09.2026): zu einem beliebigen
 Knoten Ursprung und alle Ableitungen anzeigen. Grundlage für die spätere
 Umstellung des #164-Ergebnisfensters (ersetzt dort Phase 7).

 Modell: Ursprung ist ein STRUKT oder eine Wurzel-FILE_PART (parent_ID=0).
 FILE_PART-Knoten bilden einen eigenen Baum (Datei -> Sections). Auf jeden
 FILE_PART zeigen 0-1 Anker (Anbindung), 0-n Copies, 0-n Links; auf Copies
 und STRUKTs 0-n Links. In BAUM_INHALT trägt die Anbindungszeile die
 FILE_PART-ID, nicht die Anker-ID - Copies und Links zeigen daher auf den
 FILE_PART (Altbestand evtl. auf den Anker, wird mitgelesen).

 Anzeige: Baum vollständig, aufgeklappt nur bis zum Ausgangsknoten (fett).
 Zeilen mit Kindern zeigen Zählung ("2 Sections · 3 Copies"). Angebundene
 Container (x.zip bei x.zip//a.pdf) als "Enthalten in" oberhalb.
 Doppelklick springt in den Baum (Links: zum Elternknoten des Links).
 Später Treffer im Ergebnisfenster: Pfad bis zu den Treffern aufklappen,
 reiner Dateinamen-Treffer = zugeklappte Wurzelzeile.

 Stand:
 1) erledigt: zond_dbase_get_referrers() (zond_dbase.c).
 2) erledigt: 20allgemein/verwendung.c/.h, verwendung_anzeigen().
 3) erledigt: suchen_springe_zu_knoten()/_zu_baum_fs() öffentlich
    (suchen.h), von verwendung.c mitbenutzt.
 4) erledigt: Menüpunkt "Herkunft und Verwendung" (Strg+Umschalt+J) nur
    im Hauptmenü neben "Zu Ursprung springen" (Nutzer-Entscheidung, keine
    Kontextmenüs), cb_win_verwendung() in headerbar.c. Wirkt auf den
    Cursor im aktiven Baum; BAUM_FS übergibt file_part/section (node_id 0),
    Verzeichnisse: Hinweis "nicht verfügbar".
 5) erledigt: Sprung direkt auf die Kopfzeile eines Links.
    zond_tree_store_get_iter_link() sucht in der links-Liste der Zielzeile
    den Eintrag mit head_nr == Link-ID (Kopfzeile eindeutig, gespiegelte
    Zeilen haben head_nr 0). Link-Zeilen teilen sich die Daten mit dem Ziel
    (Spalte 2 = Ziel-ID), ht_node_id kennt nur Ursprungsknoten - daher
    findet get_iter_by_node_id() Links nicht. suchen_springe_zu_iter()
    ausgelagert, suchen_springe_zu_knoten() nutzt es. Aufruf auf einer
    Kopfzeile übergibt die Link-ID: Ursprung ist das Ziel, der Link wird
    markiert.
 6) erledigt: indirekte Fundstellen, Stufe 1 - Knoten X ist mit sichtbar,
    wo ein Link auf einen Vorfahren A von X steht (der Baum spiegelt den
    Teilbaum von A unter dem Link). verwendung_vorfahren() liefert die
    angezeigte Vorfahrenkette (Section: übergeordnete Sections bis zur
    angebundenen, dann Eltern des Ankers; Anker als FILE_PART-ID plus
    Anker-ID für Altbestand), je Vorfahr zond_dbase_get_referrers(LINK).
    Anzeige als zugeklappte, kursive Gruppe "indirekt sichtbar" unter
    Anbindung/Copy/Strukturpunkt. Sprung: Kopfzeile des Links, dann per
    verwendung_abstieg() durch die gespiegelten Zeilen (nachladen,
    Spalte 2 vergleichen) bis zu X. Zyklenfrei, da keine Rekursion in
    Link-Ziele.
    Nachtrag: bei Sections wird für eine angebundene übergeordnete Section
    zusätzlich die Anker-ID abgefragt (Altbestand).
    Punkte 1-6 vom Nutzer mit Test.ZND getestet (30.09.2026).
 7) erledigt (01.10.2026): indirekte Fundstellen, Stufe 2 - X ist auch dort sichtbar, wo
    die Stelle eines Links L (aus Stufe 1) ihrerseits über einen Link L2
    auf einen Vorfahren von L gespiegelt wird (L2 -> B -> ... -> L -> A ->
    ... -> X). Breitensuche: Warteschlange der Positionen (Start X), für
    jede Position deren Vorfahren und die Links darauf sammeln, jeden Link
    genau einmal weiterverfolgen (Menge "besucht") - sonst Endlosschleife,
    wenn ein Link auf einen eigenen Vorfahren zeigt (Baum spiegelt dann
    endlos, nur durch lazy loading nicht sichtbar). Anzeige als Kette
    "über L2 auf B > L auf A"; Sprung: verwendung_abstieg() je Glied der
    Kette hintereinander (Kopfzeile L2, abwärts bis zur Spiegelung von L,
    weiter abwärts bis X). Erst angehen, wenn Stufe 1 im Alltag nicht
    reicht. Testdaten: C:\Users\nc-kr\laufende Akten\Test\Test.ZND
    (Anlagenband -> Link Schriftsatz -> Link Gliederung -> Link
    Beweisthema 1 -> Zeuge Müller) und Test_Zyklus.ZND (zusätzlich Link
    von Beweisthema 1 auf Gliederung).
    Umsetzung: verwendung_add_indirekt() als Breitensuche über Positionen
    (IndirektPos: Knoten oder Link, Abstiegsrest, Beschriftung, Links des
    Weges). Zyklenschutz je Weg (jeder Link nur einmal), dazu Grenzen
    VERWENDUNG_MAX_KETTE (8 Links je Weg) und VERWENDUNG_MAX_INDIREKT (200
    Fundstellen, danach Hinweiszeile). Eine gespiegelte Link-Zeile trägt in
    Spalte 2 die ID ihres Ziels - der Abstieg setzt sich daher aus den
    Teilwegen der Glieder zusammen (verwendung_link_anzeige_id()).
    Beschriftung von außen nach innen, Glieder mit " › " getrennt.

 #187 Bug (30.09.2026, Nutzer-Fund mit Test.ZND): Schriftsatz -> Link
 Gliederung -> gespiegelter Link Beweisthema 1 zeigte als Kinder Zeuge
 Müller, a.pdf (Kopie) UND noch einmal Beweisthema 1.

 Ursache (zond_tree_store.c): Ein gespiegelter Link M zeigt auf die
 Kopfzeile L des Links, nicht auf den Ursprung. Wurde M aufgeklappt,
 solange L selbst noch nicht geladen war (nur Dummy), fügte der
 Dummy-Zweig in zond_tree_store_load_node() einen Link auf das aufgelöste
 Ziel als einziges Kind ein - das Ziel erschien als eigenes Kind. Beim
 späteren Laden von L wurden dessen Kinder zusätzlich an M weitergereicht.
 Zweiter Fehler: zond_tree_store_insert_linked_nodes() reichte neue Kinder
 auch an gespiegelte Zeilen weiter, die noch nicht geladen waren - danach
 standen echte Kinder neben dem Dummy, die Zeile galt als geladen und
 wurde nie vollständig geladen (Links darunter fehlten, Dummy sichtbar).

 Fix: zond_tree_store_ensure_loaded() lädt eine noch nicht geladene
 Link-Zeile, bevor ihre Kinder gespiegelt werden (in load_link für das
 Ziel, in load_node vor der Rekursion in ein Kind). insert_linked_nodes()
 überspringt noch nicht geladene Zeilen. Der Dummy-Zweig in load_node ist
 damit unerreichbar und gibt nur noch eine Warnung aus.

 Testen mit Test.ZND/Test_Zyklus.ZND: gespiegelten Link zuerst, dann den
 echten aufklappen und umgekehrt; in Test_Zyklus tief verschachteln; neuen
 Punkt unter Beweisthema 1 anlegen - muß an jeder Spiegelstelle genau
 einmal erscheinen. Alle Tests vom Nutzer bestanden (30.09.2026).

 #188 Ergebnisfenster der Baumsuche neu auf Basis von #186 (ersetzt die
 Darstellung aus #164 samt der zurückgestellten Section-Aggregation).
 Ziel: Liste der Ursprünge (Datei oder Strukturpunkt), je Ursprung der
 vollständige Baum wie im Widget "Herkunft und Verwendung"; aufgeklappt
 nur der Pfad bis zu den Treffern, Treffer fett. Reiner Treffer im
 Dateipfad: nur die Wurzelzeile (zugeklappt, mit Zählung). Mehrere
 Treffer im selben Ursprung: eine Wurzel. Nutzer-Entscheidungen: indirekte
 Fundstellen mit anzeigen (zugeklappt); Baum sofort komplett aufbauen,
 erst bei Bedarf auf Laden beim Aufklappen umstellen.

 Schritte:
 1) erledigt: verwendung.c getrennt in Baumaufbau (Verwendung = Projekt +
    Store) und Fenster (VerwendungFenster). Öffentlich:
    verwendung_store_new(), verwendung_ursprung_ermitteln(),
    verwendung_store_add_ursprung(), verwendung_store_zaehlen(),
    verwendung_treeview_new() (Spalten, Tooltip, Sprung),
    verwendung_hervorheben() mit Treffermenge und Trefferart
    (VERWENDUNG_TREFFER_PFAD = Datei-/Section-Zeile, _TEXT = übrige
    Zeilen, _ALLE), verwendung_zeile_knoten() (Knoten einer Zeile, für
    "In Baum Auswertung kopieren").
 2) erledigt: suchen.c - Rohtreffer aus suchen_db() (zond_suchen 0 = file_part,
    1 = node_text, 2 = text) per verwendung_ursprung_ermitteln() nach
    Ursprung gruppieren; IDs 0-2 (Wurzeln, Versionsknoten mit node_text
    "1") ausfiltern. Pfadtreffer auf Sections derselben Datei fallen in
    die Wurzel.
 3) erledigt: Treffer hervorheben - zond_suchen 0 -> PFAD, sonst TEXT (bei
    FILE_PART also die Anbindungszeile).
 4) erledigt: Ergebnisfenster in suchen.c ersetzen - ein Baum
    (verwendung_treeview_new), oberste Zeilen = Ursprünge, Mehrfachauswahl;
    Kontextmenü "In Baum Auswertung kopieren" (Gleiche Ebene/Unterpunkt)
    beibehalten, über verwendung_zeile_knoten(). Danach ResultRow,
    ResultCopy, SuchenItem, suchen_resolve_file_part_node(),
    suchen_baue_row(), suchen_aggregieren(), suchen_fuellen_row_*() und
    die Listbox-Variante entfernen.
 Umsetzung 2-4 (01.10.2026): suchen_gruppieren() filtert IDs 0-2 und
 Knoten ohne eigenen Text (Anker, Links), gruppiert per
 verwendung_ursprung_ermitteln(); Pfadtreffer markieren die Wurzel-Datei,
 doppelte Markierungen (Pfadtreffer auf allen Sections) werden
 zusammengefasst. suchen_ergebnisfenster(): ein Baum mit Mehrfachauswahl,
 Titel mit Suchbegriff, Untertitel mit Anzahl Fundstellen/Ursprünge;
 Kontextmenü per Rechtsklick und am Button der Titelleiste. Kopieren nur
 für Zeilen mit eigenem Knoten (Anbindung, Copy, Strukturpunkt), Datei-,
 Section- und Link-Zeilen werden übersprungen. Neu: Meldung "Keine
 Treffer" (vorher geschah nichts). Alter Listbox-Code entfernt (suchen.c
 von 1173 auf gut 600 Zeilen). result_listbox_new() (misc.c) hat damit
 keinen Aufrufer mehr.

 #189 Fokus-Verwaltung der Bäume neu (01.10.2026). Vorher: baum_active
 wurde bei focus-in gesetzt und bei focus-out auf KEIN_BAUM zurückgesetzt,
 baum_prev bei focus-out gesetzt. KEIN_BAUM stand damit gleichzeitig für
 "Fokus im Textfeld", "Fokus im Menü" und "anderes Fenster aktiv"; daher
 der Behelf zond_baum_mit_auswahl() (Suche nach Markierungen) und
 unzuverlässige Hauptmenü-Aktionen (focus-out kam je nach Reihenfolge vor
 oder nach der Aktion). Zudem Zugriff auf treeview[-1] beim focus-out des
 Textfelds (app_window.c).

 Neu: zond_baum_aktuell() (app_window.c) fragt im Moment der Aktion
 gtk_window_get_focus(app_window) - GtkWindow behält sein Fokus-Widget,
 auch wenn ein anderes Fenster aktiv ist. Fokus auf einem Baum -> dieser
 Baum; Fokus in einem Menü-Popover (nur GTK4-Menüleiste; die GTK3-
 GtkMenuBar lässt den Fokus im Fenster unverändert) -> baum_zuletzt;
 sonst (Textfeld, Suchfeld, Umbenennen im Baum) -> KEIN_BAUM. Einzige
 Buchführung: baum_zuletzt, nur in cb_treeview_focus_in gesetzt.
 focus-out schaltet nur noch Umbenennen und das cursor-changed-Signal ab.
 baum_active, baum_prev und zond_baum_mit_auswahl() entfallen; export.c
 und der Pin-Button nutzen baum_zuletzt. Kopieren aus der Baumsuche:
 zond_baum_aktuell() == BAUM_AUSWERTUNG und dort eine Markierung.

 Zu testen: Tastenkürzel in allen drei Bäumen (Strg+C/X/V, Strg+L,
 Strg+J, Strg+Umschalt+J); Kontextmenüs; Hauptmenü-Punkte, die einen Baum
 brauchen (Bearbeiten, Aufklappen/Zuklappen, Index Auswahl, SeaDrive
 Auswahl, Export, XJustiz-Import); Strg+C/V im Textfeld und beim
 Umbenennen im Baum (darf nicht den Baum betreffen); Wechsel zwischen
 Bäumen hebt die Markierung des vorherigen auf; Kopieren aus dem
 Ergebnisfenster der Suche.

 #190 Bug (01.10.2026, Nutzer-Fund): Copy auf oberster Ebene von
 BAUM_AUSWERTUNG öffnet nicht (keine Meldung). Ursache:
 zond_treeview_determine_iter_parent() liefert bei einer Copy deren
 Elternknoten als Grundlage des Auszugs - auf oberster Ebene gibt es
 keinen (Wurzel unsichtbar), dann den Klick-Knoten selbst
 ("Notlösung"); open_auszug() sammelt dessen Kinder, der Auszug ist leer.
 Gleiches gilt, wenn die Link-Kette im "else"-Zweig bis ganz oben
 klettert. Fix in zond_treeview_open_node(): ist iter_parent der
 Klick-Knoten selbst und trägt das Ziel eigenen Inhalt (file_part),
 Einzelansicht wie bei Strg (single_view) - inkl. Einzeldatei-
 Hydrierungscheck. Ein direkt angeklickter Strukturpunkt (kein
 file_part) läuft unverändert über den Auszug seiner Kinder.
 Vom Nutzer getestet (01.10.2026).

 #191 umgesetzt (01.10.2026), vom Nutzer getestet (03.10.2026). Index erstellen (Auswahl) für
 einen PDF-Filepart aus BAUM_INHALT/BAUM_AUSWERTUNG indiziert die ganze
 Datei samt eingebetteter Dateien - gemeint ist nur der Pagetree
 (Einbettungen sind eigene Fileparts "x.pdf//anhang.pdf"). Ursache:
 process_pdf_for_ocr() (sond_process_file.c) durchläuft per
 pdf_walk_embedded_files() IMMER alle Einbettungen, auch bei
 Seitenbereich. Gleicher Fehler beim "PageTree"-Knoten in BAUM_FS
 (zond_treeviewfm_item_get_fileparts(): range NULL = ganze Datei).
 Coverage: "x.pdf" bedeutet derzeit "Seiten und alle Einbettungen"
 (Ahnen-Walk in coverage_get()), darf also bei reinem Seitenlauf nicht
 gesetzt werden. Plan: Flag pdf_pagetree_only in SondPageRange (analog
 gmessage_header_only), Einbettungen nur bei ganzer Datei verarbeiten,
 eigener Coverage-Schlüssel (umgesetzt als "x.pdf//", s.u.; Chunks/pages bleiben
 unter "x.pdf"; ohne Einbettungen gleich "x.pdf" markieren),
 clean_hashtable() Kinder dann nicht entfernen, Index löschen und
 Badges/check_coverage_one() anpassen. Später ggf. Collapse
 pagetree + alle Einbettungen -> "x.pdf".

 Semantik von "x.pdf" bei Indizierung/Coverage (Nutzerentscheidung):
 BAUM_FS DIR (PDF mit Einbettungen), Ordner, "Gesamtes Projekt": Seiten
 und alle Einbettungen. BAUM_FS LEAF "PageTree": nur Seiten. BAUM_FS LEAF
 ohne Einbettungen: beides dasselbe. BAUM_INHALT/_AUSWERTUNG: Anbindung
 "x.pdf" = nur Seiten (ein DIR wird beim Anbinden zum Strukturpunkt mit
 einzeln angebundenen Kindern), mit Section = Seitenbereich. Öffnen/
 "Öffnen mit" unverändert (ganze Datei). Schlüssel "x.pdf//" statt
 "x.pdf//pagetree" (keine Kollision möglich, entspricht dem PageTree-
 Marker "//" in BAUM_FS); E-Mail behält "y.eml//header".

 Umsetzung:
 - SondPageRange.pdf_pagetree_only, sond_page_range_new_pdf_pagetree(),
   sond_page_range_copy() (sond_process_file.c/.h). Die Kopie behebt
   nebenbei, dass "Fehlende nachindizieren" (zond_indexsuche.c) das Flag
   gmessage_header_only verlor.
 - Sammeln: zond_treeviewfm_item_get_fileparts() (PageTree-Marker, neuer
   Getter sond_tvfm_item_is_content_root_marker()) und
   zond_treeview_get_selected_fileparts_foreach() (PDF ohne Section).
   Vereinigung: ganze Datei vor "nur Seiten" vor Seitenbereich.
 - sond_process_file_do_rec()/process_pdf_for_ocr(): Einbettungen nur bei
   ganzer Datei verarbeiten, sonst nur zählen; ohne Einbettungen gilt
   "nur Seiten" als ganze Datei (dann "x.pdf" markiert).
 - sond_index(): neuer Parameter pdf_pagetree_only - Chunks/pages/
   file_pagecount unter "x.pdf", coverage_mark auf "x.pdf//". Die
   pages-Zeilen bleiben dabei stehen.
 - sond_process_fileparts(): coverage_key "x.pdf//" für "nur Seiten" und
   Seitenbereich (Vorab-Check findet über den Ahnen-Walk auch "x.pdf");
   Zusammenfassen nach oben von "x.pdf" aus. clean_hashtable() entfernt
   Kinder nur noch bei "ganze Datei" (betraf vorher auch "nur Header").
 - sond_index_ctx_coverage_invalidate(): entwertet "path//" mit; beim
   Auflösen einer ganz abgedeckten PDF wegen einer Einbettung wird "x.pdf//"
   neu eingetragen (Seiten bleiben gültig). Einschränkung: die übrigen
   Einbettungen verlieren ihre Abdeckung (ohne Dateizugriff nicht
   aufzählbar), ebenso beim Löschen nur der Seiten einer ganz abgedeckten
   PDF.
 - coverage_expand_to_pages() und get_file_status() berücksichtigen
   "x.pdf//" (Badges BAUM_FS/BAUM_INHALT).
 - Index löschen (headerbar.c): "nur Seiten" als Seitenbereich 0 bis
   Seitenzahl-1 (unbekannt: G_MAXINT) statt ganzer Datei.
 - Index durchsuchen (zond_indexsuche.c): check_coverage_one() akzeptiert
   "x.pdf//"; Trefferfilter lässt bei "nur Seiten"/Seitenbereich keine
   Treffer aus "x.pdf//..." mehr durch.
 - sond_server_repo_worker.c: nur zusätzliches FALSE beim Aufruf.

 Zu testen (PDF mit eingebetteter Datei nötig): Index erstellen für
 Anbindung x.pdf in BAUM_INHALT (Anhang darf nicht indiziert werden,
 Coverage "x.pdf//"), für PageTree in BAUM_FS, für die PDF selbst (DIR,
 alles); Anhang danach eigens indizieren; Badges; Index löschen für
 x.pdf/PageTree (Anhang bleibt); Index durchsuchen mit Auswahl x.pdf
 (keine Treffer aus dem Anhang); PDF ohne Einbettungen weiterhin "x.pdf".

 pdf_get_EF_F()) adressiert - der ist nicht eindeutig, nur der Schlüssel
 im EmbeddedFiles-Namensbaum ist es (PDF-Norm). Bei doppeltem Namen
 treffen load/lookup/modify/delete/rename (sond_fileparts.c) immer den
 ersten, sond_file_part_is_open() liefert für beide dasselbe Objekt,
 Indizierung legt beide unter demselben Pfad ab, Coverage und
 Anbindungen unterscheiden sie nicht.
 Schritt 1 (erledigt, vom Nutzer getestet): zond erzeugt keine Duplikate mehr.
 - sond_file_part_pdf_insert_embedded_file(): Name vorhanden ->
   G_IO_ERROR_EXISTS, die Suffix-Logik beim Einfügen/Verschieben
   (sond_treeviewfm.c) versucht dann "name (1)" usw.
 - Einfügen/Umbenennen in ZIP und Umbenennen in PDF melden ebenfalls
   G_IO_ERROR_EXISTS statt SOND_ERROR (ZIP-Einfügen lief vorher trotz
   Prüfung nicht in die Suffix-Logik, sondern brach mit Fehler ab).
 - pdf_insert_emb_file() (sond_pdf_helper.c): eindeutiger Schlüssel im
   Namensbaum (nach Umbenennung - ändert nur /F und /UF - konnte der
   Dateiname als Schlüssel schon belegt sein).
 Zu testen: dieselbe Datei zweimal in eine PDF bzw. ein ZIP einfügen ->
 "name (1).ext"; Datei in PDF umbenennen, dann gleichnamige neue einfügen.
 Schritt 2 (umgesetzt 02.10.2026, vom Nutzer getestet) - Adressierung über den
 Namensbaum, Nutzerentscheidung "Schlüssel folgt dem Dateinamen":
 - Adresse (sond_pdf_helper.c, pdf_emb_addresses_new()): der Dateiname,
   wenn kein anderer Eintrag denselben Dateinamen hat und keiner ihn als
   Schlüssel trägt, sonst der Schlüssel; UTF-8, "%" -> "%25", "/" -> "%2F"
   (pdf_emb_escape()). Für eindeutige Namen also wie bisher -> keine
   Migration. Angezeigt wird in BAUM_FS immer der Dateiname.
 - pdf_emb_normalize_keys(), aufgerufen in pdf_doc_to_buf() bei jedem
   Schreiben: setzt nach derselben Regel den Schlüssel auf den Dateinamen
   und baut den Namensbaum flach/sortiert neu. Die Adresse ändert sich
   dadurch nicht.
 - Einfügen: /F und /UF als Textstrings, Schlüssel = Dateiname; Name muss
   als Dateiname und als Schlüssel frei sein (sonst G_IO_ERROR_EXISTS).
 - Umbenennen: setzt /F, /UF; den Schlüssel gleicht das Schreiben an. Die
   Adresse folgt dem Namen wie bisher (before_move schreibt die
   gespeicherten Fileparts um, jetzt mit pdf_emb_escape()).
 - load/lookup/modify/delete/rename (sond_fileparts.c) und
   process_emb_file() (Index-Pfad) arbeiten mit der Adresse; Ersatzsuche
   über den Dateinamen (erster Treffer), wenn eine gespeicherte Adresse
   nicht gefunden wird (Projekt-DB ohne Migration, Nutzerentscheidung;
   Index-DB: keine Migration, Testphase).
 - Umlaut-Verdacht (rohes UTF-8 in /F) widerlegt: MuPDF liest gültiges
   UTF-8 ohne BOM korrekt.
 Geschwister nachführen (umgesetzt 02.10.2026, vom Nutzer getestet): Löschen,
 Umbenennen oder Herausverschieben eines von zwei gleichnamigen Anhängen
 macht den Namen des anderen eindeutig - dessen Adresse wechselt vom
 Schlüssel zum Dateinamen. pdf_emb_address_changes() (sond_pdf_helper.c)
 simuliert den Zustand danach über dieselbe Regel und liefert die
 geänderten Adressen; zond_treeviewfm_before_delete()/_before_move()
 schreiben damit (emb_sibling_apply()) Projekt-DB (main/work) und
 Index-DB um, in den offenen Transaktionen, nach dem Element selbst.
 before_delete schreibt für Anhänge deshalb immer in beide Datenbanken.
 Nach erfolgreichem Commit setzt after() geöffnete SondFilePart-Objekte
 auf die neue Adresse (emb_sibling_adjust_opened()). Logik mit
 emb_test.pdf (Testordner) außerhalb von zond geprüft.
 Dabei behoben: dbase_zond_update_path() (project.c) hatte kein WHERE und
 traf jeden Pfad, der nur mit dem alten beginnt ("Akte" -> auch
 "Akte_alt"); sond_index_ctx_rename_file() benutzte LIKE ("%" und "_" in
 Pfaden sind dort Platzhalter) und erfasste in chunks/pages/... nur "//",
 nicht "/" - beim Umbenennen eines Ordners blieben die Index-Einträge der
 Dateien darin unter dem alten Pfad. Beide jetzt: Pfad gleich oder
 beginnt mit alt + "/" (per SUBSTR).
 Test-PDF: Testordner/emb_test.pdf - Schlüssel anhang_1/anhang_2 (beide
 bild.txt), a/b%c und notiz-2 (beide notiz.txt), fremder_schluessel
 (einzeln.txt); Suchwörter Bildeins, Bildzwei, Notizalpha, Notizbeta,
 Einzeln, Seitentext.
 Zu testen: fremde PDF mit zwei gleichnamigen Anhängen (beide in BAUM_FS
 öffnen, anbinden, indizieren); Anhang umbenennen und die Anbindung
 öffnen; Anhang mit "%" oder "/" im Schlüssel; bestehende Anbindungen auf
 Anhänge weiterhin öffnen; OCR einer PDF mit Anhängen.

 #194 umgesetzt (03.10.2026), vom Nutzer getestet - Befund: Mimepart-Nummern einer E-Mail werden in der
 Index-DB nicht nachgeführt. Beim Löschen/Verschieben eines Mimeparts
 zählt dbase_zond_update_gmessage_index() (project.c) die gespeicherten
 Fileparts in der zond-DB (main/work) um, die Index-DB (.sond_index.db:
 chunks, pages, coverage, file_pagecount, container_entrycount) aber
 nicht - zond_treeviewfm_before_delete()/_before_move() rufen dort nur
 clear_file/coverage_clear bzw. rename_file für den betroffenen Pfad
 selbst auf. Nach Löschen von "x.eml//2" stehen Chunks/Coverage des
 bisherigen "//3" weiter unter "//3", obwohl der Teil jetzt "//2" ist:
 Suchtreffer zeigen auf den falschen Mimepart, Badges stimmen nicht,
 container_entrycount ist veraltet. Fix: Gegenstück zu
 dbase_zond_update_gmessage_index() für die Index-DB (alle Tabellen mit
 Pfad), in derselben Transaktion aufrufen.
 Umsetzung:
 - sond_index_ctx_update_gmessage_index() (sond_index.c): chunks, pages,
   file_pagecount, container_entrycount, gmessage_inline, coverage. Zwei
   Durchgänge über vorläufige Werte ("-N"), sonst verletzt das Hochzählen
   die eindeutigen Schlüssel. Nur rein numerische Segmente ("header"
   bleibt), SUBSTR statt LIKE. Per sqlite3 an einer Testtabelle geprüft.
 - sond_index_ctx_clear_gmessage_structure(): container_entrycount und
   gmessage_inline der Mail werden nach jeder Änderung an ihren Mimeparts
   verworfen und bei der nächsten Indizierung neu ermittelt (Art eines
   hineinverschobenen Teils - inline/Anhang - ist ohne die Mail nicht
   bekannt). Bis dahin: Badge der angebundenen Mail nur nach Header,
   GMessage-bewusstes Zusammenfassen der Coverage setzt aus.
 - zond_treeviewfm_before_delete()/_before_move(): Aufruf neben
   dbase_zond_update_gmessage_index() (gmessage_index_renumber()); beim
   Hineinverschieben zusätzlich "x.eml//alpha" -> Zielindex in der
   Index-DB (blieb vorher dauerhaft "alpha").
 - Dabei behoben: dbase_zond_update_gmessage_index() (project.c) zählte
   per CAST auch den Platzhalter "alpha" als 0 - beim Verschieben an
   Position 0 wurde er zu "//1", das anschließende "alpha" -> "0" fand
   nichts, zwei Anbindungen zeigten auf "//1". Jetzt nur numerische
   Segmente, SUBSTR statt LIKE.
 Zu testen: Mail mit mehreren Teilen ganz indizieren, mittleren Teil
 löschen -> Suchtreffer/Badges der folgenden Teile stimmen; Teil an
 Position 0 verschieben -> Anbindungen öffnen den richtigen Teil.

 #195 Bug (02.10.2026, bei #193 gefunden), behoben, vom Nutzer getestet: Umbenennen
 (F2) eines Mimeparts verschob ihn in den Datenbanken an Index 0.
 sond_treeviewfm_text_edited() sandte before_move mit index_to = 0;
 zond_treeviewfm_before_move() behandelte das wie ein Verschieben in die
 E-Mail: "//2" -> "//alpha", nachfolgende Indizes -1, dann ab 0 +1,
 "//alpha" -> "//0". Physisch ändert Umbenennen aber nur den Anzeigenamen
 (sond_file_part_gmessage_rename_file()). Folge: Anbindungen der Teile 0
 bis 2 zeigten auf falsche Teile, in der Index-DB blieb der Teil unter
 "x.eml//alpha" stehen.
 Fix: text_edited sendet index_to = -1 (Umbenennen an Ort und Stelle);
 before_move ändert dann bei Mimeparts (rename_in_gmessage) keine Pfade,
 Indizes oder Coverage, Transaktionen/Kontext für after bleiben gleich.
 Zu testen: Mimepart einer E-Mail mit mehreren Anhängen umbenennen,
 Anbindungen der Anhänge danach öffnen; Verschieben innerhalb der E-Mail
 weiterhin korrekt.

 #196 Bug (02.10.2026, Nutzer-Fund), behoben, vom Nutzer getestet: Nach Umbenennen
 eines Ordners in BAUM_FS ließen sich darin bereits geöffnete Dateien
 bis zum Neustart nicht mehr öffnen (die Datenbanken waren korrekt
 umgeschrieben). sond_treeviewfm_text_edited() rief adjust_sfps_in_dir()
 nach sond_tvfm_item_rename() mit path_or_section als altem Pfad auf - der
 war da schon der neue - und mit dem bloßen Namen als neuem Pfad. Die
 interned SondFilePart-Objekte blieben auf dem alten Pfad. Fix: alten Pfad
 vorher sichern, danach mit altem und neuem vollem Pfad nachführen.
 adjust_sfps_in_dir() trifft jetzt nur Pfade unterhalb ("alt/..."): vorher
 auch "sub38/..." für "sub" bzw. "0/10/..." für Mimepart-Verzeichnis
 "0/1".

 #197 umgesetzt (03.10.2026), vom Nutzer getestet: E-Mail in BAUM_INHALT/
 _AUSWERTUNG - Gegenstück zu #191. Indizieren einer Anbindung "x.eml"
 verarbeitete die ganze Mail samt Anhängen.
 Semantik (Nutzerentscheidung, Variante (i)): in jedem Baum steht ein
 Knoten für das, was er dort darstellt.
 - BAUM_FS "Message": nur Header ("x.eml//header"); die Mimeparts sind
   dort eigene Knoten mit eigenem Badge. Unverändert.
 - BAUM_INHALT/_AUSWERTUNG "x.eml" (angebundener "Message"-Knoten):
   Header + Inline-Teile = das, was der Renderer zeigt. Inline = jeder
   Mimepart ohne Content-Disposition "attachment" (auch verschachtelt);
   nicht indizierbare (Bilder) zählen nicht mit. Badge: alle Bestandteile
   indiziert -> grün, einige -> gemischt.
 Praktisch jede Mail hat in BAUM_FS den "Message"-Knoten (has_children =
 Mail hat überhaupt einen Mimepart), "x.eml" in BAUM_INHALT ist also
 immer dieser Knoten.
 Umsetzung:
 - Index-DB: neue Tabelle gmessage_inline (Mail -> Pfade ihrer
   Inline-Teile), befüllt bei jeder Indizierung einer Mail (sond_index()),
   gelöscht mit container_entrycount, umbenannt in rename_file(). So
   kommen Badge und Index durchsuchen ohne Öffnen der Mail aus.
   sond_index_ctx_gmessage_message_parts()/_get_gmessage_message_status().
 - SondPageRange.gmessage_message, sond_page_range_new_gmessage_message();
   sond_process_file()/_do_rec() bekommen jetzt den SondPageRange selbst
   (NULL = ganze Datei) statt einzelner Schalter.
 - Verarbeitung: Header wie gmessage_header_only, dazu
   process_gmessage_for_ocr(inline_only) - Anhänge übersprungen. Kein
   eigener Coverage-Schlüssel; Vorab-Prüfung über alle Bestandteile.
 - Sammeln (zond_treeview.c), Index löschen (headerbar.c: Header +
   Inline-Teile einzeln), Index durchsuchen (zond_indexsuche.c:
   Abdeckung je Bestandteil, Trefferfilter nur Mail/Header/Inline),
   Badge BAUM_INHALT.
 - Lückenanzeige bei Index durchsuchen (format_gap_line()): Einheit
   "Teile - Header und Inline-Teile" statt "Seiten"; ZIP-Archive jetzt
   "Einträge" (zählten schon immer Einträge, hießen aber "Seiten").
 - Nebenbei: "Message"-Knoten in BAUM_FS (nur Header) wurde bei Index
   durchsuchen unter "x.eml" statt "x.eml//header" geprüft und ließ im
   Trefferfilter auch Anhang-Treffer durch - behoben.
 Einschränkungen: Bestandteile einer noch nie indizierten Mail sind
 unbekannt (Badge dann nur nach Header). Chunks einer Indizierung der
 ganzen Mail unter "x.eml" (Header + Textteile) bleiben bei Index löschen
 der Anbindung stehen. Angehängte Mails innerhalb einer Mail ("x.eml//2")
 werden mangels Endung nicht als Mail erkannt.
 Zu testen: Mail mit Text, HTML-Alternative, Inline-Bild und Anhang
 anbinden, in BAUM_INHALT indizieren -> in BAUM_FS Message + Text + HTML
 grün, Anhang nicht; einen Inline-Teil in BAUM_FS de-indizieren ->
 Anbindung "gemischt"; Index löschen der Anbindung; Index durchsuchen mit
 Auswahl der Anbindung (keine Treffer aus dem Anhang).

 #198 Bug (03.10.2026, Nutzer-Fund bei #197), behoben, vom Nutzer getestet:
 Umbenennen (F2) eines Mimeparts machte einen Inline-Teil zum Anhang.
 gmessage_set_filename() (sond_gmessage_helper.c) rief
 g_mime_part_set_filename() auf, das eine fehlende Content-Disposition als
 "attachment" anlegt, und legte sonst selbst eine neue (ebenfalls
 "attachment") an. Folge: Der Teil fiel aus "Header + Inline" (#197)
 heraus - nach Index löschen des Headers zeigte die Anbindung gar keinen
 Badge mehr; Mailprogramme zeigen ihn ggf. als Anhang statt im Text.
 Fix (Nutzerentscheidung: nichts anlegen): Name in "name" des Content-Type,
 "filename" in der Content-Disposition nur, wenn es eine gibt - deren Art
 bleibt. Mit GMime geprüft. Bereits so umbenannte Teile bleiben
 "attachment" (steht in der Datei).

 #199 umgesetzt (03.10.2026), vom Nutzer getestet: Coverage einer PDF auflösen und
 wieder zusammenfassen (Einschränkungen aus #191). Ist "x.pdf" als Ganzes
 abgedeckt und wird nur ein Teil entwertet (nur Seiten löschen, ein Anhang
 gelöscht/ersetzt), verloren die übrigen Anhänge ihre Abdeckung - ohne
 Öffnen der PDF waren sie nicht aufzählbar. Seiten und alle Anhänge
 einzeln indiziert wurden nie wieder zu "x.pdf" zusammengefasst.
 Umsetzung:
 - Index-DB: Tabelle pdf_embedded (PDF -> Adressen der Anhänge, "\n"-
   getrennt; pdf_emb_escape() kodiert jetzt auch Zeilenumbrüche). Gelöscht
   mit container_entrycount, umbenannt in rename_file().
 - Erfasst, sobald irgendein Teil verarbeitet wird (Nutzerwunsch, keine
   Grenze "erst nach Indizierung der ganzen PDF"): process_pdf_for_ocr()
   bei jedem Lauf auf die PDF (auch nur Seiten/Seitenbereich);
   sond_process_fileparts() für die Eltern-PDF/-Mail eines verarbeiteten
   Teils (sond_process_file_record_structure()). Für Mails ebenso
   container_entrycount/gmessage_inline (vorher nur bei Verarbeitung der
   Mail selbst) - sond_index_ctx_record_gmessage_structure().
 - Sofortiges Neueinlesen nach Änderungen durch zond (Nutzerentscheidung):
   before_delete/before_move/before_insert merken die betroffene PDF/Mail
   vor, after() bzw. das neue Signal "after-insert" (Kopieren, sond_
   treeviewfm.c - before-insert öffnet keine Transaktion, "after" passte
   nicht) lesen ihre Struktur nach Erfolg neu ein.
 - coverage_invalidate(): beim Auflösen von "x.pdf" werden Seiten und alle
   übrigen Anhänge aus pdf_embedded wieder eingetragen.
 - coverage_try_collapse(): Seiten ("x.pdf//") und alle Anhänge abgedeckt
   -> "x.pdf" (Mindestmodus); sond_process_fileparts() fasst bei "nur
   Seiten" jetzt vom Seiten-Eintrag aus zusammen.
 Zu testen (emb_test.pdf): ganze PDF indizieren, dann nur Seiten löschen ->
 Anhänge bleiben grün; einen Anhang löschen -> übrige bleiben grün; Seiten
 und alle Anhänge einzeln indizieren -> PDF-Knoten grün, Ordner darüber
 kann zusammengefasst werden; Anhang einfügen -> PDF-Knoten "gemischt",
 nach Indizieren des neuen Anhangs wieder grün.
```

## #200 Unzulässige Zeichen beim Kopieren aus Containern (03.10.2026)

Ersetzt den Stichpunkt "verbotene Sonderzeichen beim Kopieren aus Containern escapen" der Allgemeinen Liste sowie den zurückgestellten Abschnitt "ZIP-Dateinamen mit unter Windows unzulässigen Zeichen" (TODO.md). Vom Nutzer getestet (problematisch.zip, alle Fälle in Ordnung).

```text
 #200 Beim Kopieren aus ZIP/PDF-Anhängen/E-Mail ins Dateisystem werden
 Dateinamen bereinigt: sond_sanitize_filename() in sond_file_helper.c
 ersetzt < > : " / \ | ? * und Steuerzeichen durch "_", einen führenden
 Punkt durch "_" (SeaDrive), entfernt Punkte/Leerzeichen am Ende und hängt
 bei reservierten Namen (CON, PRN, AUX, NUL, COM1-9, LPT1-9) "_" an den
 Stamm. Angewendet in process_stvfm_item_move_or_copy() (sond_treeviewfm.c,
 nur Quelle im Container UND Ziel im Dateisystem) und in
 copy_container_dir_to_fs() (sond_tvfm_item.c). Kollisionen im selben
 Ordner (auch nur durch Groß-/Kleinschreibung) werden mit " (n)" vor der
 Endung aufgelöst (sond_filename_add_counter()); die frühere Neustart-
 Schleife auf oberster Ebene hinterließ bei Kind-Kollisionen halb
 kopierte Ordner. is_valid_filename() beim Umbenennen nutzt denselben
 Sanitizer und lehnt zusätzlich Steuerzeichen, reservierte Namen,
 führenden Punkt und Punkt/Leerzeichen am Ende ab.
```
