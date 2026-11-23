/*
 sond (test_tvfm_item.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests für tvfm/sond_tvfm_item.c im Dateisystem: Anlegen von Wurzel-, Verzeichnis- und
 * Datei-Einträgen, Einlesen eines Verzeichnisses, Umbenennen, Kopieren, Verschieben, Löschen,
 * neues Verzeichnis, Pfadangaben. Container (ZIP, PDF, E-Mail) sind nicht Teil dieser Datei.
 *
 * Jeder Test legt ein temporäres Projektverzeichnis an (tvfm_test_support.h) und stellt path_root
 * (Klassenfeld von SondFilePart, global) darauf ein. Der Eigentümer der Einträge ist ein kleiner
 * Nachbau, der die Wurzel liefert und die Signale vor und nach dem Verschieben mitzählt.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "../sond_fileparts.h"
#include "../tvfm/sond_tvfm_item.h"
#include "../tvfm/sond_tvfm_item_private.h"
#include "../tvfm/sond_tvfm_owner.h"
#include "tvfm_test_support.h"

/* ---------------------------------------------------------------------------------------- */
/* Tests                                                                                     */

static void test_gueltiger_dateiname(void) {
	g_assert_true(sond_tvfm_item_is_valid_filename("Rechnung 2026.pdf"));
	g_assert_true(sond_tvfm_item_is_valid_filename("ä ö ü.txt"));
	g_assert_false(sond_tvfm_item_is_valid_filename(NULL));
	g_assert_false(sond_tvfm_item_is_valid_filename(""));
	g_assert_false(sond_tvfm_item_is_valid_filename("a/b.txt"));
	g_assert_false(sond_tvfm_item_is_valid_filename("a\\b.txt"));
}

static void test_wurzel(void) {
	Fix *f = fix_new();
	SondTVFMItem *w = wurzel(f);
	SondTVFMItemPrivate *p = sond_tvfm_item_get_priv(w);
	gchar *fp = NULL;

	//leeres Verzeichnis: keine Kinder
	g_assert_cmpint(sond_tvfm_item_get_item_type(w), ==, SOND_TVFM_ITEM_TYPE_DIR);
	g_assert_false(p->has_children);
	g_assert_null(sond_tvfm_item_get_display_name(w));
	g_assert_null(sond_tvfm_item_get_path_or_section(w));
	fp = sond_tvfm_item_get_filepart(w);
	g_assert_null(fp);
	g_object_unref(w);

	//Dateien und Verzeichnisse mit führendem Punkt zählen nicht
	schreibe(f, ".sond_index.db", "x");
	w = wurzel(f);
	g_assert_false(sond_tvfm_item_get_priv(w)->has_children);
	g_object_unref(w);

	schreibe(f, "a.txt", "A");
	w = wurzel(f);
	g_assert_true(sond_tvfm_item_get_priv(w)->has_children);
	g_object_unref(w);

	fix_free(f);
}

static void test_kinder_laden(void) {
	Fix *f = fix_new();
	SondTVFMItem *w = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;

	schreibe(f, "a.txt", "A");
	schreibe(f, "b.txt", "B");
	schreibe(f, ".versteckt", "x");
	schreibe(f, "sub/c.txt", "C");
	w = wurzel(f);

	g_assert_cmpint(sond_tvfm_item_load_children(w, &arr, NULL, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpuint(arr->len, ==, 3); //a.txt, b.txt, sub; .versteckt nicht
	g_assert_nonnull(kind(arr, "a.txt"));
	g_assert_nonnull(kind(arr, "b.txt"));
	g_assert_null(kind(arr, ".versteckt"));

	//Datei: Blatt, Pfad im sfp; Verzeichnis: Pfad im Item
	g_assert_cmpint(sond_tvfm_item_get_item_type(kind(arr, "a.txt")), ==, SOND_TVFM_ITEM_TYPE_LEAF);
	g_assert_null(sond_tvfm_item_get_path_or_section(kind(arr, "a.txt")));
	g_assert_cmpint(sond_tvfm_item_get_item_type(kind(arr, "sub")), ==, SOND_TVFM_ITEM_TYPE_DIR);
	g_assert_cmpstr(sond_tvfm_item_get_path_or_section(kind(arr, "sub")), ==, "sub");
	g_assert_true(sond_tvfm_item_get_priv(kind(arr, "sub"))->has_children);

	//Unterverzeichnis laden: relativer Pfad zur Wurzel
	{
		GPtrArray *sub = NULL;

		g_assert_cmpint(sond_tvfm_item_load_children(kind(arr, "sub"), &sub, NULL, &error), ==, 0);
		g_assert_cmpuint(sub->len, ==, 1);
		g_assert_cmpstr(sond_tvfm_item_get_display_name(g_ptr_array_index(sub, 0)), ==, "c.txt");
		g_ptr_array_unref(sub);
	}

	g_ptr_array_unref(arr);
	g_object_unref(w);
	fix_free(f);
}

static void test_kinder_eines_blatts(void) {
	Fix *f = fix_new();
	SondTVFMItem *a = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;

	schreibe(f, "a.txt", "A");
	a = datei(f, "a.txt");

	//ein Blatt ohne Abschnitte hat keine Kinder: Fehler, kein Absturz
	g_assert_cmpint(sond_tvfm_item_load_children(a, &arr, NULL, &error), ==, -1);
	g_assert_nonnull(error);
	g_assert_null(arr);
	g_clear_error(&error);

	g_object_unref(a);
	fix_free(f);
}

static void test_filepart(void) {
	Fix *f = fix_new();
	SondTVFMItem *a = NULL, *c = NULL, *sub = NULL;
	gchar *fp = NULL;

	schreibe(f, "a.txt", "A");
	schreibe(f, "sub/c.txt", "C");
	a = datei(f, "a.txt");
	c = datei(f, "sub/c.txt");
	sub = verzeichnis(f, "sub");

	fp = sond_tvfm_item_get_filepart(a);
	g_assert_cmpstr(fp, ==, "a.txt");
	g_free(fp);
	fp = sond_tvfm_item_get_filepart(c);
	g_assert_cmpstr(fp, ==, "sub/c.txt");
	g_free(fp);
	fp = sond_tvfm_item_get_filepart(sub);
	g_assert_cmpstr(fp, ==, "sub");
	g_free(fp);

	g_assert_cmpstr(sond_tvfm_item_get_display_name(c), ==, "c.txt");
	g_assert_cmpstr(sond_tvfm_item_get_basename(sub), ==, "sub");

	g_object_unref(a);
	g_object_unref(c);
	g_object_unref(sub);
	fix_free(f);
}

static void test_fs_angaben(void) {
	Fix *f = fix_new();
	SondTVFMItem *a = NULL, *sub = NULL, *w = NULL;
	gint64 mtime = 0, size = 0;
	gboolean is_dir = TRUE;
	gchar *rel = NULL;

	schreibe(f, "a.txt", "12345");
	schreibe(f, "sub/c.txt", "C");
	a = datei(f, "a.txt");
	sub = verzeichnis(f, "sub");
	w = wurzel(f);

	g_assert_true(sond_tvfm_item_fs_stat(a, &mtime, &size, &is_dir));
	g_assert_cmpint(size, ==, 5);
	g_assert_false(is_dir);
	g_assert_cmpint(mtime, >, 0);

	g_assert_true(sond_tvfm_item_fs_path(a, &rel, &is_dir));
	g_assert_cmpstr(rel, ==, "a.txt");
	g_assert_false(is_dir);
	g_free(rel);

	g_assert_true(sond_tvfm_item_fs_path(sub, &rel, &is_dir));
	g_assert_cmpstr(rel, ==, "sub");
	g_assert_true(is_dir);
	g_free(rel);

	//die Wurzel hat keinen eigenen Pfad
	g_assert_false(sond_tvfm_item_fs_path(w, &rel, &is_dir));

	g_object_unref(a);
	g_object_unref(sub);
	g_object_unref(w);
	fix_free(f);
}

static void test_umbenennen_datei(void) {
	Fix *f = fix_new();
	SondTVFMItem *w = wurzel(f);
	SondTVFMItem *a = NULL;
	GError *error = NULL;

	schreibe(f, "a.txt", "A");
	schreibe(f, "b.txt", "B");
	a = datei(f, "a.txt");

	//auf ein vorhandenes Ziel: Fehler, beide Dateien bleiben
	g_assert_cmpint(sond_tvfm_item_rename(a, w, "b.txt", &error), ==, -1);
	g_assert_true(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_EXISTS));
	g_clear_error(&error);
	{
		gchar *x = lese(f, "a.txt");
		gchar *y = lese(f, "b.txt");

		g_assert_cmpstr(x, ==, "A");
		g_assert_cmpstr(y, ==, "B");
		g_free(x);
		g_free(y);
	}

	g_assert_cmpint(sond_tvfm_item_rename(a, w, "c.txt", &error), ==, 0);
	g_assert_no_error(error);
	g_assert_false(existiert(f, "a.txt"));
	g_assert_true(existiert(f, "c.txt"));
	{
		gchar *fp = sond_tvfm_item_get_filepart(a);

		g_assert_cmpstr(fp, ==, "c.txt"); //das sfp führt den neuen Pfad
		g_free(fp);
	}

	g_object_unref(a);
	g_object_unref(w);
	fix_free(f);
}

static void test_umbenennen_verzeichnis(void) {
	Fix *f = fix_new();
	SondTVFMItem *w = wurzel(f);
	SondTVFMItem *sub = NULL, *andere = NULL;
	GError *error = NULL;

	schreibe(f, "sub/c.txt", "C");
	schreibe(f, "andere/d.txt", "D");
	sub = verzeichnis(f, "sub");
	andere = verzeichnis(f, "andere");

	g_assert_cmpint(sond_tvfm_item_rename(sub, w, "andere", &error), ==, -1);
	g_assert_true(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_EXISTS));
	g_clear_error(&error);
	g_assert_true(existiert(f, "sub/c.txt"));

	g_assert_cmpint(sond_tvfm_item_rename(sub, w, "neu", &error), ==, 0);
	g_assert_no_error(error);
	g_assert_false(existiert(f, "sub"));
	g_assert_true(existiert(f, "neu/c.txt"));
	g_assert_cmpstr(sond_tvfm_item_get_path_or_section(sub), ==, "neu");

	g_object_unref(sub);
	g_object_unref(andere);
	g_object_unref(w);
	fix_free(f);
}

static void test_kopieren_datei(void) {
	Fix *f = fix_new();
	SondTVFMItem *a = NULL, *ziel = NULL;
	GError *error = NULL;

	schreibe(f, "a.txt", "A");
	schreibe(f, "ziel/x.txt", "X");
	a = datei(f, "a.txt");
	ziel = verzeichnis(f, "ziel");

	g_assert_cmpint(sond_tvfm_item_copy(a, ziel, "a.txt", 0, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_true(existiert(f, "a.txt")); //Quelle bleibt
	{
		gchar *x = lese(f, "ziel/a.txt");

		g_assert_cmpstr(x, ==, "A");
		g_free(x);
	}

	g_object_unref(a);
	g_object_unref(ziel);
	fix_free(f);
}

//Verzeichnis rekursiv, unter anderem Namen in die Wurzel
static void test_kopieren_verzeichnis(void) {
	Fix *f = fix_new();
	SondTVFMItem *w = wurzel(f);
	SondTVFMItem *sub = NULL;
	GError *error = NULL;

	schreibe(f, "sub/c.txt", "C");
	schreibe(f, "sub/tief/d.txt", "D");
	sub = verzeichnis(f, "sub");

	g_assert_cmpint(sond_tvfm_item_copy(sub, w, "sub_kopie", 0, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_true(existiert(f, "sub/tief/d.txt")); //Quelle bleibt
	{
		gchar *x = lese(f, "sub_kopie/tief/d.txt");

		g_assert_cmpstr(x, ==, "D");
		g_free(x);
	}

	//das Ziel liegt innerhalb der Quelle: abgewiesen, ohne etwas anzulegen
	g_assert_cmpint(sond_tvfm_item_copy(sub, sub, "innen", 0, &error), ==, -1);
	g_assert_true(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT));
	g_clear_error(&error);
	g_assert_false(existiert(f, "sub/innen"));

	g_object_unref(sub);
	g_object_unref(w);
	fix_free(f);
}

static void test_verschieben(void) {
	Fix *f = fix_new();
	SondTVFMItem *w = wurzel(f);
	SondTVFMItem *a = NULL, *sub = NULL, *ziel = NULL;
	GError *error = NULL;

	schreibe(f, "a.txt", "A");
	schreibe(f, "sub/c.txt", "C");
	schreibe(f, "ziel/x.txt", "X");
	a = datei(f, "a.txt");
	sub = verzeichnis(f, "sub");
	ziel = verzeichnis(f, "ziel");

	//Datei: weg am alten Ort, da am neuen; der Eigentümer wird vorher und nachher gefragt
	g_assert_cmpint(sond_tvfm_item_move(a, ziel, "a.txt", 0, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_false(existiert(f, "a.txt"));
	{
		gchar *x = lese(f, "ziel/a.txt");

		g_assert_cmpstr(x, ==, "A");
		g_free(x);
	}
	g_assert_cmpint(f->owner->n_before_move, ==, 1);
	g_assert_cmpint(f->owner->n_after, ==, 1);
	g_assert_true(f->owner->last_ok);

	//Verzeichnis
	g_assert_cmpint(sond_tvfm_item_move(sub, ziel, "sub", 0, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_false(existiert(f, "sub"));
	g_assert_true(existiert(f, "ziel/sub/c.txt"));
	g_assert_cmpint(f->owner->n_after, ==, 2);

	g_object_unref(a);
	g_object_unref(sub);
	g_object_unref(ziel);
	g_object_unref(w);
	fix_free(f);
}

static void test_loeschen(void) {
	Fix *f = fix_new();
	SondTVFMItem *w = wurzel(f);
	SondTVFMItem *a = NULL, *sub = NULL;
	GError *error = NULL;

	schreibe(f, "a.txt", "A");
	schreibe(f, "sub/c.txt", "C");
	schreibe(f, "sub/tief/d.txt", "D");
	a = datei(f, "a.txt");
	sub = verzeichnis(f, "sub");

	g_assert_cmpint(sond_tvfm_item_delete(a, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_false(existiert(f, "a.txt"));

	//Verzeichnis mit Inhalt
	g_assert_cmpint(sond_tvfm_item_delete(sub, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_false(existiert(f, "sub"));

	//die Wurzel lässt sich nicht löschen
	g_assert_cmpint(sond_tvfm_item_delete(w, &error), ==, 0);
	g_assert_true(g_file_test(f->dir, G_FILE_TEST_IS_DIR));

	g_object_unref(a);
	g_object_unref(sub);
	g_object_unref(w);
	fix_free(f);
}

static void test_neues_verzeichnis(void) {
	Fix *f = fix_new();
	SondTVFMItem *w = wurzel(f);
	SondTVFMItem *sub = NULL;
	gchar *pfad = NULL;
	GError *error = NULL;

	schreibe(f, "sub/c.txt", "C");
	sub = verzeichnis(f, "sub");

	g_assert_cmpint(sond_tvfm_item_mkdir(w, TRUE, &pfad, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpstr(pfad, ==, "Neues Verzeichnis");
	g_assert_true(existiert(f, "Neues Verzeichnis"));
	g_free(pfad);

	//ein zweites bekommt einen Zähler
	g_assert_cmpint(sond_tvfm_item_mkdir(w, TRUE, &pfad, &error), ==, 0);
	g_assert_cmpstr(pfad, ==, "Neues Verzeichnis (1)");
	g_assert_true(existiert(f, "Neues Verzeichnis (1)"));
	g_free(pfad);

	//in einem Unterverzeichnis
	g_assert_cmpint(sond_tvfm_item_mkdir(sub, TRUE, &pfad, &error), ==, 0);
	g_assert_cmpstr(pfad, ==, "sub/Neues Verzeichnis");
	g_assert_true(existiert(f, "sub/Neues Verzeichnis"));
	g_free(pfad);

	g_object_unref(sub);
	g_object_unref(w);
	fix_free(f);
}

/* Wird ein Verzeichnis umbenannt oder verschoben, führt adjust_sfps_in_dir die Pfade der darunter
 * geöffneten Dateien nach; "sub" trifft dabei nicht "sub38/...". Im Dateisystem (Eltern-sfp NULL)
 * sind das die geöffneten Dateien der Klasse. */
static void test_sfps_nachfuehren(void) {
	Fix *f = fix_new();
	SondFilePart *sfp_c = NULL, *sfp_tief = NULL, *sfp_fremd = NULL;
	GError *error = NULL;

	schreibe(f, "sub/c.txt", "C");
	schreibe(f, "sub/tief/e.txt", "E");
	schreibe(f, "sub38/d.txt", "D");
	sfp_c = sond_file_part_create(NULL, "sub/c.txt", &error);
	sfp_tief = sond_file_part_create(NULL, "sub/tief/e.txt", &error);
	sfp_fremd = sond_file_part_create(NULL, "sub38/d.txt", &error);
	g_assert_nonnull(sfp_c);
	g_assert_nonnull(sfp_tief);
	g_assert_nonnull(sfp_fremd);

	sond_tvfm_item_adjust_sfps_in_dir(NULL, NULL, "sub", "neu");
	g_assert_cmpstr(sond_file_part_get_path(sfp_c), ==, "neu/c.txt");
	g_assert_cmpstr(sond_file_part_get_path(sfp_tief), ==, "neu/tief/e.txt");
	g_assert_cmpstr(sond_file_part_get_path(sfp_fremd), ==, "sub38/d.txt");

	g_object_unref(sfp_c);
	g_object_unref(sfp_tief);
	g_object_unref(sfp_fremd);
	fix_free(f);
}

void test_tvfm_item_register(void) {
	g_test_add_func("/tvfm_item/gueltiger_dateiname", test_gueltiger_dateiname);
	g_test_add_func("/tvfm_item/wurzel", test_wurzel);
	g_test_add_func("/tvfm_item/kinder_laden", test_kinder_laden);
	g_test_add_func("/tvfm_item/kinder_eines_blatts", test_kinder_eines_blatts);
	g_test_add_func("/tvfm_item/filepart", test_filepart);
	g_test_add_func("/tvfm_item/fs_angaben", test_fs_angaben);
	g_test_add_func("/tvfm_item/umbenennen_datei", test_umbenennen_datei);
	g_test_add_func("/tvfm_item/umbenennen_verzeichnis", test_umbenennen_verzeichnis);
	g_test_add_func("/tvfm_item/kopieren_datei", test_kopieren_datei);
	g_test_add_func("/tvfm_item/kopieren_verzeichnis", test_kopieren_verzeichnis);
	g_test_add_func("/tvfm_item/verschieben", test_verschieben);
	g_test_add_func("/tvfm_item/loeschen", test_loeschen);
	g_test_add_func("/tvfm_item/neues_verzeichnis", test_neues_verzeichnis);
	g_test_add_func("/tvfm_item/sfps_nachfuehren", test_sfps_nachfuehren);
}
