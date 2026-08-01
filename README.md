# zond
zond ist ein Programm zur Organisation auch großer Mengen digital gespeicherter Inhalte. Der Schwerpunkt liegt auf digitalen Dokumenten im PDF-Format.

Mit zond können solche Inhalte strukturiert und gegliedert werden; PDF-Dokumente können auch in beliebigem Umfang untergliedert werden. Für die nahe Zukunft ist geplant, daß aus Abschnitten von PDF-Dateien beliebig "virtuelle PDFs" erzeugt werden können, die dann wie eine einheitliche PDF-Datei behandelt bzw. bearbeitet werden können.

# "Design"
Digitale Inhalte werden projektbezogen organisiert. Es gibt ein Dateiverzeichnis, in welchem sich eine Projektdatei befindet. Sämtliche Dateien, die in dem Projekt erfaßt sind oder erfaßt werden können sollen, müssen sich ebenfalls in diesem Projektverzeichnis - oder seinen Unterverzeichnissen - befinden. Selbstverständlich können Dateien zu beliebigen Zeitpunkten hinzugefügt werden.

Die Organisation der digitalen Inhalte geschieht in drei Ebenen, in denen die Informationen jeweils in Baumdiagrammen dargestellt werden.

Die unterste Ebene ist das Projektverzeichnis selbst. Hier werden alle Dateien und Unterverzeichnisse in einem Verzeichnisbaum angezeigt. Es handelt sich um eine erweiterte Ansicht, wobei hier das englische Wort "augmented" passender wäre; so werden etwa die zu PDF-Dateien gespeicherten Abschnitte angezeigt. Zukünftig sollen Dateien, die in Containern enthalten sind (E-Mails, Zip-Dateien, PDFs, die "associated files" enthalten, etc.), ebenfalls in dieser Ansicht angezeigt werden können und ein direkter Zugriff auf z.B. E-Mail-Anhänge oder zip-Archive möglich sein. Aus der Ebene des Dateiverzeichnisses heraus können Dateien geöffnet werden. Zudem können hier die "üblichen" Operationen auf Dateisystemebene (kopieren, löschen, verschieben, erzeugen weiterer Unterverzeichnisse) vorgenommen werden.

Eine "Mittelschicht" bildet das Bestandsverzeichnis. Dieses kann zunächst durch einfügen von "Strukturpunkten" gegliedert werden. Strukturpunkte sind Knoten in der Baumdarstellung, denen keine Datei oder kein Dateiabschnitt zugeordnet ist. Strukturpunkte können beliebig benannt werden und in unbeschränkter Anzahl - auch als Unterpunkte von anderen Strukturpunkten - eingefügt werden. In das Bestandsverzeichnis kann weiterhin eine Auswahl der Dateien oder Dateibestandteile aus dem Dateiverzeichnis kopiert werden. Es muß nicht die ganze Datei in das Bestandsverzeichnis kopiert werden; sofern - z.B. - eine PDF-Datei in verschiedene Abschnitte unterteilt ist, können auch nur einzelne dieser Abschnitte in das Bestandsverzeichnis aufgenommen werden. Z.B. enthält eine PDF-Datei ein - belangloses - Übersendungsschreiben und auf den folgenden Seiten das - belangreiche - übersendete Schreiben; hier kann - wenn die PDF-Datei einen Abschnitt hat, der das übersendete Schreiben umfaßt - nur dieser in das Bestandsverzeichnis kopiert werden. Der Aufbau des Bestandsverzeichnisses folgt strengen Regeln: Jede Datei bzw. jeder Dateiteil kann nur einmal im Bestandsverzeichnis abgebildet sein. Die erfaßte Datei bzw. der erfaßte Dateiabschnitt enthält immer auch sämtliche Unterabschnitte. Strukturpunkte können an angebundene Dateien oder deren Unterabschnitte nicht angebunden werden.

Die freieste Ebene stellt das Auswertungsverzeichnis dar. Hier können Strukturpunkte und Kopien von im Bestandsverzeichnis enthaltenen angebundenen Dateien oder Dateiabschnitten in beliebiger Ordnung angebunden werden. Die Beschränkungen des Bestandsverzeichnisses gibt es hier nicht. Zusätzlich können Links auf andere Punkte des Auswertungs- oder Bestandsverzeichnisses eingefügt werden; diese bilden eine synchron gehaltene Kopie des verlinkten Punktes mit seinen Unterpunkten ab.

# Installation
Die zip-Datei des letzten Releases herunterladen, in beliebigem Verzeichnis entpacken. Die ausführbare Programmdatei mit dem Namen "zond.exe" befindet sich im Unterverzeichnis "bin/".



# Bedienung

## Hauptfenster

Das Hauptfenster gliedert sich in eine Titelleiste (Headerbar) mit Menü, drei nebeneinander angeordnete Baumansichten sowie ein Notizfeld für Freitext zum jeweils markierten Punkt. Es öffnet sich beim Start eingerastet auf der linken Bildschirmhälfte (halbe Bildschirmbreite, volle Arbeitsflächenhöhe).

Im Normalmodus sind das Bestandsverzeichnis und das Auswertungsverzeichnis nebeneinander sichtbar, das Dateiverzeichnis ist ausgeblendet. Über den Umschalter "FS" in der Titelleiste wird stattdessen das Dateiverzeichnis neben dem Bestandsverzeichnis eingeblendet, während das Auswertungsverzeichnis zurücktritt; das Bestandsverzeichnis ist somit in beiden Modi sichtbar.

Unterhalb der drei Bäume befindet sich eine Statuszeile, die u.a. Fehlermeldungen beim Rendern der Baumzeilen anzeigt.

### Bedienung der Baumansichten

Alle drei Bäume beruhen auf derselben Basiskomponente und teilen sich daher folgendes Verhalten:

- Rechtsklick auf einen Eintrag öffnet ein Kontextmenü mit den für die jeweilige Ebene zulässigen Operationen. Ein Klick auf eine noch nicht selektierte Zeile setzt zuvor den Cursor auf diese Zeile.
- Mehrfachauswahl ist möglich; dabei können jedoch nicht gleichzeitig ein Knoten und einer seiner Vor- oder Nachfahren markiert sein.
- Der Zeilentext ist per Inline-Bearbeitung änderbar (Doppelklick auf die bereits selektierte Zeile bzw. F2 – Standard-GTK-Verhalten, kein eigener Shortcut im Programm definiert); im Bestands- und Auswertungsverzeichnis wird die Änderung sofort gespeichert.
- Ausgeschnittene, aber noch nicht eingefügte Zeilen werden ausgegraut dargestellt; die aktuelle Cursor-Zeile ist unterstrichen.
- **Es gibt keine Drag-&-Drop-Unterstützung.** Verschieben, Kopieren und Anbinden von Knoten erfolgt ausschließlich über die Menü- bzw. Tastaturbefehle Kopieren/Ausschneiden/Einfügen.
- Bei fokussierter Baumansicht öffnet das Tippen einer beliebigen druckbaren Taste (ohne Strg) automatisch ein Suchfeld (Popover), mit dem sich der Baum nach dem eingegebenen Text durchsuchen läßt (s. "Bearbeiten → Suchen").
- Ein Doppelklick (bzw. das Signal "row-activated") öffnet im Dateiverzeichnis die zugehörige Datei, im Bestands- und Auswertungsverzeichnis den mit dem Punkt verknüpften Abschnitt im PDF-Viewer bzw. löst allgemein "Öffnen" aus. Wird dabei zusätzlich die Umschalttaste gehalten, öffnet sich bei PDF-Abschnitten stets ein neues Viewer-Fenster, statt ein bereits offenes wiederzuverwenden.

Im Dateiverzeichnis besitzt das Kontextmenü zusätzlich (unter Windows) einen Abschnitt zur SeaDrive-Cloud-Synchronisation: Dateien/Verzeichnisse können als "immer offline verfügbar" markiert, diese Markierung wieder aufgehoben oder der lokale Cache geleert werden – jeweils für das gesamte Verzeichnis oder nur die Auswahl.

Im Bestands- und Auswertungsverzeichnis werden Zeilen, denen bereits ein Notiztext hinterlegt ist, farblich hervorgehoben. Verlinkte Punkte (s. "Bearbeiten → Als Link einfügen") werden kursiv dargestellt, der "Kopf" eines Links zusätzlich violett.

### Notizfeld

Zu jedem markierten Punkt kann im Notizfeld neben den Baumansichten ein Freitext hinterlegt werden; er wird beim Verlassen des Feldes automatisch gespeichert. Über den Pin-Knopf läßt sich der angezeigte Text an einem Punkt festhalten, während im Baum weiter navigiert wird; der danebenliegende Knopf springt zur angepinnten Zeile zurück (dieser Knopf ist erst aktiv, nachdem einmal gepinnt wurde).

### Tastenkürzel im Hauptfenster

| Tastenkombination | Wirkung |
|---|---|
| Strg+Q | Programm beenden |
| Strg+P | Punkt einfügen, gleiche Ebene |
| Strg+Umschalt+P | Punkt einfügen, Unterebene |
| Strg+C | Kopieren |
| Strg+X | Ausschneiden |
| Strg+V | Einfügen, gleiche Ebene |
| Strg+Umschalt+V | Einfügen, Unterebene |
| Strg+L | Als Link einfügen, gleiche Ebene |
| Strg+Umschalt+L | Als Link einfügen, Unterebene |
| Strg+Entf | Löschen |
| Strg+J | Zu Ursprung springen |
| Strg+O | Öffnen |

Weitere feste Interaktionen ohne Menü-Accelerator: eine beliebige druckbare Taste öffnet das Such-Popover (s.o.); ein Doppelklick öffnet Datei bzw. Abschnitt.

## Menü

### Projekt

**Neu** legt ein neues Projekt an. War bereits ein Projekt geöffnet, wird zunächst nachgefragt, ob dieses geschlossen werden soll. Anschließend öffnet sich ein Dateiauswahl-Dialog ("Projekt anlegen"), in dem Verzeichnis und Dateiname gemeinsam bestimmt werden; das Namensfeld ist mit ".ZND" vorbelegt, der eigentliche Projektname muss davor eingetippt werden. Es werden zwei SQLite-Datenbanken angelegt: die eigentliche Projektdatei sowie eine gleichnamige Arbeitskopie mit der Endung ".tmp", in die während der Arbeit geschrieben wird und die beim Speichern in die Projektdatei übertragen wird.

**Öffnen** fragt bei ungespeichertem, geöffnetem Projekt ebenfalls zunächst nach dem Schließen und öffnet dann einen Datei-öffnen-Dialog. Nach dem Laden ist das Bestandsverzeichnis editierbar, das Auswertungsverzeichnis dagegen schreibgeschützt für Zellentext-Bearbeitung.

**Speichern** ist nur aktiv, solange ungespeicherte Änderungen vorliegen, und überträgt die Arbeitskopie vollständig in die eigentliche Projektdatei. Ist unter "Einstellungen" das automatische Speichern aktiviert, geschieht dies zusätzlich automatisch alle zehn Minuten.

**Schließen** fragt bei ungespeicherten Änderungen "Änderungen aktuelles Projekt speichern?" ab (Ja/Nein; Abbruch über Fenster schließen/Escape verwirft nichts und bricht den Schließvorgang ab). Beim tatsächlichen Schließen werden alle offenen Viewer-Fenster behandelt (s.u.), beide Bäume geleert und die temporäre Arbeitsdatei (".tmp") gelöscht. Dieselbe Funktion wird auch beim Schließen des Hauptfensters oder bei "Beenden" durchlaufen.

**Export als odt-Dokument** *(in Arbeit, noch nicht fertiggestellt)*: Angelegt ist bereits die Umwandlung eines Baums über eine zunächst erzeugte RTF-Datei, die anschließend über eine extern installierte, mit der Endung ".odt" verknüpfte Anwendung (unter Windows i.d.R. LibreOffice/soffice.exe, per Windows-Dateizuordnung ermittelt) in das odt-Format konvertiert wird; ein Dateiauswahl-Dialog legt den Zieldateinamen fest. Vorgesehen, aber im Menü noch nicht angebunden ist die Wahl des Umfangs (gesamter Baum / markierte Zweige / markierte Punkte); im aktuellen Stand erzeugt der Menüaufruf deshalb noch ein leeres Dokument ohne Bauminhalt.

**Index erstellen** (für das gesamte Projektverzeichnis oder nur die ausgewählten Punkte) fragt zunächst den OCR-Modus ab: kein OCR, vorhandenen versteckten Text prüfen und bei Bedarf ergänzen (Standardauswahl), oder vorhandenen versteckten Text löschen und die Seite neu erkennen. Die Indizierung läuft danach im Hintergrund; ein Info-Fenster zeigt den Fortschritt und erlaubt den Abbruch.

**Index durchsuchen** (gesamtes Projektverzeichnis oder nur ausgewählte Punkte) prüft bei einer Auswahl zunächst, ob alle betroffenen Seiten bereits vollständig indiziert sind; bei Lücken erscheint eine Liste der unvollständigen Dateien mit den Optionen "Jetzt nachindizieren", "Trotzdem suchen" oder "Abbrechen". Die eigentliche Suchmaske enthält ein Feld "Suchbegriff:" sowie ein optionales Feld "Im Kontext von:"; ist Letzteres gefüllt, müssen beide Begriffe im selben Textabschnitt vorkommen (logisches UND, keine weiteren Operatoren wählbar). Enthält der Suchbegriff ein Leerzeichen, wird er automatisch als zusammenhängende Wortfolge gesucht. Ergänzend wird auch nach passenden Dateinamen gesucht. Treffer erscheinen in einer Ergebnisliste (Datei/Seite/Fundstelle); Doppelklick springt an die Fundstelle im internen Viewer bzw. in der jeweiligen Dateivorschau. Aus dem Ergebnisfenster heraus lassen sich markierte Treffer als Kopie in das Auswertungsverzeichnis übernehmen, sofern dieses gerade aktiv ist.

### Bearbeiten

Die folgenden Funktionen stehen im Bestands- und Auswertungsverzeichnis zur Verfügung; im Dateiverzeichnis sind nur Kopieren und Ausschneiden möglich (Anbinden erfolgt dort über "Einfügen" im Zielbaum, s.u.).

**Punkt einfügen** (gleiche Ebene / Unterebene) legt sofort, ohne Namensabfrage, einen neuen Strukturpunkt mit dem Text "Neuer Punkt" an; der Name wird anschließend per Inline-Bearbeitung der Zelle vergeben. Punkte können nicht als Unterpunkt einer angebundenen Datei oder eines Links (außer dessen Kopf) eingefügt werden.

**Kopieren** und **Ausschneiden** merken sich zunächst nur die markierten Zeilen (Referenz), ohne etwas zu verändern; ausgeschnittene Zeilen werden ausgegraut dargestellt. Erst **Einfügen** (gleiche Ebene / Unterebene) wird wirksam: 

- Wird aus dem Dateiverzeichnis eingefügt, werden die Dateien bzw. Verzeichnisse im Zielbaum "angebunden" (neuer Anbindungsknoten je Datei, bei Verzeichnissen rekursiv mit Strukturpunkten je Unterordner); bereits angebundene Dateien werden dabei übersprungen und im Info-Fenster gemeldet.
- Wird innerhalb desselben Baums ausgeschnitten und eingefügt, wird der Knoten tatsächlich verschoben.
- Wird kopiert (nicht ausgeschnitten) und im Auswertungsverzeichnis eingefügt, entsteht dort ein echter neuer Knoten (rekursiv inkl. Unterpunkte). Im Bestandsverzeichnis ist "Einfügen" einer Kopie dagegen wirkungslos – hier gilt die Regel, dass jede Datei/jeder Abschnitt nur einmal vorkommen darf.
- Der Zielknoten darf kein Abkömmling der zu verschiebenden/kopierenden Auswahl sein.

**Als Link einfügen** (gleiche Ebene / Unterebene) ist nur im Auswertungsverzeichnis wirksam (im Bestandsverzeichnis wirkungslos) und legt keine Kopie, sondern eine Referenz auf den Originalknoten an; Original und Link bleiben dadurch synchron – Änderungen am Original (Text, Unterpunkte) erscheinen auch am Link. Verweist die Quelle bereits selbst auf einen Link, wird direkt auf dessen Ziel verwiesen, damit keine Ketten entstehen. Ausschneiden und "Als Link einfügen" lassen sich nicht kombinieren.

**Löschen** entfernt den markierten Knoten **ohne Sicherheitsabfrage**; bei reinen Strukturpunkten im Bestandsverzeichnis werden zuvor alle Unterpunkte rekursiv mitgelöscht. Ist der Punkt oder die zugehörige Datei an anderer Stelle (Auswertungsverzeichnis-Kopie) noch in Verwendung, wird das Löschen für diesen Knoten übersprungen, um verwaiste Kopien zu vermeiden. Ein Link, der nicht der Kopf ist, wird beim Löschen ignoriert.

**Anbindung entfernen** ist nur im Bestandsverzeichnis aktiv und unterscheidet sich von "Löschen": Es wird nur der Anbindungspunkt selbst entfernt, seine Unterpunkte bleiben erhalten und rücken eine Ebene auf.

**Zu Ursprung springen** navigiert je nach Art des Punktes zum zugehörigen Ausgangspunkt: bei einem Link zum Linkziel, bei einer Kopie im Auswertungsverzeichnis zum Original im Bestandsverzeichnis, bei einer angebundenen Datei/einem Abschnitt zur entsprechenden Stelle im Dateiverzeichnis (das dafür bei Bedarf automatisch eingeblendet wird).

**Suchen** öffnet ein Suchfeld (s. Hauptfenster); ab drei eingegebenen Zeichen und Enter wird gesucht. Durchsucht werden gleichzeitig Dateipfad, Zeilentext **und der Notiztext** aller Punkte, nicht nur der sichtbare Baumtext. Treffer erscheinen in einer eigenen Ergebnisliste; Doppelklick springt zur Fundstelle.

**Öffnen** bzw. **Öffnen mit** öffnet die zum Punkt gehörende Datei. PDF-Dateien werden über "Öffnen" stets im internen Viewer angezeigt (der zugehörige Abschnitt bzw. bei Punkten im Auswertungsverzeichnis eine ggf. aus mehreren Fundstellen zusammengesetzte Ansicht). Bestimmte Formate (Text, Bilder, ODT, DOCX, E-Mail-Bestandteile) werden über "Öffnen" ebenfalls immer intern angezeigt; "Öffnen mit" umgeht diese interne Anzeige. Andere Dateien werden mit der Standardanwendung des Systems geöffnet; "Öffnen mit" öffnet dafür unter Windows den systemeigenen Auswahldialog für die Anwendung. In Containern (ZIP, E-Mail, PDF) eingebettete Dateien werden dafür zunächst in eine temporäre Datei extrahiert.

**Icon ändern** weist allen markierten Zeilen ohne weitere Rückfrage eines der folgenden Icons zu (nur im Bestands-/Auswertungsverzeichnis möglich):

Punkt, Ordner, Datei, PDF-Datei, PDF, Anbindung, Akte, Ausführbar, Text, Writer/Word, PowerPoint, Tabelle, Bild, Video, Audio, E-Mail, HTML, Durchsuchung, Ort, TKÜ, Wichtig, Observation, CD, Person, Personen sowie die Farbmarkierungen Orange, Blau, Rot, Grün, Türkis und Magenta.

### PDF-Dateien

**PDF reparieren** wendet auf alle im gerade aktiven Baum markierten PDF-Dateien (ohne weitere Rückfrage) einen Strukturneuaufbau an, der von MuPDF stammende, aber fehlerhafte oder verwaiste interne Objekte bereinigt; die Seitenreihenfolge bleibt dabei unverändert. Die Originaldatei wird direkt überschrieben. Ist eine der markierten Dateien bereits in einem Viewer-Fenster geöffnet, wird die Reparatur für diese Datei mit einer Fehlermeldung abgelehnt. Bei Erfolg erscheint keine Bestätigung, nur Fehler werden gemeldet.

### Ansicht, Extras, Einstellungen, Hilfe

Das Menü "Ansicht" klappt die Baumstruktur vollständig oder ausgehend vom aktuellen Zweig auf bzw. wieder zu und lädt die Ansicht neu. "Extras" enthält eine interne Testfunktion ohne Bedeutung für den normalen Gebrauch. Unter "Einstellungen" lassen sich der Zoomfaktor des internen Viewers und das automatische Speichern (alle zehn Minuten) an- bzw. abschalten. "Hilfe" enthält den Über-Dialog sowie die Update-Funktion (s.u.).

## Viewer

Der in zond integrierte PDF-Viewer basiert auf MuPDF (nicht Poppler). Er zeigt den zu einem Punkt des Bestands- oder Auswertungsverzeichnisses gehörenden Abschnitt einer PDF-Datei an und dient zugleich dazu, neue Abschnitte innerhalb einer PDF-Datei zu definieren.

### Anzeige und Bedienelemente

Die Seiten werden fortlaufend in einer scrollbaren Ansicht dargestellt und nur bei tatsächlicher Sichtbarkeit im Hintergrund gerendert. Über ein Eingabefeld in der Titelleiste kann direkt zu einer Seite gesprungen werden, eine daneben liegende Volltextsuche durchsucht das Dokument seitenweise vorwärts oder rückwärts und hebt Treffer farblich hervor ("Kein Treffer", falls nichts gefunden wird). Eine ein- und ausblendbare Miniaturansicht (Thumbnails) erlaubt die Mehrfachauswahl von Seiten für die unten beschriebenen Seitenoperationen. Der Zoomfaktor läßt sich über ein Eingabefeld zwischen 10 % und 400 % einstellen. Es gibt keine Tastatur-Navigation über Pfeiltasten oder Bild-hoch/-runter; die Navigation erfolgt über Scrollleiste/Mausrad, das Seitenzahlfeld oder Klick auf ein Thumbnail. Ein eigenes Rechtsklick-Kontextmenü besitzt der Viewer nicht – sämtliche Funktionen sind über die Werkzeugleiste bzw. deren Menü-Knopf erreichbar.

In der Werkzeugleiste kann zwischen den Werkzeugen Zeiger, Textmarker, Unterstreichen und Kommentar (Notiz) gewählt werden. Mit gedrückter linker Maustaste wird – je nach Werkzeug – Text markiert und farblich hervorgehoben (Standardfarbe) bzw. unterstrichen (fest grün), eine Notiz an der angeklickten Stelle eingefügt, oder – bei aktivem Zeiger-Werkzeug außerhalb von Text – die Ansicht per Ziehen verschoben. Eine eigene Farbauswahl für Markierungen gibt es nicht. Markierter Text kann über das Menü ("Text kopieren") in die Zwischenablage kopiert werden.

Eine Notiz wird durch einfachen Klick mit aktivem Kommentar-Werkzeug erzeugt und öffnet sofort ein Bearbeitungsfenster (Popover) mit einem einzelnen Mehrzeilen-Textfeld ohne weitere Optionen (kein Titel-, Autoren- oder Farbfeld); beim Schließen des Popovers wird die Änderung übernommen. Beim bloßen Überfahren einer bestehenden Notiz mit der Maus wird ihr Inhalt in einem Popover angezeigt. Notizen lassen sich per Ziehen verschieben; würde dies dazu führen, dass die Notiz in einem anderen, gerade geöffneten Abschnitt erscheint oder verschwindet, wird die Verschiebung verworfen und der Abschnitt muss zuvor geschlossen werden. Eine angeklickte Markierung, Unterstreichung oder Notiz kann mit der Entf-Taste gelöscht werden; physisch entfernt wird sie erst beim Speichern.

In der Werkzeugleiste befinden sich eigene Knöpfe zum Speichern (erst aktiv, sobald ungespeicherte Änderungen vorliegen) und Drucken (Standard-GTK-Druckdialog mit Rand 0). Über den Menü-Knopf sind zudem die folgenden Seitenoperationen erreichbar:

- **Seiten kopieren / ausschneiden** übernimmt die in der Thumbnail-Leiste markierten Seiten in ein projektweites Zwischenspeicher-Dokument; "Ausschneiden" löscht anschließend zusätzlich die Originalseiten.
- **Seiten drehen** fragt zunächst den Winkel ab (90° im Uhrzeigersinn, 180°, 90° gegen den Uhrzeigersinn) und danach, auf welche Seiten dieser angewendet werden soll (alle, markierte, oder eine frei eingegebene Seitenauswahl, z.B. "1-3,5,7-9").
- **Seiten löschen** fragt ebenso die betroffenen Seiten ab (hier ohne die Option "alle"); Seiten, die Teil einer bestehenden Anbindung sind, können nicht gelöscht werden, ebensowenig die letzte verbleibende Seite eines Dokuments.
- **OCR** erkennt für die abgefragte Seitenauswahl (hier inkl. Option "alle") unsichtbaren Text mittels Tesseract fest auf Deutsch (keine Sprachauswahl) und schreibt ihn in die PDF-Seite ein; ein Info-Fenster mit fortlaufendem Protokoll und Abbrechen-Knopf zeigt den Fortschritt. Bei Abbruch bleiben bereits verarbeitete Seiten erhalten, die restlichen werden nicht mehr bearbeitet.
- Der Menüpunkt "Entnehmen" ist als Platzhalter für ein geplantes Feature angelegt, aber noch nicht umgesetzt.

Sämtliche Änderungen (Seiten eingefügt/gelöscht/gedreht, OCR, Notizen/Markierungen) werden zunächst nur in einem Journal vorgemerkt und wirken sich erst beim Speichern tatsächlich auf die Datei aus; ein Rückgängig-Machen einzelner Schritte (Undo) gibt es nicht – wird der Viewer ohne Speichern geschlossen, werden die vorgemerkten Änderungen verworfen und die Originaldatei bleibt unverändert. Speichern überschreibt die Originaldatei (bzw. bei eingebetteten PDFs deren Containerdatei) direkt; es wird keine neue Datei angelegt. Beim Schließen eines Viewers mit ungespeicherten Änderungen wird nachgefragt, ob gespeichert werden soll.

### Abschnitte anlegen

Ein neuer Abschnitt (eine "Anbindung") wird direkt im Viewer durch zwei aufeinanderfolgende Doppelklicks definiert: Der erste Doppelklick markiert den Anfang, der zweite das Ende des Abschnitts. Im Anschluß wird automatisch ein neuer Punkt in der Baumansicht angelegt, dessen Bezeichnung sich aus der Seiten- bzw. Positionsangabe ergibt, und das Hauptfenster in den Vordergrund geholt.

Standardmäßig wird seitengenau markiert, d.h. der Abschnitt beginnt bzw. endet jeweils am Anfang oder Ende einer vollständigen Seite. Wird beim Doppelklick zusätzlich die Umschalttaste gehalten, erfolgt die Markierung stattdessen punktgenau an der angeklickten Stelle innerhalb der Seite. Eine bereits gesetzte, aber noch nicht abgeschlossene Startmarkierung kann über den Knopf "Anbindung Anfang löschen" in der Werkzeugleiste zurückgesetzt werden. Auf einer frisch eingefügten, noch nicht gespeicherten Seite kann keine Anbindung angelegt werden.

Seiten, die Teil einer bestehenden Anbindung sind, können nicht gelöscht werden, solange die Anbindung besteht.

### Tastenkürzel im Viewer

| Tastenkombination | Wirkung |
|---|---|
| Entf | angeklickte Markierung/Notiz löschen |
| Strg+C | Seiten kopieren |
| Strg+X | Seiten ausschneiden |
| Umschalt (beim Doppelklick) | Abschnitt punktgenau statt seitengenau markieren |
| Umschalt (beim Öffnen aus dem Baum) | stets neues Viewer-Fenster öffnen |
| Alt (beim Sprung zu einer Fundstelle) | Zielposition eine Bildschirmseite höher anzeigen |

### Seiten einfügen

Aufgerufen über den Menü-Knopf der Werkzeugleiste. Der Dialog "Seiten einfügen:" fragt zunächst die Zielposition über ein einzelnes Eingabefeld "nach Seite:" ab (0 = vor der ersten Seite) und bietet danach zwei Quellen an: **"Datei"** öffnet einen Dateiauswahl-Dialog für eine beliebige andere PDF-Datei, deren Seiten eingefügt werden (ist die gewählte Datei kein PDF, erscheint die Meldung "Keine PDF-Datei"); **"Clipboard"** fügt zuvor mit "Seiten kopieren"/"Ausschneiden" gemerkte Seiten ein und ist nur wählbar, wenn dieses Zwischenspeicher tatsächlich gefüllt ist.

Ist das aktuell im Viewer angezeigte Dokument ein "virtuelles PDF" (aus mehreren Abschnitten zusammengesetzt), ist Einfügen im derzeitigen Stand nicht möglich ("Virtuelles PDF - Einfügen noch nicht implementiert"). Außerdem gilt:

Werden Seiten vor der ersten oder nach der letzten (angezeigten) Seite eingefügt (Position = 0 oder Position = letzte Seitenzahl), so werden sie am Beginn oder am Ende des im PDF-Viewer angezeigten Dokuments bzw. Abschnitts eingefügt; d.h. der Abschnitt "wächst". Einfügen vor der ersten Seite ist dabei nur möglich, wenn der angezeigte Abschnitt exakt am Seitenanfang beginnt, Einfügen nach der letzten Seite nur, wenn er exakt am Seitenende endet – andernfalls erscheint eine entsprechende Fehlermeldung. Werden Seiten hingegen innerhalb des Dokuments oder Abschnitts eingefügt und befindet sich die Position, an der eingefügt werden soll, unmittelbar am Anfang oder Ende eines (Unter-)Abschnitts, so werden die Seiten nach oder vor diesem Abschnitt eingefügt. 

`Beispiel 1: Geöffnet ist die gesamte PDF-Datei. Diese enthält einen Abschnitt, der von Seite 1 - Seite 10 reicht. Fünf Seiten werden zu Beginn des Dokuments (nach Seite 0) eingefügt. Wirkung: Die PDF-Datei wächst um fünf Seiten. Der Abschnitt wird lediglich verschoben und beginnt nun bei Seite 6.`

`Beispiel 2: Geöffnet ist ein Abschnitt, der die Seiten 1 - 10 einer PDF-Datei umfaßt. Fünf Seiten werden zu Beginn (nach Seite 0) eingefügt. Wirkung: Der Abschnitt umfaßt jetzt die Seiten 1 - 15 der PDF-Datei, die um die fünf am Anfang eingefügten Dateien wächst.`

`Beispiel 3: Geöffnet ist die gesamte PDF-Datei. Diese enthält zwei Abschnitte, die die Seiten 1 - 10 bzw. 11 - 20 umfassen. Es werden fünf Seiten nach Seite 10 eingefügt. Wirkung: Der erste Abschnitt bleibt unverändert, der Beginn des zweiten Abschnitts wird um fünf Seiten nach hinten geschoben. Zwischen den Abschnitten befinden sich jetzt die eingefügten Seiten.`

## Update

Im Menü "Hilfe" "Update" wählen. zond fragt dazu über die GitHub-API die neueste veröffentlichte Version des Projekts ab und vergleicht sie (Haupt-, Neben- und Patch-Versionsnummer) mit der eigenen. Ist keine neuere Version vorhanden, erscheint die Meldung "Aktuelle Version installiert".

Ist eine neuere Version vorhanden, wird zunächst nachgefragt, ob sie heruntergeladen und installiert werden soll. Der Download (ein zip-Archiv des Releases) erfolgt mit Fortschrittsanzeige und Abbrechen-Möglichkeit in ein Info-Fenster; anschließend wird das Archiv in ein Verzeichnis neben dem Programm entpackt. Ist noch ein Projekt geöffnet, wird dessen Pfad gemerkt und das Projekt geschlossen. Ein separates, mit dem Update mitgeliefertes Installationsprogramm übernimmt danach die eigentliche Installation: Es wartet auf die Beendigung des laufenden zond, ersetzt die Programmdateien im Installationsverzeichnis durch die neu heruntergeladenen und startet zond anschließend automatisch neu – mit dem zuvor geöffneten Projekt, sofern eines offen war.

Sofern sich beim Update die **Haupt-Versionsnummer** ändert (was bedeutet, daß sich die Struktur der Projektdatei verändert hat), wird das nicht beim Update selbst, sondern beim nächsten Öffnen der betroffenen Projektdatei erkannt: zond vergleicht die in der Projektdatei gespeicherte Versionskennung mit der eigenen und fragt bei Abweichung "Konvertieren" ab. Wird zugestimmt, wird die Projektdatei in eine neue Datenbank im aktuellen Format überführt; die ursprüngliche Datei wird anschließend nicht überschrieben, sondern im selben Verzeichnis unter ihrem bisherigen Namen mit angehängter Versionskennung umbenannt (z.B. wird aus "projekt.znd" die Sicherung "projekt.zndv0"), während die konvertierte Datei fortan unter dem gewohnten Namen zur Verfügung steht.

Ein Downgrade ist nicht automatisiert und kann durch Herunterladen eines vorangegangenen Releases und dessen manuelle Installation vorgenommen werden.
