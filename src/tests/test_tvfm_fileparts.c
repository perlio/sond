/*
 sond (test_tvfm_fileparts.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests der Auswahl -> Fileparts (tvfm/sond_tvfm_fileparts.c, znd/sond_projekt_auswahl.c):
 * Verzeichnis per readdir, Dateien, Teile in Containern, Sections (Seitenbereiche, Vereinigung,
 * unterseitige Anbindungen), E-Mail-Header und PageTree.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "../sond_index.h"
#include "../tvfm/sond_tvfm_fileparts.h"
#include "../znd/sond_projekt_auswahl.h"
#include "tvfm_test_support.h"

#define TESTTEXT "Ein kurzer Text, damit der Inhalt als Text erkannt wird."

static GHashTable* ht_neu(void) {
	return g_hash_table_new_full(NULL, NULL, g_object_unref, sond_page_range_free);
}

//Filepart-Pfade der Schlüssel, sortiert, mit "|" verbunden
static gchar* schluessel(GHashTable *ht) {
	GHashTableIter it;
	gpointer key = NULL;
	GPtrArray *namen = g_ptr_array_new_with_free_func(g_free);
	gchar *ergebnis = NULL;

	g_hash_table_iter_init(&it, ht);
	while (g_hash_table_iter_next(&it, &key, NULL))
		g_ptr_array_add(namen, sond_file_part_get_filepart(key));
	g_ptr_array_sort_values(namen, (GCompareFunc) g_strcmp0);
	g_ptr_array_add(namen, NULL);
	ergebnis = g_strjoinv("|", (gchar**) namen->pdata);
	g_ptr_array_unref(namen);

	return ergebnis;
}

static SondPageRange* bereich_von(GHashTable *ht, gchar const *pfad) {
	GHashTableIter it;
	gpointer key = NULL;
	gpointer val = NULL;

	g_hash_table_iter_init(&it, ht);
	while (g_hash_table_iter_next(&it, &key, &val)) {
		gchar *fp = sond_file_part_get_filepart(key);
		gboolean gleich = !g_strcmp0(fp, pfad);

		g_free(fp);
		if (gleich)
			return val;
	}
	g_assert_not_reached();

	return NULL;
}

static void opts_fuer(Fix *f, SondProjektSectionRegeln *regeln, SondTVFMFilepartsOpts *opts) {
	memset(opts, 0, sizeof(*opts));
	opts->root = f->dir;
	opts->section_range = sond_projekt_section_range;
	opts->section_data = regeln;
}

//Section-Item zur Datei rel
static SondTVFMItem* section_item(Fix *f, gchar const *rel, gchar const *section) {
	SondTVFMItem *d = datei(f, rel);
	SondTVFMItem *s = sond_tvfm_item_create(f->owner, sond_tvfm_item_get_sond_file_part(d), section);

	g_object_unref(d);

	return s;
}

/* ---------------------------------------------------------------------------------------- */

static void test_verzeichnis_readdir(void) {
	Fix *f = fix_new();
	SondProjektSectionRegeln regeln = { FALSE };
	SondTVFMFilepartsOpts opts;
	GHashTable *ht = ht_neu();
	GError *error = NULL;
	SondTVFMItem *w = NULL;
	gchar *s = NULL;

	schreibe(f, "a.txt", TESTTEXT);
	schreibe(f, "sub/d.txt", TESTTEXT);
	schreibe(f, "bild.jpg", "x");
	schreibe_pdf_seiten(f, "b.pdf", 2);
	opts_fuer(f, &regeln, &opts);

	w = wurzel(f);
	g_assert_cmpint(sond_tvfm_item_get_fileparts(w, ht, &opts, &error), ==, 0);
	g_assert_no_error(error);
	s = schluessel(ht);
	g_assert_cmpstr(s, ==, "a.txt|b.pdf|sub/d.txt");
	g_assert_null(bereich_von(ht, "a.txt"));

	g_free(s);
	g_object_unref(w);
	g_hash_table_destroy(ht);
	fix_free(f);
}

static void test_verzeichnis_unterordner(void) {
	Fix *f = fix_new();
	SondProjektSectionRegeln regeln = { FALSE };
	SondTVFMFilepartsOpts opts;
	GHashTable *ht = ht_neu();
	GError *error = NULL;
	SondTVFMItem *v = NULL;
	gchar *s = NULL;

	schreibe(f, "a.txt", TESTTEXT);
	schreibe(f, "sub/d.txt", TESTTEXT);
	opts_fuer(f, &regeln, &opts);

	v = verzeichnis(f, "sub");
	g_assert_cmpint(sond_tvfm_item_get_fileparts(v, ht, &opts, &error), ==, 0);
	g_assert_no_error(error);
	s = schluessel(ht);
	g_assert_cmpstr(s, ==, "sub/d.txt");

	g_free(s);
	g_object_unref(v);
	g_hash_table_destroy(ht);
	fix_free(f);
}

static void test_skip_fully_covered(void) {
	Fix *f = fix_new();
	SondProjektSectionRegeln regeln = { FALSE };
	SondTVFMFilepartsOpts opts;
	GHashTable *ht = ht_neu();
	GError *error = NULL;
	gchar *pfad = NULL;
	SondIndexCtx *ctx = NULL;
	SondTVFMItem *w = NULL;
	gchar *s = NULL;

	schreibe(f, "a.txt", TESTTEXT);
	schreibe(f, "sub/d.txt", TESTTEXT);
	pfad = g_build_filename(f->dir, "index.db", NULL);
	ctx = sond_index_ctx_new(pfad, NULL, 0, 0, &error);
	g_assert_no_error(error);
	g_assert_true(sond_index_ctx_coverage_mark(ctx, "sub", 1, &error));
	g_assert_no_error(error);

	opts_fuer(f, &regeln, &opts);
	opts.index_ctx = ctx;
	w = wurzel(f);

	//ohne skip: alles
	g_assert_cmpint(sond_tvfm_item_get_fileparts(w, ht, &opts, &error), ==, 0);
	s = schluessel(ht);
	g_assert_cmpstr(s, ==, "a.txt|sub/d.txt");
	g_free(s);
	g_hash_table_remove_all(ht);

	//mit skip: der vollständig abgedeckte Ordner fehlt
	opts.skip_fully_covered = TRUE;
	g_assert_cmpint(sond_tvfm_item_get_fileparts(w, ht, &opts, &error), ==, 0);
	g_assert_no_error(error);
	s = schluessel(ht);
	g_assert_cmpstr(s, ==, "a.txt");

	g_free(s);
	g_object_unref(w);
	g_hash_table_destroy(ht);
	sond_index_ctx_free(ctx);
	g_free(pfad);
	fix_free(f);
}

static void test_einzelne_datei(void) {
	Fix *f = fix_new();
	SondProjektSectionRegeln regeln = { FALSE };
	SondTVFMFilepartsOpts opts;
	GHashTable *ht = ht_neu();
	GError *error = NULL;
	SondTVFMItem *d = NULL;

	schreibe(f, "a.txt", TESTTEXT);
	opts_fuer(f, &regeln, &opts);
	d = datei(f, "a.txt");

	g_assert_cmpint(sond_tvfm_item_get_fileparts(d, ht, &opts, &error), ==, 0);
	g_assert_cmpuint(g_hash_table_size(ht), ==, 1);
	g_assert_null(bereich_von(ht, "a.txt"));

	g_object_unref(d);
	g_hash_table_destroy(ht);
	fix_free(f);
}

static void test_zip_ordner(void) {
	Fix *f = fix_new();
	SondProjektSectionRegeln regeln = { FALSE };
	SondTVFMFilepartsOpts opts;
	GHashTable *ht = ht_neu();
	GError *error = NULL;
	SondTVFMItem *z = NULL;
	GPtrArray *arr = NULL;
	gchar *s = NULL;

	schreibe_zip(f, "arch.zip", "ordner/", NULL, "ordner/x.txt", TESTTEXT, NULL);
	opts_fuer(f, &regeln, &opts);
	z = datei(f, "arch.zip");

	//das Archiv als Ganzes ist ein einziger Filepart
	g_assert_cmpint(sond_tvfm_item_get_fileparts(z, ht, &opts, &error), ==, 0);
	s = schluessel(ht);
	g_assert_cmpstr(s, ==, "arch.zip");
	g_free(s);
	g_hash_table_remove_all(ht);

	//das Verzeichnis im Archiv liefert seine Einträge
	g_assert_cmpint(sond_tvfm_item_load_children(z, &arr, NULL, &error), ==, 0);
	g_assert_cmpint(sond_tvfm_item_get_fileparts(kind(arr, "ordner"), ht, &opts, &error), ==, 0);
	g_assert_no_error(error);
	s = schluessel(ht);
	g_assert_cmpstr(s, ==, "arch.zip//ordner/x.txt");

	g_free(s);
	g_ptr_array_unref(arr);
	g_object_unref(z);
	g_hash_table_destroy(ht);
	fix_free(f);
}

static void test_section_vereinigung(void) {
	Fix *f = fix_new();
	SondProjektSectionRegeln regeln = { TRUE };
	SondTVFMFilepartsOpts opts;
	GHashTable *ht = ht_neu();
	GError *error = NULL;
	SondTVFMItem *s1 = NULL;
	SondTVFMItem *s2 = NULL;
	SondPageRange *r = NULL;
	gint von = 0;
	gint bis = 0;

	schreibe_pdf_seiten(f, "x.pdf", 9);
	opts_fuer(f, &regeln, &opts);
	s1 = section_item(f, "x.pdf", "{{0,0}{1,99999}}");
	s2 = section_item(f, "x.pdf", "{{5,0}{6,99999}}");
	g_assert_cmpint(sond_tvfm_item_get_item_type(s1), ==, SOND_TVFM_ITEM_TYPE_LEAF_SECTION);

	g_assert_cmpint(sond_tvfm_item_get_fileparts(s1, ht, &opts, &error), ==, 0);
	g_assert_cmpint(sond_tvfm_item_get_fileparts(s2, ht, &opts, &error), ==, 0);
	g_assert_no_error(error);

	//ein Schlüssel, zwei getrennte Bereiche
	g_assert_cmpuint(g_hash_table_size(ht), ==, 1);
	r = bereich_von(ht, "x.pdf");
	g_assert_nonnull(r);
	g_assert_cmpint(sond_page_range_count(r), ==, 2);
	sond_page_range_get(r, 0, &von, &bis);
	g_assert_cmpint(von, ==, 0);
	g_assert_cmpint(bis, ==, 1);
	sond_page_range_get(r, 1, &von, &bis);
	g_assert_cmpint(von, ==, 5);
	g_assert_cmpint(bis, ==, 6);

	g_object_unref(s1);
	g_object_unref(s2);
	g_hash_table_destroy(ht);
	fix_free(f);
}

static void test_section_unterseitig(void) {
	Fix *f = fix_new();
	SondProjektSectionRegeln regeln = { TRUE };
	SondTVFMFilepartsOpts opts;
	GHashTable *ht = ht_neu();
	GError *error = NULL;
	SondTVFMItem *s = NULL;
	SondTVFMItem *punkt = NULL;

	schreibe_pdf_seiten(f, "x.pdf", 4);
	opts_fuer(f, &regeln, &opts);
	s = section_item(f, "x.pdf", "{{1,7}{2,99999}}");
	punkt = section_item(f, "x.pdf", "{1,0}");

	//abgelehnt: beginnt mitten auf der Seite; ein Punkt zählt immer als unterseitig
	g_assert_cmpint(sond_tvfm_item_get_fileparts(s, ht, &opts, &error), ==, -1);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
	g_clear_error(&error);
	g_assert_cmpint(sond_tvfm_item_get_fileparts(punkt, ht, &opts, &error), ==, -1);
	g_clear_error(&error);
	g_assert_cmpuint(g_hash_table_size(ht), ==, 0);

	//ohne Regel: ganze Seiten 1 bis 2
	regeln.reject_unterseitig = FALSE;
	g_assert_cmpint(sond_tvfm_item_get_fileparts(s, ht, &opts, &error), ==, 0);
	g_assert_no_error(error);
	g_assert_cmpint(sond_page_range_count(bereich_von(ht, "x.pdf")), ==, 1);

	g_object_unref(s);
	g_object_unref(punkt);
	g_hash_table_destroy(ht);
	fix_free(f);
}

static void test_section_ohne_deutung(void) {
	Fix *f = fix_new();
	SondProjektSectionRegeln regeln = { FALSE };
	SondTVFMFilepartsOpts opts;
	GHashTable *ht = ht_neu();
	GError *error = NULL;
	SondTVFMItem *s = NULL;

	schreibe_pdf_seiten(f, "x.pdf", 3);
	opts_fuer(f, &regeln, &opts);
	opts.section_range = NULL;
	s = section_item(f, "x.pdf", "{{0,0}{1,99999}}");

	g_assert_cmpint(sond_tvfm_item_get_fileparts(s, ht, &opts, &error), ==, -1);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	g_object_unref(s);
	g_hash_table_destroy(ht);
	fix_free(f);
}

static void test_mail_message_nur_header(void) {
	Fix *f = fix_new();
	SondProjektSectionRegeln regeln = { FALSE };
	SondTVFMFilepartsOpts opts;
	GHashTable *ht = ht_neu();
	GError *error = NULL;
	SondTVFMItem *m = NULL;
	GPtrArray *arr = NULL;
	SondPageRange *r = NULL;

	schreibe_eml(f, "mail.eml", "Betreff", "Text der Mail", "anhang.txt", "Inhalt des Anhangs");
	opts_fuer(f, &regeln, &opts);
	m = datei(f, "mail.eml");
	g_assert_cmpint(sond_tvfm_item_load_children(m, &arr, NULL, &error), ==, 0);

	g_assert_cmpint(sond_tvfm_item_get_fileparts(kind(arr, "Message"), ht, &opts, &error), ==, 0);
	g_assert_no_error(error);
	r = bereich_von(ht, "mail.eml");
	g_assert_nonnull(r);
	g_assert_true(r->gmessage_header_only);

	g_ptr_array_unref(arr);
	g_object_unref(m);
	g_hash_table_destroy(ht);
	fix_free(f);
}

static void test_pdf_pagetree(void) {
	Fix *f = fix_new();
	SondProjektSectionRegeln regeln = { FALSE };
	SondTVFMFilepartsOpts opts;
	GHashTable *ht = ht_neu();
	GError *error = NULL;
	SondTVFMItem *p = NULL;
	GPtrArray *arr = NULL;
	SondPageRange *r = NULL;

	schreibe_pdf(f, "doc.pdf", "bild.txt", "Eins", NULL);
	opts_fuer(f, &regeln, &opts);
	p = datei(f, "doc.pdf");
	g_assert_cmpint(sond_tvfm_item_load_children(p, &arr, NULL, &error), ==, 0);

	g_assert_cmpint(sond_tvfm_item_get_fileparts(kind(arr, "PageTree"), ht, &opts, &error), ==, 0);
	g_assert_no_error(error);
	r = bereich_von(ht, "doc.pdf");
	g_assert_nonnull(r);
	g_assert_true(r->pdf_pagetree_only);

	g_ptr_array_unref(arr);
	g_object_unref(p);
	g_hash_table_destroy(ht);
	fix_free(f);
}

void test_tvfm_fileparts_register(void) {
	g_test_add_func("/tvfm_fileparts/verzeichnis_readdir", test_verzeichnis_readdir);
	g_test_add_func("/tvfm_fileparts/verzeichnis_unterordner", test_verzeichnis_unterordner);
	g_test_add_func("/tvfm_fileparts/skip_fully_covered", test_skip_fully_covered);
	g_test_add_func("/tvfm_fileparts/einzelne_datei", test_einzelne_datei);
	g_test_add_func("/tvfm_fileparts/zip_ordner", test_zip_ordner);
	g_test_add_func("/tvfm_fileparts/section_vereinigung", test_section_vereinigung);
	g_test_add_func("/tvfm_fileparts/section_unterseitig", test_section_unterseitig);
	g_test_add_func("/tvfm_fileparts/section_ohne_deutung", test_section_ohne_deutung);
	g_test_add_func("/tvfm_fileparts/mail_message_nur_header", test_mail_message_nur_header);
	g_test_add_func("/tvfm_fileparts/pdf_pagetree", test_pdf_pagetree);
}
