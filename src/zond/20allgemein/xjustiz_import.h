/*
 zond (xjustiz_import.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  pelo america

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * xjustiz_import.c/.h: Extras-Menüpunkt "XJustiz-Import", der eine
 * xjustiz_nachricht.xml (aus dem beA-Akteneinsichtsportal) auswertet
 * und die darin referenzierten PDF-Dokumente anbindet.
 *
 * Ausgangspunkt ist die xjustiz_nachricht.xml selbst, ausgewählt über
 * filepart_oeffnen() (project.c/.h) - ein eigener, container-bewusster
 * Auswahldialog, der auch in eine noch nicht entpackte ZIP hineinsehen kann
 * (der normale Dateiauswahldialog des Betriebssystems könnte das nicht).
 * Eine Vorauswahl im Dateiverzeichnis (BAUM_FS) wäre nicht praktikabel, da
 * diese Selektion verloren geht, sobald anschließend im Bestandsverzeichnis
 * die Zielposition markiert wird. Gelesen wird über die
 * sond_file_part-Maschinerie (sond_file_part_get_bytes(), s.
 * sond_fileparts.h/.c) statt über eigene libzip-Aufrufe - das deckt
 * Dateisystem UND ZIP (und potentiell weitere SondFilePart-Containertypen)
 * einheitlich ab. Die referenzierten PDF-Dokumente werden als GESCHWISTER
 * von xjustiz_nachricht.xml im selben Verzeichnis/Container gesucht
 * (sond_file_part_create() mit dem Elternteil von xjustiz_nachricht.xml und
 * dessen Verzeichnisanteil - s. ausführlichen Kommentar in xjustiz_import.c),
 * nicht durch Durchsuchen einer kompletten ZIP-Eintragsliste. Vor der
 * eigentlichen Auswertung wird geprüft, ob die markierte Datei überhaupt ein
 * XJustiz-Datensatz ist (Element "schriftgutobjekte" vorhanden) - sonst
 * Fehlermeldung statt stillschweigend 0 Dokumente anzubinden.
 *
 * STRUKTUR DER NACHRICHT (anhand eines realen beA-Akteneinsichtsportal-
 * Datensatzes analysiert - weicht in der Verschachtelung von einer naiven
 * Lesart der Spezifikation ab, s. xjustiz_import.c für Details):
 *
 *   schriftgutobjekte
 *     akte[]                     - FLACH, nicht verschachtelt. Eine davon
 *       |                          ist die Hauptakte (hat i.d.R.
 *       |                          "anzeigename"), die übrigen sind
 *       |                          Beiakten (haben i.d.R. KEIN
 *       |                          "anzeigename", aber ein eigenes
 *       |                          "aktenzeichen"). Verknüpft sind sie NICHT
 *       |                          durch Verschachtelung, sondern über
 *       |                          "aktenreferenzen" (Code "002": Hauptakte
 *       |                          -> Beiakte; Code "001": Beiakte ->
 *       |                          Hauptakte zurück). Die Hauptakte enthält
 *       |                          dazu je einen "Verweis auf: <Aktenzeichen>"
 *       |                          -Platzhalter unter ihren eigenen
 *       |                          teilakte - diese Platzhalter tragen
 *       |                          selbst nie Dokumente.
 *       |
 *       +-- inhalt/teilakte[]     - rekursiv: eine teilakte kann WEITER
 *             |                     nur teilakte ODER nur dokument als
 *             |                     Kinder haben (beliebig tief, in der
 *             |                     Praxis bisher max. 2 Ebenen beobachtet -
 *             |                     z.B. Teilakten wie "FA 1 - ehemals ..."
 *             |                     die selbst wieder "Vorakte"/"Hauptakte"
 *             |                     als Unter-Teilakten enthalten).
 *             |
 *             +-- inhalt/dokument[] - Blattebene: datei/dateiname (physischer
 *                                     Dateiname) + anzeigename.
 *
 * Für den Import wird diese Hierarchie 1:1 nachgebildet: pro akte/teilakte
 * ein Strukturpunkt (Ordner), benannt nach "anzeigename" - bei einer Beiakte
 * ohne "anzeigename" ersatzweise nach ihrem Aktenzeichen. Ein Strukturpunkt
 * wird NUR angelegt, wenn in seinem Teilbaum mindestens ein Dokument liegt -
 * leere Zweige werden komplett ausgelassen, nicht als leere Ordner angelegt.
 * Ein "Verweis auf: ..."-Platzhalter, dessen Aktenzeichen zu einer Akte der
 * Nachricht passt, wird durch diese Akte ersetzt: sie erscheint (benannt
 * nach ihrem Aktenzeichen bzw. anzeigename) an der Stelle des Platzhalters
 * statt als eigener Punkt auf oberster Ebene. Akten, auf die kein
 * Platzhalter verweist, bleiben auf oberster Ebene.
 *
 * Seitenangaben im Namen (aus ersteSeitennummer/letzteSeitennummer der
 * Dokumente, nur dort stehen sie in der XML): "Name - Bl. x - y (N Seiten)"
 * bei fortlaufend paginierten Dokumenten, sonst "Name (N Seiten)" (z.B. in
 * Vorakten, wo jedes Dokument bei 1 beginnt); Sammelordner zeigen die
 * Summe. Wo die Blattnummern springen, wird zwischen den Dokumenten ein
 * Strukturpunkt "Bl. x - y fehlt" mit Fehlersymbol eingefügt.
 * In fortlaufend paginierten Ordnern steht vor dem Namen jedes Dokuments
 * dessen Blattzahl: "x-y: Name", bei einem Blatt "x: Name".
 *
 * Feldnamen aus der offiziellen XJustiz-Spezifikation ("Einheitlicher
 * XJustiz-Strukturdatensatz für die Übermittlung von Schriftgutobjekten")
 * sowie Abgleich mit einem realen Datensatz. Gesucht wird bewußt per
 * local-name() bzw. (bei der strukturellen Navigation akte/teilakte/
 * dokument) über den von libxml2 bereits präfixfreien Knotennamen -
 * robust gegenüber Namespace-Präfix-Varianten ("tns:", "xjustiz:" o.ä.).
 */

#ifndef XJUSTIZ_IMPORT_H_INCLUDED
#define XJUSTIZ_IMPORT_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct _GError GError;
typedef struct _GPtrArray GPtrArray;

typedef int gint;
typedef char gchar;
typedef int gboolean;

/*
 * Ablauf: öffnet über filepart_oeffnen() einen Dialog zur Auswahl der
 * xjustiz_nachricht.xml (kein Verzeichnis - container-bewusst, kann auch
 * in eine noch nicht entpackte ZIP hineinsehen), prüft, ob es sich
 * überhaupt um einen XJustiz-Datensatz handelt, wertet die darin
 * referenzierten PDF-Dokumente hierarchisch aus (gesucht als Geschwister
 * von xjustiz_nachricht.xml im selben Verzeichnis/Container) und bindet sie
 * an der aktuellen Cursor-Position im Bestandsverzeichnis an (Voraussetzung
 * wie beim normalen "Einfügen": Bestandsverzeichnis muß der aktive Baum
 * sein und eine Zielposition markiert sein - wird VOR dem Dialog geprüft).
 *
 * An der markierten Stelle wird zunächst EIN neuer, oberster Strukturpunkt
 * eingefügt (Benennung s.u.); darunter wird die komplette Akte/Teilakte/
 * Dokument-Hierarchie der Nachricht nachgebildet (s. ausführlichen Kommentar
 * oben und in xjustiz_import.c) - NICHT mehr flach wie zuvor.
 *
 * Benennung des obersten Strukturpunkts: aus dem Nachrichtenkopf,
 * "<Erstellungszeitpunkt kompakt, z.B. 20260805> Az. <Aktenzeichen>"
 * (nachrichtenkopf/erstellungszeitpunkt bzw. nachrichtenkopf/absender/
 * aktenzeichen). Beide Felder laut Spezifikation optional - fehlt das
 * Aktenzeichen, entfällt auch das "Az. "-Trennwort; fehlen beide, wird
 * "XJustiz-Import" als Fallback-Label verwendet.
 *
 * child: wie bei "Punkt einfügen"/"Einfügen" (win.einf-ge/-up,
 * win.paste-ge/-up) - TRUE bindet den neuen obersten Strukturpunkt als
 * Unterebene der markierten Stelle an, FALSE auf gleicher Ebene (als
 * Geschwister).
 *
 * Rückgabe: 0 Erfolg (auch wenn 0 Dokumente angebunden wurden - s.
 * *n_angebunden, *n_vorhanden und *arr_nicht_gefunden für die Details),
 * -1 Fehler (*error gesetzt - u.a. auch, wenn die gewählte Datei kein
 * XJustiz-Datensatz ist), 1 Nutzer hat den Auswahldialog abgebrochen (kein
 * *error gesetzt, keine der out-Parameter gefüllt).
 *
 * *n_angebunden: Anzahl neu angebundener Dokumente.
 * *n_vorhanden: Anzahl Dokumente, die schon (an beliebiger Stelle)
 *   angebunden waren - wurden übersprungen (wie beim normalen Anbinden).
 * *arr_nicht_gefunden: bei Erfolg (rc==0) immer belegt (ggf. mit len==0) -
 *   GPtrArray aus gchar* (Dateinamen laut XML, die im selben Verzeichnis/
 *   Container wie xjustiz_nachricht.xml nicht gefunden wurden). Aufrufer
 *   muß IMMER g_ptr_array_unref() aufrufen, wenn rc==0.
 */
gint xjustiz_import(Projekt *zond, gboolean child, gint *n_angebunden,
		gint *n_vorhanden, GPtrArray **arr_nicht_gefunden, GError **error);

#endif // XJUSTIZ_IMPORT_H_INCLUDED
