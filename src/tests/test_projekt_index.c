/*
 zond (test_projekt_index.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests der Index-Auskünfte zu Anbindungen (znd/sond_projekt_index): der Index-Status eines
 * Abschnitts, ohne Baum und ohne Dateien, nur mit einer Index-DB.
 */

#include <glib.h>
#include <glib/gstdio.h>

#include "../sond_fileparts.h"
#include "../znd/sond_projekt_index.h"

typedef struct {
	gchar *dir;
	gchar *pfad;
	SondIndexCtx *ctx;
} Idx;

static Idx* idx_new(void) {
	Idx *i = g_new0(Idx, 1);
	GError *error = NULL;

	i->dir = g_dir_make_tmp("projekt-index-XXXXXX", NULL);
	g_assert_nonnull(i->dir);
	i->pfad = g_build_filename(i->dir, "index.db", NULL);
	i->ctx = sond_index_ctx_new(i->pfad, NULL, 0, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(i->ctx);

	return i;
}

static void idx_free(Idx *i) {
	gchar *wal = g_strconcat(i->pfad, "-wal", NULL);
	gchar *shm = g_strconcat(i->pfad, "-shm", NULL);

	sond_index_ctx_free(i->ctx);
	g_remove(i->pfad);
	g_remove(wal);
	g_remove(shm);
	g_rmdir(i->dir);
	g_free(wal);
	g_free(shm);
	g_free(i->pfad);
	g_free(i->dir);
	g_free(i);
}

static void markiere(Idx *i, gchar const *pfad) {
	GError *error = NULL;

	g_assert_true(sond_index_ctx_coverage_mark(i->ctx, pfad, 1, &error));
	g_assert_no_error(error);
}

static void test_abschnitt(void) {
	Idx *i = idx_new();
	gchar const *bereich = "{{1,0}{3,99999}}";
	gchar const *punkt = "{2,17}";

	//noch nichts indiziert
	g_assert_cmpint(sond_projekt_index_status_abschnitt(i->ctx, "a.pdf", bereich), ==,
			SOND_INDEX_STATUS_NONE);
	g_assert_cmpint(sond_projekt_index_status_abschnitt(i->ctx, "a.pdf", punkt), ==,
			SOND_INDEX_STATUS_NONE);

	//die ganze Datei ist abgedeckt: jeder Seitenbereich darin ist es auch
	markiere(i, "a.pdf");
	g_assert_cmpint(sond_projekt_index_status_abschnitt(i->ctx, "a.pdf", bereich), ==,
			SOND_INDEX_STATUS_FULL);
	g_assert_cmpint(sond_projekt_index_status_abschnitt(i->ctx, "a.pdf", punkt), ==,
			SOND_INDEX_STATUS_FULL);

	//eine andere Datei bleibt davon unberührt
	g_assert_cmpint(sond_projekt_index_status_abschnitt(i->ctx, "b.pdf", bereich), ==,
			SOND_INDEX_STATUS_NONE);

	idx_free(i);
}

static void test_abschnitt_in_container(void) {
	Idx *i = idx_new();

	//ein Anhang oder Eintrag in einem Container hat einen eigenen Filepart
	markiere(i, "x.zip//b.pdf");
	g_assert_cmpint(sond_projekt_index_status_abschnitt(i->ctx, "x.zip//b.pdf", "{{1,0}{2,0}}"), ==,
			SOND_INDEX_STATUS_FULL);
	g_assert_cmpint(sond_projekt_index_status_abschnitt(i->ctx, "x.zip//c.pdf", "{{1,0}{2,0}}"), ==,
			SOND_INDEX_STATUS_NONE);

	idx_free(i);
}

static void test_fehlende_angaben(void) {
	Idx *i = idx_new();

	markiere(i, "a.pdf");

	//ohne Kontext, Filepart oder Abschnitt gibt es nichts zu sagen
	g_assert_cmpint(sond_projekt_index_status_abschnitt(NULL, "a.pdf", "{1,0}"), ==,
			SOND_INDEX_STATUS_NONE);
	g_assert_cmpint(sond_projekt_index_status_abschnitt(i->ctx, NULL, "{1,0}"), ==,
			SOND_INDEX_STATUS_NONE);
	g_assert_cmpint(sond_projekt_index_status_abschnitt(i->ctx, "a.pdf", NULL), ==,
			SOND_INDEX_STATUS_NONE);

	idx_free(i);
}

static GHashTable* ht_neu(void) {
	return g_hash_table_new_full(NULL, NULL, g_object_unref, sond_page_range_free);
}

static void trage_ein(GHashTable *ht, gchar const *pfad, SondPageRange *range) {
	g_hash_table_insert(ht, sond_file_part_create_leaf(pfad, NULL, "application/pdf"), range);
}

static void test_loeschen_ganze_datei(void) {
	Idx *i = idx_new();
	GHashTable *ht = ht_neu();

	markiere(i, "a.pdf");
	markiere(i, "b.pdf");
	trage_ein(ht, "a.pdf", NULL);

	g_assert_cmpint(sond_projekt_index_loeschen(i->ctx, ht, i->dir), ==, 0);
	g_assert_cmpint(sond_index_ctx_get_file_status(i->ctx, "a.pdf", 0, 99999), ==,
			SOND_INDEX_STATUS_NONE);
	g_assert_cmpint(sond_index_ctx_get_file_status(i->ctx, "b.pdf", 0, 99999), ==,
			SOND_INDEX_STATUS_FULL);

	g_hash_table_destroy(ht);
	idx_free(i);
}

static void test_loeschen_seitenbereich(void) {
	Idx *i = idx_new();
	GHashTable *ht = ht_neu();

	markiere(i, "a.pdf");
	trage_ein(ht, "a.pdf", sond_page_range_new(1, 2));

	g_assert_cmpint(sond_projekt_index_loeschen(i->ctx, ht, i->dir), ==, 0);
	g_assert_cmpint(sond_projekt_index_status_abschnitt(i->ctx, "a.pdf", "{{1,0}{2,99999}}"), ==,
			SOND_INDEX_STATUS_NONE);

	g_hash_table_destroy(ht);
	idx_free(i);
}

static void test_loeschen_mail_header(void) {
	Idx *i = idx_new();
	GHashTable *ht = ht_neu();

	markiere(i, "x.eml//header");
	markiere(i, "x.eml//0");
	trage_ein(ht, "x.eml", sond_page_range_new_gmessage_header());

	g_assert_cmpint(sond_projekt_index_loeschen(i->ctx, ht, i->dir), ==, 0);
	g_assert_cmpint(sond_index_ctx_get_file_status(i->ctx, "x.eml//header", -1, -1), ==,
			SOND_INDEX_STATUS_NONE);
	g_assert_cmpint(sond_index_ctx_get_file_status(i->ctx, "x.eml//0", -1, -1), ==,
			SOND_INDEX_STATUS_FULL);

	g_hash_table_destroy(ht);
	idx_free(i);
}

static void test_loeschen_leer(void) {
	Idx *i = idx_new();
	GHashTable *ht = ht_neu();

	markiere(i, "a.pdf");
	g_assert_cmpint(sond_projekt_index_loeschen(i->ctx, ht, i->dir), ==, 0);
	g_assert_cmpint(sond_projekt_index_loeschen(NULL, ht, i->dir), ==, 0);
	g_assert_cmpint(sond_index_ctx_get_file_status(i->ctx, "a.pdf", 0, 99999), ==,
			SOND_INDEX_STATUS_FULL);

	g_hash_table_destroy(ht);
	idx_free(i);
}

void test_projekt_index_register(void) {
	g_test_add_func("/projekt_index/abschnitt", test_abschnitt);
	g_test_add_func("/projekt_index/abschnitt_in_container", test_abschnitt_in_container);
	g_test_add_func("/projekt_index/fehlende_angaben", test_fehlende_angaben);
	g_test_add_func("/projekt_index/loeschen_ganze_datei", test_loeschen_ganze_datei);
	g_test_add_func("/projekt_index/loeschen_seitenbereich", test_loeschen_seitenbereich);
	g_test_add_func("/projekt_index/loeschen_mail_header", test_loeschen_mail_header);
	g_test_add_func("/projekt_index/loeschen_leer", test_loeschen_leer);
}
