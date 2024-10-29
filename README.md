# zond
zond ist ein Programm zur Organisation auch großer Mengen von digital gespeicherten Inhalten. Der Schwerpunkt liegt auf digitalen Dokumenten im PDF-Format.

Mit zond können solche Inhalte strukturiert und gegliedert werden; PDF-Dokumente können auch in beliebigem Umfang untergliedert werden. Für die nahe Zukunft ist geplant, daß aus Abschnitten von PDF-Dateien beliebig "virtuelle PDFs" erzeugt werden können, die dann wie eine einheitliche PDF-Datei behandelt bzw. bearbeitet werden können.

# "Design"
Digitale Inhalte werden projektbezogen organisiert. Es gibt ein Dateiverzeichnis, in welchem sich eine Projektdatei befindet. Sämtliche Dateien, die in dem Projekt erfaßt sind oder erfaßt werden können sollen, müssen sich ebenfalls in diesem Projektverzeichnis - oder seinen Unterverzeichnissen - befinden. Selbstverständlich können Dateien zu beliebigen Zeitpunkten hinzugefügt werden.

Die Organisation der digitalen Inhalte geschieht in drei Ebenen, in denen die Informationen jeweils in Baumdiagrammen dargestellt werden.

Die unterste Ebene ist das Projektverzeichnis selbst. Hier werden alle Dateien und Unterverzeichnisse in einem Verzeichnisbaum angezeigt. Es handelt sich um eine erweiterte Ansicht, wobei hier das englische Wort "augmented" passender wäre; so werden etwa die zu PDF-Dateien gespeicherten Abschnitte angezeigt. Zukünftig sollen Dateien, die in Containern enthalten sind (E-Mails, Zip-Dateien, PDFs, die "associated files" enthalten, etc.), ebenfalls in dieser Ansicht angezeigt werden und ein direkter Zugriff auf z.B. E-Mail-Anhänge oder zip-Archive möglich sein. Aus der Ebene des Dateiverzeichnisses heraus können Dateien geöffnet werden. Zudem können hier die "üblichen" Operationen auf Dateisystemebene (kopieren, löschen, verschieben, erzeugen weiterer Unterverzeichnisse) vorgenommen werden.

Eine "Mittelschicht" bildet das Bestandsverzeichnis. Dieses kann zunächst durch einfügen von "Strukturpunkten" gegliedert werden. Strukturpunkte sind Knoten in der Baumdarstellung, denen keine Datei oder kein Dateiabschnitt zugeordnet ist. Strukturpunkte können beliebig benannt werden und in unbeschränkter Anzahl - auch als Unterpunkte von anderen Strukturpunkten - eingefügt werden. In das Bestandsverzeichnis kann weiterhin eine Auswahl der Dateien oder Dateibestandteile aus dem Dateiverzeichnis kopiert werden. Es muß nicht die ganze Datei in das Bestandsverzeichnis kopiert werden; sofern - z.B. - eine PDF-Datei in verschiedene Abschnitte unterteilt ist, können auch nur einzelne dieser Abschnitte in das Bestandsverzeichnis aufgenommen werden. Z.B. enthält eine PDF-Datei ein - belangloses - Übersendungsschreiben und auf den folgenden Seiten das - belangreiche - übersendete Schreiben; hier kann - wenn die PDF-Datei einen Abschnitt hat, der das übersendete Schreiben umfaßt, nur dieser in das Bestandsverzeichnis kopiert werden. Der Aufbau des Bestandsverzeichnisses folgt strengen Regeln: Jede Datei bzw. jeder Dateiteil kann nur einmal im Bestandsverzeichnis abgebildet sein. Die erfaßte Datei bzw. der erfaßte Dateiabschnitt enthält immer auch sämtliche Unterabschnitte. Strukturpunkte können an angebundene Dateien oder deren Unterabschnitte nicht angebunden werden.

Die freieste Ebene stellt das Auswertungsverzeichnis dar. Hier können Strukturpunkte und Kopien von im Bestandsverzeichnis enthaltenen angebundenen Dateien oder Dateiabschnitten in beliebiger Ordnung angebunden werden. Die Beschränkungen des Bestandsverzeichnisses gibt es hier nicht. Zusätzlich können Links auf andere Punkte des Auswertungs- oder Bestandsverzeichnisses eingefügt werden; diese bilden eine synchron gehaltene Kopie des verlinkten Punktes mit seinen Unterpunkten ab.

# Installation
Die zip-Datei des letzten Releases herunterladen, in beliebigem Verzeichnis entpacken. Die ausführbare Programmdatei mit dem Namen "zond.exe" befindet sich im Unterverzeichnis "bin/".



# Bedienung

## Hauptfenster

## 

## Viewer

###

###

### Seiten einfügen
Werden Seiten vor der ersten oder nach der letzten (angezeigten) Seite eingefügt (Position = 0 oder Position = letzte Seitenzahl), so werden sie am Beginn oder am Ende des im PDF-Viewer angezeigten Dokuments bzw. Abschnitts eingefügt; d.h. der Abschnitt "wächst". Werden Seiten hingegen innerhalb des Dokuments oder Abschnitts eingefügt und befindet sich die Position, an der eingefügt werden soll, unmittelbar am Anfang oder Ende eines (Unter-)Abschnitts, so werden die Seiten nach oder vor diesem Abschnitt eingefügt. 

`Beispiel 1: Geöffnet ist die gesamte PDF-Datei. Diese enthält einen Abschnitt, der von Seite 1 - Seite 10 reicht. Fünf Seiten werden zu Beginn des Dokuments (nach Seite 0) eingefügt. Wirkung: Die PDF-Datei wächst um fünf Seiten. Der Abschnitt wird lediglich verschoben und beginnt nun bei Seite 6.`

`Beispiel 2: Geöffnet ist ein Abschnitt, der die Seiten 1 - 10 einer PDF-Datei umfaßt. Fünf Seiten werden zu Beginn (nach Seite 0) eingefügt. Wirkung: Der Abschnitt umfaßt jetzt die Seiten 1 - 15 der PDF-Datei, die um die fünf am Anfang eingefügten Dateien wächst.`

`Beispiel 3: Geöffnet ist die gesamte PDF-Datei. Diese enthält zwei Abschnitte, die die Seiten 1 - 10 bzw. 11 - 20 umfassen. Es werden fünf Seiten nach Seite 10 eingefügt. Wirkung: Der erste Abschnitt bleibt unverändert, der Beginn des zweiten Abschnitts wird um fünf Seiten nach hinten geschoben. Zwischen den Abschnitten befinden sich jetzt die eingefügten Seiten.`

## Update
Im Menu "Hilfe" "Update" wählen. Falls eine Internetverbindung besteht, wird geprüft, ob eine neuere Version vorhanden; ggf. wird diese heruntergeladen und installiert. Anschließend wird zond neu gestartet. Sofern ein "Sprung" der erstrangiges Versionsnummer vorliegt - was bedeutet, daß sich die Struktur der Projektdatei verändert hat -, ist zugleich ein Programm zur Konvertierung der Projektdatei enthalten; von der ursprünglichen Datei wird eine Sicherungskopie angefertigt. Ein Downgrade kann durch Herunterladen eines vorangegangenen Releases und dessen Installation vborgenmommen werden.
