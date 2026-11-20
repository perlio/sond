/*
 sond (test_dbase_zond.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests für DBaseZond (src/znd/dbase_zond.c) mit temporären Dateien: nur store (Client) und
 * store mit angehängtem work (zond). Geprüft werden die Dual-Write-Funktionen, das Zurückrollen
 * und die Prüfung auf Verweise. Die Index-Funktionen (dbase_zond_index.c) sind nicht Teil davon.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#include "../znd/dbase_zond.h"
#include "../znd/zond_dbase.h"

typedef struct {
	gchar *dir;
	gchar *store_path;
	gchar *work_path;
	DBaseZond *db;
} Fixture;

//Testdaten: ein FILE_PART (ID 10) und ein Anker dazu im BAUM_INHALT (ID 11, type 2)
static gchar const *SQL_DATA =
		"INSERT INTO knoten (ID, parent_ID, older_sibling_ID, type, link, file_part) "
		"VALUES (10, 0, 0, 5, NULL, 'dir/a.pdf'); "
		"INSERT INTO knoten (ID, parent_ID, older_sibling_ID, type, link, file_part) "
		"VALUES (11, 1, 0, 2, 10, NULL);";

static void exec_ok(ZondDBase *zond_dbase, gchar const *sql) {
	gchar *errmsg = NULL;
	gint rc = sqlite3_exec(zond_dbase_get_dbase(zond_dbase), sql, NULL, NULL, &errmsg);

	if (rc != SQLITE_OK)
		g_error("SQL '%s': %s", sql, errmsg ? errmsg : "?");
	sqlite3_free(errmsg);
}

//Erste Spalte der ersten Zeile als Text (g_free()), NULL ohne Zeile
static gchar* query_text(DBaseZond *db, gchar const *sql) {
	sqlite3_stmt *stmt = NULL;
	gchar *result = NULL;
	sqlite3 *sqlite = zond_dbase_get_dbase(db->zond_dbase_store);

	g_assert_cmpint(sqlite3_prepare_v2(sqlite, sql, -1, &stmt, NULL), ==, SQLITE_OK);
	if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0))
		result = g_strdup((gchar const*) sqlite3_column_text(stmt, 0));
	sqlite3_finalize(stmt);

	return result;
}

static void fixture_set_up(Fixture *f, gboolean with_work) {
	GError *error = NULL;
	ZondDBase *store = NULL;
	ZondDBase *work = NULL;

	f->dir = g_dir_make_tmp("dbase_zond_XXXXXX", &error);
	g_assert_no_error(error);
	f->store_path = g_build_filename(f->dir, "store.ZND", NULL);
	f->work_path = g_build_filename(f->dir, "work.ZND", NULL);

	store = zond_dbase_new(f->store_path, FALSE, TRUE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(store);
	exec_ok(store, SQL_DATA);

	f->db = g_new0(DBaseZond, 1);
	f->db->zond_dbase_store = store;

	if (!with_work)
		return;

	//wie project_create_dbase_zond(): work ist eine Kopie von store, als Schema "work" angehängt
	work = zond_dbase_new(f->work_path, TRUE, FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(work);
	g_assert_cmpint(zond_dbase_backup(store, work, &error), ==, 0);
	g_assert_no_error(error);
	f->db->zond_dbase_work = work;

	{
		gchar *sql = sqlite3_mprintf("ATTACH DATABASE %Q AS work;", f->work_path);

		exec_ok(store, sql);
		sqlite3_free(sql);
	}
}

static void fixture_tear_down(Fixture *f) {
	dbase_zond_free(f->db);

	g_remove(f->store_path);
	g_remove(f->work_path);
	g_rmdir(f->dir);
	g_free(f->store_path);
	g_free(f->work_path);
	g_free(f->dir);
}

/* ---------------------------------------------------------------------- */

static void test_store_only_update_path(void) {
	Fixture f = { 0 };
	GError *error = NULL;
	gchar *path = NULL;

	fixture_set_up(&f, FALSE);
	g_assert_null(f.db->zond_dbase_work);
	g_assert_true(dbase_zond_primary(f.db) == f.db->zond_dbase_store);

	g_assert_cmpint(dbase_zond_begin(f.db, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpint(dbase_zond_update_path(f.db, "dir", "neu", &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpint(dbase_zond_commit(f.db, &error), ==, 0);
	g_assert_no_error(error);

	path = query_text(f.db, "SELECT file_part FROM knoten WHERE ID = 10;");
	g_assert_cmpstr(path, ==, "neu/a.pdf");
	g_free(path);

	fixture_tear_down(&f);
}

static void test_with_work_update_path_both(void) {
	Fixture f = { 0 };
	GError *error = NULL;
	gchar *main_path = NULL;
	gchar *work_path = NULL;

	fixture_set_up(&f, TRUE);
	g_assert_true(dbase_zond_primary(f.db) == f.db->zond_dbase_work);

	g_assert_cmpint(dbase_zond_begin(f.db, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpint(dbase_zond_update_path(f.db, "dir", "neu", &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpint(dbase_zond_commit(f.db, &error), ==, 0);
	g_assert_no_error(error);

	main_path = query_text(f.db, "SELECT file_part FROM main.knoten WHERE ID = 10;");
	work_path = query_text(f.db, "SELECT file_part FROM work.knoten WHERE ID = 10;");
	g_assert_cmpstr(main_path, ==, "neu/a.pdf");
	g_assert_cmpstr(work_path, ==, "neu/a.pdf");
	g_free(main_path);
	g_free(work_path);

	fixture_tear_down(&f);
}

static void test_with_work_rollback(void) {
	Fixture f = { 0 };
	GError *error = NULL;
	gchar *main_path = NULL;
	gchar *work_path = NULL;

	fixture_set_up(&f, TRUE);

	g_assert_cmpint(dbase_zond_begin(f.db, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpint(dbase_zond_update_path(f.db, "dir", "neu", &error), ==, 0);
	dbase_zond_rollback(f.db, &error);
	g_assert_no_error(error);

	//weder store noch work wurde verändert (atomar)
	main_path = query_text(f.db, "SELECT file_part FROM main.knoten WHERE ID = 10;");
	work_path = query_text(f.db, "SELECT file_part FROM work.knoten WHERE ID = 10;");
	g_assert_cmpstr(main_path, ==, "dir/a.pdf");
	g_assert_cmpstr(work_path, ==, "dir/a.pdf");
	g_free(main_path);
	g_free(work_path);

	fixture_tear_down(&f);
}

static void test_remove_filepart(gboolean with_work) {
	Fixture f = { 0 };
	GError *error = NULL;
	gchar *text = NULL;

	fixture_set_up(&f, with_work);

	g_assert_cmpint(dbase_zond_begin(f.db, &error), ==, 0);
	g_assert_cmpint(dbase_zond_remove_filepart(f.db, "dir/a.pdf", &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpint(dbase_zond_commit(f.db, &error), ==, 0);

	text = query_text(f.db, "SELECT ID FROM main.knoten WHERE ID = 10;");
	g_assert_null(text);
	if (with_work) {
		text = query_text(f.db, "SELECT ID FROM work.knoten WHERE ID = 10;");
		g_assert_null(text);
	}

	fixture_tear_down(&f);
}

static void test_remove_filepart_store_only(void) {
	test_remove_filepart(FALSE);
}

static void test_remove_filepart_with_work(void) {
	test_remove_filepart(TRUE);
}

static void test_test_path_store_only(void) {
	Fixture f = { 0 };
	GError *error = NULL;
	DBaseZondRef ref = DBASE_ZOND_REF_ONLY_STORE;

	fixture_set_up(&f, FALSE);

	g_assert_cmpint(dbase_zond_test_path(f.db, "dir/", &ref, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpint(ref, ==, DBASE_ZOND_REF_PRIMARY); //ohne work ist store die primäre DB

	g_assert_cmpint(dbase_zond_test_path(f.db, "andere/", &ref, &error), ==, 0);
	g_assert_cmpint(ref, ==, DBASE_ZOND_REF_NONE);

	fixture_tear_down(&f);
}

static void test_test_path_with_work(void) {
	Fixture f = { 0 };
	GError *error = NULL;
	DBaseZondRef ref = DBASE_ZOND_REF_NONE;

	fixture_set_up(&f, TRUE);

	//angebunden in work und store
	g_assert_cmpint(dbase_zond_test_path(f.db, "dir/", &ref, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpint(ref, ==, DBASE_ZOND_REF_PRIMARY);

	//Anbindung in work entfernt, im store noch vorhanden: "erst speichern"
	exec_ok(f.db->zond_dbase_work, "DELETE FROM knoten WHERE ID = 11;");
	g_assert_cmpint(dbase_zond_test_path(f.db, "dir/", &ref, &error), ==, 0);
	g_assert_cmpint(ref, ==, DBASE_ZOND_REF_ONLY_STORE);

	//auch im store entfernt: nichts angebunden
	exec_ok(f.db->zond_dbase_store, "DELETE FROM main.knoten WHERE ID = 11;");
	g_assert_cmpint(dbase_zond_test_path(f.db, "dir/", &ref, &error), ==, 0);
	g_assert_cmpint(ref, ==, DBASE_ZOND_REF_NONE);

	fixture_tear_down(&f);
}

void test_dbase_zond_register(void) {
	g_test_add_func("/dbase_zond/store_only/update_path", test_store_only_update_path);
	g_test_add_func("/dbase_zond/store_only/remove_filepart", test_remove_filepart_store_only);
	g_test_add_func("/dbase_zond/store_only/test_path", test_test_path_store_only);
	g_test_add_func("/dbase_zond/with_work/update_path", test_with_work_update_path_both);
	g_test_add_func("/dbase_zond/with_work/rollback", test_with_work_rollback);
	g_test_add_func("/dbase_zond/with_work/remove_filepart", test_remove_filepart_with_work);
	g_test_add_func("/dbase_zond/with_work/test_path", test_test_path_with_work);
}
