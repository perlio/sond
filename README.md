# zond
zond ist ein Programm zur Organisation auch großer Mengen von digital gespeicherten Inhalten. Der Schwerpunkt liegt auf digitalen Dokumenten im PDF-Format.

Mit zond können solche Inhalte strukturiert und gegliedert werden; PDF-Dokumente können auch in beliebigem Umfang untergliedert werden. Für die nahe Zukunft ist geplant, daß aus Abschnitten von PDF-Dateien beliebig "virtuelle PDFs" erzeugt werden können, die dann wie eine einheitliche PDF-Datei behandelt bzw. bearbeitet werden können.

# "Design"
Digitale Inhalte werden projektbezogen organisiert. Es gibt ein Dateiverzeichnis, in welchem sich eine Projektdatei befindet. Sämtliche Dateien, die in dem Projekt erfaßt sind oder erfaßt werden können sollen, müssen sich ebenfalls in diesem Projektverzeichnis - oder seinen Unterverzeichnissen - befinden. Selbstverständlich können Dateien zu beliebigen Zeitpunkten hinzugefügt werden.

Inhalte werden auf drei Ebenen dargestellt.

Die unterste Ebene ist das Dateiverzeichnis. Hier werden alle Dateien des Projektverzeichnisses in einem Verzeichnisbaum angezeigt. Es handelt sich um eine erweiterte Ansicht, wobei hier das englische Wort "augmented" passender wäre; so werden etwa die zu PDF-Dateien gespeicherten Abschnitte angezeigt. Zukünftig sollen Dateien, die in Containern enthalten sind (E-Mails, Zip-Dateien, PDFs, die "associated files" enthalten, etc), ebenfalls in dieser Ansicht angezeigt werden und ein direkter Zugriff auf z.B. E-Mail-Anhänge oder zip-Archive möglich sein. Aus der Ebene des Dateiverzeichnisses heraus können Dateien geöffnet werden. Zudem können die "üblichen" Operationen auf Dateisystemebene (kopieren, löschen, verschieben, erzeugen weiterer Unterverzeichnisse) hier vorgenommen werden.

Eine "Mittelschicht" bildet das Bestandsverzeichnis. Dieses kann zunächst durch einfügen von "Strukturpunkten" gegliedert werden. Strukturpunkte sind Knoten in der Baumdarstellung des Bestandsverzeichnisses, denen keine Dateu oder kein Dateiabschnitt zugeordnet ist. Strukturpunkte können beliebig benannt werden und in unbeschränkter Anzahl - auch als Unterpunkte von anderen Strukturpunkten - eingefügt werden. In das Bestandsverzeichnis kann weiterhin eine Auswahl der Dateien oder Dateibestandteile aus dem Dateiverzeichnis kopiert werden. Es muß nicht die ganze Datei in das Bestandsverzeichnis kopiert werden; sofern - z.B. - eine PDF-Datei in verschiedene Abschnitte unterteilt ist, können auch nur einzelne dieser Abschnitte in das Bestandsverzeichnis aufgenommen werden. Z.B. enthält eine PDF-Datei ein - belangloses - Übersendungsschreiben und auf den folgenden Seiten das - belangreiche - übersendete Schreiben; hier kann - wenn die PDF-Datei einen Abschnitt hat, der das übersendete Schreiben umfaßt, nur dieser in das Bestandsverzeichnis kopiert werden. Der Aufbau des Bestandsverzeichnisses folgt strengen Regeln: Jede Datei bzw. jeder Dateiteil kann nur einmal im Bestandsverzeichnis abgebildet sein. Die erfaßte Datei bzw. der erfaßte Dateiabschnitt enthält immer auch sämtliche Unterabschnitte. Strukturpunkte können an angebundene Dateien oder deren Unterabschnitte nicht angebunden werden.

Die freieste 



A. Installation

  I. Erstinstallation

    Letztes Release - zip-Datei herunterladen, in beliebigem Verzeichnis entpacken.
    Im Unterverzeichnis bin/ befinden sich die ausführbaren Dateien zond.exe und viewer.exe

  II. Update

    "Update" unter "Hilfe" wählen. Falls Internetverbindung wird geprüft, ob neuere Version vorhanden; ggf. wird diese heruntergeladen und installiert.
    Anschließend wird zond neu gestartet.
