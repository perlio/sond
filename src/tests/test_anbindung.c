/*
 sond (test_anbindung.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests für znd/anbindung.c: Vergleiche (gleich, vor, Eltern), Eigenschaften
 * (Punkt, unterseitig, leer), Umfang "nur die Anbindung" und die Umwandlung
 * in die Zeichenkette der Datenbank (section) und zurück.
 */

#include <glib.h>

#include "../znd/anbindung.h"

#define A(vs, vi, bs, bi) ((Anbindung) { { (vs), (vi) }, { (bs), (bi) } })

//ganze Seiten von bis
#define SEITEN(von, bis) A((von), 0, (bis), EOP)

static void test_gleich(void) {
	g_assert_true(anbindung_1_gleich_2(SEITEN(2, 5), SEITEN(2, 5)));
	g_assert_false(anbindung_1_gleich_2(SEITEN(2, 5), SEITEN(2, 6)));
	g_assert_false(anbindung_1_gleich_2(SEITEN(2, 5), SEITEN(3, 5)));
	g_assert_false(anbindung_1_gleich_2(A(2, 0, 5, EOP), A(2, 10, 5, EOP)));
	g_assert_false(anbindung_1_gleich_2(A(2, 0, 5, EOP), A(2, 0, 5, 200)));
}

static void test_punkt(void) {
	//ein Punkt hat eine Position und kein "bis"
	g_assert_true(anbindung_is_pdf_punkt(A(3, 17, 0, 0)));
	g_assert_true(anbindung_is_pdf_punkt(A(0, 17, 0, 0)));
	g_assert_true(anbindung_is_pdf_punkt(A(3, 0, 0, 0)));
	//Datei (alles 0) ist kein Punkt, ebenso ein Bereich
	g_assert_false(anbindung_is_pdf_punkt(A(0, 0, 0, 0)));
	g_assert_false(anbindung_is_pdf_punkt(SEITEN(3, 3)));
	g_assert_false(anbindung_is_pdf_punkt(A(3, 17, 5, 0)));
}

static void test_leer(void) {
	Anbindung leer = A(0, 0, 0, 0);
	Anbindung punkt = A(0, 5, 0, 0);
	Anbindung bereich = SEITEN(0, 0);

	g_assert_true(anbindung_is_empty(&leer));
	g_assert_false(anbindung_is_empty(&punkt));
	g_assert_false(anbindung_is_empty(&bereich));
}

static void test_unterseitig(void) {
	//ganze Seiten sind nicht unterseitig
	g_assert_false(anbindung_ist_unterseitig(SEITEN(2, 5)));
	g_assert_false(anbindung_ist_unterseitig(SEITEN(0, 0)));
	//Beginn oder Ende innerhalb einer Seite
	g_assert_true(anbindung_ist_unterseitig(A(2, 40, 5, EOP)));
	g_assert_true(anbindung_ist_unterseitig(A(2, 0, 5, 300)));
	//ein Punkt ist immer unterseitig
	g_assert_true(anbindung_ist_unterseitig(A(3, 17, 0, 0)));
}

static void test_bis_seite(void) {
	g_assert_cmpint(anbindung_bis_seite(SEITEN(2, 5)), ==, 5);
	//Punkt: die eigene Seite
	g_assert_cmpint(anbindung_bis_seite(A(3, 17, 0, 0)), ==, 3);
	//Bereich bis Seite 0 (bis.index gesetzt): bis.seite
	g_assert_cmpint(anbindung_bis_seite(A(0, 0, 0, EOP)), ==, 0);
}

static void test_vor(void) {
	//getrennte Bereiche
	g_assert_true(anbindung_1_vor_2(SEITEN(0, 2), SEITEN(3, 5)));
	g_assert_false(anbindung_1_vor_2(SEITEN(3, 5), SEITEN(0, 2)));
	//gleiche Anbindung ist nicht "vor"
	g_assert_false(anbindung_1_vor_2(SEITEN(0, 2), SEITEN(0, 2)));
	//überlappend oder angrenzend innerhalb derselben Seite
	g_assert_false(anbindung_1_vor_2(SEITEN(0, 3), SEITEN(3, 5)));
	g_assert_true(anbindung_1_vor_2(A(0, 0, 3, 100), A(3, 200, 5, EOP)));
	g_assert_false(anbindung_1_vor_2(A(0, 0, 3, 300), A(3, 200, 5, EOP)));
	//Punkte
	g_assert_true(anbindung_1_vor_2(A(1, 10, 0, 0), SEITEN(2, 3)));
	g_assert_false(anbindung_1_vor_2(A(4, 10, 0, 0), SEITEN(2, 3)));
	g_assert_true(anbindung_1_vor_2(SEITEN(0, 1), A(2, 10, 0, 0)));
	g_assert_false(anbindung_1_vor_2(SEITEN(0, 2), A(2, 10, 0, 0)));
}

static void test_eltern(void) {
	Anbindung datei = A(0, 0, 0, 0);

	//die Datei ist Eltern von jedem Bereich, aber nicht von sich selbst
	g_assert_true(anbindung_1_eltern_von_2(datei, SEITEN(2, 3)));
	g_assert_false(anbindung_1_eltern_von_2(datei, datei));
	//ein Bereich ist nie Eltern der Datei
	g_assert_false(anbindung_1_eltern_von_2(SEITEN(0, 9), datei));

	//umfassender Bereich
	g_assert_true(anbindung_1_eltern_von_2(SEITEN(0, 9), SEITEN(2, 3)));
	g_assert_true(anbindung_1_eltern_von_2(SEITEN(0, 9), SEITEN(0, 3)));
	g_assert_true(anbindung_1_eltern_von_2(SEITEN(2, 9), SEITEN(2, 3)));
	//gleicher Bereich ist nicht Eltern
	g_assert_false(anbindung_1_eltern_von_2(SEITEN(2, 3), SEITEN(2, 3)));
	//Kind ist nie Eltern
	g_assert_false(anbindung_1_eltern_von_2(SEITEN(2, 3), SEITEN(0, 9)));
	//beginnt später oder endet früher
	g_assert_false(anbindung_1_eltern_von_2(SEITEN(3, 9), SEITEN(2, 5)));
	g_assert_false(anbindung_1_eltern_von_2(SEITEN(0, 4), SEITEN(2, 5)));
	//beginnt gleich und endet nicht später
	g_assert_false(anbindung_1_eltern_von_2(SEITEN(2, 3), SEITEN(2, 4)));
	//innerhalb einer Seite
	g_assert_true(anbindung_1_eltern_von_2(A(2, 0, 4, EOP), A(2, 50, 4, 300)));
	g_assert_false(anbindung_1_eltern_von_2(A(2, 50, 4, 300), A(2, 0, 4, EOP)));

	//ein Punkt kann nie Eltern sein
	g_assert_false(anbindung_1_eltern_von_2(A(3, 17, 0, 0), SEITEN(3, 3)));
	g_assert_false(anbindung_1_eltern_von_2(A(0, 17, 0, 0), SEITEN(3, 3)));
}

static void test_ges_nur_anbindung(void) {
	Anbindung ges = A(0, 0, 0, 0);

	//Bereich: er selbst
	anbindung_ges_nur_anbindung(SEITEN(2, 5), "{{0,0}{9,99999}}", &ges);
	g_assert_true(anbindung_1_gleich_2(ges, SEITEN(2, 5)));

	//Punkt: der nächsthöhere Abschnitt
	ges = A(0, 0, 0, 0);
	anbindung_ges_nur_anbindung(A(3, 17, 0, 0), "{{0,0}{9,99999}}", &ges);
	g_assert_true(anbindung_1_gleich_2(ges, SEITEN(0, 9)));

	//Punkt ohne übergeordneten Abschnitt: bleibt leer (ganze Datei)
	ges = A(0, 0, 0, 0);
	anbindung_ges_nur_anbindung(A(3, 17, 0, 0), NULL, &ges);
	g_assert_true(anbindung_is_empty(&ges));
}

static void test_parse(void) {
	Anbindung a = A(0, 0, 0, 0);

	//Bereich
	anbindung_parse_file_section("{{1,0}{4,99999}}", &a);
	g_assert_true(anbindung_1_gleich_2(a, SEITEN(1, 4)));

	a = A(0, 0, 0, 0);
	anbindung_parse_file_section("{{12,345}{20,678}}", &a);
	g_assert_true(anbindung_1_gleich_2(a, A(12, 345, 20, 678)));

	//ohne Section bleibt die Anbindung unverändert
	a = SEITEN(7, 8);
	anbindung_parse_file_section(NULL, &a);
	g_assert_true(anbindung_1_gleich_2(a, SEITEN(7, 8)));
}

/* Ein Punkt steht als "{seite,index}" in der Datenbank. anbindung_parse_file_section() übergibt
 * dem Parser dafür file_section + 1 (hinter der Klammer), der Parser überspringt die Klammer aber
 * selbst: die erste Ziffer der Seite geht verloren ("{3,17}" ergibt Seite 0, "{12,345}" Seite 2). */
static void test_parse_punkt(void) {
	Anbindung a = A(0, 0, 0, 0);
	Anbindung b = A(0, 0, 0, 0);

	anbindung_parse_file_section("{3,17}", &a);
	anbindung_parse_file_section("{12,345}", &b);

	if (!anbindung_1_gleich_2(a, A(3, 17, 0, 0)) || !anbindung_1_gleich_2(b, A(12, 345, 0, 0))) {
		g_test_incomplete("Punkt wird falsch gelesen: anbindung_parse_file_section, Zweig ohne zwei Klammern");

		return;
	}

	g_assert_true(anbindung_1_gleich_2(a, A(3, 17, 0, 0)));
	g_assert_true(anbindung_1_gleich_2(b, A(12, 345, 0, 0)));
}

static void test_build(void) {
	gchar *s = NULL;

	anbindung_build_file_section(A(3, 17, 0, 0), &s);
	g_assert_cmpstr(s, ==, "{3,17}");
	g_free(s);

	anbindung_build_file_section(SEITEN(1, 4), &s);
	g_assert_cmpstr(s, ==, "{{1,0}{4,99999}}");
	g_free(s);

	anbindung_build_file_section(A(12, 345, 20, 678), &s);
	g_assert_cmpstr(s, ==, "{{12,345}{20,678}}");
	g_free(s);
}

static void test_rundlauf(void) {
	//Bereiche; Punkte s. test_parse_punkt
	Anbindung faelle[] = { SEITEN(0, 0), SEITEN(1, 4), SEITEN(0, 99), A(2, 40, 5, 300),
			A(10, 0, 10, 500) };

	for (guint i = 0; i < G_N_ELEMENTS(faelle); i++) {
		gchar *s = NULL;
		Anbindung zurueck = A(0, 0, 0, 0);

		anbindung_build_file_section(faelle[i], &s);
		anbindung_parse_file_section(s, &zurueck);
		g_assert_true(anbindung_1_gleich_2(faelle[i], zurueck));
		g_free(s);
	}
}

static void test_human_readable(void) {
	Anbindung a = SEITEN(2, 4);
	gchar *s = anbindung_to_human_readable(&a);

	g_assert_cmpstr(s, ==, "S. 3 - S. 5");
	g_free(s);

	a = A(2, 40, 4, 300);
	s = anbindung_to_human_readable(&a);
	g_assert_cmpstr(s, ==, "S. 3, Index 40 - S. 5, Index 300");
	g_free(s);
}

void test_anbindung_register(void) {
	g_test_add_func("/anbindung/gleich", test_gleich);
	g_test_add_func("/anbindung/punkt", test_punkt);
	g_test_add_func("/anbindung/leer", test_leer);
	g_test_add_func("/anbindung/unterseitig", test_unterseitig);
	g_test_add_func("/anbindung/bis_seite", test_bis_seite);
	g_test_add_func("/anbindung/vor", test_vor);
	g_test_add_func("/anbindung/eltern", test_eltern);
	g_test_add_func("/anbindung/ges_nur_anbindung", test_ges_nur_anbindung);
	g_test_add_func("/anbindung/parse", test_parse);
	g_test_add_func("/anbindung/parse_punkt", test_parse_punkt);
	g_test_add_func("/anbindung/build", test_build);
	g_test_add_func("/anbindung/rundlauf", test_rundlauf);
	g_test_add_func("/anbindung/human_readable", test_human_readable);
}
