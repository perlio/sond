/*
 sond (test_tvfm_index_sync.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests der Index-Pflege des Dateisystems (tvfm/sond_tvfm_index_sync.c): Löschen und Verschieben
 * von Dateien und Ordnern, Auflösen einer abdeckenden Aussage vor dem Einfügen. Gearbeitet wird
 * gegen eine echte Index-DB im temporären Projektverzeichnis. Pfade sind projektrelativ.
 */

#include <glib.h>

#include "../sond_index.h"
#include "../tvfm/sond_tvfm_index_sync.h"
#include "tvfm_test_support.h"

typedef struct {
	Fix *fix;
	SondIndexCtx *ctx;
} Ctx;

static Ctx* ctx_new(void) {
	Ctx *c = g_new0(Ctx, 1);
	GError *error = NULL;
	gchar *pfad = NULL;

	c->fix = fix_new();
	pfad = g_build_filename(c->fix->dir, "index.db", NULL);
	c->ctx = sond_index_ctx_new(pfad, NULL, 0, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(c->ctx);
	g_free(pfad);

	return c;
}

static void ctx_free(Ctx *c) {
	sond_index_ctx_free(c->ctx);
	fix_free(c->fix);
	g_free(c);
}

//Datei als indiziert vermerken: Abdeckung (Modus 1) und Seitenzahl
static void indiziert(Ctx *c, gchar const *rel, gint seiten) {
	GError *error = NULL;

	g_assert_true(sond_index_ctx_coverage_mark(c->ctx, rel, 1, &error));
	g_assert_no_error(error);
	g_assert_true(sond_index_ctx_set_page_count(c->ctx, rel, seiten, &error));
	g_assert_no_error(error);
}

/* ---------------------------------------------------------------------------------------- */
/* ohne Index                                                                               */

static void test_ohne_index(void) {
	GError *error = NULL;

	g_assert_true(sond_tvfm_index_sync_prepare_insert(NULL, "a.pdf", "/x", &error));
	g_assert_true(sond_tvfm_index_sync_moved(NULL, "a.pdf", "b.pdf", &error));
	g_assert_true(sond_tvfm_index_sync_deleted(NULL, "a.pdf", &error));
	g_assert_no_error(error);
}

static void test_ohne_pfad(void) {
	Ctx *c = ctx_new();
	GError *error = NULL;

	indiziert(c, "a.pdf", 3);
	g_assert_true(sond_tvfm_index_sync_prepare_insert(c->ctx, NULL, c->fix->dir, &error));
	g_assert_true(sond_tvfm_index_sync_moved(c->ctx, NULL, "b.pdf", &error));
	g_assert_true(sond_tvfm_index_sync_moved(c->ctx, "a.pdf", NULL, &error));
	g_assert_true(sond_tvfm_index_sync_deleted(c->ctx, NULL, &error));
	g_assert_no_error(error);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "a.pdf"), >=, 1);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "a.pdf"), ==, 3);
	ctx_free(c);
}

/* ---------------------------------------------------------------------------------------- */
/* Löschen                                                                                  */

static void test_geloescht_datei(void) {
	Ctx *c = ctx_new();
	GError *error = NULL;

	indiziert(c, "d/a.pdf", 5);
	indiziert(c, "d/b.pdf", 2);

	g_assert_true(sond_tvfm_index_sync_deleted(c->ctx, "d/a.pdf", &error));
	g_assert_no_error(error);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d/a.pdf"), <, 1);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "d/a.pdf"), ==, -1);
	//das Geschwister bleibt
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d/b.pdf"), >=, 1);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "d/b.pdf"), ==, 2);
	ctx_free(c);
}

static void test_geloescht_ordner(void) {
	Ctx *c = ctx_new();
	GError *error = NULL;

	indiziert(c, "d/a.pdf", 5);
	indiziert(c, "d/tief/b.pdf", 2);
	//ein Pfad, der nur mit demselben Anfang beginnt, ist ein anderer Ordner
	indiziert(c, "dx/c.pdf", 4);

	g_assert_true(sond_tvfm_index_sync_deleted(c->ctx, "d", &error));
	g_assert_no_error(error);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d/a.pdf"), <, 1);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d/tief/b.pdf"), <, 1);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "d/a.pdf"), ==, -1);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "d/tief/b.pdf"), ==, -1);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "dx/c.pdf"), >=, 1);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "dx/c.pdf"), ==, 4);
	ctx_free(c);
}

static void test_geloescht_unter_abdeckendem_vorfahren(void) {
	Ctx *c = ctx_new();
	GError *error = NULL;

	//der Ordner ist als Ganzes indiziert: das Löschen einer Datei kann die Aussage nicht verletzen
	g_assert_true(sond_index_ctx_coverage_mark(c->ctx, "d", 1, &error));
	g_assert_no_error(error);

	g_assert_true(sond_tvfm_index_sync_deleted(c->ctx, "d/a.pdf", &error));
	g_assert_no_error(error);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d"), >=, 1);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d/b.pdf"), >=, 1);
	ctx_free(c);
}

static void test_geloescht_container_inhalt(void) {
	Ctx *c = ctx_new();
	GError *error = NULL;

	indiziert(c, "arch.zip", 0);
	g_assert_true(sond_index_ctx_set_entry_count(c->ctx, "arch.zip", 3, &error));
	g_assert_no_error(error);
	indiziert(c, "arch.zip//x.pdf", 2);

	g_assert_true(sond_tvfm_index_sync_deleted(c->ctx, "arch.zip", &error));
	g_assert_no_error(error);
	g_assert_cmpint(sond_index_ctx_get_entry_count(c->ctx, "arch.zip"), ==, -1);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "arch.zip//x.pdf"), ==, -1);
	ctx_free(c);
}

/* ---------------------------------------------------------------------------------------- */
/* Verschieben                                                                              */

static void test_verschoben_datei(void) {
	Ctx *c = ctx_new();
	GError *error = NULL;

	indiziert(c, "a.pdf", 3);

	g_assert_true(sond_tvfm_index_sync_moved(c->ctx, "a.pdf", "neu/b.pdf", &error));
	g_assert_no_error(error);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "a.pdf"), <, 1);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "a.pdf"), ==, -1);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "neu/b.pdf"), >=, 1);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "neu/b.pdf"), ==, 3);
	ctx_free(c);
}

static void test_verschoben_ordner(void) {
	Ctx *c = ctx_new();
	GError *error = NULL;

	indiziert(c, "d/a.pdf", 5);
	indiziert(c, "d/tief/b.pdf", 2);
	indiziert(c, "dx/c.pdf", 4);

	g_assert_true(sond_tvfm_index_sync_moved(c->ctx, "d", "e", &error));
	g_assert_no_error(error);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "e/a.pdf"), ==, 5);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "e/tief/b.pdf"), ==, 2);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "e/a.pdf"), >=, 1);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "e/tief/b.pdf"), >=, 1);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "d/a.pdf"), ==, -1);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "d/tief/b.pdf"), ==, -1);
	//Ordner mit gleichem Anfang bleibt unberührt
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "dx/c.pdf"), ==, 4);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "ex/c.pdf"), ==, -1);
	ctx_free(c);
}

static void test_verschoben_container_inhalt(void) {
	Ctx *c = ctx_new();
	GError *error = NULL;

	indiziert(c, "arch.zip", 0);
	g_assert_true(sond_index_ctx_set_entry_count(c->ctx, "arch.zip", 3, &error));
	g_assert_no_error(error);
	indiziert(c, "arch.zip//x.pdf", 2);

	g_assert_true(sond_tvfm_index_sync_moved(c->ctx, "arch.zip", "neu.zip", &error));
	g_assert_no_error(error);
	g_assert_cmpint(sond_index_ctx_get_entry_count(c->ctx, "neu.zip"), ==, 3);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "neu.zip//x.pdf"), ==, 2);
	g_assert_cmpint(sond_index_ctx_get_entry_count(c->ctx, "arch.zip"), ==, -1);
	g_assert_cmpint(sond_index_ctx_get_page_count(c->ctx, "arch.zip//x.pdf"), ==, -1);
	ctx_free(c);
}

/* ---------------------------------------------------------------------------------------- */
/* Einfügen: abdeckende Aussage auflösen                                                    */

static void test_einfuegen_loest_abdeckung_auf(void) {
	Ctx *c = ctx_new();
	GError *error = NULL;

	schreibe(c->fix, "d/a.pdf", "A");
	schreibe(c->fix, "d/b.pdf", "B");
	g_assert_true(sond_index_ctx_coverage_mark(c->ctx, "d", 1, &error));
	g_assert_no_error(error);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d/a.pdf"), >=, 1);

	//neuer, ungeprüfter Inhalt unter d: der Ordner gilt nicht mehr als indiziert, die
	//vorhandenen Dateien einzeln weiterhin; die neue ist nicht abgedeckt
	g_assert_true(sond_tvfm_index_sync_prepare_insert(c->ctx, "d/neu.pdf", c->fix->dir, &error));
	g_assert_no_error(error);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d"), <, 1);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d/a.pdf"), >=, 1);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d/b.pdf"), >=, 1);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d/neu.pdf"), <, 1);
	ctx_free(c);
}

static void test_einfuegen_ohne_abdeckung(void) {
	Ctx *c = ctx_new();
	GError *error = NULL;

	schreibe(c->fix, "d/a.pdf", "A");
	indiziert(c, "d/a.pdf", 1);

	g_assert_true(sond_tvfm_index_sync_prepare_insert(c->ctx, "d/neu.pdf", c->fix->dir, &error));
	g_assert_no_error(error);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d/a.pdf"), >=, 1);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d/neu.pdf"), <, 1);
	ctx_free(c);
}

static void test_einfuegen_ersetzt_abgedeckte_datei(void) {
	Ctx *c = ctx_new();
	GError *error = NULL;

	schreibe(c->fix, "d/a.pdf", "A");
	indiziert(c, "d/a.pdf", 1);

	//an den Pfad einer indizierten Datei kommt anderer Inhalt: ihre Abdeckung gilt nicht mehr
	g_assert_true(sond_tvfm_index_sync_prepare_insert(c->ctx, "d/a.pdf", c->fix->dir, &error));
	g_assert_no_error(error);
	g_assert_cmpint(sond_index_ctx_coverage_get(c->ctx, "d/a.pdf"), <, 1);
	ctx_free(c);
}

void test_tvfm_index_sync_register(void) {
	g_test_add_func("/tvfm_index_sync/ohne_index", test_ohne_index);
	g_test_add_func("/tvfm_index_sync/ohne_pfad", test_ohne_pfad);
	g_test_add_func("/tvfm_index_sync/geloescht_datei", test_geloescht_datei);
	g_test_add_func("/tvfm_index_sync/geloescht_ordner", test_geloescht_ordner);
	g_test_add_func("/tvfm_index_sync/geloescht_unter_abdeckendem_vorfahren",
			test_geloescht_unter_abdeckendem_vorfahren);
	g_test_add_func("/tvfm_index_sync/geloescht_container_inhalt", test_geloescht_container_inhalt);
	g_test_add_func("/tvfm_index_sync/verschoben_datei", test_verschoben_datei);
	g_test_add_func("/tvfm_index_sync/verschoben_ordner", test_verschoben_ordner);
	g_test_add_func("/tvfm_index_sync/verschoben_container_inhalt", test_verschoben_container_inhalt);
	g_test_add_func("/tvfm_index_sync/einfuegen_loest_abdeckung_auf", test_einfuegen_loest_abdeckung_auf);
	g_test_add_func("/tvfm_index_sync/einfuegen_ohne_abdeckung", test_einfuegen_ohne_abdeckung);
	g_test_add_func("/tvfm_index_sync/einfuegen_ersetzt_abgedeckte_datei",
			test_einfuegen_ersetzt_abgedeckte_datei);
}
