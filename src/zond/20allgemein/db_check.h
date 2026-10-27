/*
 zond (db_check.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef DB_CHECK_H_INCLUDED
#define DB_CHECK_H_INCLUDED

#include <glib.h>

typedef struct _ZondDBase ZondDBase;
typedef struct sqlite3 sqlite3;

/* Konsistenzprüfung der Projektdatenbank (Tabelle "knoten").
 *
 * Geschwisterketten: Jeder Elternknoten (parent_ID > 0) hat genau einen Kopf
 * (older_sibling_ID = 0), und seine Kinder bilden von dort aus eine lückenlose
 * Kette ohne Verzweigung. Die App zeigt beim Laden nur die Kette, der sie von
 * dem Kopf mit der kleinsten ID aus folgt (jeweils zum jüngeren Geschwister mit
 * der kleinsten ID). Alles andere ist in der Datenbank vorhanden, aber nicht
 * sichtbar. Ein Befund beschreibt einen Elternknoten und zerlegt dessen Kinder
 * in Zweige (Ketten), wie sie in der Datenbank verkettet sind. */
typedef enum {
	DB_BEFUND_VERZWEIGUNG, //Kinder mit demselben älteren Geschwister (auch 0)
	DB_BEFUND_LUECKE, //älteres Geschwister fehlt oder gehört zu anderem Elternknoten
	DB_BEFUND_UNERREICHBAR, //Kinder ohne Weg vom Kopf (Zyklus)
	DB_BEFUND_WAISE, //Elternknoten existiert nicht
	DB_BEFUND_INTEGRITAET, //quick_check / foreign_key_check
	DB_NUM_BEFUND_ARTEN
} DbBefundArt;

typedef enum {
	DB_ZWEIG_KOPF, //beginnt bei einem Kopf (older_sibling_ID = 0)
	DB_ZWEIG_GABEL, //folgt auf einen Knoten, auf den noch ein anderer Zweig folgt
	DB_ZWEIG_LUECKE, //älteres Geschwister fehlt oder gehört zu anderem Elternknoten
	DB_ZWEIG_ZYKLUS //Zyklus ohne Kopf
} DbZweigArt;

typedef struct {
	DbZweigArt art;
	gboolean sichtbar; //Kette, der die App beim Laden folgt
	gboolean gewaehlt; //vom Aufrufer vor db_aktion_ausfuehren() zu setzen
	gint64 vorgaenger_id; //bei DB_ZWEIG_GABEL: der gemeinsame ältere Knoten
	GArray *knoten_ids; //gint64, in Kettenreihenfolge
} DbZweig;

typedef struct {
	guint arten; //Bitmaske 1 << DbBefundArt
	gint64 eltern_id; //0 bei DB_BEFUND_INTEGRITAET
	gint eltern_typ; //Typ des Elternknotens, -1 wenn er fehlt
	GPtrArray *zweige; //DbZweig*, sichtbarer Zweig (falls vorhanden) zuerst
	gchar *text; //nur bei DB_BEFUND_INTEGRITAET
	gboolean reparierbar;
} DbBefund;

typedef struct {
	GPtrArray *befunde; //DbBefund*
} DbCheckErgebnis;

void db_befund_free(DbBefund*);

void db_check_ergebnis_clear(DbCheckErgebnis*);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(DbCheckErgebnis, db_check_ergebnis_clear)

gchar const* db_befund_art_name(DbBefundArt);

//Anzahl der Befunde (Elternknoten), die die Art enthalten
gint db_check_anzahl(DbCheckErgebnis const*, DbBefundArt);

//TRUE, wenn es keinen Befund gibt
gboolean db_check_ok(DbCheckErgebnis const*);

gint db_check_anzahl_reparierbar(DbCheckErgebnis const*);

/* Prüfung, nur lesend. mit_integritaet: zusätzlich PRAGMA quick_check und
 * foreign_key_check (langsamer, nicht beim Öffnen). */
gint db_check_ausfuehren(ZondDBase*, gboolean mit_integritaet,
		DbCheckErgebnis*, GError**);

typedef enum {
	DB_AKTION_REPARIEREN,
	DB_AKTION_VERWERFEN
} DbAktion;

typedef struct {
	gint n_eltern; //Elternknoten, an denen etwas geändert wurde
	gint n_zweige; //behandelte Zweige
	gint n_sammelpunkte; //angelegte Sammelpunkte
	GPtrArray *meldungen; //gchar*: was nicht möglich war und warum
} DbAktionErgebnis;

void db_aktion_ergebnis_clear(DbAktionErgebnis*);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(DbAktionErgebnis, db_aktion_ergebnis_clear)

/* Behandelt die gewählten, nicht sichtbaren Zweige (DbZweig.gewaehlt) der
 * übergebenen Befunde.
 *
 * DB_AKTION_REPARIEREN: Unter einer Wurzel oder einem Punkt kommen die Zweige
 * in einen neuen Punkt "Wiedergefundene Knoten" am Ende der sichtbaren Kette
 * (markiert durch Symbol und Notiz). Unter einer Datei oder einem Abschnitt
 * (FILE_PART) wird die Kette nach der Lage der Abschnitte neu aufgebaut; bei
 * Überschneidungen nicht.
 *
 * DB_AKTION_VERWERFEN: Löscht die Zweige samt Unterbäumen, außer sie enthalten
 * FILE_PART-Zeilen (Dateien und Abschnitte werden nie gelöscht, solange die
 * Datei existiert) oder es zeigt von außerhalb etwas auf sie.
 *
 * Läuft in einer Transaktion und wird zurückgerollt, wenn danach der Zustand
 * nicht dem erwarteten entspricht. Der Zustand der Datenbank wird frisch
 * gelesen; die Zweige werden anhand ihres ersten Knotens wiedererkannt. */
gint db_aktion_ausfuehren(ZondDBase*, DbAktion, GPtrArray *befunde,
		DbAktionErgebnis*, GError**);

/* Wie db_aktion_ausfuehren(), aber ohne eigene Transaktion: der Aufrufer hat
 * sie geöffnet und entscheidet über Commit oder Rollback. */
gint db_aktion_ausfuehren_innen(ZondDBase*, DbAktion, GPtrArray *befunde,
		DbAktionErgebnis*, GError**);

//Legt unter eltern einen Sammelpunkt an (älteres Geschwister tail, 0 = erstes Kind)
gint db_sammelpunkt_neu(sqlite3*, gint64 eltern, gint64 tail, gint64 *sp_id,
		GError**);

//Ende der sichtbaren Kette der Kinder von eltern, 0 wenn es keine gibt
gint db_check_sichtbarer_tail(sqlite3*, gint64 eltern, gint64 *tail, GError**);

#endif // DB_CHECK_H_INCLUDED
