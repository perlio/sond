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
 * xjustiz_import.c/.h ( 22.09.2026, neues Feature "XJustiz-Import" ):
 * Extras-Menüpunkt, der eine ZIP-Datei aus dem beA-Akteneinsichtsportal
 * (Download der Akteneinsicht: xjustiz_nachricht.xml + PDF-Dokumente in
 * einer ZIP) auswertet und die darin referenzierten PDF-Dokumente OHNE
 * Entpacken - direkt aus der ZIP heraus, wie beim bereits vorhandenen
 * "ZIP-Anbinden" - an der aktuell markierten Stelle im Bestandsverzeichnis
 * anbindet. Als Baum-Beschriftung wird "anzeigename" aus der XML
 * verwendet (physischer Dateiname in der ZIP bleibt unverändert - analog
 * zum bestehenden anbinden_label-Mechanismus, s. sond_tvfm_item.c).
 *
 * Feldnamen aus der offiziellen XJustiz-Spezifikation ("Einheitlicher
 * XJustiz-Strukturdatensatz für die Übermittlung von Schriftgutobjekten")
 * sowie Abgleich mit dem quelloffenen Viewer openXJV: pro referenziertem
 * Dokument <dokument> (unterhalb <schriftgutobjekte>, Namespace bewußt
 * per local-name() ignoriert - robust gegenüber Präfix-/Fachmodul-
 * Varianten) je ein <datei><dateiname> (physischer Dateiname) und ein
 * <anzeigename> (menschenlesbarer Titel).
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
 * Ablauf: öffnet immer den Dateiauswahl-Dialog (kein automatisches Suchen
 * im Projektverzeichnis), liest aus der gewählten ZIP xjustiz_nachricht.xml,
 * wertet die darin referenzierten PDF-Dokumente aus und bindet sie an der
 * aktuellen Cursor-Position im Bestandsverzeichnis an (Voraussetzung wie
 * beim normalen "Einfügen": Bestandsverzeichnis muß der aktive Baum sein
 * und eine Zielposition markiert sein).
 *
 * child: wie bei "Punkt einfügen"/"Einfügen" (win.einf-ge/-up,
 * win.paste-ge/-up) - TRUE bindet die Dokumente als Unterebene der
 * markierten Stelle an, FALSE auf gleicher Ebene (als Geschwister).
 * Innerhalb der importierten Dokumente selbst werden sie immer
 * hintereinander als Geschwister angehängt (erstes Dokument gemäß child,
 * alle weiteren als dessen Geschwister).
 *
 * Rückgabe: 0 Erfolg (auch wenn 0 Dokumente angebunden wurden - s.
 * *n_angebunden, *n_vorhanden und *arr_nicht_gefunden für die Details),
 * -1 Fehler (*error gesetzt), 1 Nutzer hat Dateiauswahl abgebrochen (kein
 * *error gesetzt, keine der out-Parameter gefüllt).
 *
 * *n_angebunden: Anzahl neu angebundener Dokumente.
 * *n_vorhanden: Anzahl Dokumente, die schon (an beliebiger Stelle)
 *   angebunden waren - wurden übersprungen (wie beim normalen Anbinden).
 * *arr_nicht_gefunden: bei Erfolg (rc==0) immer belegt (ggf. mit len==0) -
 *   GPtrArray aus gchar* (Dateinamen laut XML, die im ZIP-Archiv nicht
 *   oder nicht eindeutig gefunden wurden). Aufrufer muß IMMER
 *   g_ptr_array_unref() aufrufen, wenn rc==0.
 */
gint xjustiz_import(Projekt *zond, gboolean child, gint *n_angebunden,
		gint *n_vorhanden, GPtrArray **arr_nicht_gefunden, GError **error);

#endif // XJUSTIZ_IMPORT_H_INCLUDED
