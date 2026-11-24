/*
 sond (test_tvfm_passwort.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests des Passwortschutzes im Dateibaum: Vorprüfung vor Kopieren und Verschieben
 * (tvfm/sond_tvfm_preflight.c) und dauerhaftes Entsperren von ZIP-Einträgen und PDFs
 * (tvfm/sond_tvfm_unlock.c). Die Dialogfunktion des Passwortspeichers ist nachgebaut, die
 * geschützten ZIP-Archive und PDF-Dateien werden im Test erzeugt.
 */

#include <glib.h>
#include <string.h>
#include <zip.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "../sond_password.h"
#include "../tvfm/sond_tvfm_preflight.h"
#include "../tvfm/sond_tvfm_unlock.h"
#include "tvfm_test_support.h"

/* ---------------------------------------------------------------------------------------- */
/* nachgebauter Dialog                                                                      */

typedef struct {
	SondPromptResult res;
	gchar const *passwort; //bei OK
} Antwort;

static struct {
	Antwort const *antworten;
	guint n;
	guint aufrufe;
	gboolean letzte_ueberspringen_erlaubt;
} dialog;

static SondPromptResult dialog_func(gchar const *what, gboolean wrong, gboolean allow_skip,
		gchar **password, gpointer user_data) {
	Antwort a = { SOND_PROMPT_CANCEL, NULL };

	(void) what;
	(void) wrong;
	(void) user_data;

	if (dialog.aufrufe < dialog.n)
		a = dialog.antworten[dialog.aufrufe];
	dialog.aufrufe++;
	dialog.letzte_ueberspringen_erlaubt = allow_skip;
	if (a.res == SOND_PROMPT_OK)
		*password = g_strdup(a.passwort);

	return a.res;
}

static void dialog_start(Antwort const *antworten, guint n) {
	sond_password_clear();
	dialog.antworten = antworten;
	dialog.n = n;
	dialog.aufrufe = 0;
	sond_password_set_prompt_func(dialog_func, NULL);
}

static void dialog_ende(void) {
	sond_password_set_prompt_func(NULL, NULL);
	sond_password_clear();
}

/* ---------------------------------------------------------------------------------------- */
/* Testdaten                                                                                */

/*
 * arch.zip:
 *   offen.txt            unverschlüsselt
 *   geheim.txt           Passwort pw1
 *   ordner/tief.txt      Passwort pw1
 *   ordner/anders.txt    Passwort pw2
 */
static Fix* zip_fix(void) {
	Fix *f = fix_new();

	schreibe_zip_passwort(f, "arch.zip",
			"offen.txt", "Offen", NULL,
			"geheim.txt", "Geheim", "pw1",
			"ordner/", NULL, NULL,
			"ordner/tief.txt", "Tief", "pw1",
			"ordner/anders.txt", "Anders", "pw2",
			NULL);

	return f;
}

//Kind des Elements parent mit dem Anzeigenamen name (Referenz, Freigabe durch den Aufrufer)
static SondTVFMItem* kind_von(SondTVFMItem *parent, gchar const *name) {
	GPtrArray *arr = NULL;
	GError *error = NULL;
	SondTVFMItem *k = NULL;

	g_assert_cmpint(sond_tvfm_item_load_children(parent, &arr, NULL, &error), ==, 0);
	g_assert_no_error(error);
	k = kind(arr, name);
	g_assert_nonnull(k);
	g_object_ref(k);
	g_ptr_array_unref(arr);

	return k;
}

//Item eines Eintrags in arch.zip ("geheim.txt" oder "ordner/tief.txt")
static SondTVFMItem* eintrag(Fix *f, gchar const *pfad) {
	SondTVFMItem *z = datei(f, "arch.zip");
	SondTVFMItem *aktuell = z;
	gchar **teile = g_strsplit(pfad, "/", -1);

	for (guint i = 0; teile[i]; i++) {
		SondTVFMItem *naechstes = kind_von(aktuell, teile[i]);

		g_object_unref(aktuell);
		aktuell = naechstes;
	}
	g_strfreev(teile);

	return aktuell;
}

static GPtrArray* liste(SondTVFMItem *item) {
	GPtrArray *arr = g_ptr_array_new_with_free_func(g_object_unref);

	g_ptr_array_add(arr, item);

	return arr;
}

static gboolean eintrag_verschluesselt(Fix *f, gchar const *rel_zip, gchar const *pfad) {
	SondFilePart *sfp = sond_file_part_from_filepart(rel_zip, NULL);
	gboolean enc = FALSE;

	(void) f;
	g_assert_nonnull(sfp);
	enc = sond_file_part_zip_entry_is_encrypted(sfp, pfad);
	g_object_unref(sfp);

	return enc;
}

/* ---------------------------------------------------------------------------------------- */
/* Vorprüfung                                                                               */

static void preflight_start(SondTVFMPreflightResult *res, gboolean move) {
	res->aborted = FALSE;
	res->failure = NULL;
	sond_password_action_begin(!move);
}

static void preflight_ende(SondTVFMPreflightResult *res) {
	sond_password_action_end();
	g_free(res->failure);
}

static void test_preflight_dateisystem(void) {
	Fix *f = zip_fix();
	SondTVFMPreflightResult res;
	GPtrArray *items = NULL;

	schreibe(f, "sub/x.txt", "X");
	items = liste(verzeichnis(f, "sub"));
	dialog_start(NULL, 0);

	preflight_start(&res, FALSE);
	g_assert_true(sond_tvfm_preflight(items, NULL, FALSE, &res));
	g_assert_false(res.aborted);
	g_assert_null(res.failure);
	preflight_ende(&res);
	g_assert_cmpuint(dialog.aufrufe, ==, 0);

	g_ptr_array_unref(items);
	dialog_ende();
	fix_free(f);
}

static void test_preflight_unverschluesselter_eintrag(void) {
	Fix *f = zip_fix();
	SondTVFMPreflightResult res;
	GPtrArray *items = liste(eintrag(f, "offen.txt"));

	dialog_start(NULL, 0);
	preflight_start(&res, FALSE);
	g_assert_true(sond_tvfm_preflight(items, NULL, FALSE, &res));
	preflight_ende(&res);
	g_assert_cmpuint(dialog.aufrufe, ==, 0);

	g_ptr_array_unref(items);
	dialog_ende();
	fix_free(f);
}

static void test_preflight_bekanntes_passwort(void) {
	Fix *f = zip_fix();
	SondTVFMPreflightResult res;
	GPtrArray *items = liste(eintrag(f, "geheim.txt"));

	dialog_start(NULL, 0);
	sond_password_set("arch.zip", SOND_PASSWORD_CONTENT, "pw1");

	preflight_start(&res, FALSE);
	g_assert_true(sond_tvfm_preflight(items, NULL, FALSE, &res));
	preflight_ende(&res);
	g_assert_cmpuint(dialog.aufrufe, ==, 0); //still mit dem gemerkten Passwort

	g_ptr_array_unref(items);
	dialog_ende();
	fix_free(f);
}

static void test_preflight_fragt_und_merkt(void) {
	Fix *f = zip_fix();
	SondTVFMPreflightResult res;
	GPtrArray *items = liste(eintrag(f, "geheim.txt"));
	Antwort antworten[] = { { SOND_PROMPT_OK, "pw1" } };

	dialog_start(antworten, 1);

	preflight_start(&res, FALSE);
	g_assert_true(sond_tvfm_preflight(items, NULL, FALSE, &res));
	g_assert_false(res.aborted);
	preflight_ende(&res);
	g_assert_cmpuint(dialog.aufrufe, ==, 1);

	//das bewährte Passwort ist gemerkt: ein zweiter Durchgang fragt nicht mehr
	preflight_start(&res, FALSE);
	g_assert_true(sond_tvfm_preflight(items, NULL, FALSE, &res));
	preflight_ende(&res);
	g_assert_cmpuint(dialog.aufrufe, ==, 1);

	g_ptr_array_unref(items);
	dialog_ende();
	fix_free(f);
}

static void test_preflight_falsches_passwort_dann_richtiges(void) {
	Fix *f = zip_fix();
	SondTVFMPreflightResult res;
	GPtrArray *items = liste(eintrag(f, "geheim.txt"));
	Antwort antworten[] = { { SOND_PROMPT_OK, "falsch" }, { SOND_PROMPT_OK, "pw1" } };

	dialog_start(antworten, 2);

	preflight_start(&res, FALSE);
	g_assert_true(sond_tvfm_preflight(items, NULL, FALSE, &res));
	preflight_ende(&res);
	g_assert_cmpuint(dialog.aufrufe, ==, 2);

	g_ptr_array_unref(items);
	dialog_ende();
	fix_free(f);
}

static void test_preflight_abbruch(void) {
	Fix *f = zip_fix();
	SondTVFMPreflightResult res;
	GPtrArray *items = liste(eintrag(f, "geheim.txt"));
	Antwort antworten[] = { { SOND_PROMPT_CANCEL, NULL } };

	dialog_start(antworten, 1);

	preflight_start(&res, FALSE);
	g_assert_false(sond_tvfm_preflight(items, NULL, FALSE, &res));
	g_assert_true(res.aborted);
	g_assert_null(res.failure);
	preflight_ende(&res);

	g_ptr_array_unref(items);
	dialog_ende();
	fix_free(f);
}

static void test_preflight_ueberspringen_beim_kopieren(void) {
	Fix *f = zip_fix();
	SondTVFMPreflightResult res;
	GPtrArray *items = liste(eintrag(f, "geheim.txt"));
	Antwort antworten[] = { { SOND_PROMPT_SKIP, NULL } };

	dialog_start(antworten, 1);

	preflight_start(&res, FALSE);
	g_assert_true(sond_tvfm_preflight(items, NULL, FALSE, &res));
	g_assert_false(res.aborted);
	g_assert_true(dialog.letzte_ueberspringen_erlaubt);
	g_assert_true(sond_password_action_is_skipped("arch.zip//geheim.txt"));
	preflight_ende(&res);

	g_ptr_array_unref(items);
	dialog_ende();
	fix_free(f);
}

static void test_preflight_ueberspringen_beim_verschieben_bricht_ab(void) {
	Fix *f = zip_fix();
	SondTVFMPreflightResult res;
	GPtrArray *items = liste(eintrag(f, "geheim.txt"));
	Antwort antworten[] = { { SOND_PROMPT_SKIP, NULL } };

	dialog_start(antworten, 1);

	preflight_start(&res, TRUE);
	g_assert_false(sond_tvfm_preflight(items, NULL, TRUE, &res));
	g_assert_true(res.aborted);
	g_assert_false(dialog.letzte_ueberspringen_erlaubt); //beim Verschieben nicht anbieten
	preflight_ende(&res);

	g_ptr_array_unref(items);
	dialog_ende();
	fix_free(f);
}

static void test_preflight_verschieben_im_selben_archiv(void) {
	Fix *f = zip_fix();
	SondTVFMPreflightResult res;
	SondTVFMItem *ziel = datei(f, "arch.zip");
	GPtrArray *items = liste(eintrag(f, "geheim.txt"));

	dialog_start(NULL, 0);

	//im selben Archiv ist Verschieben ein Umbenennen, ohne den Inhalt zu lesen
	preflight_start(&res, TRUE);
	g_assert_true(sond_tvfm_preflight(items, ziel, TRUE, &res));
	preflight_ende(&res);
	g_assert_cmpuint(dialog.aufrufe, ==, 0);

	//Kopieren dagegen liest
	preflight_start(&res, FALSE);
	g_assert_false(sond_tvfm_preflight(items, ziel, FALSE, &res));
	preflight_ende(&res);
	g_assert_cmpuint(dialog.aufrufe, ==, 1);

	g_object_unref(ziel);
	g_ptr_array_unref(items);
	dialog_ende();
	fix_free(f);
}

static void test_preflight_ordner_im_archiv(void) {
	Fix *f = zip_fix();
	SondTVFMPreflightResult res;
	GPtrArray *items = liste(eintrag(f, "ordner"));
	Antwort antworten[] = { { SOND_PROMPT_OK, "pw1" }, { SOND_PROMPT_OK, "pw2" } };

	dialog_start(antworten, 2);

	//beide Einträge unter ordner sind verschlüsselt, mit verschiedenen Passwörtern
	preflight_start(&res, FALSE);
	g_assert_true(sond_tvfm_preflight(items, NULL, FALSE, &res));
	g_assert_false(res.aborted);
	preflight_ende(&res);
	g_assert_cmpuint(dialog.aufrufe, ==, 2);

	g_ptr_array_unref(items);
	dialog_ende();
	fix_free(f);
}

static void test_preflight_ohne_dialog_meldet_fehler(void) {
	Fix *f = zip_fix();
	SondTVFMPreflightResult res;
	GPtrArray *items = liste(eintrag(f, "geheim.txt"));

	sond_password_clear();
	sond_password_set_prompt_func(NULL, NULL);

	preflight_start(&res, FALSE);
	g_assert_false(sond_tvfm_preflight(items, NULL, FALSE, &res));
	g_assert_false(res.aborted);
	g_assert_nonnull(res.failure);
	preflight_ende(&res);

	g_ptr_array_unref(items);
	fix_free(f);
}

/* ---------------------------------------------------------------------------------------- */
/* Entsperren                                                                               */

typedef struct {
	guint refresh;
	gboolean busy;
	gboolean lokal;
	guint ensure_local;
	gchar *meldung;
} HookDaten;

static gboolean hook_ensure_local(SondFilePart *sfp, gpointer data) {
	HookDaten *h = data;

	(void) sfp;
	h->ensure_local++;

	return h->lokal;
}

static void hook_refresh(SondFilePart *sfp, gpointer data) {
	HookDaten *h = data;

	(void) sfp;
	h->refresh++;
}

static void hook_report(gchar const *message, gpointer data) {
	HookDaten *h = data;

	g_free(h->meldung);
	h->meldung = g_strdup(message);
}

static gboolean hook_busy(SondFilePart *sfp, gpointer data) {
	(void) sfp;

	return ((HookDaten*) data)->busy;
}

static SondUnlockHooks hooks_von(HookDaten *h) {
	return (SondUnlockHooks ) { hook_ensure_local, hook_refresh, hook_report, hook_busy, h };
}

static void test_unlock_sammeln_und_zaehlen(void) {
	Fix *f = zip_fix();
	SondUnlockJobs jobs;
	SondTVFMItem *z = datei(f, "arch.zip");
	SondTVFMItem *e = eintrag(f, "geheim.txt");
	guint n = 99;
	gchar *meldung = NULL;

	//nichts Geschütztes: Dateisystem-Verzeichnis, unverschlüsselter Eintrag
	schreibe(f, "sub/x.txt", "X");
	sond_tvfm_unlock_jobs_init(&jobs);
	{
		SondTVFMItem *offen = eintrag(f, "offen.txt");
		SondTVFMItem *dir = verzeichnis(f, "sub");

		sond_tvfm_unlock_collect_item(&jobs, offen);
		sond_tvfm_unlock_collect_item(&jobs, dir);
		g_object_unref(offen);
		g_object_unref(dir);
	}
	g_assert_cmpint(sond_tvfm_unlock_prepare(&jobs, NULL, &n, &meldung), ==, SOND_UNLOCK_NOTHING);
	g_assert_cmpuint(n, ==, 0);
	sond_tvfm_unlock_jobs_clear(&jobs);

	//ein einzelner Eintrag
	sond_tvfm_unlock_jobs_init(&jobs);
	sond_tvfm_unlock_collect_item(&jobs, e);
	sond_tvfm_unlock_collect_item(&jobs, e); //doppelt: einmal
	g_assert_cmpint(sond_tvfm_unlock_prepare(&jobs, NULL, &n, &meldung), ==, SOND_UNLOCK_READY);
	g_assert_cmpuint(n, ==, 1);
	sond_tvfm_unlock_jobs_clear(&jobs);

	//das ganze Archiv: alle drei verschlüsselten Einträge, auch zusammen mit einem davon
	sond_tvfm_unlock_jobs_init(&jobs);
	sond_tvfm_unlock_collect_item(&jobs, z);
	sond_tvfm_unlock_collect_item(&jobs, e);
	g_assert_cmpint(sond_tvfm_unlock_prepare(&jobs, NULL, &n, &meldung), ==, SOND_UNLOCK_READY);
	g_assert_cmpuint(n, ==, 3);
	sond_tvfm_unlock_jobs_clear(&jobs);

	//ein Ordner im Archiv: die zwei darunter
	sond_tvfm_unlock_jobs_init(&jobs);
	{
		SondTVFMItem *dir = eintrag(f, "ordner");

		sond_tvfm_unlock_collect_item(&jobs, dir);
		g_object_unref(dir);
	}
	g_assert_cmpint(sond_tvfm_unlock_prepare(&jobs, NULL, &n, &meldung), ==, SOND_UNLOCK_READY);
	g_assert_cmpuint(n, ==, 2);
	sond_tvfm_unlock_jobs_clear(&jobs);

	g_free(meldung);
	g_object_unref(z);
	g_object_unref(e);
	fix_free(f);
}

static void test_unlock_vorbereiten_nicht_lokal(void) {
	Fix *f = zip_fix();
	SondUnlockJobs jobs;
	HookDaten h = { 0, FALSE, FALSE, 0, NULL };
	SondUnlockHooks hooks = hooks_von(&h);
	SondTVFMItem *e = eintrag(f, "geheim.txt");
	guint n = 0;

	sond_tvfm_unlock_jobs_init(&jobs);
	sond_tvfm_unlock_collect_item(&jobs, e);
	g_assert_cmpint(sond_tvfm_unlock_prepare(&jobs, &hooks, &n, NULL), ==, SOND_UNLOCK_NOT_LOCAL);
	g_assert_cmpuint(h.ensure_local, ==, 1);

	h.lokal = TRUE;
	g_assert_cmpint(sond_tvfm_unlock_prepare(&jobs, &hooks, &n, NULL), ==, SOND_UNLOCK_READY);
	sond_tvfm_unlock_jobs_clear(&jobs);

	g_object_unref(e);
	fix_free(f);
}

static void test_unlock_pdf_geoeffnet(void) {
	Fix *f = fix_new();
	SondUnlockJobs jobs;
	HookDaten h = { 0, TRUE, TRUE, 0, NULL };
	SondUnlockHooks hooks = hooks_von(&h);
	SondTVFMItem *pdf = NULL;
	gchar *meldung = NULL;
	guint n = 0;

	schreibe_pdf_passwort(f, "geschuetzt.pdf", "abc");
	pdf = datei(f, "geschuetzt.pdf");

	sond_tvfm_unlock_jobs_init(&jobs);
	sond_tvfm_unlock_collect_item(&jobs, pdf);
	g_assert_cmpint(sond_tvfm_unlock_prepare(&jobs, &hooks, &n, &meldung), ==, SOND_UNLOCK_BUSY);
	g_assert_cmpuint(n, ==, 1);
	g_assert_nonnull(meldung);
	g_assert_nonnull(strstr(meldung, "geschuetzt.pdf"));

	h.busy = FALSE;
	g_assert_cmpint(sond_tvfm_unlock_prepare(&jobs, &hooks, &n, NULL), ==, SOND_UNLOCK_READY);
	sond_tvfm_unlock_jobs_clear(&jobs);

	g_free(meldung);
	g_object_unref(pdf);
	fix_free(f);
}

static void test_unlock_zip_mit_bekannten_passwoertern(void) {
	Fix *f = zip_fix();
	SondUnlockJobs jobs;
	SondUnlockResult res;
	HookDaten h = { 0, FALSE, TRUE, 0, NULL };
	SondUnlockHooks hooks = hooks_von(&h);
	SondTVFMItem *z = datei(f, "arch.zip");

	dialog_start(NULL, 0);
	sond_password_set("arch.zip", SOND_PASSWORD_CONTENT, "pw1");
	sond_password_set("arch.zip//ordner/anders.txt", SOND_PASSWORD_ENTRY, "pw2");

	g_assert_true(eintrag_verschluesselt(f, "arch.zip", "geheim.txt"));
	g_assert_false(eintrag_verschluesselt(f, "arch.zip", "offen.txt"));

	sond_tvfm_unlock_jobs_init(&jobs);
	sond_tvfm_unlock_collect_item(&jobs, z);
	sond_tvfm_unlock_run(&jobs, &hooks, &res);

	g_assert_false(res.aborted);
	g_assert_cmpuint(res.done, ==, 3);
	g_assert_cmpuint(res.skipped, ==, 0);
	g_assert_cmpuint(res.decrypted->len, ==, 3);
	g_assert_cmpuint(res.skipped_fps->len, ==, 0);
	g_assert_cmpuint(h.refresh, ==, 3); //die Platzhalter sind durch echte Elemente ersetzt
	g_assert_cmpuint(dialog.aufrufe, ==, 0);

	g_assert_false(eintrag_verschluesselt(f, "arch.zip", "geheim.txt"));
	g_assert_false(eintrag_verschluesselt(f, "arch.zip", "ordner/tief.txt"));
	g_assert_false(eintrag_verschluesselt(f, "arch.zip", "ordner/anders.txt"));

	//Inhalt ist erhalten
	{
		SondFilePart *sfp = sond_file_part_from_filepart("arch.zip//ordner/anders.txt", NULL);
		GBytes *bytes = NULL;

		g_assert_nonnull(sfp);
		bytes = sond_file_part_get_bytes(sfp, NULL);
		g_assert_nonnull(bytes);
		g_assert_cmpmem(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes), "Anders", 6);
		g_bytes_unref(bytes);
		g_object_unref(sfp);
	}

	//ein zweiter Lauf findet nichts mehr
	sond_tvfm_unlock_result_clear(&res);
	sond_tvfm_unlock_jobs_clear(&jobs);
	sond_tvfm_unlock_jobs_init(&jobs);
	sond_tvfm_unlock_collect_item(&jobs, z);
	g_assert_cmpint(sond_tvfm_unlock_prepare(&jobs, NULL, NULL, NULL), ==, SOND_UNLOCK_NOTHING);
	sond_tvfm_unlock_jobs_clear(&jobs);

	g_object_unref(z);
	dialog_ende();
	fix_free(f);
}

static void test_unlock_zip_fragt_nach(void) {
	Fix *f = zip_fix();
	SondUnlockJobs jobs;
	SondUnlockResult res;
	HookDaten h = { 0, FALSE, TRUE, 0, NULL };
	SondUnlockHooks hooks = hooks_von(&h);
	SondTVFMItem *dir = eintrag(f, "ordner");
	Antwort antworten[] = { { SOND_PROMPT_OK, "pw1" }, { SOND_PROMPT_OK, "pw2" } };

	dialog_start(antworten, 2);

	sond_tvfm_unlock_jobs_init(&jobs);
	sond_tvfm_unlock_collect_item(&jobs, dir);
	sond_tvfm_unlock_run(&jobs, &hooks, &res);

	g_assert_false(res.aborted);
	g_assert_cmpuint(res.done, ==, 2);
	g_assert_cmpuint(dialog.aufrufe, ==, 2);
	g_assert_false(eintrag_verschluesselt(f, "arch.zip", "ordner/tief.txt"));
	g_assert_false(eintrag_verschluesselt(f, "arch.zip", "ordner/anders.txt"));
	//was nicht gewählt war, bleibt verschlüsselt
	g_assert_true(eintrag_verschluesselt(f, "arch.zip", "geheim.txt"));

	sond_tvfm_unlock_result_clear(&res);
	sond_tvfm_unlock_jobs_clear(&jobs);
	g_object_unref(dir);
	dialog_ende();
	fix_free(f);
}

static void test_unlock_zip_alle_ueberspringen(void) {
	Fix *f = zip_fix();
	SondUnlockJobs jobs;
	SondUnlockResult res;
	SondTVFMItem *z = datei(f, "arch.zip");
	Antwort antworten[] = { { SOND_PROMPT_SKIP_ALL, NULL } };

	dialog_start(antworten, 1);

	sond_tvfm_unlock_jobs_init(&jobs);
	sond_tvfm_unlock_collect_item(&jobs, z);
	sond_tvfm_unlock_run(&jobs, NULL, &res);

	g_assert_false(res.aborted);
	g_assert_cmpuint(res.done, ==, 0);
	g_assert_cmpuint(res.skipped, ==, 3);
	g_assert_cmpuint(res.skipped_fps->len, ==, 3);
	g_assert_cmpuint(dialog.aufrufe, ==, 1);
	g_assert_true(eintrag_verschluesselt(f, "arch.zip", "geheim.txt"));

	sond_tvfm_unlock_result_clear(&res);
	sond_tvfm_unlock_jobs_clear(&jobs);
	g_object_unref(z);
	dialog_ende();
	fix_free(f);
}

static void test_unlock_zip_abbrechen(void) {
	Fix *f = zip_fix();
	SondUnlockJobs jobs;
	SondUnlockResult res;
	SondTVFMItem *z = datei(f, "arch.zip");
	Antwort antworten[] = { { SOND_PROMPT_CANCEL, NULL } };

	dialog_start(antworten, 1);

	sond_tvfm_unlock_jobs_init(&jobs);
	sond_tvfm_unlock_collect_item(&jobs, z);
	sond_tvfm_unlock_run(&jobs, NULL, &res);

	g_assert_true(res.aborted);
	g_assert_cmpuint(res.done, ==, 0);
	g_assert_true(eintrag_verschluesselt(f, "arch.zip", "geheim.txt"));

	sond_tvfm_unlock_result_clear(&res);
	sond_tvfm_unlock_jobs_clear(&jobs);
	g_object_unref(z);
	dialog_ende();
	fix_free(f);
}

static gboolean pdf_verlangt_passwort(Fix *f, gchar const *rel) {
	gchar *p = g_build_filename(f->dir, rel, NULL);
	fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	pdf_document *doc = NULL;
	gboolean verlangt = FALSE;

	fz_try(ctx) {
		doc = pdf_open_document(ctx, p);
		verlangt = pdf_needs_password(ctx, doc);
	}
	fz_catch(ctx)
		g_error("PDF nicht lesbar: %s", fz_caught_message(ctx));

	pdf_drop_document(ctx, doc);
	fz_drop_context(ctx);
	g_free(p);

	return verlangt;
}

static void test_unlock_pdf(void) {
	Fix *f = fix_new();
	SondUnlockJobs jobs;
	SondUnlockResult res;
	HookDaten h = { 0, FALSE, TRUE, 0, NULL };
	SondUnlockHooks hooks = hooks_von(&h);
	SondTVFMItem *pdf = NULL;
	Antwort antworten[] = { { SOND_PROMPT_OK, "abc" } };

	schreibe_pdf_passwort(f, "geschuetzt.pdf", "abc");
	g_assert_true(pdf_verlangt_passwort(f, "geschuetzt.pdf"));
	pdf = datei(f, "geschuetzt.pdf");
	dialog_start(antworten, 1);

	sond_tvfm_unlock_jobs_init(&jobs);
	sond_tvfm_unlock_collect_item(&jobs, pdf);
	sond_tvfm_unlock_run(&jobs, &hooks, &res);

	g_assert_false(res.aborted);
	g_assert_cmpuint(res.done, ==, 1);
	g_assert_cmpuint(res.decrypted->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(res.decrypted, 0), ==, "geschuetzt.pdf");
	g_assert_cmpuint(h.refresh, ==, 1);
	g_assert_false(pdf_verlangt_passwort(f, "geschuetzt.pdf"));

	sond_tvfm_unlock_result_clear(&res);
	sond_tvfm_unlock_jobs_clear(&jobs);
	g_object_unref(pdf);
	dialog_ende();
	fix_free(f);
}

static void test_unlock_pdf_falsches_passwort_abbruch(void) {
	Fix *f = fix_new();
	SondUnlockJobs jobs;
	SondUnlockResult res;
	SondTVFMItem *pdf = NULL;
	Antwort antworten[] = { { SOND_PROMPT_CANCEL, NULL } };

	schreibe_pdf_passwort(f, "geschuetzt.pdf", "abc");
	pdf = datei(f, "geschuetzt.pdf");
	dialog_start(antworten, 1);

	sond_tvfm_unlock_jobs_init(&jobs);
	sond_tvfm_unlock_collect_item(&jobs, pdf);
	sond_tvfm_unlock_run(&jobs, NULL, &res);

	g_assert_true(res.aborted);
	g_assert_cmpuint(res.done, ==, 0);
	g_assert_true(pdf_verlangt_passwort(f, "geschuetzt.pdf"));

	sond_tvfm_unlock_result_clear(&res);
	sond_tvfm_unlock_jobs_clear(&jobs);
	g_object_unref(pdf);
	dialog_ende();
	fix_free(f);
}

static void test_unlock_gesperrte_teile(void) {
	Fix *f = zip_fix();
	SondUnlockResult res;
	GPtrArray *items = g_ptr_array_new_with_free_func(NULL);
	SondLockedItem zip_eintrag = { (gchar*) "arch.zip//geheim.txt", SOND_LOCKED_ZIP_ENTRY, NULL };
	SondLockedItem fehlt = { (gchar*) "nicht_da.zip//x.txt", SOND_LOCKED_ZIP_ENTRY, NULL };
	SondLockedItem pdf = { (gchar*) "geschuetzt.pdf", SOND_LOCKED_PDF, NULL };

	schreibe_pdf_passwort(f, "geschuetzt.pdf", "abc");
	dialog_start(NULL, 0);
	sond_password_set("arch.zip", SOND_PASSWORD_CONTENT, "pw1");
	sond_password_set("geschuetzt.pdf", SOND_PASSWORD_CONTENT, "abc");

	g_ptr_array_add(items, &zip_eintrag);
	g_ptr_array_add(items, &fehlt);
	g_ptr_array_add(items, &pdf);

	sond_tvfm_unlock_locked_items(items, NULL, &res);

	g_assert_false(res.aborted);
	g_assert_cmpuint(res.done, ==, 2);
	g_assert_cmpuint(res.skipped_fps->len, ==, 1); //was sich nicht bereitstellen ließ
	g_assert_cmpstr(g_ptr_array_index(res.skipped_fps, 0), ==, "nicht_da.zip//x.txt");
	g_assert_false(eintrag_verschluesselt(f, "arch.zip", "geheim.txt"));
	g_assert_true(eintrag_verschluesselt(f, "arch.zip", "ordner/tief.txt"));
	g_assert_false(pdf_verlangt_passwort(f, "geschuetzt.pdf"));

	sond_tvfm_unlock_result_clear(&res);
	g_ptr_array_unref(items);
	dialog_ende();
	fix_free(f);
}

void test_tvfm_passwort_register(void) {
	g_test_add_func("/tvfm_passwort/preflight_dateisystem", test_preflight_dateisystem);
	g_test_add_func("/tvfm_passwort/preflight_unverschluesselter_eintrag",
			test_preflight_unverschluesselter_eintrag);
	g_test_add_func("/tvfm_passwort/preflight_bekanntes_passwort", test_preflight_bekanntes_passwort);
	g_test_add_func("/tvfm_passwort/preflight_fragt_und_merkt", test_preflight_fragt_und_merkt);
	g_test_add_func("/tvfm_passwort/preflight_falsches_passwort_dann_richtiges",
			test_preflight_falsches_passwort_dann_richtiges);
	g_test_add_func("/tvfm_passwort/preflight_abbruch", test_preflight_abbruch);
	g_test_add_func("/tvfm_passwort/preflight_ueberspringen_beim_kopieren",
			test_preflight_ueberspringen_beim_kopieren);
	g_test_add_func("/tvfm_passwort/preflight_ueberspringen_beim_verschieben_bricht_ab",
			test_preflight_ueberspringen_beim_verschieben_bricht_ab);
	g_test_add_func("/tvfm_passwort/preflight_verschieben_im_selben_archiv",
			test_preflight_verschieben_im_selben_archiv);
	g_test_add_func("/tvfm_passwort/preflight_ordner_im_archiv", test_preflight_ordner_im_archiv);
	g_test_add_func("/tvfm_passwort/preflight_ohne_dialog_meldet_fehler",
			test_preflight_ohne_dialog_meldet_fehler);
	g_test_add_func("/tvfm_passwort/unlock_sammeln_und_zaehlen", test_unlock_sammeln_und_zaehlen);
	g_test_add_func("/tvfm_passwort/unlock_vorbereiten_nicht_lokal",
			test_unlock_vorbereiten_nicht_lokal);
	g_test_add_func("/tvfm_passwort/unlock_pdf_geoeffnet", test_unlock_pdf_geoeffnet);
	g_test_add_func("/tvfm_passwort/unlock_zip_mit_bekannten_passwoertern",
			test_unlock_zip_mit_bekannten_passwoertern);
	g_test_add_func("/tvfm_passwort/unlock_zip_fragt_nach", test_unlock_zip_fragt_nach);
	g_test_add_func("/tvfm_passwort/unlock_zip_alle_ueberspringen", test_unlock_zip_alle_ueberspringen);
	g_test_add_func("/tvfm_passwort/unlock_zip_abbrechen", test_unlock_zip_abbrechen);
	g_test_add_func("/tvfm_passwort/unlock_pdf", test_unlock_pdf);
	g_test_add_func("/tvfm_passwort/unlock_pdf_falsches_passwort_abbruch",
			test_unlock_pdf_falsches_passwort_abbruch);
	g_test_add_func("/tvfm_passwort/unlock_gesperrte_teile", test_unlock_gesperrte_teile);
}
