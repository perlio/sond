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

/* Ergebnis der Prüfung der Geschwisterketten in "knoten". Jeder Elternknoten
 * (parent_ID > 0) hat genau einen Kopf (older_sibling_ID = 0), und die Kinder
 * bilden von dort aus eine lückenlose Kette. */
typedef struct {
	gint n_koepfe; //Elternknoten mit mehreren Köpfen (Teile der Kinder unsichtbar)
	gint n_gabelungen; //Knoten mit mehreren jüngeren Geschwistern
	gint n_luecken; //älteres Geschwister fehlt oder liegt unter anderem Elternknoten
	gint n_unerreichbar; //Kinder, die vom Kopf aus nicht erreichbar sind (ohne n_koepfe)
	GPtrArray *beispiele; //gchar*, höchstens 20
} DbCheckKetten;

void db_check_ketten_clear(DbCheckKetten*);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(DbCheckKetten, db_check_ketten_clear)

gboolean db_check_ketten_ok(DbCheckKetten const*);

/* Prüfung, nur lesend. Für die übergebene Datenbank (Schema main). */
gint db_check_ketten(ZondDBase*, DbCheckKetten*, GError**);

/* Reparatur der Mehrfachköpfe: hängt die Ketten je Elternknoten hintereinander
 * (Köpfe nach ID sortiert). Elternknoten mit Gabelung oder nicht eindeutiger
 * Kette werden nicht angefasst (*n_uebersprungen). Läuft in einer Transaktion
 * und wird zurückgerollt, wenn danach nicht genau die übersprungenen
 * Elternknoten Mehrfachköpfe haben. Idempotent. */
gint db_repair_koepfe(ZondDBase*, gint *n_repariert, gint *n_uebersprungen,
		GError**);

#endif // DB_CHECK_H_INCLUDED
