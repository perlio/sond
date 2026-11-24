/*
 sond (test_sond_password.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests des sitzungsweiten Passwortspeichers (sond_password.c): Ablage nach Rolle, Reihenfolge der
 * Kandidaten, Umbenennen und Vergessen nach Präfix, Abfrage beim Nutzer (mit nachgebauter
 * Dialogfunktion) und die Klammer um eine Aktion über mehrere Dateien (Überspringen, Abbrechen).
 */

#include <glib.h>

#include "../sond_password.h"

/* ---------------------------------------------------------------------------------------- */
/* nachgebaute Dialogfunktion                                                               */

typedef struct {
	SondPromptResult const *antworten; //Folge der Antworten; danach CANCEL
	guint n;
	guint aufrufe;
	gchar *was;
	gboolean falsch;
	gboolean ueberspringen_erlaubt;
} Dialog;

static Dialog dialog;

static SondPromptResult dialog_func(gchar const *what, gboolean wrong, gboolean allow_skip,
		gchar **password, gpointer user_data) {
	SondPromptResult antwort = SOND_PROMPT_CANCEL;

	(void) user_data;

	g_free(dialog.was);
	dialog.was = g_strdup(what);
	dialog.falsch = wrong;
	dialog.ueberspringen_erlaubt = allow_skip;
	if (dialog.aufrufe < dialog.n)
		antwort = dialog.antworten[dialog.aufrufe];
	dialog.aufrufe++;

	if (antwort == SOND_PROMPT_OK)
		*password = g_strdup("eingabe");

	return antwort;
}

static void dialog_start(SondPromptResult const *antworten, guint n) {
	sond_password_clear();
	g_free(dialog.was);
	dialog = (Dialog ) { antworten, n, 0, NULL, FALSE, FALSE };
	sond_password_set_prompt_func(dialog_func, NULL);
}

static void dialog_ende(void) {
	sond_password_set_prompt_func(NULL, NULL);
	g_free(dialog.was);
	dialog.was = NULL;
	sond_password_clear();
}

static void get_erwartet(gchar const *filepart, SondPasswordRole rolle, gchar const *erwartet) {
	gchar *pw = sond_password_get(filepart, rolle);

	g_assert_cmpstr(pw, ==, erwartet);
	sond_password_free(pw);
}

/* ---------------------------------------------------------------------------------------- */
/* Ablage                                                                                   */

static void test_ablegen_und_lesen(void) {
	sond_password_clear();

	get_erwartet("a.zip//x.pdf", SOND_PASSWORD_ENTRY, NULL);

	sond_password_set("a.zip//x.pdf", SOND_PASSWORD_ENTRY, "eins");
	sond_password_set("a.zip//x.pdf", SOND_PASSWORD_CONTENT, "zwei");
	get_erwartet("a.zip//x.pdf", SOND_PASSWORD_ENTRY, "eins");
	get_erwartet("a.zip//x.pdf", SOND_PASSWORD_CONTENT, "zwei");
	get_erwartet("a.zip//y.pdf", SOND_PASSWORD_ENTRY, NULL);

	//ersetzen
	sond_password_set("a.zip//x.pdf", SOND_PASSWORD_ENTRY, "drei");
	get_erwartet("a.zip//x.pdf", SOND_PASSWORD_ENTRY, "drei");
	get_erwartet("a.zip//x.pdf", SOND_PASSWORD_CONTENT, "zwei");

	//leer oder NULL entfernt
	sond_password_set("a.zip//x.pdf", SOND_PASSWORD_ENTRY, "");
	get_erwartet("a.zip//x.pdf", SOND_PASSWORD_ENTRY, NULL);
	sond_password_set("a.zip//x.pdf", SOND_PASSWORD_CONTENT, NULL);
	get_erwartet("a.zip//x.pdf", SOND_PASSWORD_CONTENT, NULL);

	//Entfernen eines nicht vorhandenen Passworts schadet nicht
	sond_password_set("nie.zip", SOND_PASSWORD_ENTRY, NULL);

	sond_password_clear();
}

static void test_kopie_ist_unabhaengig(void) {
	gchar *pw = NULL;

	sond_password_clear();
	sond_password_set("a.zip", SOND_PASSWORD_CONTENT, "geheim");
	pw = sond_password_get("a.zip", SOND_PASSWORD_CONTENT);
	g_assert_cmpstr(pw, ==, "geheim");
	sond_password_free(pw); //überschreibt nur die Kopie
	get_erwartet("a.zip", SOND_PASSWORD_CONTENT, "geheim");
	sond_password_free(NULL);
	sond_password_clear();
}

static void test_alles_vergessen(void) {
	sond_password_set("a.zip", SOND_PASSWORD_CONTENT, "eins");
	sond_password_set("b.pdf", SOND_PASSWORD_CONTENT, "zwei");
	sond_password_clear();
	get_erwartet("a.zip", SOND_PASSWORD_CONTENT, NULL);
	get_erwartet("b.pdf", SOND_PASSWORD_CONTENT, NULL);
}

/* ---------------------------------------------------------------------------------------- */
/* Kandidaten                                                                               */

static void kandidaten_erwartet(gchar const *filepart, gchar const *e0, gchar const *e1,
		gchar const *e2, gchar const *e3) {
	GPtrArray *arr = sond_password_candidates_entry(filepart);
	gchar const *erwartet[] = { e0, e1, e2, e3 };
	guint n = 0;

	for (guint i = 0; i < G_N_ELEMENTS(erwartet); i++)
		if (erwartet[i])
			n++;

	g_assert_cmpuint(arr->len, ==, n);
	for (guint i = 0; i < n; i++)
		g_assert_cmpstr(g_ptr_array_index(arr, i), ==, erwartet[i]);
	sond_password_array_free(arr);
}

static void test_kandidaten_reihenfolge(void) {
	sond_password_clear();

	//nichts bekannt
	kandidaten_erwartet("a.zip//x.pdf", NULL, NULL, NULL, NULL);

	//Standardpasswort des Archivs
	sond_password_set("a.zip", SOND_PASSWORD_CONTENT, "standard");
	kandidaten_erwartet("a.zip//x.pdf", "standard", NULL, NULL, NULL);

	//eigenes Passwort des Eintrags zuerst
	sond_password_set("a.zip//x.pdf", SOND_PASSWORD_ENTRY, "eigen");
	kandidaten_erwartet("a.zip//x.pdf", "eigen", "standard", NULL, NULL);

	//Passwort eines anderen Eintrags desselben Archivs zuletzt
	sond_password_set("a.zip//y.pdf", SOND_PASSWORD_ENTRY, "fremd");
	kandidaten_erwartet("a.zip//x.pdf", "eigen", "standard", "fremd", NULL);

	//ohne Doppelte
	sond_password_set("a.zip//y.pdf", SOND_PASSWORD_ENTRY, "standard");
	kandidaten_erwartet("a.zip//x.pdf", "eigen", "standard", NULL, NULL);

	//für den Eintrag y gilt entsprechend
	kandidaten_erwartet("a.zip//y.pdf", "standard", "eigen", NULL, NULL);

	sond_password_clear();
}

static void test_kandidaten_abgrenzung(void) {
	sond_password_clear();

	sond_password_set("a.zip//x.pdf", SOND_PASSWORD_ENTRY, "a_x");
	sond_password_set("b.zip//x.pdf", SOND_PASSWORD_ENTRY, "b_x");
	//Eintrag im Archiv selbst, nicht in einem darin liegenden: tiefer liegende zählen nicht
	sond_password_set("a.zip//inner.zip//z.pdf", SOND_PASSWORD_ENTRY, "tief");
	//Inhaltspasswort einer PDF gehört nicht zu den Kandidaten ihres Archivs
	sond_password_set("a.zip//x.pdf", SOND_PASSWORD_CONTENT, "pdf_inhalt");

	kandidaten_erwartet("a.zip//w.pdf", "a_x", NULL, NULL, NULL);
	kandidaten_erwartet("b.zip//w.pdf", "b_x", NULL, NULL, NULL);

	//ohne umgebendes Archiv: nur das eigene
	sond_password_set("lose.pdf", SOND_PASSWORD_ENTRY, "lose");
	kandidaten_erwartet("lose.pdf", "lose", NULL, NULL, NULL);
	kandidaten_erwartet("andere.pdf", NULL, NULL, NULL, NULL);

	sond_password_clear();
}

/* ---------------------------------------------------------------------------------------- */
/* Umbenennen und Vergessen                                                                 */

static void test_umbenennen(void) {
	sond_password_clear();

	sond_password_set("a.zip", SOND_PASSWORD_CONTENT, "archiv");
	sond_password_set("a.zip//x.pdf", SOND_PASSWORD_ENTRY, "eintrag");
	sond_password_set("a.zip//x.pdf", SOND_PASSWORD_CONTENT, "inhalt");
	sond_password_set("a.zip/ordner/y.txt", SOND_PASSWORD_ENTRY, "unterordner");
	//anderer Name mit gleichem Anfang
	sond_password_set("a.zipx", SOND_PASSWORD_CONTENT, "anders");
	sond_password_set("ab.zip//x.pdf", SOND_PASSWORD_ENTRY, "ab");

	sond_password_rename_prefix("a.zip", "neu.zip");

	get_erwartet("a.zip", SOND_PASSWORD_CONTENT, NULL);
	get_erwartet("a.zip//x.pdf", SOND_PASSWORD_ENTRY, NULL);
	get_erwartet("neu.zip", SOND_PASSWORD_CONTENT, "archiv");
	get_erwartet("neu.zip//x.pdf", SOND_PASSWORD_ENTRY, "eintrag");
	get_erwartet("neu.zip//x.pdf", SOND_PASSWORD_CONTENT, "inhalt");
	get_erwartet("neu.zip/ordner/y.txt", SOND_PASSWORD_ENTRY, "unterordner");
	get_erwartet("a.zipx", SOND_PASSWORD_CONTENT, "anders");
	get_erwartet("ab.zip//x.pdf", SOND_PASSWORD_ENTRY, "ab");

	//ein Eintrag allein
	sond_password_rename_prefix("neu.zip//x.pdf", "neu.zip//z.pdf");
	get_erwartet("neu.zip//z.pdf", SOND_PASSWORD_ENTRY, "eintrag");
	get_erwartet("neu.zip//z.pdf", SOND_PASSWORD_CONTENT, "inhalt");
	get_erwartet("neu.zip//x.pdf", SOND_PASSWORD_ENTRY, NULL);

	//Umbenennen ohne Passwörter schadet nicht
	sond_password_rename_prefix("nichts", "anderes");

	sond_password_clear();
}

static void test_umbenennen_ohne_tabelle(void) {
	//nach clear() ist die Tabelle leer, nicht NULL; hier nur: kein Absturz
	sond_password_clear();
	sond_password_rename_prefix("a", "b");
	sond_password_forget_prefix("a");
}

static void test_vergessen(void) {
	sond_password_clear();

	sond_password_set("a.zip", SOND_PASSWORD_CONTENT, "archiv");
	sond_password_set("a.zip//x.pdf", SOND_PASSWORD_ENTRY, "eintrag");
	sond_password_set("a.zip//x.pdf", SOND_PASSWORD_CONTENT, "inhalt");
	sond_password_set("a.zip/ordner/y.txt", SOND_PASSWORD_ENTRY, "unterordner");
	sond_password_set("a.zipx", SOND_PASSWORD_CONTENT, "anders");

	sond_password_forget_prefix("a.zip//x.pdf");
	get_erwartet("a.zip//x.pdf", SOND_PASSWORD_ENTRY, NULL);
	get_erwartet("a.zip//x.pdf", SOND_PASSWORD_CONTENT, NULL);
	get_erwartet("a.zip", SOND_PASSWORD_CONTENT, "archiv");

	sond_password_forget_prefix("a.zip");
	get_erwartet("a.zip", SOND_PASSWORD_CONTENT, NULL);
	get_erwartet("a.zip/ordner/y.txt", SOND_PASSWORD_ENTRY, NULL);
	get_erwartet("a.zipx", SOND_PASSWORD_CONTENT, "anders");

	sond_password_clear();
}

/* ---------------------------------------------------------------------------------------- */
/* Abfrage beim Nutzer                                                                      */

static void test_abfrage_ohne_dialog(void) {
	sond_password_set_prompt_func(NULL, NULL);
	g_assert_false(sond_password_can_prompt());
	g_assert_null(sond_password_prompt("x", FALSE));
}

static void test_abfrage_ok(void) {
	SondPromptResult antworten[] = { SOND_PROMPT_OK };
	gchar *pw = NULL;

	dialog_start(antworten, 1);
	g_assert_true(sond_password_can_prompt());

	pw = sond_password_prompt("Passwort für x.pdf", TRUE);
	g_assert_cmpstr(pw, ==, "eingabe");
	sond_password_free(pw);
	g_assert_cmpuint(dialog.aufrufe, ==, 1);
	g_assert_cmpstr(dialog.was, ==, "Passwort für x.pdf");
	g_assert_true(dialog.falsch);
	g_assert_false(dialog.ueberspringen_erlaubt); //keine Aktion: nie überspringen
	g_assert_cmpint(sond_password_last_result(), ==, SOND_PROMPT_OK);
	dialog_ende();
}

static gpointer thread_func(gpointer data) {
	(void) data;
	if (sond_password_can_prompt())
		return GINT_TO_POINTER(1);
	return sond_password_prompt("x", FALSE); //NULL, ohne den Dialog zu rufen
}

static void test_abfrage_nur_im_hauptthread(void) {
	SondPromptResult antworten[] = { SOND_PROMPT_OK };
	gpointer ergebnis = NULL;
	GThread *t = NULL;

	dialog_start(antworten, 1);

	t = g_thread_new("fremd", thread_func, NULL);
	ergebnis = g_thread_join(t);
	g_assert_null(ergebnis);
	g_assert_cmpuint(dialog.aufrufe, ==, 0);
	dialog_ende();
}

static void test_abfrage_ausserhalb_aktion_ohne_dauerwirkung(void) {
	SondPromptResult antworten[] = { SOND_PROMPT_SKIP_ALL, SOND_PROMPT_CANCEL, SOND_PROMPT_OK };
	gchar *pw = NULL;

	dialog_start(antworten, 3);

	g_assert_null(sond_password_prompt("x", FALSE));
	g_assert_cmpint(sond_password_last_result(), ==, SOND_PROMPT_SKIP_ALL);
	g_assert_null(sond_password_prompt("x", FALSE)); //wird wieder gefragt
	g_assert_cmpint(sond_password_last_result(), ==, SOND_PROMPT_CANCEL);
	g_assert_false(sond_password_action_cancelled());
	pw = sond_password_prompt("x", FALSE);
	g_assert_nonnull(pw);
	sond_password_free(pw);
	g_assert_cmpuint(dialog.aufrufe, ==, 3);
	dialog_ende();
}

/* ---------------------------------------------------------------------------------------- */
/* Aktion über mehrere Dateien                                                              */

static void test_aktion_ueberspringen(void) {
	SondPromptResult antworten[] = { SOND_PROMPT_SKIP, SOND_PROMPT_OK };
	gchar *pw = NULL;

	dialog_start(antworten, 2);
	sond_password_action_begin(TRUE);

	g_assert_null(sond_password_prompt("x", FALSE));
	g_assert_true(dialog.ueberspringen_erlaubt);
	g_assert_cmpint(sond_password_last_result(), ==, SOND_PROMPT_SKIP);
	g_assert_false(sond_password_action_cancelled());

	//Überspringen gilt nur für dieses Element
	pw = sond_password_prompt("y", FALSE);
	g_assert_nonnull(pw);
	sond_password_free(pw);
	g_assert_cmpuint(dialog.aufrufe, ==, 2);
	g_assert_cmpint(sond_password_last_result(), ==, SOND_PROMPT_OK);

	sond_password_action_end();
	dialog_ende();
}

static void test_aktion_alle_ueberspringen(void) {
	SondPromptResult antworten[] = { SOND_PROMPT_SKIP_ALL, SOND_PROMPT_OK };

	dialog_start(antworten, 2);
	sond_password_action_begin(TRUE);

	g_assert_null(sond_password_prompt("x", FALSE));
	g_assert_cmpint(sond_password_last_result(), ==, SOND_PROMPT_SKIP_ALL);

	//danach nicht mehr fragen, aber überspringen melden
	g_assert_null(sond_password_prompt("y", FALSE));
	g_assert_null(sond_password_prompt("z", FALSE));
	g_assert_cmpuint(dialog.aufrufe, ==, 1);
	g_assert_cmpint(sond_password_last_result(), ==, SOND_PROMPT_SKIP);
	g_assert_false(sond_password_action_cancelled());

	//eine neue Aktion fragt wieder
	sond_password_action_end();
	sond_password_action_begin(TRUE);
	g_assert_nonnull(sond_password_prompt("w", FALSE));
	g_assert_cmpuint(dialog.aufrufe, ==, 2);
	sond_password_action_end();
	dialog_ende();
}

static void test_aktion_abbrechen(void) {
	SondPromptResult antworten[] = { SOND_PROMPT_CANCEL, SOND_PROMPT_OK };

	dialog_start(antworten, 2);
	sond_password_action_begin(FALSE);

	g_assert_null(sond_password_prompt("x", FALSE));
	g_assert_false(dialog.ueberspringen_erlaubt);
	g_assert_true(sond_password_action_cancelled());
	g_assert_null(sond_password_prompt("y", FALSE));
	g_assert_cmpuint(dialog.aufrufe, ==, 1);
	g_assert_cmpint(sond_password_last_result(), ==, SOND_PROMPT_CANCEL);

	sond_password_action_end();
	g_assert_false(sond_password_action_cancelled());
	dialog_ende();
}

static void test_aktion_geschachtelt(void) {
	SondPromptResult antworten[] = { SOND_PROMPT_SKIP_ALL, SOND_PROMPT_OK };

	dialog_start(antworten, 2);
	sond_password_action_begin(TRUE);
	sond_password_action_begin(FALSE); //zählt nicht: der äußerste Aufruf bestimmt

	g_assert_null(sond_password_prompt("x", FALSE));
	g_assert_true(dialog.ueberspringen_erlaubt);

	//das Ende des inneren Aufrufs hebt nichts auf
	sond_password_action_end();
	g_assert_null(sond_password_prompt("y", FALSE));
	g_assert_cmpuint(dialog.aufrufe, ==, 1);

	sond_password_action_end();
	g_assert_nonnull(sond_password_prompt("z", FALSE));
	g_assert_cmpuint(dialog.aufrufe, ==, 2);
	dialog_ende();
}

static void test_aktion_ende_ohne_beginn(void) {
	sond_password_action_end(); //schadet nicht
	g_assert_false(sond_password_action_cancelled());
}

static void test_aktion_uebersprungene_teile(void) {
	GPtrArray *arr = NULL;

	sond_password_action_begin(TRUE);
	g_assert_false(sond_password_action_is_skipped("a.zip//x.pdf"));
	g_assert_false(sond_password_action_is_skipped(NULL));

	sond_password_action_skip_add("b.zip//y.pdf");
	sond_password_action_skip_add("a.zip//x.pdf");
	sond_password_action_skip_add("a.zip//x.pdf"); //doppelt: einmal
	g_assert_true(sond_password_action_is_skipped("a.zip//x.pdf"));
	g_assert_false(sond_password_action_is_skipped("a.zip//z.pdf"));

	arr = sond_password_action_skipped();
	g_assert_cmpuint(arr->len, ==, 2);
	//die Reihenfolge ist nicht festgelegt (siehe TODO #270): beide kommen vor
	g_assert_true((!g_strcmp0(g_ptr_array_index(arr, 0), "a.zip//x.pdf") &&
			!g_strcmp0(g_ptr_array_index(arr, 1), "b.zip//y.pdf")) ||
			(!g_strcmp0(g_ptr_array_index(arr, 0), "b.zip//y.pdf") &&
			!g_strcmp0(g_ptr_array_index(arr, 1), "a.zip//x.pdf")));
	g_ptr_array_unref(arr);

	//ein innerer Beginn setzt die Liste nicht zurück, das Ende der äußersten Aktion schon
	sond_password_action_begin(TRUE);
	g_assert_true(sond_password_action_is_skipped("a.zip//x.pdf"));
	sond_password_action_end();
	g_assert_true(sond_password_action_is_skipped("a.zip//x.pdf"));
	sond_password_action_end();
	g_assert_false(sond_password_action_is_skipped("a.zip//x.pdf"));

	arr = sond_password_action_skipped();
	g_assert_cmpuint(arr->len, ==, 0);
	g_ptr_array_unref(arr);

	//eine neue Aktion beginnt mit leerer Liste
	sond_password_action_begin(TRUE);
	sond_password_action_skip_add("c.zip//z.pdf");
	sond_password_action_end();
	sond_password_action_begin(TRUE);
	g_assert_false(sond_password_action_is_skipped("c.zip//z.pdf"));
	sond_password_action_end();
}

void test_sond_password_register(void) {
	g_test_add_func("/sond_password/ablegen_und_lesen", test_ablegen_und_lesen);
	g_test_add_func("/sond_password/kopie_ist_unabhaengig", test_kopie_ist_unabhaengig);
	g_test_add_func("/sond_password/alles_vergessen", test_alles_vergessen);
	g_test_add_func("/sond_password/kandidaten_reihenfolge", test_kandidaten_reihenfolge);
	g_test_add_func("/sond_password/kandidaten_abgrenzung", test_kandidaten_abgrenzung);
	g_test_add_func("/sond_password/umbenennen", test_umbenennen);
	g_test_add_func("/sond_password/umbenennen_ohne_tabelle", test_umbenennen_ohne_tabelle);
	g_test_add_func("/sond_password/vergessen", test_vergessen);
	g_test_add_func("/sond_password/abfrage_ohne_dialog", test_abfrage_ohne_dialog);
	g_test_add_func("/sond_password/abfrage_ok", test_abfrage_ok);
	g_test_add_func("/sond_password/abfrage_nur_im_hauptthread", test_abfrage_nur_im_hauptthread);
	g_test_add_func("/sond_password/abfrage_ausserhalb_aktion_ohne_dauerwirkung",
			test_abfrage_ausserhalb_aktion_ohne_dauerwirkung);
	g_test_add_func("/sond_password/aktion_ueberspringen", test_aktion_ueberspringen);
	g_test_add_func("/sond_password/aktion_alle_ueberspringen", test_aktion_alle_ueberspringen);
	g_test_add_func("/sond_password/aktion_abbrechen", test_aktion_abbrechen);
	g_test_add_func("/sond_password/aktion_geschachtelt", test_aktion_geschachtelt);
	g_test_add_func("/sond_password/aktion_ende_ohne_beginn", test_aktion_ende_ohne_beginn);
	g_test_add_func("/sond_password/aktion_uebersprungene_teile", test_aktion_uebersprungene_teile);
}
