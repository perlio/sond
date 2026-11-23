/*
 sond (test_tvfm_search.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests der Dateisuche nach Namen (tvfm/sond_tvfm_search.c): Suchart, Groß- und Kleinschreibung,
 * Dateien und Verzeichnisse als Treffer, Suche in Containern (ZIP, PDF-Anhänge, E-Mail), Abbruch,
 * Doppelte. Die Treffer sind Fileparts; die Reihenfolge der Verzeichniseinträge ist nicht
 * festgelegt, verglichen wird deshalb die sortierte Liste.
 */

#include <glib.h>
#include <string.h>

#include "../tvfm/sond_tvfm_search.h"
#include "tvfm_test_support.h"

/*
 * Verzeichnis der Tests:
 *   a.txt
 *   Bericht_alt.txt
 *   sub/Bericht_neu.txt
 *   sub/tief/bericht.TXT
 *   arch.zip: ordner/, ordner/inhalt.txt, bericht_im_zip.txt
 *   mail.eml: Anhang bericht_anhang.txt
 *   doc.pdf: eingebettet bericht_emb.txt
 */
static Fix* baum(void) {
	Fix *f = fix_new();

	schreibe(f, "a.txt", "A");
	schreibe(f, "Bericht_alt.txt", "B");
	schreibe(f, "sub/Bericht_neu.txt", "C");
	schreibe(f, "sub/tief/bericht.TXT", "D");
	schreibe_zip(f, "arch.zip", "ordner/", NULL, "ordner/inhalt.txt", "I", "bericht_im_zip.txt", "Z",
			NULL);
	schreibe_eml(f, "mail.eml", "Betreff", "Text der Mail", "bericht_anhang.txt", "Anhang");
	schreibe_pdf(f, "doc.pdf", "bericht_emb.txt", "Emb", NULL);

	return f;
}

static gint vergleiche(gconstpointer a, gconstpointer b) {
	return g_strcmp0(*(gchar* const*) a, *(gchar* const*) b);
}

//sortierte Treffer, mit "|" verbunden; nicht gesuchte Wurzel-Items übergibt start
static gchar* sucht(Fix *f, GPtrArray *start, SondTVFMSearchOpts const *opts, gint *rc_out) {
	SondTVFMSearch *s = sond_tvfm_search_new(SOND_TVFM_OWNER(f->owner), opts);
	GError *error = NULL;
	GPtrArray *hits = NULL;
	gchar *erg = NULL;
	gint rc = sond_tvfm_search_run(s, start, &error);

	if (rc_out)
		*rc_out = rc;
	g_clear_error(&error);

	hits = sond_tvfm_search_take_hits(s);
	g_ptr_array_sort(hits, vergleiche);
	{
		GString *str = g_string_new(NULL);

		for (guint i = 0; i < hits->len; i++) {
			if (i)
				g_string_append_c(str, '|');
			g_string_append(str, g_ptr_array_index(hits, i));
		}
		erg = g_string_free(str, FALSE);
	}
	g_ptr_array_unref(hits);
	sond_tvfm_search_free(s);

	return erg;
}

static gchar* suche_wurzel(Fix *f, gchar const *text, SondTVFMSearchMode mode, gboolean gross,
		gboolean dateien, gboolean verz, gboolean container) {
	SondTVFMSearchOpts opts = { text, mode, gross, dateien, verz, container };
	SondTVFMItem *w = wurzel(f);
	GPtrArray *start = g_ptr_array_new();
	gchar *erg = NULL;

	g_ptr_array_add(start, w);
	erg = sucht(f, start, &opts, NULL);
	g_ptr_array_unref(start);
	g_object_unref(w);

	return erg;
}

static void pruefe(gchar *ist, gchar const *soll) {
	g_assert_cmpstr(ist, ==, soll);
	g_free(ist);
}

static void test_enthaelt_ohne_gross_und_klein(void) {
	Fix *f = baum();

	//Dateien (auch die Container selbst, wenn ihr Name passt), ohne Inhalt der Container
	pruefe(suche_wurzel(f, "bericht", SOND_TVFM_SEARCH_CONTAINS, FALSE, TRUE, FALSE, FALSE),
			"Bericht_alt.txt|sub/Bericht_neu.txt|sub/tief/bericht.TXT");

	fix_free(f);
}

static void test_enthaelt_mit_gross_und_klein(void) {
	Fix *f = baum();

	pruefe(suche_wurzel(f, "Bericht", SOND_TVFM_SEARCH_CONTAINS, TRUE, TRUE, FALSE, FALSE),
			"Bericht_alt.txt|sub/Bericht_neu.txt");
	pruefe(suche_wurzel(f, "bericht", SOND_TVFM_SEARCH_CONTAINS, TRUE, TRUE, FALSE, FALSE),
			"sub/tief/bericht.TXT");

	fix_free(f);
}

static void test_ganzer_name(void) {
	Fix *f = baum();

	pruefe(suche_wurzel(f, "a.txt", SOND_TVFM_SEARCH_WHOLE_NAME, FALSE, TRUE, FALSE, FALSE),
			"a.txt");
	//Teil eines Namens genügt nicht
	pruefe(suche_wurzel(f, "bericht", SOND_TVFM_SEARCH_WHOLE_NAME, FALSE, TRUE, FALSE, FALSE), "");
	//ohne Rücksicht auf Groß- und Kleinschreibung
	pruefe(suche_wurzel(f, "A.TXT", SOND_TVFM_SEARCH_WHOLE_NAME, FALSE, TRUE, FALSE, FALSE),
			"a.txt");
	pruefe(suche_wurzel(f, "A.TXT", SOND_TVFM_SEARCH_WHOLE_NAME, TRUE, TRUE, FALSE, FALSE), "");

	fix_free(f);
}

static void test_muster(void) {
	Fix *f = baum();

	pruefe(suche_wurzel(f, "?.txt", SOND_TVFM_SEARCH_WILDCARD, FALSE, TRUE, FALSE, FALSE),
			"a.txt");
	pruefe(suche_wurzel(f, "bericht*.txt", SOND_TVFM_SEARCH_WILDCARD, FALSE, TRUE, FALSE, FALSE),
			"Bericht_alt.txt|sub/Bericht_neu.txt|sub/tief/bericht.TXT");
	pruefe(suche_wurzel(f, "*.zip", SOND_TVFM_SEARCH_WILDCARD, FALSE, TRUE, FALSE, FALSE),
			"arch.zip");

	fix_free(f);
}

static void test_verzeichnisse(void) {
	Fix *f = baum();

	pruefe(suche_wurzel(f, "sub", SOND_TVFM_SEARCH_CONTAINS, FALSE, FALSE, TRUE, FALSE), "sub");
	pruefe(suche_wurzel(f, "tief", SOND_TVFM_SEARCH_CONTAINS, FALSE, FALSE, TRUE, FALSE),
			"sub/tief");
	//Dateien und Verzeichnisse
	pruefe(suche_wurzel(f, "sub", SOND_TVFM_SEARCH_CONTAINS, FALSE, TRUE, TRUE, FALSE), "sub");
	//weder noch: keine Treffer
	pruefe(suche_wurzel(f, "sub", SOND_TVFM_SEARCH_CONTAINS, FALSE, FALSE, FALSE, FALSE), "");

	fix_free(f);
}

static void test_container_durchsuchen(void) {
	Fix *f = baum();
	gchar *ist = NULL;

	/* In Containern: ZIP-Eintrag, E-Mail-Anhang, PDF-Anhang; die Schreibweise der Treffer ist
	 * "Container//Pfad" */
	ist = suche_wurzel(f, "bericht", SOND_TVFM_SEARCH_CONTAINS, FALSE, TRUE, FALSE, TRUE);
	g_assert_nonnull(strstr(ist, "arch.zip//bericht_im_zip.txt"));
	g_assert_nonnull(strstr(ist, "mail.eml//"));
	g_assert_nonnull(strstr(ist, "doc.pdf//"));
	g_assert_nonnull(strstr(ist, "sub/Bericht_neu.txt"));
	g_free(ist);

	//Verzeichnis im ZIP
	pruefe(suche_wurzel(f, "ordner", SOND_TVFM_SEARCH_CONTAINS, FALSE, FALSE, TRUE, TRUE),
			"arch.zip//ordner");
	pruefe(suche_wurzel(f, "inhalt", SOND_TVFM_SEARCH_CONTAINS, FALSE, TRUE, FALSE, TRUE),
			"arch.zip//ordner/inhalt.txt");

	//ohne "in Containern" wird nichts darin gefunden
	pruefe(suche_wurzel(f, "inhalt", SOND_TVFM_SEARCH_CONTAINS, FALSE, TRUE, FALSE, FALSE), "");

	fix_free(f);
}

static void test_container_als_treffer(void) {
	Fix *f = baum();

	//ein Container zählt als Datei und wird auch ohne Durchsuchen gefunden
	pruefe(suche_wurzel(f, "arch", SOND_TVFM_SEARCH_CONTAINS, FALSE, TRUE, FALSE, FALSE),
			"arch.zip");
	pruefe(suche_wurzel(f, "mail", SOND_TVFM_SEARCH_CONTAINS, FALSE, TRUE, FALSE, FALSE),
			"mail.eml");
	pruefe(suche_wurzel(f, "doc", SOND_TVFM_SEARCH_CONTAINS, FALSE, TRUE, FALSE, FALSE),
			"doc.pdf");
	//aber nicht als Verzeichnis
	pruefe(suche_wurzel(f, "arch", SOND_TVFM_SEARCH_CONTAINS, FALSE, FALSE, TRUE, FALSE), "");

	fix_free(f);
}

static void test_abbruch(void) {
	Fix *f = baum();
	SondTVFMSearchOpts opts = { "bericht", SOND_TVFM_SEARCH_CONTAINS, FALSE, TRUE, FALSE, FALSE };
	SondTVFMSearch *s = sond_tvfm_search_new(SOND_TVFM_OWNER(f->owner), &opts);
	SondTVFMItem *w = wurzel(f);
	GPtrArray *start = g_ptr_array_new();
	GPtrArray *hits = NULL;
	GError *error = NULL;

	g_ptr_array_add(start, w);

	//vor dem Lauf abgebrochen: 0, keine Treffer
	sond_tvfm_search_cancel(s);
	g_assert_cmpint(sond_tvfm_search_run(s, start, &error), ==, 0);
	g_assert_no_error(error);
	hits = sond_tvfm_search_take_hits(s);
	g_assert_cmpuint(hits->len, ==, 0);

	g_ptr_array_unref(hits);
	g_ptr_array_unref(start);
	g_object_unref(w);
	sond_tvfm_search_free(s);
	fix_free(f);
}

static void test_ohne_doppelte(void) {
	Fix *f = baum();
	SondTVFMSearchOpts opts = { "bericht", SOND_TVFM_SEARCH_CONTAINS, FALSE, TRUE, FALSE, FALSE };
	SondTVFMItem *w = wurzel(f);
	SondTVFMItem *sub = verzeichnis(f, "sub");
	GPtrArray *start = g_ptr_array_new();
	gchar *ist = NULL;

	//"sub" liegt unter der Wurzel: seine Treffer kommen einmal
	g_ptr_array_add(start, w);
	g_ptr_array_add(start, sub);
	ist = sucht(f, start, &opts, NULL);
	g_assert_cmpstr(ist, ==, "Bericht_alt.txt|sub/Bericht_neu.txt|sub/tief/bericht.TXT");
	g_free(ist);

	g_ptr_array_unref(start);
	g_object_unref(w);
	g_object_unref(sub);
	fix_free(f);
}

static void test_treffer_einmal_abholen(void) {
	Fix *f = baum();
	SondTVFMSearchOpts opts = { "a.txt", SOND_TVFM_SEARCH_WHOLE_NAME, FALSE, TRUE, FALSE, FALSE };
	SondTVFMSearch *s = sond_tvfm_search_new(SOND_TVFM_OWNER(f->owner), &opts);
	SondTVFMItem *w = wurzel(f);
	GPtrArray *start = g_ptr_array_new();
	GPtrArray *hits = NULL;

	g_ptr_array_add(start, w);
	g_assert_cmpint(sond_tvfm_search_run(s, start, NULL), ==, 0);

	hits = sond_tvfm_search_take_hits(s);
	g_assert_cmpuint(hits->len, ==, 1);
	g_ptr_array_unref(hits);
	g_assert_null(sond_tvfm_search_take_hits(s)); //gilt nur einmal

	g_assert_cmpuint(sond_tvfm_search_get_n_skipped(s), ==, 0);
	g_assert_cmpuint(sond_tvfm_search_get_n_not_loaded(s), ==, 0);

	g_ptr_array_unref(start);
	g_object_unref(w);
	sond_tvfm_search_free(s);
	fix_free(f);
}

static void test_start_in_unterverzeichnis(void) {
	Fix *f = baum();
	SondTVFMSearchOpts opts = { "bericht", SOND_TVFM_SEARCH_CONTAINS, FALSE, TRUE, FALSE, FALSE };
	SondTVFMItem *sub = verzeichnis(f, "sub");
	GPtrArray *start = g_ptr_array_new();
	gchar *ist = NULL;

	g_ptr_array_add(start, sub);
	ist = sucht(f, start, &opts, NULL);
	g_assert_cmpstr(ist, ==, "sub/Bericht_neu.txt|sub/tief/bericht.TXT");
	g_free(ist);

	g_ptr_array_unref(start);
	g_object_unref(sub);
	fix_free(f);
}

void test_tvfm_search_register(void) {
	g_test_add_func("/tvfm_search/enthaelt_ohne_gross_und_klein", test_enthaelt_ohne_gross_und_klein);
	g_test_add_func("/tvfm_search/enthaelt_mit_gross_und_klein", test_enthaelt_mit_gross_und_klein);
	g_test_add_func("/tvfm_search/ganzer_name", test_ganzer_name);
	g_test_add_func("/tvfm_search/muster", test_muster);
	g_test_add_func("/tvfm_search/verzeichnisse", test_verzeichnisse);
	g_test_add_func("/tvfm_search/container_durchsuchen", test_container_durchsuchen);
	g_test_add_func("/tvfm_search/container_als_treffer", test_container_als_treffer);
	g_test_add_func("/tvfm_search/abbruch", test_abbruch);
	g_test_add_func("/tvfm_search/ohne_doppelte", test_ohne_doppelte);
	g_test_add_func("/tvfm_search/treffer_einmal_abholen", test_treffer_einmal_abholen);
	g_test_add_func("/tvfm_search/start_in_unterverzeichnis", test_start_in_unterverzeichnis);
}
