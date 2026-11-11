/*
 sond (sond_zond_schema.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_zond_schema.h"

#include "zond/zond_version.h"

gint sond_zond_schema_create(sqlite3 *db, GError **error) {
	gchar *errmsg = NULL;
	gchar *sql = NULL;
	gint rc = 0;

	/*
	 type = BAUM_ROOT
	 ID = 1 (Inhalt) oder 2 (Auswertung)
	 parent_ID = 0 und older_sibling_ID = 0
	 Rest = NULL

	 type = BAUM_STRUKT (inhalt und auswertung)
	 parent_ID und older_sibling_ID
	 icon_name
	 node_text
	 text

	 type = BAUM_INHALT_FILE_LINK
	 parent_ID und older_sibling_ID
	 link=FILE
	 icon_name
	 node_text
	 text

	 type = FILE_PART
	 parent_ID und older_sibling_ID je nach parent/older_sibling
	 ziel_id_von, index_von nach Abschnitt
	 icon_name
	 node_text
	 text

	 type = BAUM_AUSWERTUNG_COPY
	 parent_ID und older_sibling_ID
	 link = ID von BAUM_INHALT_FILE, _VIRT_PDF oder PDF_ABSCHNITT oder PDF_PUNKT
	 rel_path
	 icon_name
	 node_text
	 text

	 type = BAUM_AUSWERTUNG_LINK
	 parent_ID und older_sibling_ID
	 link = ID von STRUKT, BAUM_INHALT_FILE, BAUM_INHALT_FILE_PART oder BAUM_AUSWERTUNG_COPY
	 Rest = 0

	 */
	//Tabellenstruktur erstellen
	sql = //Haupttabelle
			"DROP TABLE IF EXISTS knoten; "

					"CREATE TABLE knoten ("
					"ID INTEGER PRIMARY KEY, "
					"parent_ID INTEGER NOT NULL, "
					"older_sibling_ID INTEGER NOT NULL, "
					"type INTEGER, "
					"link INTEGER, "
					"file_part TEXT, "
					"section TEXT, "
					"icon_name TEXT, "
					"node_text TEXT, "
					"text TEXT, "
					"FOREIGN KEY (parent_ID) REFERENCES knoten (ID) "
					"ON DELETE CASCADE ON UPDATE CASCADE, "
					"FOREIGN KEY (older_sibling_ID) REFERENCES knoten (ID) "
					"ON DELETE CASCADE ON UPDATE CASCADE, "
					"FOREIGN KEY (link) REFERENCES knoten (ID) "
					"ON DELETE CASCADE ON UPDATE CASCADE "
					") STRICT; "

					"INSERT INTO knoten (ID, parent_id, older_sibling_id, "
					"node_text) VALUES (0, 0, 0, '" G_STRINGIFY(ZOND_VERSION_MAJOR) "');"
			"INSERT INTO knoten (ID, parent_id, older_sibling_id, type) "
			"VALUES (1, 0, 0, 0);"//root baum_inhalt
			"INSERT INTO knoten (ID, parent_id, older_sibling_id, type) "
			"VALUES (2, 0, 0, 0);"//root baum_auswertung
			;

	rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_string("SQLITE"), 0,
					"%s\nsqlite3_exec\n"
							"\nresult code: %s\n%s", __func__,
					sqlite3_errstr(rc), errmsg);
		sqlite3_free(errmsg);

		return -1;
	}

	return 0;
}
