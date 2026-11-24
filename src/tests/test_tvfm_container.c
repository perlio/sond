/*
 sond (test_tvfm_container.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests von tvfm/sond_tvfm_item.c für Container: ZIP-Archive, E-Mails und PDF-Dateien mit
 * eingebetteten Dateien. Anlegen und Einlesen, Pfade, Umbenennen, Kopieren, Verschieben, Löschen
 * und neues Verzeichnis im Archiv. Die Testdateien legt tvfm_test_support.c an.
 */

#include <glib.h>
#include <string.h>
#include <zip.h>

#include "../sond_fileparts.h"
#include "../tvfm/sond_tvfm_item.h"
#include "../tvfm/sond_tvfm_item_private.h"
#include "tvfm_test_support.h"

//Namen der Einträge eines ZIP-Archivs, sortiert, mit "|" verbunden
static gchar* zip_namen(Fix *f, gchar const *rel) {
	gchar *p = g_build_filename(f->dir, rel, NULL);
	int err = 0;
	zip_t *z = zip_open(p, ZIP_RDONLY, &err);
	GPtrArray *namen = g_ptr_array_new_with_free_func(g_free);
	GString *str = g_string_new(NULL);

	g_assert_nonnull(z);
	for (zip_int64_t i = 0; i < zip_get_num_entries(z, 0); i++)
		g_ptr_array_add(namen, g_strdup(zip_get_name(z, i, 0)));
	zip_close(z);
	g_ptr_array_sort_values(namen, (GCompareFunc) g_strcmp0);
	for (guint i = 0; i < namen->len; i++) {
		if (i)
			g_string_append_c(str, '|');
		g_string_append(str, g_ptr_array_index(namen, i));
	}
	g_ptr_array_unref(namen);
	g_free(p);

	return g_string_free(str, FALSE);
}

static void pruefe(gchar *ist, gchar const *soll) {
	g_assert_cmpstr(ist, ==, soll);
	g_free(ist);
}

/* ---------------------------------------------------------------------------------------- */
/* ZIP                                                                                       */

static Fix* zip_fix(void) {
	Fix *f = fix_new();

	schreibe_zip(f, "arch.zip", "ordner/", NULL, "ordner/inhalt.txt", "Inhalt", "wurzel.txt", "Wurzel",
			NULL);

	return f;
}

static void test_zip_anlegen(void) {
	Fix *f = zip_fix();
	SondTVFMItem *z = datei(f, "arch.zip");
	SondTVFMItemPrivate *p = sond_tvfm_item_get_priv(z);
	gchar *fp = NULL;

	//das Archiv ist ein Verzeichnis mit Kindern, noch ohne Pfad im Archiv
	g_assert_cmpint(sond_tvfm_item_get_item_type(z), ==, SOND_TVFM_ITEM_TYPE_DIR);
	g_assert_true(p->has_children);
	g_assert_null(sond_tvfm_item_get_path_or_section(z));
	g_assert_cmpstr(sond_tvfm_item_get_display_name(z), ==, "arch.zip");
	fp = sond_tvfm_item_get_filepart(z);
	g_assert_cmpstr(fp, ==, "arch.zip");
	g_free(fp);

	//kein Eintrag im Dateisystem im Sinn von fs_stat: das Archiv selbst schon
	g_assert_true(sond_tvfm_item_fs_stat(z, NULL, NULL, NULL));

	g_object_unref(z);
	fix_free(f);
}

static void test_zip_kinder(void) {
	Fix *f = zip_fix();
	SondTVFMItem *z = datei(f, "arch.zip");
	GPtrArray *arr = NULL;
	GPtrArray *sub = NULL;
	GError *error = NULL;
	gchar *fp = NULL;

	g_assert_cmpint(sond_tvfm_item_load_children(z, &arr, NULL, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpuint(arr->len, ==, 2);

	g_assert_cmpint(sond_tvfm_item_get_item_type(kind(arr, "ordner")), ==, SOND_TVFM_ITEM_TYPE_DIR);
	g_assert_cmpstr(sond_tvfm_item_get_path_or_section(kind(arr, "ordner")), ==, "ordner");
	g_assert_cmpint(sond_tvfm_item_get_item_type(kind(arr, "wurzel.txt")), ==,
			SOND_TVFM_ITEM_TYPE_LEAF);

	fp = sond_tvfm_item_get_filepart(kind(arr, "wurzel.txt"));
	g_assert_cmpstr(fp, ==, "arch.zip//wurzel.txt");
	g_free(fp);
	fp = sond_tvfm_item_get_filepart(kind(arr, "ordner"));
	g_assert_cmpstr(fp, ==, "arch.zip//ordner");
	g_free(fp);

	//Einträge im Archiv sind keine Dateien im Dateisystem
	g_assert_false(sond_tvfm_item_fs_path(kind(arr, "wurzel.txt"), NULL, NULL));

	//Unterverzeichnis
	g_assert_cmpint(sond_tvfm_item_load_children(kind(arr, "ordner"), &sub, NULL, &error), ==, 0);
	g_assert_cmpuint(sub->len, ==, 1);
	g_assert_cmpstr(sond_tvfm_item_get_display_name(g_ptr_array_index(sub, 0)), ==, "inhalt.txt");
	fp = sond_tvfm_item_get_filepart(g_ptr_array_index(sub, 0));
	g_assert_cmpstr(fp, ==, "arch.zip//ordner/inhalt.txt");
	g_free(fp);

	g_ptr_array_unref(sub);
	g_ptr_array_unref(arr);
	g_object_unref(z);
	fix_free(f);
}

static void test_zip_umbenennen(void) {
	Fix *f = zip_fix();
	SondTVFMItem *z = datei(f, "arch.zip");
	GPtrArray *arr = NULL;
	GError *error = NULL;

	g_assert_cmpint(sond_tvfm_item_load_children(z, &arr, NULL, &error), ==, 0);

	//Datei im Archiv
	g_assert_cmpint(sond_tvfm_item_rename(kind(arr, "wurzel.txt"), z, "neu.txt", &error), ==, 0);
	g_assert_no_error(error);
	pruefe(zip_namen(f, "arch.zip"), "neu.txt|ordner/|ordner/inhalt.txt");

	//Verzeichnis im Archiv
	g_assert_cmpint(sond_tvfm_item_rename(kind(arr, "ordner"), z, "mappe", &error), ==, 0);
	g_assert_no_error(error);
	pruefe(zip_namen(f, "arch.zip"), "mappe/|mappe/inhalt.txt|neu.txt");
	g_assert_cmpstr(sond_tvfm_item_get_path_or_section(kind(arr, "ordner")), ==, "mappe");

	g_ptr_array_unref(arr);
	g_object_unref(z);
	fix_free(f);
}

static void test_zip_neues_verzeichnis(void) {
	Fix *f = zip_fix();
	SondTVFMItem *z = datei(f, "arch.zip");
	gchar *pfad = NULL;
	GError *error = NULL;

	g_assert_cmpint(sond_tvfm_item_mkdir(z, TRUE, &pfad, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_nonnull(pfad);
	g_assert_nonnull(strstr(zip_namen(f, "arch.zip"), "Neues Verzeichnis/"));
	g_free(pfad);

	g_object_unref(z);
	fix_free(f);
}

static void test_zip_loeschen(void) {
	Fix *f = zip_fix();
	SondTVFMItem *z = datei(f, "arch.zip");
	GPtrArray *arr = NULL;
	GError *error = NULL;

	g_assert_cmpint(sond_tvfm_item_load_children(z, &arr, NULL, &error), ==, 0);

	g_assert_cmpint(sond_tvfm_item_delete(kind(arr, "wurzel.txt"), &error), ==, 0);
	g_assert_no_error(error);
	pruefe(zip_namen(f, "arch.zip"), "ordner/|ordner/inhalt.txt");

	//Verzeichnis im Archiv löschen ist nicht umgesetzt: Fehler, das Archiv bleibt
	g_assert_cmpint(sond_tvfm_item_delete(kind(arr, "ordner"), &error), ==, -1);
	g_assert_nonnull(error);
	g_clear_error(&error);
	pruefe(zip_namen(f, "arch.zip"), "ordner/|ordner/inhalt.txt");

	g_ptr_array_unref(arr);
	g_object_unref(z);
	fix_free(f);
}

static void test_zip_kopieren_ins_dateisystem(void) {
	Fix *f = zip_fix();
	SondTVFMItem *z = datei(f, "arch.zip");
	SondTVFMItem *ziel = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;
	gchar *x = NULL;

	schreibe(f, "ziel/x.txt", "X");
	ziel = verzeichnis(f, "ziel");
	g_assert_cmpint(sond_tvfm_item_load_children(z, &arr, NULL, &error), ==, 0);

	//Datei
	g_assert_cmpint(sond_tvfm_item_copy(kind(arr, "wurzel.txt"), ziel, "w.txt", 0, &error), ==, 0);
	g_assert_no_error(error);
	x = lese(f, "ziel/w.txt");
	g_assert_cmpstr(x, ==, "Wurzel");
	g_free(x);
	pruefe(zip_namen(f, "arch.zip"), "ordner/|ordner/inhalt.txt|wurzel.txt"); //Quelle bleibt

	//Verzeichnis aus dem Archiv
	g_assert_cmpint(sond_tvfm_item_copy(kind(arr, "ordner"), ziel, "mappe", 0, &error), ==, 0);
	g_assert_no_error(error);
	x = lese(f, "ziel/mappe/inhalt.txt");
	g_assert_cmpstr(x, ==, "Inhalt");
	g_free(x);

	g_ptr_array_unref(arr);
	g_object_unref(ziel);
	g_object_unref(z);
	fix_free(f);
}

static void test_zip_kopieren_ins_archiv(void) {
	Fix *f = zip_fix();
	SondTVFMItem *z = datei(f, "arch.zip");
	SondTVFMItem *a = NULL;
	GError *error = NULL;

	schreibe(f, "a.txt", "Neu");
	a = datei(f, "a.txt");

	g_assert_cmpint(sond_tvfm_item_copy(a, z, "a.txt", 0, &error), ==, 0);
	g_assert_no_error(error);
	pruefe(zip_namen(f, "arch.zip"), "a.txt|ordner/|ordner/inhalt.txt|wurzel.txt");
	g_assert_true(existiert(f, "a.txt")); //Quelle bleibt

	g_object_unref(a);
	g_object_unref(z);
	fix_free(f);
}

static void test_zip_verschieben_ins_dateisystem(void) {
	Fix *f = zip_fix();
	SondTVFMItem *z = datei(f, "arch.zip");
	SondTVFMItem *ziel = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;
	gchar *x = NULL;

	schreibe(f, "ziel/x.txt", "X");
	ziel = verzeichnis(f, "ziel");
	g_assert_cmpint(sond_tvfm_item_load_children(z, &arr, NULL, &error), ==, 0);

	g_assert_cmpint(sond_tvfm_item_move(kind(arr, "wurzel.txt"), ziel, "w.txt", 0, &error), ==, 0);
	g_assert_no_error(error);
	x = lese(f, "ziel/w.txt");
	g_assert_cmpstr(x, ==, "Wurzel");
	g_free(x);
	pruefe(zip_namen(f, "arch.zip"), "ordner/|ordner/inhalt.txt"); //aus dem Archiv entfernt
	g_assert_cmpint(f->owner->n_after, ==, 1);
	g_assert_true(f->owner->last_ok);

	g_ptr_array_unref(arr);
	g_object_unref(ziel);
	g_object_unref(z);
	fix_free(f);
}

/* ---------------------------------------------------------------------------------------- */
/* E-Mail                                                                                    */

static void test_eml_kinder(void) {
	Fix *f = fix_new();
	SondTVFMItem *m = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;
	gchar *label = NULL;

	schreibe_eml(f, "mail.eml", "Betreff", "Text der Mail", "anhang.txt", "Inhalt des Anhangs");
	m = datei(f, "mail.eml");

	g_assert_cmpint(sond_tvfm_item_get_item_type(m), ==, SOND_TVFM_ITEM_TYPE_DIR);
	g_assert_true(sond_tvfm_item_get_priv(m)->has_children);

	g_assert_cmpint(sond_tvfm_item_load_children(m, &arr, NULL, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpuint(arr->len, ==, 3); //Message, Text, Anhang

	//erstes Kind: Message (Marker), beim Anbinden mit dem Namen der Datei
	g_assert_cmpstr(sond_tvfm_item_get_display_name(g_ptr_array_index(arr, 0)), ==, "Message");
	g_assert_true(sond_tvfm_item_is_content_root_marker(g_ptr_array_index(arr, 0)));
	label = sond_tvfm_item_get_anbinden_label(g_ptr_array_index(arr, 0));
	g_assert_cmpstr(label, ==, "mail.eml");
	g_free(label);

	g_assert_nonnull(kind(arr, "text/plain"));
	g_assert_nonnull(kind(arr, "anhang.txt"));
	g_assert_cmpint(sond_tvfm_item_get_item_type(kind(arr, "anhang.txt")), ==,
			SOND_TVFM_ITEM_TYPE_LEAF);

	g_ptr_array_unref(arr);
	g_object_unref(m);
	fix_free(f);
}

static void test_eml_kopieren_und_loeschen(void) {
	Fix *f = fix_new();
	SondTVFMItem *m = NULL, *ziel = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;
	gchar *x = NULL;

	schreibe_eml(f, "mail.eml", "Betreff", "Text der Mail", "anhang.txt", "Inhalt des Anhangs");
	schreibe(f, "ziel/x.txt", "X");
	m = datei(f, "mail.eml");
	ziel = verzeichnis(f, "ziel");
	g_assert_cmpint(sond_tvfm_item_load_children(m, &arr, NULL, &error), ==, 0);

	//Anhang herauskopieren
	g_assert_cmpint(sond_tvfm_item_copy(kind(arr, "anhang.txt"), ziel, "anhang.txt", 0, &error), ==, 0);
	g_assert_no_error(error);
	x = lese(f, "ziel/anhang.txt");
	g_assert_cmpstr(x, ==, "Inhalt des Anhangs");
	g_free(x);

	//die Message (Header und Text) lässt sich weder kopieren noch löschen
	g_assert_cmpint(sond_tvfm_item_copy(g_ptr_array_index(arr, 0), ziel, "m.txt", 0, &error), ==, -1);
	g_assert_nonnull(error);
	g_clear_error(&error);
	g_assert_cmpint(sond_tvfm_item_delete(g_ptr_array_index(arr, 0), &error), ==, -1);
	g_assert_nonnull(error);
	g_clear_error(&error);

	g_ptr_array_unref(arr);
	g_object_unref(m);
	g_object_unref(ziel);
	fix_free(f);
}

//ein Teil der E-Mail wird aus der Datei entfernt
static void test_eml_teil_loeschen(void) {
	Fix *f = fix_new();
	SondTVFMItem *m = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;
	gchar *eml = NULL;

	schreibe_eml(f, "mail.eml", "Betreff", "Text der Mail", "anhang.txt", "Inhalt des Anhangs");
	eml = lese(f, "mail.eml");
	g_assert_nonnull(strstr(eml, "anhang.txt"));
	g_free(eml);

	m = datei(f, "mail.eml");
	g_assert_cmpint(sond_tvfm_item_load_children(m, &arr, NULL, &error), ==, 0);
	g_assert_cmpint(sond_tvfm_item_delete(kind(arr, "anhang.txt"), &error), ==, 0);
	g_assert_no_error(error);

	eml = lese(f, "mail.eml");
	g_assert_null(strstr(eml, "anhang.txt"));
	g_assert_nonnull(strstr(eml, "Text der Mail"));
	g_free(eml);

	g_ptr_array_unref(arr);
	g_object_unref(m);
	fix_free(f);
}

/* ---------------------------------------------------------------------------------------- */
/* PDF                                                                                       */

static void test_pdf_ohne_anhaenge(void) {
	Fix *f = fix_new();
	SondTVFMItem *p = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;

	schreibe_pdf(f, "leer.pdf", NULL);
	p = datei(f, "leer.pdf");

	//ohne Anhänge ist die PDF-Datei ein Blatt
	g_assert_cmpint(sond_tvfm_item_get_item_type(p), ==, SOND_TVFM_ITEM_TYPE_LEAF);
	g_assert_false(sond_tvfm_item_get_priv(p)->has_children);
	g_assert_cmpint(sond_tvfm_item_load_children(p, &arr, NULL, &error), ==, -1);
	g_clear_error(&error);

	g_object_unref(p);
	fix_free(f);
}

static void test_pdf_mit_anhaengen(void) {
	Fix *f = fix_new();
	SondTVFMItem *p = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;
	gchar *fp = NULL;
	gchar *label = NULL;
	guint n_bild = 0;

	schreibe_pdf(f, "doc.pdf", "bild.txt", "Eins", "bild.txt", "Zwei", "notiz.txt", "Notiz", NULL);
	p = datei(f, "doc.pdf");

	g_assert_cmpint(sond_tvfm_item_get_item_type(p), ==, SOND_TVFM_ITEM_TYPE_DIR);
	g_assert_true(sond_tvfm_item_get_priv(p)->has_children);

	g_assert_cmpint(sond_tvfm_item_load_children(p, &arr, NULL, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpuint(arr->len, ==, 4); //PageTree und drei Anhänge

	g_assert_cmpstr(sond_tvfm_item_get_display_name(g_ptr_array_index(arr, 0)), ==, "PageTree");
	g_assert_true(sond_tvfm_item_is_content_root_marker(g_ptr_array_index(arr, 0)));
	label = sond_tvfm_item_get_anbinden_label(g_ptr_array_index(arr, 0));
	g_assert_cmpstr(label, ==, "doc.pdf");
	g_free(label);

	//angezeigt wird der Dateiname, auch bei gleichen Namen; die Adresse unterscheidet sie
	for (guint i = 1; i < arr->len; i++)
		if (!g_strcmp0(sond_tvfm_item_get_display_name(g_ptr_array_index(arr, i)), "bild.txt"))
			n_bild++;
	g_assert_cmpuint(n_bild, ==, 2);
	{
		GHashTable *ht = g_hash_table_new(g_str_hash, g_str_equal);
		GList *l = NULL;

		for (guint i = 1; i < arr->len; i++) {
			gchar *a = sond_tvfm_item_get_filepart(g_ptr_array_index(arr, i));

			g_assert_cmpint(g_hash_table_contains(ht, a), ==, FALSE); //Adressen sind verschieden
			g_hash_table_add(ht, a);
		}
		for (l = g_hash_table_get_keys(ht); l; l = l->next)
			g_free(l->data);
		g_hash_table_destroy(ht);
	}

	//der eindeutige Name ist die Adresse
	fp = sond_tvfm_item_get_filepart(kind(arr, "notiz.txt"));
	g_assert_cmpstr(fp, ==, "doc.pdf//notiz.txt");
	g_free(fp);

	g_ptr_array_unref(arr);
	g_object_unref(p);
	fix_free(f);
}

static void test_pdf_anhang_kopieren(void) {
	Fix *f = fix_new();
	SondTVFMItem *p = NULL, *ziel = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;
	gchar *x = NULL;

	schreibe_pdf(f, "doc.pdf", "notiz.txt", "Notiz", NULL);
	schreibe(f, "ziel/x.txt", "X");
	p = datei(f, "doc.pdf");
	ziel = verzeichnis(f, "ziel");
	g_assert_cmpint(sond_tvfm_item_load_children(p, &arr, NULL, &error), ==, 0);

	g_assert_cmpint(sond_tvfm_item_copy(kind(arr, "notiz.txt"), ziel, "notiz.txt", 0, &error), ==, 0);
	g_assert_no_error(error);
	x = lese(f, "ziel/notiz.txt");
	g_assert_cmpstr(x, ==, "Notiz");
	g_free(x);

	//der PageTree lässt sich weder kopieren noch löschen
	g_assert_cmpint(sond_tvfm_item_copy(g_ptr_array_index(arr, 0), ziel, "pt.pdf", 0, &error), ==, -1);
	g_clear_error(&error);
	g_assert_cmpint(sond_tvfm_item_delete(g_ptr_array_index(arr, 0), &error), ==, -1);
	g_clear_error(&error);

	g_ptr_array_unref(arr);
	g_object_unref(p);
	g_object_unref(ziel);
	fix_free(f);
}

//ein eingebetteter Anhang wird aus der PDF-Datei entfernt, die anderen bleiben
static void test_pdf_anhang_loeschen(void) {
	Fix *f = fix_new();
	SondTVFMItem *p = NULL, *p2 = NULL;
	GPtrArray *arr = NULL;
	GPtrArray *arr2 = NULL;
	GError *error = NULL;

	schreibe_pdf(f, "doc.pdf", "bild.txt", "Eins", "notiz.txt", "Notiz", NULL);
	p = datei(f, "doc.pdf");
	g_assert_cmpint(sond_tvfm_item_load_children(p, &arr, NULL, &error), ==, 0);
	g_assert_cmpuint(arr->len, ==, 3);

	g_assert_cmpint(sond_tvfm_item_delete(kind(arr, "bild.txt"), &error), ==, 0);
	g_assert_no_error(error);

	//die Datei neu einlesen
	p2 = datei(f, "doc.pdf");
	g_assert_cmpint(sond_tvfm_item_load_children(p2, &arr2, NULL, &error), ==, 0);
	g_assert_cmpuint(arr2->len, ==, 2);
	g_assert_null(kind(arr2, "bild.txt"));
	g_assert_nonnull(kind(arr2, "notiz.txt"));

	g_ptr_array_unref(arr);
	g_ptr_array_unref(arr2);
	g_object_unref(p);
	g_object_unref(p2);
	fix_free(f);
}

void test_tvfm_container_register(void) {
	g_test_add_func("/tvfm_container/zip_anlegen", test_zip_anlegen);
	g_test_add_func("/tvfm_container/zip_kinder", test_zip_kinder);
	g_test_add_func("/tvfm_container/zip_umbenennen", test_zip_umbenennen);
	g_test_add_func("/tvfm_container/zip_neues_verzeichnis", test_zip_neues_verzeichnis);
	g_test_add_func("/tvfm_container/zip_loeschen", test_zip_loeschen);
	g_test_add_func("/tvfm_container/zip_kopieren_ins_dateisystem", test_zip_kopieren_ins_dateisystem);
	g_test_add_func("/tvfm_container/zip_kopieren_ins_archiv", test_zip_kopieren_ins_archiv);
	g_test_add_func("/tvfm_container/zip_verschieben_ins_dateisystem", test_zip_verschieben_ins_dateisystem);
	g_test_add_func("/tvfm_container/eml_kinder", test_eml_kinder);
	g_test_add_func("/tvfm_container/eml_kopieren_und_loeschen", test_eml_kopieren_und_loeschen);
	g_test_add_func("/tvfm_container/eml_teil_loeschen", test_eml_teil_loeschen);
	g_test_add_func("/tvfm_container/pdf_ohne_anhaenge", test_pdf_ohne_anhaenge);
	g_test_add_func("/tvfm_container/pdf_mit_anhaengen", test_pdf_mit_anhaengen);
	g_test_add_func("/tvfm_container/pdf_anhang_kopieren", test_pdf_anhang_kopieren);
	g_test_add_func("/tvfm_container/pdf_anhang_loeschen", test_pdf_anhang_loeschen);
}
