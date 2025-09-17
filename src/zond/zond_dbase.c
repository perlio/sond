/*
 zond (zond_dbase.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2022  pelo america

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

#include "zond_dbase.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <gtk/gtk.h>

#include "zond_convert.h"

#include "20allgemein/ziele.c"

#include "../misc.h"

typedef enum {
	PROP_PATH = 1, PROP_DBASE, N_PROPERTIES
} ZondDBaseProperty;

typedef struct {
	gchar *path;
	sqlite3 *dbase;
} ZondDBasePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondDBase, zond_dbase, G_TYPE_OBJECT)

static void zond_dbase_set_property(GObject *object, guint property_id,
		const GValue *value, GParamSpec *pspec) {
	ZondDBase *self = ZOND_DBASE(object);
	ZondDBasePrivate *priv = zond_dbase_get_instance_private(self);

	switch ((ZondDBaseProperty) property_id) {
	case PROP_PATH:
		g_free(priv->path);
		priv->path = g_value_dup_string(value);
		break;

	case PROP_DBASE:
		priv->dbase = g_value_get_pointer(value);
		break;

	default:
		/* We don't have any other property... */
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void zond_dbase_get_property(GObject *object, guint property_id,
		GValue *value, GParamSpec *pspec) {
	ZondDBase *self = ZOND_DBASE(object);
	ZondDBasePrivate *priv = zond_dbase_get_instance_private(self);

	switch ((ZondDBaseProperty) property_id) {
	case PROP_PATH:
		g_value_set_string(value, priv->path);
		break;

	case PROP_DBASE:
		g_value_set_pointer(value, priv->dbase);
		break;

	default:
		/* We don't have any other property... */
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

void zond_dbase_finalize_stmts(sqlite3 *db) {
	sqlite3_stmt *stmt = NULL;

	while ((stmt = sqlite3_next_stmt(db, NULL)))
		sqlite3_finalize(stmt);

	return;
}

static void zond_dbase_finalize(GObject *self) {
	ZondDBasePrivate *priv = zond_dbase_get_instance_private(ZOND_DBASE(self));

	if (priv->dbase)
		zond_dbase_finalize_stmts(priv->dbase);
	sqlite3_close(priv->dbase);

	g_free(priv->path);

	G_OBJECT_CLASS(zond_dbase_parent_class)->finalize(self);

	return;
}

static void zond_dbase_class_init(ZondDBaseClass *klass) {
	GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = zond_dbase_finalize;

	object_class->set_property = zond_dbase_set_property;
	object_class->get_property = zond_dbase_get_property;

	obj_properties[PROP_PATH] = g_param_spec_string("path", "gchar*",
			"Pfad zur Datei.",
			NULL, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

	obj_properties[PROP_DBASE] = g_param_spec_pointer("dbase", "sqlite3*",
			"Datenbankverbindung.", G_PARAM_READWRITE);

	g_object_class_install_properties(object_class, N_PROPERTIES,
			obj_properties);

	return;
}

static void zond_dbase_init(ZondDBase *self) {
//    ZondDBasePrivate* priv = zond_dbase_get_instance_private( self );

	return;
}

gint zond_dbase_create_db_maj_1(sqlite3 *db, GError **error) {
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
					"node_text) VALUES (0, 0, 0, '" MAJOR "');"
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

static gchar*
zond_dbase_get_version(sqlite3 *db, gchar **errmsg) {
	gint rc = 0;
	sqlite3_stmt *stmt = NULL;
	gchar *v_string = NULL;

	rc = sqlite3_prepare_v2(db, "SELECT node_text FROM knoten WHERE ID = 0;",
			-1, &stmt, NULL);
	if (rc != SQLITE_OK) //vielleicht weil maj=0
	{
		rc = sqlite3_prepare_v2(db,
				"SELECT node_text FROM baum_inhalt WHERE node_id = 0;", -1,
				&stmt, NULL);
		if (rc != SQLITE_OK) {
			if (errmsg)
				*errmsg = g_strconcat("Bei Aufruf sqlite3_prepare_v2:\n",
						sqlite3_errstr(rc), NULL);

			return NULL;
		}
	}

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		if (errmsg)
			*errmsg = add_string(
					g_strconcat("Bei Aufruf sqlite3_step:\n",
							sqlite3_errmsg(db), NULL), *errmsg);
		sqlite3_finalize(stmt);

		return NULL;
	}

	if (!sqlite3_column_text(stmt, 0)
			|| !g_strcmp0((const gchar*) sqlite3_column_text(stmt, 0), "")) {
		sqlite3_finalize(stmt);
		ERROR_S_MESSAGE_VAL("ZND-Datei enthält keine Versionsbezeichnung", NULL)
	}

	v_string = g_strdup((const gchar* ) sqlite3_column_text(stmt, 0));

	sqlite3_finalize(stmt);

	return v_string;
}

static gint zond_dbase_open(ZondDBase *zond_dbase, gboolean create_file,
		gboolean create, gchar **errmsg) {
	gint rc = 0;

	ZondDBasePrivate *zond_dbase_priv = zond_dbase_get_instance_private(
			zond_dbase);

	rc = sqlite3_open_v2(zond_dbase_priv->path, &(zond_dbase_priv->dbase),
			SQLITE_OPEN_READWRITE
					| ((create_file || create) ? SQLITE_OPEN_CREATE : 0), NULL);
	if (rc != SQLITE_OK) //Datei nicht vorhanden und weder create_file noch create
	{
		if (errmsg)
			*errmsg = g_strconcat("Bei Aufruf sqlite3_open_v2:\n",
					sqlite3_errstr(rc), NULL);

		return -1;
	} else if (!(create_file || create)) //Alt-Datei war vorhanden - Versions-Check
	{
		gchar *v_string = NULL;

		v_string = zond_dbase_get_version(zond_dbase_priv->dbase, errmsg);
		if (!v_string)
			ERROR_S

		if (g_strcmp0(v_string, MAJOR)) {
			gint rc = 0;
			GError *error = NULL;
			gchar *message = NULL;

			message = g_strdup_printf(
					"Projekt in abweichender Version (%s) gespeichert",
					v_string);
			rc = abfrage_frage( NULL, message, "Konvertieren", NULL);
			g_free(message);

			if (rc != GTK_RESPONSE_YES) {
				if (errmsg)
					*errmsg = g_strdup_printf("%s\nFalsche Projektversion (%s)",
							__func__, v_string);
				g_free(v_string);

				return -1;
			}

			sqlite3_close(zond_dbase_priv->dbase);

			rc = zond_convert(zond_dbase, v_string, &error);
			g_free(v_string);
			if (rc) {
				if (errmsg)
					*errmsg = g_strdup_printf("%s\n%s", __func__,
							error->message);
				g_error_free(error);

				return -1;
			}

			rc = sqlite3_open_v2(zond_dbase_priv->path,
					&(zond_dbase_priv->dbase), SQLITE_OPEN_READWRITE, NULL);
			if (rc != SQLITE_OK) //Datei nicht vorhanden und weder create_file noch create
			{
				if (errmsg)
					*errmsg = g_strconcat("Bei Aufruf sqlite3_open_v2:\n",
							sqlite3_errstr(rc), NULL);

				return -1;
			}
		} else
			g_free(v_string);
	} else if (create) //Datenbank soll neu angelegt werden
	{
		gint rc = 0;
		GError *error = NULL;

		//Abfrage, ob überschrieben werden soll, überflüssig - schon im filechooser
		rc = zond_dbase_create_db_maj_1(zond_dbase_priv->dbase, &error);
		if (rc) {
			if (errmsg)
				*errmsg = g_strdup_printf("%s\n%s", __func__, error->message);
			g_error_free(error);

			return -1;
		}
	}

	rc = sqlite3_exec(zond_dbase_priv->dbase,
			"PRAGMA foreign_keys = ON; PRAGMA recursive_triggers = 1; ",
			NULL, NULL, errmsg);
	if (rc != SQLITE_OK)
		ERROR_S

	return 0;
}

ZondDBase*
zond_dbase_new(const gchar *path, gboolean create_file, gboolean create,
		gchar **errmsg) {
	gint rc = 0;
	ZondDBase *zond_dbase = NULL;

	g_return_val_if_fail(path, NULL);

	zond_dbase = g_object_new( ZOND_TYPE_DBASE, "path", path, NULL);

	rc = zond_dbase_open(zond_dbase, create_file, create, errmsg);
	if (rc == -1) {
		g_object_unref(zond_dbase);

		ERROR_S_VAL(NULL)
	}

	return zond_dbase;
}

void zond_dbase_close(ZondDBase *zond_dbase) {
	g_object_unref(zond_dbase);

	return;
}

sqlite3*
zond_dbase_get_dbase(ZondDBase *zond_dbase) {
	ZondDBasePrivate *priv = zond_dbase_get_instance_private(zond_dbase);

	return priv->dbase;
}

const gchar*
zond_dbase_get_path(ZondDBase *zond_dbase) {
	ZondDBasePrivate *priv = zond_dbase_get_instance_private(zond_dbase);

	return priv->path;
}

gint zond_dbase_backup(ZondDBase *src, ZondDBase *dst, gchar **errmsg) {
	gint rc = 0;
	sqlite3 *db_src = NULL;
	sqlite3 *db_dst = NULL;
	sqlite3_backup *backup = NULL;

	db_src = zond_dbase_get_dbase(src);
	db_dst = zond_dbase_get_dbase(dst);

	//Datenbank öffnen
	backup = sqlite3_backup_init(db_dst, "main", db_src, "main");
	if (!backup) {
		if (errmsg)
			*errmsg = g_strconcat(__func__, "\nsqlite3_backup_init\n",
					sqlite3_errmsg(db_dst), NULL);

		return -1;
	}
	rc = sqlite3_backup_step(backup, -1);
	sqlite3_backup_finish(backup);
	if (rc != SQLITE_DONE) {
		if (errmsg && rc == SQLITE_NOTADB)
			*errmsg = g_strdup("Datei ist "
					"keine SQLITE-Datenbank");
		else if (errmsg)
			*errmsg = g_strconcat(__func__, "\nsqlite3_backup_step:\n",
					sqlite3_errmsg(db_dst), NULL);

		return -1;
	}

	return 0;
}

static gint zond_dbase_prepare_stmts(ZondDBase *zond_dbase, gint num,
		const gchar **sql, sqlite3_stmt **stmt, GError **error) {
	for (gint i = 0; i < num; i++) {
		gint rc = 0;

		ZondDBasePrivate *priv = zond_dbase_get_instance_private(zond_dbase);

		rc = sqlite3_prepare_v2(priv->dbase, sql[i], -1, &stmt[i], NULL);
		if (rc != SQLITE_OK) {
			//aufräumen
			for (gint u = 0; u < i; u++)
				sqlite3_finalize(stmt[u]);

			ERROR_Z_DBASE
		}
	}

	return 0;
}

gint zond_dbase_prepare(ZondDBase *zond_dbase, const gchar *func,
		const gchar **sql, gint num_stmts, sqlite3_stmt ***stmt, GError **error) {
	if (!(*stmt = g_object_get_data(G_OBJECT(zond_dbase), func))) {
		gint rc = 0;

		*stmt = g_malloc0(sizeof(sqlite3_stmt*) * num_stmts);

		rc = zond_dbase_prepare_stmts(zond_dbase, num_stmts, sql, *stmt, error);
		if (rc) {
			g_free(*stmt);

			ERROR_Z
		}

		g_object_set_data_full(G_OBJECT(zond_dbase), func, *stmt, g_free);
	} else
		for (gint i = 0; i < num_stmts; i++)
			sqlite3_reset((*stmt)[i]);

	return 0;
}

gint zond_dbase_begin(ZondDBase *zond_dbase, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "BEGIN; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	return 0;
}

gint zond_dbase_commit(ZondDBase *zond_dbase, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "COMMIT; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	return 0;
}

gint zond_dbase_rollback(ZondDBase *zond_dbase, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "ROLLBACK; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	return 0;
}

static gint zond_dbase_rollback_to_statement(ZondDBase *zond_dbase,
		GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "ROLLBACK TO statement; " };

	//Prüfen, ob schon Rollback vAw
	if (sqlite3_get_autocommit(zond_dbase_get_dbase(zond_dbase)))
		return 0;

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	return 0;
}

gint zond_dbase_test_path(ZondDBase *zond_dbase, const gchar *filepart,
		GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "SELECT k1.ID FROM knoten k1 INNER JOIN "
			"knoten k2 ON k1.link=k2.ID WHERE k1.type=2 AND k2.file_part LIKE ?1 COLLATE BINARY; "
	};

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_text(stmt[0], 1, filepart, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if ((rc != SQLITE_ROW) && (rc != SQLITE_DONE))
		ERROR_Z_DBASE

	if (rc == SQLITE_ROW)
		return 1;

	return 0;
}

gint zond_dbase_test_path_section(ZondDBase *zond_dbase, const gchar *filepart,
		gchar const* section, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;
	Anbindung anbindung = { 0 };

	const gchar *sql[] = { "SELECT k1.ID, k1.section FROM knoten k1 INNER JOIN "
			"knoten k2 ON k1.link=k2.ID WHERE k1.type=2 AND k2.file_part LIKE ?1 COLLATE BINARY; "
	};

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_text(stmt[0], 1, filepart, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	anbindung_parse_file_section(section, &anbindung);

	do {
		Anbindung anbindung_int = { 0 };

		rc = sqlite3_step(stmt[0]);
		if ((rc != SQLITE_ROW) && (rc != SQLITE_DONE))
			ERROR_Z_DBASE

		if (rc == SQLITE_ROW) {
			guchar const* section_db = NULL;

			section_db = sqlite3_column_text(stmt[0], 1);
			anbindung_parse_file_section((gchar const*) section_db, &anbindung_int);

			if (anbindung_1_eltern_von_2(anbindung_int, anbindung) ||
					anbindung_1_gleich_2(anbindung_int, anbindung))
				return 1;

		}
	} while (rc == SQLITE_ROW);

	return 0;
}

gint zond_dbase_insert_node(ZondDBase *zond_dbase, gint anchor_ID,
		gboolean child, gint type, gint link, const gchar *file_part,
		gchar const *section, const gchar *icon_name, const gchar *node_text,
		const gchar *text, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] =
			{ "SAVEPOINT statement; ",

					"INSERT INTO knoten "
							"(parent_id, older_sibling_id, type, link, file_part, section, icon_name, node_text, text ) "
							"VALUES ("
							"CASE ?1 " //child
							"WHEN 0 THEN (SELECT parent_ID FROM knoten WHERE ID=?2) "
							"WHEN 1 THEN ?2 "//anchor_id
							"END, "
							"CASE ?1 "//child
							"WHEN 0 THEN ?2 "
							"WHEN 1 THEN 0 "
							"END, "
							"?3, "//type
							"?4, "//link
							"?5, "//file_part
							"?6, "//section
							"?7, "//icon_name
							"?8, "//node_text
							"?9 "//text

							"); ",

					"UPDATE knoten SET older_sibling_ID=last_insert_rowid() "
							"WHERE "
							"parent_ID=(SELECT parent_ID FROM knoten WHERE ID=last_insert_rowid()) "
							"AND "
							"older_sibling_ID=(SELECT older_sibling_ID FROM knoten WHERE ID=last_insert_rowid()) "
							"AND "
							"ID!=last_insert_rowid() "
							"AND "
							"ID!=0; ",

					"VALUES (last_insert_rowid()); ",

					"RELEASE statement; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	//Damit alles auf NULL gestellt wird
	sqlite3_clear_bindings(stmt[1]);

	rc = sqlite3_bind_int(stmt[1], 1, child);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_int(stmt[1], 2, anchor_ID);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_int(stmt[1], 3, type);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	if (link) {
		rc = sqlite3_bind_int(stmt[1], 4, link);
		if (rc != SQLITE_OK)
			ERROR_Z_DBASE
	}

	rc = sqlite3_bind_text(stmt[1], 5, file_part, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_text(stmt[1], 6, section, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_text(stmt[1], 7, icon_name, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_text(stmt[1], 8, node_text, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_text(stmt[1], 9, text, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[1]);
	if (rc != SQLITE_DONE)
		ERROR_Z_ROLLBACK

	rc = sqlite3_step(stmt[2]);
	if (rc != SQLITE_DONE)
		ERROR_Z_ROLLBACK

	rc = sqlite3_step(stmt[3]);
	if (rc != SQLITE_ROW)
		ERROR_Z_ROLLBACK

	rc = sqlite3_step(stmt[4]);
	if (rc != SQLITE_DONE)
		ERROR_Z_ROLLBACK

	return sqlite3_column_int(stmt[3], 0);
}

gint zond_dbase_create_file_root(ZondDBase *zond_dbase, const gchar *file_part,
		gchar const *icon_name, gchar const *node_text, gchar const *text,
		gint *file_part_root, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] =
			{
					"INSERT INTO knoten "
							"(parent_id, older_sibling_id, type, file_part, icon_name, node_text, text) "
							"VALUES (0, 0, 5, ?1, ?2, ?3, ?4); ",

					"VALUES (last_insert_rowid()); " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_bind_text(stmt[0], 1, file_part, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_text(stmt[0], 2, icon_name, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_text(stmt[0], 3, node_text, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_text(stmt[0], 4, text, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[1]);
	if (rc == SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("ZOND_DBASE"), 0,
					"%s\nKnoten konnte nicht eingefügt werden", __func__);

		ERROR_Z_ROLLBACK
	} else if (rc != SQLITE_ROW)
		ERROR_Z_ROLLBACK

	if (file_part_root)
		*file_part_root = sqlite3_column_int(stmt[1], 0);

	return 0;
}

gint zond_dbase_update_icon_name(ZondDBase *zond_dbase, gint node_id,
		const gchar *icon_name, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "UPDATE knoten "
			"SET icon_name=?2 WHERE ID=?1; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_text(stmt[0], 2, icon_name, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE)
		ERROR_Z_DBASE

	return 0;
}

gint zond_dbase_update_node_text(ZondDBase *zond_dbase, gint node_id,
		const gchar *node_text, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "UPDATE knoten "
			"SET node_text=?2 WHERE ID=?1; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	rc = sqlite3_bind_text(stmt[0], 2, node_text, -1, NULL);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	return 0;
}

gint zond_dbase_update_text(ZondDBase *zond_dbase, gint node_id,
		const gchar *text, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "UPDATE knoten "
			"SET text=?2 WHERE ID=?1; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	rc = sqlite3_bind_text(stmt[0], 2, text, -1, NULL);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	return 0;
}

gint zond_dbase_update_path(ZondDBase *zond_dbase, const gchar *old_path,
		const gchar *new_path, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "UPDATE knoten SET file_part = "
			"REPLACE( SUBSTR( file_part, 1, LENGTH( ?1 ) ), ?1, ?2 ) || "
			"SUBSTR( file_part, LENGTH( ?1 ) + 1 );" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_text(stmt[0], 1, old_path, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_text(stmt[0], 2, new_path, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE)
		ERROR_Z_DBASE

	return 0;
}

gint zond_dbase_verschieben_knoten(ZondDBase *zond_dbase, gint node_id,
		gint anchor_id, gboolean child, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "SAVEPOINT statement; ",

	"UPDATE knoten SET older_sibling_id="
			"(SELECT older_sibling_ID FROM knoten WHERE ID=?1)" //node_id
			"WHERE older_sibling_ID=?1; ",

	//zunächst Knoten, vor den eingefügt wird, ändern
			"UPDATE knoten SET "
					"older_sibling_ID=?1 WHERE "
					"parent_ID= "
					"CASE ?3 "
					"WHEN 0 THEN (SELECT parent_ID FROM knoten WHERE ID=?2) "//anchor
					"WHEN 1 THEN ?2 "
					"END "
					"AND "
					"older_sibling_ID= "
					"CASE ?3 "
					"WHEN 0 THEN ?2 "
					"WHEN 1 THEN ?1 "
					"END; ",

			//zu verschiebenden Knoten einfügen
			"UPDATE knoten SET "
					"parent_ID= "
					"CASE ?3 "//child
					"WHEN 0 THEN (SELECT parent_ID FROM knoten WHERE ID=?2) "//anchor_id
					"WHEN 1 THEN ?2 "
					"END, "
					"older_sibling_ID="
					"CASE ?3 "
					"WHEN 0 THEN ?2 "
					"WHEN 1 THEN 0 "
					"END "
					"WHERE ID=?1; ",

			"RELEASE statement; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	rc = sqlite3_bind_int(stmt[1], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_int(stmt[2], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_int(stmt[2], 2, anchor_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_int(stmt[2], 3, child);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_int(stmt[3], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_int(stmt[3], 2, anchor_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_int(stmt[3], 3, child);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[1]);
	if (rc != SQLITE_DONE)
		ERROR_Z_ROLLBACK

	rc = sqlite3_step(stmt[2]);
	if (rc != SQLITE_DONE)
		ERROR_Z_ROLLBACK

	rc = sqlite3_step(stmt[3]);
	if (rc != SQLITE_DONE)
		ERROR_Z_ROLLBACK

	rc = sqlite3_step(stmt[4]);
	if (rc != SQLITE_DONE)
		ERROR_Z_ROLLBACK

	return 0;
}

gint zond_dbase_remove_node(ZondDBase *zond_dbase, gint node_id, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "SAVEPOINT statement; ",

	"UPDATE knoten SET older_sibling_ID=(SELECT older_sibling_ID FROM knoten "
			"WHERE ID=?1) WHERE "
			"older_sibling_ID=?1; ",

	"DELETE FROM knoten WHERE ID=?1; ",

	"RELEASE statement;" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_bind_int(stmt[1], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_bind_int(stmt[2], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[1]);
	if (rc != SQLITE_DONE)
		ERROR_Z_ROLLBACK

	rc = sqlite3_step(stmt[2]);
	if (rc != SQLITE_DONE)
		ERROR_Z_ROLLBACK

	rc = sqlite3_step(stmt[3]);
	if (rc != SQLITE_DONE)
		ERROR_Z_ROLLBACK

	return 0;
}

gint zond_dbase_get_node(ZondDBase *zond_dbase, gint node_id, gint *type,
		gint *link, gchar **file_part, gchar **section, gchar **icon_name,
		gchar **node_text, gchar **text, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = {
			"SELECT type, link, file_part, section, icon_name, node_text, text "
					"FROM knoten WHERE ID=?1;" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc == SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("ZOND_DBASE"), 0,
					"%s\n%s", __func__, "node_id nicht gefunden");

		return -1;
	} else if (rc != SQLITE_ROW)
		ERROR_Z_DBASE
			//richtiger Fähler

	if (type)
		*type = sqlite3_column_int(stmt[0], 0);
	if (link)
		*link = sqlite3_column_int(stmt[0], 1);
	if (file_part)
		*file_part = g_strdup((const gchar* ) sqlite3_column_text(stmt[0], 2));
	if (section)
		*section = g_strdup((const gchar* ) sqlite3_column_text(stmt[0], 3));
	if (icon_name)
		*icon_name = g_strdup((const gchar* ) sqlite3_column_text(stmt[0], 4));
	if (node_text)
		*node_text = g_strdup((const gchar* ) sqlite3_column_text(stmt[0], 5));
	if (text)
		*text = g_strdup((const gchar* ) sqlite3_column_text(stmt[0], 6));

	return 0;
}

gint zond_dbase_get_type_and_link(ZondDBase *zond_dbase, gint node_id,
		gint *type, gint *link, GError **error) {
	gint rc = 0;

	rc = zond_dbase_get_node(zond_dbase, node_id, type, link, NULL, NULL,
	NULL, NULL, NULL, error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);
		return -1;
	}

	return 0;
}

gint zond_dbase_get_text(ZondDBase *zond_dbase, gint node_id, gchar **text,
		GError **error) {
	gint rc = 0;

	rc = zond_dbase_get_node(zond_dbase, node_id, NULL, NULL, NULL, NULL, NULL,
			NULL, text, error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);
		return -1;
	}

	return 0;
}

gint zond_dbase_get_file_part_root(ZondDBase *zond_dbase,
		const gchar *file_part, gint *file_part_root, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "SELECT (ID) "
			"FROM knoten WHERE file_part=?1;" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_bind_text(stmt[0], 1, file_part, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE)
		ERROR_Z_DBASE
			//richtiger Fähler

	if (file_part_root)
		*file_part_root = sqlite3_column_int(stmt[0], 0);

	return 0;
}

gint zond_dbase_get_tree_root(ZondDBase *zond_dbase, gint node_id, gint *root,
		GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] =
			{
					"WITH RECURSIVE cte_knoten (ID, parent_ID, older_sibling_ID) AS ( "
							"VALUES (?1,(SELECT parent_ID FROM knoten WHERE ID=?1),(SELECT older_sibling_ID FROM knoten WHERE ID=?1)) "
							"UNION ALL "
							"SELECT knoten.ID, knoten.parent_id, knoten.older_sibling_ID FROM knoten JOIN cte_knoten "
							"WHERE knoten.ID = cte_knoten.parent_ID "
							") SELECT ID AS ID_CTE FROM cte_knoten "
							"WHERE cte_knoten.parent_ID=0 AND cte_knoten.older_sibling_ID=0; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc == SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("ZOND_DBASE"), 0,
					"%s\n%s", __func__, "node_id nicht gefunden");

		return -1;
	} else if (rc != SQLITE_ROW)
		ERROR_Z_DBASE
			//richtiger Fähler

	if (root)
		*root = sqlite3_column_int(stmt[0], 0);

	return 0;
}

gint zond_dbase_get_parent(ZondDBase *zond_dbase, gint node_id, gint *parent_id,
		GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "SELECT (parent_ID) "
			"FROM knoten WHERE ID=?1;" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	rc = sqlite3_step(stmt[0]);
	if (rc == SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("ZOND_DBASE"), 0,
					"%s\n%s", __func__, "node_id nicht gefunden");

		return -1;
	} else if (rc != SQLITE_ROW) //richtiger Fähler
	{
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	if (parent_id)
		*parent_id = sqlite3_column_int(stmt[0], 0);

	return 0;
}

gint zond_dbase_get_first_child(ZondDBase *zond_dbase, gint node_id,
		gint *first_child, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = {
	//...
			"SELECT ID FROM knoten WHERE parent_ID=?1 AND older_sibling_ID=0;" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE)
		ERROR_Z_DBASE

	if (first_child)
		*first_child = sqlite3_column_int(stmt[0], 0);

	return 0;
}

gint zond_dbase_get_older_sibling(ZondDBase *zond_dbase, gint node_id,
		gint *older_sibling_id, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "SELECT (older_sibling_ID) "
			"FROM knoten WHERE ID=?1;" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc == SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("ZOND_DBASE"), 0,
					"%s\n%s", __func__, "node_id nicht gefunden");

		return -1;
	} else if (rc != SQLITE_ROW) //richtiger Fähler
	{
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	if (older_sibling_id)
		*older_sibling_id = sqlite3_column_int(stmt[0], 0);

	return 0;
}

gint zond_dbase_get_younger_sibling(ZondDBase *zond_dbase, gint node_id,
		gint *younger_sibling_id, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "SELECT ID FROM knoten WHERE older_sibling_ID=?1;" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE)
		ERROR_Z_DBASE

	if (younger_sibling_id) {
		if (rc == SQLITE_ROW)
			*younger_sibling_id = sqlite3_column_int(stmt[0], 0);
		else
			*younger_sibling_id = 0;
	}

	return 0;
}

/** ergibt Anknüpfung im Baum, wenn angeknüpft **/
gint zond_dbase_get_baum_inhalt_file_from_file_part(ZondDBase *zond_dbase,
		gint file_part, gint *baum_inhalt_file, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "SELECT ID FROM knoten WHERE type=2 AND link=?1;" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_int(stmt[0], 1, file_part);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE) //richtiger Fähler
	{
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	if (rc == SQLITE_ROW && baum_inhalt_file)
		*baum_inhalt_file = sqlite3_column_int(stmt[0], 0);

	return 0;
}

gint zond_dbase_get_baum_auswertung_copy(ZondDBase *zond_dbase, gint node_id,
		gint *baum_auswertung_copy, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "SELECT ID FROM knoten WHERE type=3 AND link=?1;" };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE) //richtiger Fähler
	{
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	if (rc == SQLITE_ROW && baum_auswertung_copy)
		*baum_auswertung_copy = sqlite3_column_int(stmt[0], 0);

	return 0;
}

gint zond_dbase_get_first_baum_inhalt_file_child(ZondDBase *zond_dbase, gint ID,
		gint *baum_inhalt_file, gint *file_part, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] =
			{
					"WITH RECURSIVE cte_knoten (ID) AS ( "
							"VALUES (?1) "
							"UNION ALL "
							"SELECT knoten.ID "
							"FROM knoten JOIN cte_knoten WHERE knoten.parent_ID=cte_knoten.ID "
							") SELECT knoten.ID, knoten.link "
							"FROM knoten JOIN cte_knoten WHERE knoten.type=2 AND knoten.link=cte_knoten.ID; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_int(stmt[0], 1, ID);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	do {
		gint res = 0;

		res = sqlite3_step(stmt[0]);
		if (res != SQLITE_ROW && res != SQLITE_DONE)
			ERROR_Z_DBASE
				//richtiger Fähler

		if (res == SQLITE_DONE)
			break;

		if (baum_inhalt_file)
			*baum_inhalt_file = sqlite3_column_int(stmt[0], 0);
		if (file_part)
			*file_part = sqlite3_column_int(stmt[0], 1);

		break;
	} while (1);

	return 0;
}

gint zond_dbase_get_section(ZondDBase *zond_dbase, gchar const* filepart,
		gchar const* section, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] = { "SELECT ID FROM knoten WHERE file_part=?1 AND "
			"(section=?2 OR (section IS NULL AND ?2 IS NULL));"
	};

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_text(stmt[0], 1, filepart, -1, NULL);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	if (section)
		rc = sqlite3_bind_text(stmt[0], 2, section, -1, NULL);
	else
		rc = sqlite3_bind_null(stmt[0], 2);

	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE)
		ERROR_Z_DBASE

	if (rc == SQLITE_ROW)
		return sqlite3_column_int(stmt[0], 0);

	return 0;
}

/** Top-Funktion!!!
 Prüft, ob node_id (muß FILE_PART sein) oder dessen Eltern angebunden sind
 Gibt Anbindung (baum_inhalt_file) und angebundenen file_part (id_file_part) zurück
 **/
gint zond_dbase_find_baum_inhalt_file(ZondDBase *zond_dbase, gint node_id,
		gint *baum_inhalt_file, gint *id_file_part, gchar **file_part,
		GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] =
			{
					"WITH RECURSIVE cte_knoten (ID) AS ( "
							"VALUES (?1) "
							"UNION ALL "
							"SELECT knoten.parent_ID FROM knoten JOIN cte_knoten "
							"WHERE knoten.ID=cte_knoten.ID AND knoten.parent_ID!=0 "
							") SELECT knoten.ID, cte_knoten.ID, knoten2.file_part "
							"FROM knoten JOIN cte_knoten JOIN knoten AS knoten2 WHERE knoten.type=2 AND knoten.link=cte_knoten.ID "
							"AND knoten2.ID=cte_knoten.ID; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK)
		ERROR_Z_DBASE

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE)
		ERROR_Z_DBASE
			//richtiger Fähler

	if (rc == SQLITE_ROW) {
		if (baum_inhalt_file)
			*baum_inhalt_file = sqlite3_column_int(stmt[0], 0);
		if (id_file_part)
			*id_file_part = sqlite3_column_int(stmt[0], 1);
		if (file_part)
			*file_part = g_strdup(
					(gchar const* ) sqlite3_column_text(stmt[0], 2));
	}

	return 0;
}

gint zond_dbase_is_file_part_copied(ZondDBase *zond_dbase, gint search_root,
		gboolean *copied, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;

	const gchar *sql[] =
			{
					"WITH RECURSIVE cte_knoten (ID) AS ( "
							"VALUES (?1) "
							"UNION ALL "
							"SELECT knoten.ID "
							"FROM knoten JOIN cte_knoten WHERE knoten.parent_ID=cte_knoten.ID "
							") SELECT knoten.ID "
							"FROM knoten JOIN cte_knoten WHERE knoten.type=3 AND knoten.link=cte_knoten.ID; " };

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = sqlite3_bind_int(stmt[0], 1, search_root);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE) //richtiger Fähler
	{
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	if (rc == SQLITE_ROW) {
		if (copied)
			*copied = TRUE;
	} else if (copied)
		*copied = FALSE;

	return 0;
}

gint zond_dbase_get_arr_sections(ZondDBase* zond_dbase, gint attached, gchar const* file_part,
		GArray** arr_sections, GError** error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;
	const gchar *sql[1] = { NULL };

	if (!attached)
		sql[0] = "SELECT ID, section FROM main.knoten WHERE file_part=?1;";
	else
		sql[0] = "SELECT ID, section FROM work.knoten WHERE file_part=?1;";

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_text(stmt[0], 1, file_part, -1, NULL);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					rc, "%s\n%s", __func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	*arr_sections = g_array_new(FALSE, FALSE, sizeof(Section));
	g_array_set_clear_func(*arr_sections, (GDestroyNotify) section_free);

	do {
		Section section = { 0, };

		rc = sqlite3_step(stmt[0]);
		if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
			g_array_unref(*arr_sections);
			ERROR_Z_DBASE
		} else if (rc == SQLITE_DONE)
			break;

		section.ID = sqlite3_column_int(stmt[0], 0);
		section.section = g_strdup((gchar const*) sqlite3_column_text(stmt[0], 1));

		//wenn section == NULL, dann brauch es nicht gespeichert zu werden,
		//ist ja die ganze PDF-Datei - keine Anpassung nötig
		if (section.section) g_array_append_val(*arr_sections, section);
	} while (rc == SQLITE_ROW);

	return 0;
}

gint zond_dbase_update_section(ZondDBase *zond_dbase, gint attached, gint node_id,
		const gchar *section, GError **error) {
	gint rc = 0;
	sqlite3_stmt **stmt = NULL;
	const gchar *sql[1] = { NULL };

	if (!attached)
		sql[0] = "UPDATE main.knoten SET section=?2 WHERE ID=?1;";
	else
		sql[0] = "UPDATE work.knoten SET section=?2 WHERE ID=?1;";

	rc = zond_dbase_prepare(zond_dbase, __func__, sql, nelem(sql), &stmt,
			error);
	if (rc)
		ERROR_Z

	rc = sqlite3_bind_int(stmt[0], 1, node_id);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	rc = sqlite3_bind_text(stmt[0], 2, section, -1, NULL);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	rc = sqlite3_step(stmt[0]);
	if (rc != SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"),
					sqlite3_errcode(zond_dbase_get_dbase(zond_dbase)), "%s\n%s",
					__func__, sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)));

		return -1;
	}

	return 0;
}


