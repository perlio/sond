/*
 zond (db_regeln.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef DB_REGELN_H_INCLUDED
#define DB_REGELN_H_INCLUDED

#include <glib.h>

typedef struct _ZondDBase ZondDBase;

/* Regelwerk für eine gültige Projektdatenbank (Tabelle "knoten").
 *
 * Gültig ist, was dem Regelkatalog entspricht. Jede Abweichung ist ein Mangel
 * mit der Nummer der verletzten Regel. Zu jeder Regel gibt es eine Liste
 * zulässiger Aktionen mit einem Standard; der Nutzer wählt pro Knoten.
 *
 * Regeln (nach Wichtigkeit geordnet):
 *  A1 Zeile 0 (Version) sowie die Wurzeln 1 (Inhalt) und 2 (Auswertung)
 *     fehlen oder haben falsche Werte
 *  B1 Knoten ohne Verbindung zu einer Wurzel (Elternknoten fehlt, parent_ID
 *     leer oder Schleife in der Elternkette)
 *  B2 Unsichtbare Zweige: Kinder, die nicht in der einen lückenlosen Kette vom
 *     Kopf stehen (mehrere Köpfe, Gabelungen, Lücken, Zyklen)
 *  C1 Knotentyp unbekannt (gültig sind 0 bis 5; Typ 0 nur bei ID 1 und 2)
 *  C2 Knoten unter einem Elternknoten, unter dem sein Typ nicht stehen darf
 *  C3 Knoten im falschen Baum (Anker nur im Inhalt, Copy und Link nur in der
 *     Auswertung)
 *  C4 Spalten passen nicht zum Typ
 *  D1 Verweisziel: Anker, Copy oder Link ohne Ziel oder mit einem Ziel
 *     unzulässigen Typs
 *  D2 Datei mehrfach angebunden (der Baum zeigt sie nur an einer Stelle)
 *  E1 Abschnitt gehört zu einer anderen Datei als der Eintrag, unter dem er
 *     steht (Löschen möglich, wenn nichts darauf verweist)
 *  E2 Datei oder Abschnitt doppelt (gleicher Pfad und Abschnitt, Löschen
 *     möglich, wenn nichts darauf verweist)
 *  E3 Abschnitte überschneiden sich oder liegen nicht im übergeordneten
 *     Abschnitt (Löschen möglich, wenn nichts darauf verweist)
 *  E4 Datei fehlt auf der Festplatte (nur auf Wunsch geprüft, Löschen möglich)
 *  E5 Absoluter Pfad in der Dateiliste (Pfade sind relativ zum Projektordner;
 *     Umwandeln möglich, wenn die Datei im Projektordner liegt)
 *  F1 Dateiintegrität (nur Anzeige)
 *
 * Typen: 0 Wurzel, 1 Punkt, 2 Anker, 3 Copy, 4 Link, 5 Datei oder Abschnitt. */
typedef enum {
	DB_REGEL_A1,
	DB_REGEL_B1,
	DB_REGEL_B2,
	DB_REGEL_C1,
	DB_REGEL_C2,
	DB_REGEL_C3,
	DB_REGEL_C4,
	DB_REGEL_D1,
	DB_REGEL_D2,
	DB_REGEL_E1,
	DB_REGEL_E2,
	DB_REGEL_E3,
	DB_REGEL_E4,
	DB_REGEL_E5,
	DB_REGEL_F1,
	DB_NUM_REGELN
} DbRegel;

typedef enum {
	DB_AKT_BEHALTEN,
	DB_AKT_LOESCHEN, //Knoten samt Unterbäumen löschen
	DB_AKT_SAMMELPUNKT, //in einen markierten Punkt "Wiedergefundene Knoten" legen
	DB_AKT_EINORDNEN, //Kette nach der Lage der Abschnitte neu aufbauen
	DB_AKT_UMWANDELN, //Copy zum Punkt
	DB_AKT_BEREINIGEN, //Spalten auf den Sollwert bringen
	DB_AKT_ANLEGEN, //fehlende Grundzeile anlegen
	DB_AKT_RELATIVIEREN, //absoluten Dateipfad in einen relativen umwandeln
	DB_NUM_AKT
} DbAkt;

typedef struct {
	DbRegel regel;
	gint64 knoten_id; //Schlüsselknoten (B2: erster Knoten des Zweigs)
	GArray *knoten_ids; //gint64; B2: alle Knoten des Zweigs, sonst nur knoten_id
	gint64 eltern_id; //Elternknoten (Kontext), 0 wenn keiner
	gint baum; //1 Inhalt, 2 Auswertung, 3 Dateibaum, 4 ohne Wurzel
	gchar *text; //Beschreibung der Verletzung
	guint aktionen; //Bitmaske 1 << DbAkt; Schnittmenge aller Mängel des Knotens
	DbAkt standard;
	DbAkt gewaehlt; //vom Aufrufer zu setzen, Voreinstellung = standard
	gint ziel_baum; //für DB_AKT_SAMMELPUNKT: 1 Inhalt, 2 Auswertung
	gboolean schleife; //B2: Einträge folgen im Kreis aufeinander (text zeigt A -> B -> A)
	gboolean sichtbar; //der Eintrag wird im geladenen Baum angezeigt
	gint64 gehoert_unter; //nicht sichtbar: nächster sichtbarer übergeordneter Eintrag, sonst 0
	gchar *ziel_pfad; //E5: der relative Pfad (file_part), zu dem umgewandelt wird; NULL = nicht möglich
} DbMangel;

typedef struct {
	GPtrArray *maengel; //DbMangel*, sortiert nach Regel und Knoten
	GHashTable *unsichtbar; //Menge (GINT_TO_POINTER der Knoten-ID): im geladenen Baum nicht angezeigt
} DbRegelErgebnis;

typedef struct {
	gint n_geloescht;
	gint n_verschoben;
	gint n_sammelpunkte;
	gint n_umgewandelt;
	gint n_bereinigt;
	gint n_angelegt;
	gint n_relativ; //E5: auf relative Pfade umgestellt
	gint n_ketten; //B2: behandelte Zweige
	GPtrArray *meldungen; //gchar*: was nicht möglich war und warum
} DbRegelAktionErgebnis;

void db_mangel_free(DbMangel*);

void db_regel_ergebnis_clear(DbRegelErgebnis*);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(DbRegelErgebnis, db_regel_ergebnis_clear)

void db_regel_aktion_ergebnis_clear(DbRegelAktionErgebnis*);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(DbRegelAktionErgebnis, db_regel_aktion_ergebnis_clear)

//"A1", "C3" ...
gchar const* db_regel_name(DbRegel);

//Überschrift für den Nutzer
gchar const* db_regel_titel(DbRegel);

//kurzer Name für den Nutzer
gchar const* db_regel_kurz(DbRegel);

//ein Satz: Was gilt in einer gültigen Datei?
gchar const* db_regel_gilt(DbRegel);

//ausführliche Erklärung (Tooltip, Legende)
gchar const* db_regel_erklaerung(DbRegel);

//was die Aktion bewirkt
gchar const* db_akt_beschreibung(DbAkt);

//Bezeichnung der Knotenart für den Nutzer (Punkt, Anbindung, Copy ...)
gchar const* db_typ_name(gint typ);

gchar const* db_akt_name(DbAkt);

//Rang einer Aktion für den Vorrang (Löschen höher als Sammelpunkt usw.)
gint db_akt_rang(DbAkt);

//Anzahl der Mängel einer Regel
gint db_regel_anzahl(DbRegelErgebnis const*, DbRegel);

//Setzt die gewählte Aktion für alle Mängel desselben Knotens
void db_mangel_setze_aktion(GPtrArray *maengel, DbMangel const *mangel,
		DbAkt aktion);

//alle Mängel auf ihre Standardaktion
void db_regel_ergebnis_standard(DbRegelErgebnis*);

/* Prüft die ganze Tabelle gegen den Regelkatalog (nur lesend).
 * mit_integritaet: zusätzlich quick_check der Datei (F1).
 * fehlende_pfade: Menge (gchar*) der äußeren Pfade von Dateizeilen, die auf der
 * Festplatte fehlen; NULL = Dateien nicht geprüft (keine Befunde der Regel E4).
 * projekt_dir: Projektordner; nur damit lässt sich ein absoluter Pfad (E5) in einen
 * relativen umwandeln (NULL: E5 wird nur gemeldet). */
gint db_regeln_pruefen(ZondDBase*, gboolean mit_integritaet,
		GHashTable *fehlende_pfade, gchar const *projekt_dir, DbRegelErgebnis*,
		GError**);

/* Äußere Pfade (Teil vor dem ersten "//") aller Dateizeilen ohne Doppelte, für die
 * Prüfung auf der Festplatte; NULL bei Fehler. Freigabe: g_ptr_array_unref. */
GPtrArray* db_regeln_dateipfade(ZondDBase*, GError**);

/* Führt die gewählten Aktionen aus, in einer Transaktion.
 *
 * Schicht 1: Anlegen und Bereinigen der Grundzeilen (A1), Verschieben in
 * Sammelpunkte, Löschen. Danach neue Prüfung (Befunde, die sich erledigt haben,
 * entfallen). Schicht 2: Umwandeln, Bereinigen, Ketten (B2) auf dem neuen Stand.
 *
 * Schranken: Dateien und Abschnitte (Typ 5) werden nur gelöscht, wenn sie selbst
 * der gewählte Eintrag sind (Regeln E1 bis E3), nie als Teil eines anderen
 * Eintrags; nichts wird gelöscht, worauf von außerhalb noch ein Verweis zeigt;
 * Zeilen 0, 1 und 2 nie. Was nicht ausgeführt werden konnte, steht in ergebnis->meldungen. */
gint db_regeln_ausfuehren(ZondDBase*, GPtrArray *maengel,
		DbRegelAktionErgebnis*, GError**);

#endif // DB_REGELN_H_INCLUDED
