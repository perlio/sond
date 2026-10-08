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
 * und die darin referenzierten PDF-Dokumente anbindet. Als Baum-Beschriftung
 * der einzelnen Dokumente wird "anzeigename" aus der XML verwendet
 * (physischer Dateiname bleibt unverändert - analog zum bestehenden
 * anbinden_label-Mechanismus, s. sond_tvfm_item.c).
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
 * Die Dokumente werden nicht direkt an der markierten Stelle im
 * Bestandsverzeichnis angebunden, sondern dort wird zunächst EIN neuer
 * Strukturpunkt eingefügt, in den anschließend alle Dokumente eingefügt
 * werden. Benennung des Strukturpunkts aus dem Nachrichtenkopf/
 * Grunddaten der xjustiz_nachricht.xml: Name des Produkts und
 * Erstellungszeitpunkt (beide laut Spezifikation optional, s.
 * xjustiz_parse_nachricht()/xjustiz_format_zeitpunkt() in xjustiz_import.c).
 *
 * Feldnamen aus der offiziellen XJustiz-Spezifikation ("Einheitlicher
 * XJustiz-Strukturdatensatz für die Übermittlung von Schriftgutobjekten")
 * sowie Abgleich mit dem quelloffenen Viewer openXJV: pro referenziertem
 * Dokument <dokument> (unterhalb <schriftgutobjekte>, Namespace bewußt
 * per local-name() ignoriert - robust gegenüber Präfix-/Fachmodul-
 * Varianten) je ein <datei><dateiname> (physischer Dateiname) und ein
 * <anzeigename> (menschenlesbarer Titel). Für die Strukturpunkt-Benennung
 * zusätzlich <nameDesProdukts> (laut Spezifikation unter grunddaten/
 * herstellerinformation) und <erstellungszeitpunkt> (laut Spezifikation
 * direkt unter nachrichtenkopf) - beide ebenfalls per local-name() und
 * ohne feste Verschachtelungstiefe gesucht.
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
 * referenzierten PDF-Dokumente aus (gesucht als Geschwister von
 * xjustiz_nachricht.xml im selben Verzeichnis/Container) und bindet sie an
 * der aktuellen Cursor-Position im Bestandsverzeichnis an (Voraussetzung
 * wie beim normalen "Einfügen": Bestandsverzeichnis muß der aktive Baum
 * sein und eine Zielposition markiert sein - wird VOR dem Dialog geprüft).
 *
 * An der markierten Stelle wird zunächst ein neuer Strukturpunkt eingefügt;
 * die einzelnen Dokumente werden als dessen Unterpunkte angebunden (nicht
 * mehr direkt an der markierten Stelle). Die Benennung des Strukturpunkts
 * erfolgt aus dem Nachrichtenkopf der XJustiz-Nachricht: Produktname
 * (Element "nameDesProdukts") und Erstellungszeitpunkt (Element
 * "erstellungszeitpunkt", umformatiert als "TT.MM.JJJJ hh:mm"), durch
 * Leerzeichen getrennt. Fehlt eines der beiden Felder, wird nur das
 * vorhandene verwendet; fehlen beide, "XJustiz-Import" als Fallback-Label.
 * Beide Felder werden unabhängig von ihrer genauen Verschachtelungstiefe
 * per lokalem Elementnamen gesucht (wie auch sonst in dieser Datei üblich),
 * da Namespace-Präfix und Schachtelungstiefe laut XJustiz-Spezifikation
 * je Fachmodul variieren können.
 *
 * child: wie bei "Punkt einfügen"/"Einfügen" (win.einf-ge/-up,
 * win.paste-ge/-up) - TRUE bindet den neuen Strukturpunkt als Unterebene der
 * markierten Stelle an, FALSE auf gleicher Ebene (als Geschwister). Die
 * Dokumente selbst werden immer als Unterpunkte des Strukturpunkts angehängt,
 * hintereinander als Geschwister.
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
