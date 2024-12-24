/*
 zond (zond_convert.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2024  pelo america

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

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <mupdf/fitz.h>
#include <sqlite3.h>
#include <stdio.h>
#include <fcntl.h>

#include "zond_dbase.h"
#include "zond_treeview.h"

#include "99conv/pdf.h"
#include "99conv/general.h"

static gint treeviews_get_page_num_from_dest_doc(fz_context *ctx,
		pdf_document *doc, const gchar *dest, gchar **errmsg) {
	pdf_obj *obj_dest_string = NULL;
	pdf_obj *obj_dest = NULL;
	pdf_obj *pageobj = NULL;
	gint page_num = 0;

	obj_dest_string = pdf_new_string(ctx, dest, strlen(dest));
	fz_try( ctx )
		obj_dest = pdf_lookup_dest(ctx, doc, obj_dest_string);
fz_always	( ctx )
		pdf_drop_obj(ctx, obj_dest_string);
fz_catch	( ctx )
		ERROR_MUPDF("pdf_lookup_dest")

	pageobj = pdf_array_get(ctx, obj_dest, 0);

	if (pdf_is_int(ctx, pageobj))
		page_num = pdf_to_int(ctx, pageobj);
	else {
		fz_try( ctx )
			page_num = pdf_lookup_page_number(ctx, doc, pageobj);
fz_catch		( ctx ) ERROR_MUPDF( "pdf_lookup_page_number" )
	}

	return page_num;
}

static gint zond_convert_create_db_maj_0(sqlite3 *db, gchar **errmsg) {
	gchar *errmsg_ii = NULL;
	gchar *sql = NULL;
	gint rc = 0;

	//Tabellenstruktur erstellen
	sql = //Haupttabelle
			"DROP TABLE IF EXISTS links; "
					"DROP TABLE IF EXISTS dateien;"
					"DROP TABLE IF EXISTS ziele;"
					"DROP TABLE IF EXISTS baum_inhalt;"
					"DROP TABLE IF EXISTS baum_auswertung; "

					"CREATE TABLE baum_inhalt ("
					"node_id INTEGER PRIMARY KEY,"
					"parent_id INTEGER NOT NULL,"
					"older_sibling_id INTEGER NOT NULL,"
					"icon_name VARCHAR(50),"
					"node_text VARCHAR(200), "
					"FOREIGN KEY (parent_id) REFERENCES baum_inhalt (node_id) "
					"ON DELETE CASCADE ON UPDATE CASCADE, "
					"FOREIGN KEY (older_sibling_id) REFERENCES baum_inhalt (node_id) "
					"ON DELETE CASCADE ON UPDATE CASCADE "
					"); "

					"INSERT INTO baum_inhalt (node_id, parent_id, older_sibling_id, "
					"node_text) VALUES (0, 0, 0, '" MAJOR "');"

			//Hilfstabelle "dateien"
			//hier werden angebundene Dateien erfaßt
			"CREATE TABLE dateien ("
			"rel_path VARCHAR(200) PRIMARY KEY,"
			"node_id INTEGER NOT NULL, "
			"FOREIGN KEY (node_id) REFERENCES baum_inhalt (node_id) "
			"ON DELETE CASCADE ON UPDATE CASCADE);"

			//Hilfstabelle "ziele"
			//hier werden Anbindungen an Dateien mit Zusatzinfo abgelegt
			"CREATE TABLE ziele ("
			"ziel_id_von VARCHAR(50), "
			"index_von INTEGER, "
			"ziel_id_bis VARCHAR(50), "
			"index_bis INTEGER, "
			"rel_path VARCHAR(200) NOT NULL, "
			"node_id INTEGER NOT NULL, "
			"PRIMARY KEY (ziel_id_von, index_von, ziel_id_bis, index_bis), "
			"FOREIGN KEY (rel_path) REFERENCES dateien (rel_path) "
			"ON DELETE CASCADE ON UPDATE CASCADE,"
			"FOREIGN KEY (node_id) REFERENCES baum_inhalt (node_id) "
			"ON DELETE CASCADE ON UPDATE CASCADE );"

			//Auswertungs-Baum
			"CREATE TABLE baum_auswertung ( "
			"node_id INTEGER PRIMARY KEY,"
			"parent_id INTEGER NOT NULL,"
			"older_sibling_id INTEGER NOT NULL,"
			"icon_name VARCHAR(50),"
			"node_text VARCHAR(200),"
			"text VARCHAR, "
			"ref_id INTEGER NULL DEFAULT NULL,"
			"FOREIGN KEY (parent_id) REFERENCES baum_auswertung (node_id) "
			"ON DELETE CASCADE ON UPDATE CASCADE, "
			"FOREIGN KEY (older_sibling_id) REFERENCES baum_auswertung (node_id) "
			"ON DELETE CASCADE ON UPDATE CASCADE, "
			"FOREIGN KEY (ref_id) REFERENCES baum_inhalt (node_id) "
			"ON DELETE RESTRICT ON UPDATE RESTRICT );"

			"INSERT INTO baum_auswertung (node_id, parent_id, older_sibling_id) "
			"VALUES (0, 0, 0); "//mit eingang

			"CREATE TABLE links ( "
			"ID INTEGER PRIMARY KEY AUTOINCREMENT, "//order of appe...
			"baum_id INTEGER, "
			"node_id INTEGER, "
			"projekt_target VARCHAR (200), "
			"baum_id_target INTEGER, "
			"node_id_target INTEGER "
			" ); "

			"CREATE TRIGGER delete_links_baum_inhalt_trigger BEFORE DELETE ON baum_inhalt "
			"BEGIN "
			"DELETE FROM links WHERE node_id=old.node_id AND baum_id=1; "
			//etwaige Links, die auf gelöschten Knoten zeigen, auch löschen
			"DELETE FROM baum_inhalt WHERE node_id = "
			"(SELECT node_id FROM links WHERE node_id_target = old.node_id);"
			"END; "

			"CREATE TRIGGER delete_links_baum_auswertung_trigger BEFORE DELETE ON baum_auswertung "
			"BEGIN "
			"DELETE FROM links WHERE node_id=old.node_id AND baum_id=2; "
			"DELETE FROM baum_auswertung WHERE node_id = "
			"(SELECT node_id FROM links WHERE node_id_target = old.node_id);"
			"END; ";

	rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg_ii);
	if (rc != SQLITE_OK) {
		if (errmsg)
			*errmsg = g_strconcat("Bei Aufruf sqlite3_exec (create db): "
					"\nresult code: ", sqlite3_errstr(rc), "\nerrmsg: ",
					errmsg_ii, NULL);
		sqlite3_free(errmsg_ii);

		return -1;
	}

	return 0;
}

//v0.7: in Tabellen baum_inhalt und baum_auswertung Spalte icon_id statt icon_name in v0.8
static gint zond_convert_from_v0_7(sqlite3 *db_convert, gchar **errmsg) {
	gint rc = 0;
	gchar *errmsg_ii = NULL;

	gchar *sql =
			"INSERT INTO main.baum_inhalt "
					"SELECT node_id, parent_id, older_sibling_id, "
					"CASE icon_id "
					"WHEN 0 THEN 'dialog-error' "
					"WHEN 1 THEN 'emblem-new' "
					"WHEN 2 THEN 'folder' "
					"WHEN 3 THEN 'document-open' "
					"WHEN 4 THEN 'pdf' "
					"WHEN 5 THEN 'anbindung' "
					"WHEN 6 THEN 'akte' "
					"WHEN 7 THEN 'application-x-executable' "
					"WHEN 8 THEN 'text-x-generic' "
					"WHEN 9 THEN 'x-office-document' "
					"WHEN 10 THEN 'x-office-presentation' "
					"WHEN 11 THEN 'x-office-spreadsheet' "
					"WHEN 12 THEN 'emblem-photo' "
					"WHEN 13 THEN 'video-x-generic' "
					"WHEN 14 THEN 'audio-x-generic' "
					"WHEN 15 THEN 'mail-unread' "
					"WHEN 16 THEN 'emblem-web' "
					"WHEN 25 THEN 'system-log-out' "
					"WHEN 26 THEN 'mark-location' "
					"WHEN 27 THEN 'phone' "
					"WHEN 28 THEN 'emblem-important' "
					"WHEN 29 THEN 'camera-web' "
					"WHEN 30 THEN 'media-optical' "
					"WHEN 31 THEN 'user-info' "
					"WHEN 32 THEN 'system-users' "
					"WHEN 33 THEN 'orange' "
					"WHEN 34 THEN 'blau' "
					"WHEN 35 THEN 'rot' "
					"WHEN 36 THEN 'gruen' "
					"WHEN 37 THEN 'tuerkis' "
					"WHEN 38 THEN 'magenta' "
					"ELSE 'process-stop' "
					"END, "
					"node_text FROM old.baum_inhalt WHERE node_id!=0; "
					"INSERT INTO main.dateien SELECT uri, node_id FROM old.dateien; "
					"INSERT INTO main.ziele SELECT ziel_id_von, index_von, ziel_id_bis, index_bis, "
					"(SELECT uri FROM old.dateien WHERE datei_id=old.ziele.datei_id), "
					"node_id FROM old.ziele; "
					"INSERT INTO main.baum_auswertung "
					"SELECT node_id, parent_id, older_sibling_id, "
					"CASE icon_id "
					"WHEN 0 THEN 'dialog-error' "
					"WHEN 1 THEN 'emblem-new' "
					"WHEN 2 THEN 'folder' "
					"WHEN 3 THEN 'document-open' "
					"WHEN 4 THEN 'pdf' "
					"WHEN 5 THEN 'anbindung' "
					"WHEN 6 THEN 'akte' "
					"WHEN 7 THEN 'application-x-executable' "
					"WHEN 8 THEN 'text-x-generic' "
					"WHEN 9 THEN 'x-office-document' "
					"WHEN 10 THEN 'x-office-presentation' "
					"WHEN 11 THEN 'x-office-spreadsheet' "
					"WHEN 12 THEN 'emblem-photo' "
					"WHEN 13 THEN 'video-x-generic' "
					"WHEN 14 THEN 'audio-x-generic' "
					"WHEN 15 THEN 'mail-unread' "
					"WHEN 16 THEN 'emblem-web' "
					"WHEN 25 THEN 'system-log-out' "
					"WHEN 26 THEN 'mark-location' "
					"WHEN 27 THEN 'phone' "
					"WHEN 28 THEN 'emblem-important' "
					"WHEN 29 THEN 'camera-web' "
					"WHEN 30 THEN 'media-optical' "
					"WHEN 31 THEN 'user-info' "
					"WHEN 32 THEN 'system-users' "
					"WHEN 33 THEN 'orange' "
					"WHEN 34 THEN 'blau' "
					"WHEN 35 THEN 'rot' "
					"WHEN 36 THEN 'gruen' "
					"WHEN 37 THEN 'tuerkis' "
					"WHEN 38 THEN 'magenta' "
					"ELSE 'process-stop' "
					"END, "
					"node_text, text, ref_id FROM old.baum_auswertung WHERE node_id!=0; ";

	rc = sqlite3_exec(db_convert, sql, NULL, NULL, &errmsg_ii);
	if (rc != SQLITE_OK) {
		if (errmsg)
			*errmsg = g_strconcat("Bei Aufruf sqlite3_exec:\n"
					"result code: ", sqlite3_errstr(rc), "\nerrmsg: ",
					errmsg_ii, NULL);
		sqlite3_free(errmsg_ii);

		return -1;
	}

	return 0;
}

//v0.8 ist wie Maj_0, aber ohne links
//v0.9 ist wie 0.8, aber mit Tabelle eingang
static gint zond_convert_from_v0_8_or_v0_9(sqlite3 *db_convert, gchar **errmsg) {
	gint rc = 0;

	gchar *sql =
			"INSERT INTO baum_inhalt SELECT node_id, parent_id, older_sibling_id, "
					"icon_name, node_text FROM old.baum_inhalt WHERE node_id != 0; "
					"INSERT INTO baum_auswertung SELECT * FROM old.baum_auswertung WHERE node_id != 0; "
					"INSERT INTO dateien SELECT * FROM old.dateien; "
					"INSERT INTO ziele SELECT * FROM old.ziele; ";

	rc = sqlite3_exec(db_convert, sql, NULL, NULL, errmsg);
	if (rc != SQLITE_OK) {
		if (errmsg)
			*errmsg = add_string(g_strdup("Bei Aufruf sqlite3_exec:\n"),
					*errmsg);

		return -1;
	}

	return 0;
}

//v0.10 ist wie v0.9, aber mit links
static gint zond_convert_from_v0_10(sqlite3 *db_convert, gchar **errmsg) {
	gint rc = 0;

	gchar *sql =
			"INSERT INTO baum_inhalt SELECT node_id, parent_id, older_sibling_id, "
					"icon_name, node_text FROM old.baum_inhalt WHERE node_id != 0; "
					"INSERT INTO baum_auswertung SELECT * FROM old.baum_auswertung WHERE node_id != 0; "
					"INSERT INTO links SELECT * FROM old.links; "
					"INSERT INTO dateien SELECT * FROM old.dateien; "
					"INSERT INTO ziele SELECT * FROM old.ziele; ";

	rc = sqlite3_exec(db_convert, sql, NULL, NULL, errmsg);
	if (rc != SQLITE_OK) {
		if (errmsg)
			*errmsg = add_string(g_strdup("Bei Aufruf sqlite3_exec:\n"),
					*errmsg);

		return -1;
	}

	return 0;
}

static gint zond_convert_from_legacy_to_maj_0(const gchar *path,
		gchar const *v_string, gchar **errmsg) //eingang hinzugefügt
{
	gint rc = 0;
	sqlite3 *db = NULL;
	gchar *sql = NULL;
	gchar *path_old = NULL;
	gchar *path_new = NULL;

	path_new = g_strconcat(path, ".tmp", NULL);
	rc = sqlite3_open(path_new, &db);
	if (rc != SQLITE_OK) {
		if (errmsg)
			*errmsg = g_strconcat("Bei Aufruf "
					"sqlite3_open:\n", sqlite3_errmsg(db), NULL);
		g_free(path_new);

		return -1;
	}

	if (zond_convert_create_db_maj_0(db, errmsg)) {
		sqlite3_close(db);
		g_free(path_new);

		ERROR_S
	}

	sql = g_strdup_printf("ATTACH DATABASE '%s' AS old;", path);
	rc = sqlite3_exec(db, sql, NULL, NULL, errmsg);
	g_free(sql);
	if (rc != SQLITE_OK) {
		if (errmsg)
			*errmsg = g_strconcat("Bei Aufruf sqlite3_exec:\n"
					"result code: ", sqlite3_errstr(rc), "\nerrmsg: ", *errmsg,
					NULL);
		sqlite3_close(db);
		g_free(path_new);

		return -1;
	}

	if (!g_strcmp0(v_string, "v0.7")) {
		rc = zond_convert_from_v0_7(db, errmsg);
		if (rc) {
			sqlite3_close(db);
			g_free(path_new);

			ERROR_S
		}
	} else if (!g_strcmp0(v_string, "v0.8") || !g_strcmp0(v_string, "v0.9")) {
		rc = zond_convert_from_v0_8_or_v0_9(db, errmsg);
		if (rc) {
			sqlite3_close(db);
			g_free(path_new);
			ERROR_S
		}
	} else if (!g_strcmp0(v_string, "v0.10")) {
		rc = zond_convert_from_v0_10(db, errmsg);
		if (rc) {
			sqlite3_close(db);
			g_free(path_new);
			ERROR_S
		}
	}

	sqlite3_close(db);

	path_old = g_strconcat(path, v_string, NULL);
	rc = g_rename(path, path_old);
	g_free(path_old);
	if (rc) {
		if (errmsg)
			*errmsg = g_strconcat("Bei Aufruf g_rename:\n", strerror( errno),
					NULL);
		g_free(path_new);

		return -1;
	}

	rc = g_rename(path_new, path);
	g_free(path_new);
	if (rc) {
		if (errmsg)
			*errmsg = g_strconcat("Bei Aufruf g_rename:\n", strerror( errno),
					NULL);

		return -1;
	}

	return 0;
}

static gint zond_convert_get_younger_sibling_0(ZondDBase *zond_dbase, Baum baum,
		gint node_id, GError **error) {
	gint rc = 0;
	gint younger_sibling = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = {
	//...
			"SELECT node_id "
					"FROM old.baum_inhalt WHERE older_sibling_id = ?1;",

			"SELECT node_id "
					"FROM old.baum_auswertung WHERE older_sibling_id = ?1;" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_int(stmt[baum - 1], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[baum - 1]);
	if (rc != SQLITE_DONE && rc != SQLITE_ROW)
		ERROR_Z_DBASE

	if (rc == SQLITE_ROW)
		younger_sibling = sqlite3_column_int(stmt[baum - 1], 0);

	return younger_sibling;
}

static gint zond_convert_get_first_child_0(ZondDBase *zond_dbase, Baum baum,
		gint node_id, GError **error) {
	gint rc = 0;
	gint first_child_id = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = {
	//...
			"SELECT node_id FROM old.baum_inhalt "
					"WHERE parent_id=? AND older_sibling_id=0 AND node_id!=0;",

			"SELECT node_id FROM old.baum_auswertung "
					"WHERE parent_id=? AND older_sibling_id=0 AND node_id!=0;" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_int(stmt[baum - 1], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[baum - 1]);
	if (rc != SQLITE_DONE && rc != SQLITE_ROW)
		ERROR_Z_DBASE

	if (rc == SQLITE_ROW)
		first_child_id = sqlite3_column_int(stmt[baum - 1], 0);

	return first_child_id;
}

static gint zond_convert_get_node_from_baum_auswertung_0(ZondDBase *zond_dbase,
		gint node_id, gchar **icon_name, gchar **node_text, gchar **text,
		gint *ref_id, gint *is_link_target, gint *baum_target, gint *id_target,
		GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	gchar const *sql[] =
			{
					"SELECT icon_name, node_text, text, ref_id, links_target.node_id, "
							"links_origin.baum_id_target, links_origin.node_id_target "
							"FROM old.baum_auswertung "
							//Nächste Zeile: Knoten, auf die ein Link zeigt
							"LEFT JOIN old.links AS links_target "
							"ON old.baum_auswertung.node_id=links_target.node_id_target AND links_target.baum_id_target=2 "
							"LEFT JOIN old.links AS links_origin ON old.baum_auswertung.node_id=links_origin.node_id "
							"WHERE old.baum_auswertung.node_id=?1; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE && rc != SQLITE_ROW)
		ERROR_Z_DBASE

	if (rc == SQLITE_ROW) {
		*icon_name = g_strdup((gchar const* ) sqlite3_column_text(stmt[0], 0));
		*node_text = g_strdup((gchar const* ) sqlite3_column_text(stmt[0], 1));
		*text = g_strdup((gchar const* ) sqlite3_column_text(stmt[0], 2));
		*ref_id = sqlite3_column_int(stmt[0], 3);
		*is_link_target = sqlite3_column_int(stmt[0], 4);
		*baum_target = sqlite3_column_int(stmt[0], 5);
		*id_target = sqlite3_column_int(stmt[0], 6);
	} else {
		if (error)
			*error = g_error_new( ZOND_ERROR, 0,
					"%s\nnode_id nicht in baum_auswertung gefunden", __func__);
		return -1;
	}

	return 0;
}

static gint zond_convert_get_node_from_baum_inhalt_0(ZondDBase *zond_dbase,
		gint node_id, gchar **icon_name, gchar **node_text, gchar **rel_path,
		gchar **rel_path_ziel, gchar **ziel_von, gint *index_von,
		gchar **ziel_bis, gint *index_bis, gboolean *is_linked, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	gchar const *sql[] =
			{
					"SELECT old.baum_inhalt.icon_name, old.baum_inhalt.node_text, old.dateien.rel_path, old.ziele.rel_path, "
							"ziel_id_von, index_von, ziel_id_bis, index_bis, old.links.node_id, old.baum_auswertung.node_id "
							"FROM old.baum_inhalt "
							"LEFT JOIN old.dateien ON old.baum_inhalt.node_id=old.dateien.node_id "
							"LEFT JOIN old.ziele ON old.baum_inhalt.node_id=old.ziele.node_id "
							"LEFT JOIN old.links ON old.baum_inhalt.node_id=old.links.node_id_target AND old.links.baum_id_target=1 "
							"LEFT JOIN old.baum_auswertung ON old.baum_inhalt.node_id=old.baum_auswertung.ref_id "
							"WHERE old.baum_inhalt.node_id=?1;" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE && rc != SQLITE_ROW)
		ERROR_Z_DBASE

	if (rc == SQLITE_ROW) {
		*icon_name = g_strdup((gchar const* ) sqlite3_column_text(stmt[0], 0));
		*node_text = g_strdup((gchar const* ) sqlite3_column_text(stmt[0], 1));
		*rel_path = g_strdup((gchar const* ) sqlite3_column_text(stmt[0], 2));
		*rel_path_ziel = g_strdup(
				(gchar const* ) sqlite3_column_text(stmt[0], 3));
		*ziel_von = g_strdup((gchar const* ) sqlite3_column_text(stmt[0], 4));
		*index_von = sqlite3_column_int(stmt[0], 5);
		*ziel_bis = g_strdup((gchar const* ) sqlite3_column_text(stmt[0], 6));
		*index_bis = sqlite3_column_int(stmt[0], 7);
		*is_linked = (gboolean) sqlite3_column_int(stmt[0], 8)
				| (gboolean) sqlite3_column_int(stmt[0], 9);
	} else {
		if (error)
			*error = g_error_new( ZOND_ERROR, 0,
					"%s\nnode_id nicht in baum_inhalt gefunden", __func__);
		return -1;
	}

	return 0;
}

typedef struct _DataConvert {
	fz_context *ctx;
	pdf_document *doc;
	FILE *logfile;
} DataConvert;

gint zond_convert_0_to_1_baum_inhalt(ZondDBase*, gint, gboolean, gint,
		DataConvert*, GArray*, gint*, GError**);

static gint zond_convert_0_to_1_baum_inhalt_insert(ZondDBase *zond_dbase,
		gint anchor_id, gboolean child, gint node_id, gchar const *icon_name,
		gchar const *node_text, gchar const *rel_path,
		gchar const *rel_path_ziel, gchar const *ziel_von, gint index_von,
		gchar const *ziel_bis, gint index_bis, DataConvert *data_convert,
		GArray *arr_targets, gint *node_inserted, GError **error) {
	if (!rel_path && !ziel_von) //STRUKT
			{
		*node_inserted = zond_dbase_insert_node(zond_dbase, anchor_id, child,
				ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL, icon_name,
				node_text, NULL, error);
		if (*node_inserted == -1)
			ERROR_Z
	} else if (rel_path && !ziel_von) //datei
			{
		//erstmal Test, ob file da und zu öffnen
		gint fd = 0;
		gchar *filename = NULL;
		gchar *charset = NULL;
		gboolean exists = TRUE;
		gint node_inserted_root = 0;
		gint first_child_file = 0;

		charset = g_get_codeset();
		filename = g_convert(rel_path, -1, charset, "UTF-8", NULL, NULL, error);
		g_free(charset);
		if (!filename)
			ERROR_Z

		fd = open(filename, O_RDONLY);
		g_free(filename);
		if (fd < 0) {
			gchar *errmsg = NULL;
			size_t count = 0;

			exists = FALSE;

			errmsg = g_strdup_printf("'%s' konnte nicht geöffnet werden: %s\n",
					rel_path, strerror( errno));
			count = fwrite(errmsg, sizeof(gchar), strlen(errmsg),
					data_convert->logfile);
			if (count < strlen(errmsg))
				g_warning(
						"Nachricht '%s' konnte nicht ins Logfile geschrieben werden",
						errmsg);
			g_free(errmsg);

			//Als Strukt einfügen
			*node_inserted = zond_dbase_insert_node(zond_dbase, anchor_id,
					child, ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL,
					icon_name, node_text, NULL, error);
			if (*node_inserted == -1)
				ERROR_Z
		} else //Datei existiert und kann geöffnet werden
		{
			close(fd);

			gint rc = 0;
			gchar *file_part = NULL;

			file_part = g_strdup_printf("/%s//", rel_path);

			//FILE_PART_ROOT einfügen
			rc = zond_dbase_create_file_root(zond_dbase, file_part, icon_name,
					node_text, NULL, &node_inserted_root, error);
			g_free(file_part);
			if (rc)
				ERROR_Z

					//BAUM_INHALT_FILE_PART einfügen
			*node_inserted = zond_dbase_insert_node(zond_dbase, anchor_id,
					child, ZOND_DBASE_TYPE_BAUM_INHALT_FILE, node_inserted_root,
					NULL, NULL, NULL, NULL, NULL, error);
			if (*node_inserted == -1)
				ERROR_Z
		}

		//neue Rekursion mit FILE_PART_ROOT starten
		//in Version bis 0 ja nur bei PDF-Dateien möglich
		//d.h. Test, ob es sich um PDF handelt, überflüssig
		first_child_file = zond_convert_get_first_child_0(zond_dbase,
				BAUM_INHALT, node_id, error);
		if (first_child_file == -1)
			ERROR_Z

		if (first_child_file) {
			gint rc = 0;
			gint node_inserted_file = 0;
			gint anchor_child = 0;

			anchor_child = *node_inserted;

			if (exists) //wenn Datei nicht existiert, muß man nicht versuchen, sie zu öffnen
			{
				gchar *file_part = NULL;

				file_part = g_strdup_printf("/%s//", rel_path);

				rc = pdf_open_and_authen_document(data_convert->ctx, TRUE, TRUE,
						file_part, NULL, &(data_convert->doc), NULL, error);
				g_free(file_part);
				if (rc) {
					gchar *errmsg = NULL;
					size_t count = 0;

					errmsg = g_strdup_printf(
							"PDF '%s' konnte nicht geöffnet werden: %s\n",
							rel_path, (*error)->message);
					count = fwrite(errmsg, sizeof(gchar), strlen(errmsg),
							data_convert->logfile);
					if (count < strlen(errmsg))
						g_warning(
								"Nachricht '%s' konnte nicht ins Logfile geschrieben werden",
								errmsg);
					g_free(errmsg);

					g_clear_error(error);
				}

				anchor_child = node_inserted_root;
			}

			rc = zond_convert_0_to_1_baum_inhalt(zond_dbase, anchor_child, TRUE,
					first_child_file, data_convert, arr_targets,
					&node_inserted_file, error);
			pdf_drop_document(data_convert->ctx, data_convert->doc);
			data_convert->doc = NULL;
			if (rc)
				ERROR_Z
		}
	} else if (!rel_path && ziel_von) {
		gchar *file_part = NULL;
		gchar *section = NULL;
		Anbindung anbindung = { 0 };
		gchar *errmsg = NULL;

		if (data_convert->doc) //doc wurde nicht nicht gefunden
		{
			anbindung.von.seite = treeviews_get_page_num_from_dest_doc(
					data_convert->ctx, data_convert->doc, ziel_von, &errmsg);
			if (anbindung.von.seite == -1) {
				if (error)
					*error = g_error_new( ZOND_ERROR, 0, "%s\n%s", __func__,
							errmsg);
				g_free(errmsg);

				return -1;
			}

			anbindung.bis.seite = treeviews_get_page_num_from_dest_doc(
					data_convert->ctx, data_convert->doc, ziel_bis, &errmsg);
			if (anbindung.bis.seite == -1) {
				if (error)
					*error = g_error_new( ZOND_ERROR, 0, "%s\n%s", __func__,
							errmsg);
				g_free(errmsg);

				return -1;
			}

			anbindung.von.index = index_von;
			anbindung.bis.index = index_bis;

			anbindung_build_file_section(anbindung, &section);
			file_part = g_strdup_printf("/%s//", rel_path_ziel);
			*node_inserted = zond_dbase_insert_node(zond_dbase, anchor_id,
					child, ZOND_DBASE_TYPE_FILE_PART, 0, file_part, section,
					icon_name, node_text, NULL, error);
			g_free(section);
			g_free(file_part);
			if (*node_inserted == -1)
				ERROR_Z
		} else {
			gchar *logmsg = NULL;
			size_t count = 0;

			*node_inserted = zond_dbase_insert_node(zond_dbase, anchor_id,
					child, ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL,
					icon_name, node_text, NULL, error);
			if (*node_inserted == -1)
				ERROR_Z

			logmsg = g_strdup_printf("  Abschnitt Von: %s,%d   Bis: %s,%d\n",
					ziel_von, index_von, ziel_bis, index_bis);
			count = fwrite(logmsg, sizeof(gchar), strlen(logmsg),
					data_convert->logfile);
			if (count < strlen(logmsg))
				g_warning(
						"Nachricht '%s' konnte nicht ins Logfile geschrieben werden",
						logmsg);
			g_free(logmsg);
		}
	}

	return 0;
}

typedef struct _Target {
	gint baum_target;
	gint id_target;
	gint id_target_new;
} Target;

gint zond_convert_0_to_1_baum_inhalt(ZondDBase *zond_dbase, gint anchor_id,
		gboolean child, gint node_id, DataConvert *data_convert,
		GArray *arr_targets, gint *node_inserted, GError **error) {
	gint rc = 0;
	gchar *icon_name = NULL;
	gchar *node_text = NULL;
	gchar *rel_path = NULL;
	gchar *rel_path_ziel = NULL;
	gchar *ziel_von = NULL;
	gint index_von = 0;
	gchar *ziel_bis = NULL;
	gint index_bis = 0;
	gboolean is_linked = 0;
	gint younger_sibling = 0;
	gboolean stop_rec = FALSE;

	rc = zond_convert_get_node_from_baum_inhalt_0(zond_dbase, node_id,
			&icon_name, &node_text, &rel_path, &rel_path_ziel, &ziel_von,
			&index_von, &ziel_bis, &index_bis, &is_linked, error);
	if (rc)
		ERROR_Z

	rc = zond_convert_0_to_1_baum_inhalt_insert(zond_dbase, anchor_id, child,
			node_id, icon_name, node_text, rel_path, rel_path_ziel, ziel_von,
			index_von, ziel_bis, index_bis, data_convert, arr_targets,
			node_inserted, error);
	if (rel_path)
		stop_rec = TRUE; //datei-root
	g_free(icon_name);
	g_free(node_text);
	g_free(rel_path);
	g_free(rel_path_ziel);
	g_free(ziel_von);
	g_free(ziel_bis);
	if (rc)
		ERROR_Z

	if (is_linked) {
		Target target = { BAUM_INHALT, node_id, *node_inserted };
		g_array_append_val(arr_targets, target);
	}

	if (!stop_rec) //weil ist Datei; neuer Zweig wird eingefügt
	{
		gint first_child = 0;

		first_child = zond_convert_get_first_child_0(zond_dbase, BAUM_INHALT,
				node_id, error);
		if (first_child == -1)
			ERROR_Z

		if (first_child > 0) {
			gint rc = 0;
			gint node_inserted_loop = 0;

			rc = zond_convert_0_to_1_baum_inhalt(zond_dbase, *node_inserted,
					TRUE, first_child, data_convert, arr_targets,
					&node_inserted_loop, error);
			if (rc)
				ERROR_Z
		}
	}

	younger_sibling = zond_convert_get_younger_sibling_0(zond_dbase,
			BAUM_INHALT, node_id, error);
	if (younger_sibling == -1)
		ERROR_Z

	if (younger_sibling > 0) {
		gint rc = 0;
		gint node_inserted_loop = 0;

		rc = zond_convert_0_to_1_baum_inhalt(zond_dbase, *node_inserted, FALSE,
				younger_sibling, data_convert, arr_targets, &node_inserted_loop,
				error);
		if (rc)
			ERROR_Z
	}

	return 0;
}

typedef struct _Links {
	gint node_id_new;
	gint baum_target;
	gint id_target;
} Links;

static gint zond_convert_0_to_1_baum_auswertung(ZondDBase *zond_dbase,
		gint anchor_id, gboolean child, gint node_id, GArray *arr_links,
		GArray *arr_targets, GError **error) {
	gint rc = 0;
	gchar *icon_name = NULL;
	gchar *node_text = NULL;
	gchar *text = NULL;
	gint ref_id = 0;
	gint is_linked = 0;
	gint baum_target = 0;
	gint id_target = 0;
	gint first_child = 0;
	gint younger_sibling = 0;
	gint node_inserted = 0;

	rc = zond_convert_get_node_from_baum_auswertung_0(zond_dbase, node_id,
			&icon_name, &node_text, &text, &ref_id, &is_linked, &baum_target,
			&id_target, error);
	if (rc)
		ERROR_Z

	if (id_target) //ist ein Link, hat target gespeichert
	{
		node_inserted = zond_dbase_insert_node(zond_dbase, anchor_id, child,
				ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK, 0, NULL, NULL, NULL, NULL,
				NULL, error);
		if (node_inserted == -1)
			ERROR_Z

		Links link = { node_inserted, baum_target, id_target };
		g_array_append_val(arr_links, link);
	} else {
		if (!ref_id) //STRUKT
		{
			node_inserted = zond_dbase_insert_node(zond_dbase, anchor_id, child,
					ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL, icon_name,
					node_text, text, error);
			if (node_inserted == -1)
				ERROR_Z
		} else //datei
		{
			node_inserted = zond_dbase_insert_node(zond_dbase, anchor_id, child,
					ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY, ref_id, NULL, NULL,
					icon_name, node_text, text, error);
			if (node_inserted == -1)
				ERROR_Z

			Links link = { node_inserted, BAUM_INHALT, ref_id };
			g_array_append_val(arr_links, link);
		}

		if (is_linked) {
			Target target = { BAUM_AUSWERTUNG, node_id, node_inserted };
			g_array_append_val(arr_targets, target);
		}

		first_child = zond_convert_get_first_child_0(zond_dbase,
				BAUM_AUSWERTUNG, node_id, error);
		if (first_child == -1)
			ERROR_Z

		if (first_child > 0) {
			gint rc = 0;

			rc = zond_convert_0_to_1_baum_auswertung(zond_dbase, node_inserted,
					TRUE, first_child, arr_links, arr_targets, error);
			if (rc)
				ERROR_Z
		}

		younger_sibling = zond_convert_get_younger_sibling_0(zond_dbase,
				BAUM_AUSWERTUNG, node_id, error);
		if (younger_sibling == -1)
			ERROR_Z

		if (younger_sibling > 0) {
			gint rc = 0;

			rc = zond_convert_0_to_1_baum_auswertung(zond_dbase, node_inserted,
					FALSE, younger_sibling, arr_links, arr_targets, error);
			if (rc)
				ERROR_Z
		}
	}

	return 0;
}

gint zond_convert_0_to_1_update_link(ZondDBase *zond_dbase, gint node_id,
		gint link, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "UPDATE knoten "
			"SET link=?2 WHERE ID=?1; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_int(stmt[0], 2, link);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE)
		ERROR_Z_DBASE

	return 0;
}

static gint zond_convert_do_0_to_1(ZondDBase *zond_dbase,
		DataConvert *data_convert, GArray *arr_targets, GArray *arr_links,
		GError **error) {
	gint first_child = 0;

	//ersten Knoten Baum_inhalt
	first_child = zond_convert_get_first_child_0(zond_dbase, BAUM_INHALT, 0,
			error);
	if (first_child == -1)
		ERROR_Z

	if (first_child) {
		gint rc = 0;
		gint node_inserted = 0;

		data_convert->ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
		if (!data_convert->ctx) {
			if (error)
				*error = g_error_new( ZOND_ERROR, 0,
						"%s\nfz_context konnte nicht initialisiert werden",
						__func__);

			return -1;
		}

		rc = zond_convert_0_to_1_baum_inhalt(zond_dbase, 1, TRUE, first_child,
				data_convert, arr_targets, &node_inserted, error);
		fz_drop_context(data_convert->ctx);
		if (rc)
			ERROR_Z
	}

	//ersten Knoten Baum_auswertung
	first_child = zond_convert_get_first_child_0(zond_dbase, BAUM_AUSWERTUNG, 0,
			error);
	if (first_child == -1)
		ERROR_Z

	if (first_child) {
		gint rc = 0;

		rc = zond_convert_0_to_1_baum_auswertung(zond_dbase, 2, TRUE,
				first_child, arr_links, arr_targets, error);
		if (rc)
			ERROR_Z
	}

	//arr_targets durchgehen
	for (gint i = 0; i < arr_links->len; i++) {
		Links link = { 0 };

		link = g_array_index(arr_links, Links, i);

		for (gint u = 0; u < arr_targets->len; u++) {
			Target target = { 0 };

			target = g_array_index(arr_targets, Target, u);

			if (link.baum_target == target.baum_target
					&& link.id_target == target.id_target) {
				gint rc = 0;
				gint type = 0;
				gint link_node = 0;

				rc = zond_dbase_get_type_and_link(zond_dbase,
						target.id_target_new, &type, &link_node, error);
				if (rc)
					ERROR_Z

				if (type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE)
					target.id_target_new = link_node;

				rc = zond_convert_0_to_1_update_link(zond_dbase,
						link.node_id_new, target.id_target_new, error);
				if (rc)
					ERROR_Z

				break;
			}
		}
	}

	return 0;
}

static gint zond_convert_0_to_1(ZondDBase *zond_dbase, GError **error) {
	DataConvert data_convert = { 0 };
	GArray *arr_targets = NULL;
	GArray *arr_links = NULL;
	gchar *project_dir = NULL;
	gchar *filename = NULL;
	gint rc = 0;
	size_t count = 0;
	gchar const *message = NULL;

	project_dir = g_path_get_dirname(zond_dbase_get_path(zond_dbase));
	g_chdir(project_dir);
	g_free(project_dir);

	//Log_datei öffnen
	filename = g_strdup_printf("%s_conv_from_v0_to_v1.log",
			zond_dbase_get_path(zond_dbase));
	data_convert.logfile = fopen(filename, "wb");
	g_free(filename);
	if (!data_convert.logfile) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("stdlib"), errno,
					strerror( errno));
		return -1;
	}

	arr_targets = g_array_new( FALSE, FALSE, sizeof(Target));
	arr_links = g_array_new( FALSE, FALSE, sizeof(Links));

	rc = zond_convert_do_0_to_1(zond_dbase, &data_convert, arr_targets,
			arr_links, error);
	g_array_unref(arr_links);
	g_array_unref(arr_targets);

	message = (rc) ? "Konvertierung abgebrochen" : "Konvertierung beendet";

	count = fwrite(message, sizeof(gchar), strlen(message),
			data_convert.logfile);
	fclose(data_convert.logfile);

	if (count < strlen(message))
		g_warning("Log-Datei konnte nicht beschrieben werden");

	if (rc)
		ERROR_Z

	return 0;
}

static gint zond_convert_from_maj_0_to_1(ZondDBase *zond_dbase, GError **error) {
	gint rc = 0;
	gchar const *path = NULL;
	gchar *sql = NULL;
	gchar *path_old = NULL;
	gchar *path_new = NULL;
	gchar *errmsg = NULL;
	sqlite3 *db = NULL;

	path = zond_dbase_get_path(zond_dbase);

	path_new = g_strconcat(path, ".tmp", NULL);
	rc = sqlite3_open(path_new, &db);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		g_free(path_new);

		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE"), rc,
					"%s\nsqlite3_open\n%s", __func__, sqlite3_errstr(rc));

		return -1;
	}

	if (zond_dbase_create_db_maj_1(db, error)) {
		sqlite3_close(db);
		g_free(path_new);

		ERROR_Z
	}

	sql = g_strdup_printf("ATTACH DATABASE '%s' AS old;", path);
	rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
	g_free(sql);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		g_free(path_new);

		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
					"%s\n%s", __func__, errmsg);
		g_free(errmsg);

		return -1;
	}

	g_object_set(zond_dbase, "dbase", db, NULL);

	rc = zond_convert_0_to_1(zond_dbase, error);
	zond_dbase_finalize_stmts(zond_dbase_get_dbase(zond_dbase));
	sqlite3_close(zond_dbase_get_dbase(zond_dbase));
	g_object_set(zond_dbase, "dbase", NULL, NULL);
	if (rc) {
		g_free(path_new);

		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	path_old = g_strconcat(path, "v0", NULL);
	rc = g_rename(path, path_old);
	g_free(path_old);
	if (rc) {
		if (error)
			*error = g_error_new( ZOND_ERROR, 0,
					"%s\ng_rename (v0) gibt Fehlermeldung " "%s" " zurück",
					__func__, strerror( errno));
		g_free(path_new);

		return -1;
	}

	rc = g_rename(path_new, path);
	g_free(path_new);
	if (rc) {
		if (error)
			*error = g_error_new( ZOND_ERROR, 0,
					"%s\ng_rename gibt Fehlermeldung " "%s" " zurück", __func__,
					strerror( errno));

		return -1;
	}

	return 0;
}

gint zond_convert(ZondDBase *zond_dbase, gchar const *v_string, GError **error) {
	if (v_string[0] == 'v') //legacy...
			{
		gint rc = 0;
		gchar *errmsg = NULL;

		rc = zond_convert_from_legacy_to_maj_0(zond_dbase_get_path(zond_dbase),
				v_string, &errmsg);
		if (rc) {
			if (error)
				*error = g_error_new( ZOND_ERROR, 0, "%s\n%s", __func__,
						errmsg);
			g_free(errmsg);

			return -1;
		}
	} else if (!g_ascii_isdigit(v_string[0])) {
		if (error)
			*error = g_error_new( ZOND_ERROR, 0,
					"%s\nUnbekannte Versionsbezeichnung", __func__);

		return -1;
	}

	if (atoi(v_string) > atoi( MAJOR)) {
		if (error)
			*error = g_error_new( ZOND_ERROR, 0,
					"%s\nVersion noch nicht verfügbar", __func__);

		return -1;
	} else if (atoi(v_string) == 0) //ist auch, wenn v_string[0] 'v' ist, dann ist aber zu 0 umgewandelt. Paßt also
			{
		gint rc = 0;

		//aktewalisieren von maj_0 auf maj_1
		rc = zond_convert_from_maj_0_to_1(zond_dbase, error);
		if (rc == -1)
			ERROR_Z
	}
	//später: if ( atoi( v_string ) == 1 ) ...

	return 0;
}

