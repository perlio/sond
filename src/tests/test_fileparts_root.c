/*
 sond (test_fileparts_root.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests der Wurzel des Dateisystems je Thread (sond_file_part_set_thread_root): Fileparts mit
 * relativen Pfaden (auch Teile in einem ZIP) werden relativ zu ihr gelesen und geschrieben, andere
 * Threads sehen sie nicht. Der Server legt so je Job ein temporäres Verzeichnis an.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <zip.h>

#include "../sond_fileparts.h"

#define INHALT_A "hallo welt"
#define INHALT_E "inhalt im zip"

typedef struct {
	gchar *dir;
	gchar *sub;
	gchar *datei;
	gchar *zipdatei;
} Wurzel;

static void zip_anlegen(gchar const *pfad) {
	zip_error_t ze;
	zip_source_t *src = NULL;
	zip_t *za = NULL;
	gint fehler = 0;

	za = zip_open(pfad, ZIP_CREATE | ZIP_TRUNCATE, &fehler);
	g_assert_nonnull(za);
	zip_error_init(&ze);
	src = zip_source_buffer(za, g_strdup(INHALT_E), strlen(INHALT_E), 1);
	g_assert_nonnull(src);
	g_assert_cmpint(zip_file_add(za, "e.txt", src, ZIP_FL_ENC_UTF_8), >=, 0);
	g_assert_cmpint(zip_close(za), ==, 0);
	zip_error_fini(&ze);
}

static Wurzel* wurzel_neu(void) {
	Wurzel *w = g_new0(Wurzel, 1);

	w->dir = g_dir_make_tmp("fileparts-root-XXXXXX", NULL);
	g_assert_nonnull(w->dir);
	w->sub = g_build_filename(w->dir, "sub", NULL);
	g_assert_cmpint(g_mkdir(w->sub, 0700), ==, 0);
	w->datei = g_build_filename(w->sub, "a.txt", NULL);
	g_assert_true(g_file_set_contents(w->datei, INHALT_A, -1, NULL));
	w->zipdatei = g_build_filename(w->sub, "x.zip", NULL);
	zip_anlegen(w->zipdatei);

	return w;
}

static void wurzel_frei(Wurzel *w) {
	sond_file_part_set_thread_root(NULL);
	g_remove(w->datei);
	g_remove(w->zipdatei);
	g_rmdir(w->sub);
	g_rmdir(w->dir);
	g_free(w->dir);
	g_free(w->sub);
	g_free(w->datei);
	g_free(w->zipdatei);
	g_free(w);
}

//Inhalt eines Fileparts als String (g_free) oder NULL, wenn es sich nicht öffnen oder lesen lässt
static gchar* teil_lesen(gchar const *filepart) {
	SondFilePart *sfp = sond_file_part_from_filepart(filepart, NULL);
	GBytes *bytes = NULL;
	gchar *text = NULL;

	if (!sfp)
		return NULL;

	bytes = sond_file_part_get_bytes(sfp, NULL);
	if (bytes) {
		gsize len = 0;
		gconstpointer data = g_bytes_get_data(bytes, &len);

		text = g_strndup(data, len);
		g_bytes_unref(bytes);
	}
	g_object_unref(sfp);

	return text;
}

static void test_lesen(void) {
	Wurzel *w = wurzel_neu();
	gchar *text = NULL;

	sond_file_part_set_thread_root(w->dir);

	text = teil_lesen("sub/a.txt");
	g_assert_cmpstr(text, ==, INHALT_A);
	g_free(text);

	//ein Teil in einem Container, der Pfad der Wurzeldatei ist relativ
	text = teil_lesen("sub/x.zip//e.txt");
	g_assert_cmpstr(text, ==, INHALT_E);
	g_free(text);

	//ohne Wurzel gibt es die relative Datei nicht (das Arbeitsverzeichnis ist ein anderes)
	sond_file_part_set_thread_root(NULL);
	text = teil_lesen("sub/a.txt");
	g_assert_null(text);

	wurzel_frei(w);
}

static gpointer fremder_thread(gpointer data) {
	gchar *text = teil_lesen(data);

	return text;
}

static void test_andere_threads(void) {
	Wurzel *w = wurzel_neu();
	GThread *t = NULL;
	gchar *text = NULL;

	sond_file_part_set_thread_root(w->dir);

	//ein anderer Thread hat keine Wurzel und findet die Datei nicht
	t = g_thread_new("fremd", fremder_thread, (gpointer) "sub/a.txt");
	text = g_thread_join(t);
	g_assert_null(text);

	//eigener Thread mit eigener Wurzel, der andere Thread bleibt davon unberührt
	text = teil_lesen("sub/a.txt");
	g_assert_cmpstr(text, ==, INHALT_A);
	g_free(text);

	wurzel_frei(w);
}

static void test_schreiben(void) {
	Wurzel *w = wurzel_neu();
	SondFilePart *sfp = NULL;
	GBytes *neu = g_bytes_new_static("neuer inhalt", 12);
	gchar *inhalt = NULL;

	sond_file_part_set_thread_root(w->dir);

	sfp = sond_file_part_from_filepart("sub/a.txt", NULL);
	g_assert_nonnull(sfp);
	g_assert_cmpint(sond_file_part_replace(sfp, neu, NULL), ==, 0);
	g_object_unref(sfp);
	g_bytes_unref(neu);

	//die Änderung steht in der Datei unter der Wurzel
	g_assert_true(g_file_get_contents(w->datei, &inhalt, NULL, NULL));
	g_assert_cmpstr(inhalt, ==, "neuer inhalt");
	g_free(inhalt);

	wurzel_frei(w);
}

void test_fileparts_root_register(void) {
	g_test_add_func("/fileparts_root/lesen", test_lesen);
	g_test_add_func("/fileparts_root/andere_threads", test_andere_threads);
	g_test_add_func("/fileparts_root/schreiben", test_schreiben);
}
