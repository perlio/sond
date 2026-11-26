/*
 sond (test_anbindung_zaehlung.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests der Seitenzählung (zond/anbindung_zaehlung.c): anbindung_get_orig, _get_sichtbar,
 * _korrigieren und _aktualisieren rechnen Seitenzahlen einer Anbindung zwischen der Zählung der
 * Datei auf der Platte, der Live-Zählung des geöffneten Dokuments (mit gelöschten und noch nicht
 * gespeicherten eingefügten Seiten) und der Zählung des Viewers um.
 *
 * Getestet wird die echte Datei, gegen die echten Zugriffsfunktionen von zond_pdf_document.c. Das
 * Dokument ist ein ZondPdfDocument ohne geöffnete Datei, dessen Seiten-Array der Test füllt;
 * PdfDocumentPage und ZPDFDPart sind die echten Strukturen.
 */

#include <glib.h>

#include "../viewer/anbindung_zaehlung.h"
#include "../znd/anbindung.h"

/* ---------------------------------------------------------------------------------------- */
/* Dokument und Abschnitt ohne PDF-Datei                                                     */

/* Ein ZondPdfDocument ohne geöffnete Datei: das Seiten-Array wird im Test gefüllt. Es hängt nie
 * an einer Datei und wird deshalb nicht freigegeben (finalize braucht die geöffnete Datei); die
 * Zählung liest nur die Seiten und die Grenzen des Abschnitts über die echten Zugriffsfunktionen. */

#define A(vs, vi, bs, bi) ((Anbindung) { { (vs), (vi) }, { (bs), (bi) } })

static PdfDocumentPage* neue_seite(ZondPdfDocument *d) {
	PdfDocumentPage *p = g_new0(PdfDocumentPage, 1);

	p->document = d;

	return p;
}

static ZondPdfDocument* neues_dokument(gint n) {
	ZondPdfDocument *d = g_object_new(ZOND_TYPE_PDF_DOCUMENT, NULL);

	for (gint i = 0; i < n; i++) {
		PdfDocumentPage *p = neue_seite(d);

		p->page_akt = i;
		g_ptr_array_add(zond_pdf_document_get_arr_pages(d), p);
	}

	return d;
}

static void dokument_frei(ZondPdfDocument *d) {
	GPtrArray *pages = zond_pdf_document_get_arr_pages(d);

	//die Seiten gibt das Array frei (ohne geladene Seite, ohne Kontext); das Dokument bleibt liegen
	g_ptr_array_set_size(pages, 0);
}

static PdfDocumentPage* seite(ZondPdfDocument *d, gint i) {
	return g_ptr_array_index(zond_pdf_document_get_arr_pages(d), i);
}

static ZPDFDPart* neuer_teil(ZondPdfDocument *d, gint von, gint bis) {
	ZPDFDPart *t = g_new0(ZPDFDPart, 1);

	t->zond_pdf_document = d;
	t->first_page = seite(d, von);
	t->first_index = 0;
	t->last_page = seite(d, bis);
	t->last_index = EOP;

	return t;
}

//Seite wurde in den Abschnitt eingefügt und ist noch nicht gespeichert
static void eingefuegt(PdfDocumentPage *p, ZPDFDPart *teil) {
	p->inserted = TRUE;
	p->inserted_in = teil;
}

static Anbindung ganze_seiten(gint von, gint bis) {
	return A(von, 0, bis, EOP);
}

#define ERWARTET(name, anb, erste, letzte) G_STMT_START { \
	Anbindung e_a = (anb); \
	g_test_message("%s", (name)); \
	g_assert_cmpint(e_a.von.seite, ==, (erste)); \
	g_assert_cmpint(e_a.bis.seite, ==, (letzte)); \
} G_STMT_END

/* Wie viewer_host_update_section_schema: Platte (vor dem Speichern) -> live -> Platte (danach) */
static Anbindung speichern_nachfuehren(ZPDFDPart *teil, Anbindung db) {
	anbindung_aktualisieren(teil->zond_pdf_document, &db);
	anbindung_korrigieren(teil, &db);

	return db;
}

//Nach dem Speichern: gelöschte Seiten sind aus der Datei raus, eingefügte sind drin
static void nach_dem_speichern(ZondPdfDocument *d) {
	for (guint i = 0; i < zond_pdf_document_get_arr_pages(d)->len; i++) {
		PdfDocumentPage *p = seite(d, i);

		p->inserted = FALSE;
		p->inserted_in = NULL;
		if (p->deleted)
			p->on_disk_deleted = TRUE;
	}
}

/* ---------------------------------------------------------------------------------------- */
/* feste Fälle                                                                               */

//Seite 4 (live) wird gelöscht und gespeichert
static void test_loeschen_speichern(void) {
	ZondPdfDocument *d = neues_dokument(20);
	ZPDFDPart *teil = neuer_teil(d, 0, 19);

	seite(d, 4)->deleted = TRUE;

	ERWARTET("A1 Anbindung 2-3 (vor der Seite)", speichern_nachfuehren(teil, ganze_seiten(2, 3)), 2, 3);
	ERWARTET("A2 Anbindung 6-7 (hinter der Seite)", speichern_nachfuehren(teil, ganze_seiten(6, 7)), 5, 6);
	ERWARTET("A3 Anbindung 3-6 (über die Seite)", speichern_nachfuehren(teil, ganze_seiten(3, 6)), 3, 5);

	g_free(teil);
	dokument_frei(d);
}

//danach, in derselben Sitzung: die gelöschte Seite steht noch als Karteileiche im Array
static void test_nach_dem_speichern(void) {
	ZondPdfDocument *d = neues_dokument(20);
	ZPDFDPart *teil = neuer_teil(d, 0, 19);

	seite(d, 4)->deleted = TRUE;
	nach_dem_speichern(d);

	{
		Anbindung a = ganze_seiten(5, 6); //DB nach dem Speichern: Platte

		anbindung_aktualisieren(d, &a);
		ERWARTET("B1 DB 5-6 in Live-Zählung", a, 6, 7);
	}
	{
		Anbindung a = ganze_seiten(11, 11); //im Viewer live Seite 11 angeklickt
		Anbindung t = a;

		anbindung_get_orig(d, &a);
		anbindung_get_sichtbar(d, &t);
		ERWARTET("B2 neue Anbindung live 11: Platte", a, 10, 10);
		ERWARTET("B3 neue Anbindung live 11: sichtbar", t, 10, 10);
	}
	{
		Anbindung a = ganze_seiten(1, 1);
		Anbindung t = a;

		anbindung_get_orig(d, &a);
		anbindung_get_sichtbar(d, &t);
		ERWARTET("B4 neue Anbindung live 1 (vor Seite 4)", a, 1, 1);
		ERWARTET("B5 sichtbar", t, 1, 1);
	}

	//zweites Speichern ohne neue Änderung
	ERWARTET("C1 DB 5-6 bleibt", speichern_nachfuehren(teil, ganze_seiten(5, 6)), 5, 6);
	ERWARTET("C2 DB 2-3 bleibt", speichern_nachfuehren(teil, ganze_seiten(2, 3)), 2, 3);
	ERWARTET("C3 DB 3-5 bleibt", speichern_nachfuehren(teil, ganze_seiten(3, 5)), 3, 5);

	g_free(teil);
	dokument_frei(d);
}

//Neustart: frisches Dokument ohne Karteileichen
static void test_neustart(void) {
	ZondPdfDocument *d = neues_dokument(19);
	Anbindung a = ganze_seiten(5, 6);
	Anbindung o = ganze_seiten(10, 10);
	Anbindung t = o;

	anbindung_aktualisieren(d, &a);
	ERWARTET("D1 DB 5-6 bleibt 5-6", a, 5, 6);
	anbindung_get_orig(d, &o);
	anbindung_get_sichtbar(d, &t);
	ERWARTET("D2 neue Anbindung 10: Platte", o, 10, 10);
	ERWARTET("D3 neue Anbindung 10: sichtbar", t, 10, 10);

	dokument_frei(d);
}

//gelöschte Seite liegt außerhalb des gespeicherten Teils
static void test_loeschung_ausserhalb(void) {
	ZondPdfDocument *d = neues_dokument(20);
	ZPDFDPart *teil = neuer_teil(d, 10, 15);

	seite(d, 4)->deleted = TRUE; //noch nicht gespeichert, anderer Teil

	ERWARTET("E1 DB 12-13 im Teil 10-15", speichern_nachfuehren(teil, ganze_seiten(12, 13)), 12, 13);

	g_free(teil);
	dokument_frei(d);
}

//früher gespeicherte Löschung (Seite 2) und neue Löschung (Seite 8) im Teil
static void test_alte_und_neue_loeschung(void) {
	ZondPdfDocument *d = neues_dokument(20);
	ZPDFDPart *teil = neuer_teil(d, 0, 19);

	seite(d, 2)->deleted = TRUE;
	seite(d, 2)->on_disk_deleted = TRUE;
	seite(d, 8)->deleted = TRUE; //neu

	/* Platte vor dem Speichern: ohne Seite 2; Platten-Seite 10 ist live 11, die neue Löschung
	 * (live 8 = Platte 7) liegt davor */
	ERWARTET("F1 DB 10-11", speichern_nachfuehren(teil, ganze_seiten(10, 11)), 9, 10);
	ERWARTET("F2 DB 1-1 (vor beiden)", speichern_nachfuehren(teil, ganze_seiten(1, 1)), 1, 1);
	ERWARTET("F3 DB 4-9 (über die neue Löschung)", speichern_nachfuehren(teil, ganze_seiten(4, 9)), 4, 8);

	g_free(teil);
	dokument_frei(d);
}

//eingefügte und gleich wieder gelöschte Seite (Phantom) im Teil
static void test_phantom_seite(void) {
	ZondPdfDocument *d = neues_dokument(12);
	ZPDFDPart *teil = neuer_teil(d, 0, 11);
	ZPDFDPart *block = neuer_teil(d, 5, 5);
	Anbindung t = ganze_seiten(8, 8);
	Anbindung o = ganze_seiten(8, 8);

	eingefuegt(seite(d, 5), block);
	seite(d, 5)->deleted = TRUE;

	anbindung_get_sichtbar(d, &t);
	ERWARTET("G1 sichtbar live 8", t, 7, 7);
	anbindung_get_orig(d, &o);
	ERWARTET("G2 Platte live 8 (Phantom zählt nicht)", o, 7, 7);

	g_free(teil);
	g_free(block);
	dokument_frei(d);
}

/* Eine über einen inzwischen freigegebenen Abschnitt eingefügte Seite (inserted_in ist NULL) gilt
 * an der Grenze einer Anbindung nicht als zu ihr gehörend; eine über einen lebenden Abschnitt
 * eingefügte, der die Anbindung umfasst, schon. */
static void test_eingefuegt_randseite(void) {
	ZondPdfDocument *d = neues_dokument(11);
	ZPDFDPart *teil = neuer_teil(d, 2, 5);
	Anbindung a;

	//eine Seite hinter dem Teil (live 6), eingefügt über den Teil; Platte hat sonst 10 Seiten
	eingefuegt(seite(d, 6), teil);
	teil->last_page = seite(d, 6); //der Abschnitt wurde um sie erweitert

	a = ganze_seiten(2, 5);
	anbindung_aktualisieren(d, &a);
	ERWARTET("H1 Randseite gehört zur gleichen Anbindung", a, 2, 6);

	//der Abschnitt ist freigegeben: die Zuordnung fehlt
	seite(d, 6)->inserted_in = NULL;
	a = ganze_seiten(2, 5);
	anbindung_aktualisieren(d, &a);
	ERWARTET("H2 ohne Abschnitt außerhalb", a, 2, 5);

	g_free(teil);
	dokument_frei(d);
}

/* ---------------------------------------------------------------------------------------- */
/* Zufall mit fester Startzahl                                                               */

//Zahl der Seiten der Liste vor Index l
static gint rang(gboolean *in_liste, gint n, gint l) {
	gint r = 0;

	for (gint i = 0; i < l && i < n; i++)
		if (in_liste[i])
			r++;

	return r;
}

/* Eigenschaften mit Löschungen (ohne eingefügte Seiten), gegen eine unabhängige Referenz aus
 * Seitenlisten: D = Seiten auf der Platte vor dem Speichern, S = Seiten danach. */
static void test_eigenschaften(void) {
	GRand *r = g_rand_new_with_seed(7);
	gint geprueft = 0;
	gint abweichungen = 0;

	for (gint run = 0; run < 3000; run++) {
		gint n = 6 + g_rand_int_range(r, 0, 14);
		ZondPdfDocument *d = neues_dokument(n);
		gint pv = g_rand_int_range(r, 0, n);
		gint pb = g_rand_int_range(r, pv, n);
		gboolean whole = g_rand_int_range(r, 0, 2);
		ZPDFDPart *teil = NULL;
		gboolean *in_d = g_new0(gboolean, n);
		gboolean *in_s = g_new0(gboolean, n);
		gboolean *in_vis = g_new0(gboolean, n);
		gint nd = 0;

		if (whole) {
			pv = 0;
			pb = n - 1;
		}
		teil = neuer_teil(d, pv, pb);

		for (gint i = 0; i < n; i++) {
			gint w = g_rand_int_range(r, 0, 100);

			if (w < 15) {
				seite(d, i)->deleted = TRUE;
				seite(d, i)->on_disk_deleted = TRUE;
			}
			else if (w < 30)
				seite(d, i)->deleted = TRUE;
		}

		for (gint i = 0; i < n; i++) {
			PdfDocumentPage *p = seite(d, i);

			in_d[i] = !p->on_disk_deleted;
			in_s[i] = !(p->on_disk_deleted || (p->deleted && i >= pv && i <= pb));
			in_vis[i] = !p->deleted;
			if (in_d[i])
				nd++;
		}

		//Platten-Seiten (D-Index) auf Live-Index
		for (gint k = 0; k < 6; k++) {
			gint dv = g_rand_int_range(r, 0, MAX(nd, 1));
			gint db = g_rand_int_range(r, dv, MAX(nd, 1));
			gint lv = -1, lb = -1, c = 0;

			for (gint i = 0; i < n; i++) {
				if (!in_d[i])
					continue;
				if (c == dv)
					lv = i;
				if (c == db)
					lb = i;
				c++;
			}
			if (lv < 0 || lb < 0 || !in_s[lv] || !in_s[lb])
				continue; //Rand-Seiten müssen überleben

			{
				Anbindung x = ganze_seiten(dv, db);
				Anbindung live = x;
				Anbindung zurueck;
				Anbindung gespeichert = speichern_nachfuehren(teil, x);
				gint ev = rang(in_s, n, lv);
				gint eb = rang(in_s, n, lb);

				anbindung_aktualisieren(d, &live);
				zurueck = live;
				anbindung_get_orig(d, &zurueck);

				geprueft++;
				if (live.von.seite != lv || live.bis.seite != lb) {
					abweichungen++;
					g_test_message("P0 Platte->live z%d: %d-%d ergab %d-%d erwartet %d-%d", run, dv,
							db, live.von.seite, live.bis.seite, lv, lb);
				}
				if (zurueck.von.seite != dv || zurueck.bis.seite != db) {
					abweichungen++;
					g_test_message("P1 live->Platte z%d: %d-%d ergab %d-%d", run, dv, db,
							zurueck.von.seite, zurueck.bis.seite);
				}
				if (gespeichert.von.seite != ev || gespeichert.bis.seite != eb) {
					abweichungen++;
					g_test_message("P2 Speichern z%d: %d-%d ergab %d-%d erwartet %d-%d", run, dv,
							db, gespeichert.von.seite, gespeichert.bis.seite, ev, eb);
				}
			}
		}

		//angeklickte, sichtbare Live-Seiten: Platte und sichtbar
		for (gint l = 0; l < n; l++) {
			Anbindung o, t;

			if (seite(d, l)->deleted)
				continue;
			o = ganze_seiten(l, l);
			t = o;
			anbindung_get_orig(d, &o);
			anbindung_get_sichtbar(d, &t);
			geprueft++;
			if (o.von.seite != rang(in_d, n, l)) {
				abweichungen++;
				g_test_message("P3 get_orig z%d live %d: %d erwartet %d", run, l, o.von.seite,
						rang(in_d, n, l));
			}
			if (t.von.seite != rang(in_vis, n, l)) {
				abweichungen++;
				g_test_message("P4 sichtbar z%d live %d: %d erwartet %d", run, l, t.von.seite,
						rang(in_vis, n, l));
			}
		}

		g_free(in_d);
		g_free(in_s);
		g_free(in_vis);
		g_free(teil);
		dokument_frei(d);
	}

	g_test_message("Eigenschaften: %d Prüfungen, %d Abweichungen", geprueft, abweichungen);
	g_rand_free(r);
	g_assert_cmpint(geprueft, >, 0);
	g_assert_cmpint(abweichungen, ==, 0);
}

/* Eingefügte Blöcke: ein gespeicherter Teil T (Platte pv..pb) mit eigenen eingefügten Blöcken in
 * den Lücken pv..pb+1, dazu Karteileichen und neue Löschungen. Erwartung aus der Konstruktion:
 * Randblöcke gehören zu x, wenn x den Teil umfasst (dv <= pv und db >= pb). */
static void test_eingefuegte_bloecke(void) {
	GRand *r = g_rand_new_with_seed(11);
	gint geprueft = 0;
	gint abweichungen = 0;

	for (gint run = 0; run < 4000; run++) {
		gint nd = 6 + g_rand_int_range(r, 0, 12);
		gint pv = g_rand_int_range(r, 0, nd);
		gint pb = g_rand_int_range(r, pv, nd);
		gint nbl = g_rand_int_range(r, 0, 4);
		gint blk_count[40] = { 0 }; //eingefügte Seiten je Lücke 0..nd
		gboolean kl_vor[40] = { FALSE }; //Karteileiche in der Lücke
		gboolean kl_zuerst[40] = { FALSE };
		gboolean neu_geloescht[40] = { FALSE };
		gint live_d[40];  //Live-Position der Plattenseiten
		gint first_blk[40], last_blk[40];
		ZondPdfDocument *d = neues_dokument(0);
		GPtrArray *pages = zond_pdf_document_get_arr_pages(d);
		ZPDFDPart *teil = NULL;
		gint tv, tb;

		for (gint b = 0; b < nbl; b++) {
			gint g = pv + g_rand_int_range(r, 0, pb - pv + 2);

			if (!blk_count[g])
				blk_count[g] = 1 + g_rand_int_range(r, 0, 2);
		}
		for (gint g = 0; g <= nd; g++) {
			kl_vor[g] = g_rand_int_range(r, 0, 100) < 20;
			kl_zuerst[g] = g_rand_int_range(r, 0, 2);
			if (g < nd)
				neu_geloescht[g] = g_rand_int_range(r, 0, 100) < 15;
		}

		//Live-Array aufbauen
		for (gint g = 0; g <= nd; g++) {
			gint order = kl_zuerst[g];

			for (gint step = 0; step < 2; step++) {
				if ((step == 0) == (order == 1)) { //Karteileiche
					if (kl_vor[g]) {
						PdfDocumentPage *p = neue_seite(d);

						p->deleted = TRUE;
						p->on_disk_deleted = TRUE;
						g_ptr_array_add(pages, p);
					}
				}
				else { //eingefügter Block
					first_blk[g] = pages->len;
					for (gint k = 0; k < blk_count[g]; k++)
						g_ptr_array_add(pages, neue_seite(d)); //inserted unten
					last_blk[g] = pages->len - 1;
				}
			}
			if (g < nd) {
				PdfDocumentPage *p = neue_seite(d);

				p->deleted = neu_geloescht[g];
				live_d[g] = pages->len;
				g_ptr_array_add(pages, p);
			}
		}
		for (gint i = 0; i < (gint) pages->len; i++)
			((PdfDocumentPage*) g_ptr_array_index(pages, i))->page_akt = i;

		//Teil T: Grenzen einschließlich eigener Randblöcke
		tv = blk_count[pv] ? first_blk[pv] : live_d[pv];
		tb = blk_count[pb + 1] ? last_blk[pb + 1] : live_d[pb];
		teil = g_new0(ZPDFDPart, 1);
		teil->zond_pdf_document = d;
		teil->first_page = g_ptr_array_index(pages, tv);
		teil->first_index = 0;
		teil->last_page = g_ptr_array_index(pages, tb);
		teil->last_index = EOP;
		for (gint g = 0; g <= nd; g++)
			for (gint k = 0; k < blk_count[g]; k++)
				eingefuegt(g_ptr_array_index(pages, first_blk[g] + k), teil);

		for (gint k = 0; k < 6; k++) {
			gint dv = g_rand_int_range(r, 0, nd);
			gint db = g_rand_int_range(r, dv, nd);
			gboolean umfasst = dv <= pv && db >= pb;
			gint ev_live, eb_live;
			Anbindung x, live, gespeichert;
			gint ev, eb;
			g_autofree gboolean *in_s = g_new0(gboolean, pages->len);

			if (neu_geloescht[dv] || neu_geloescht[db])
				continue;

			ev_live = (umfasst && blk_count[dv]) ? first_blk[dv] : live_d[dv];
			eb_live = (umfasst && blk_count[db + 1]) ? last_blk[db + 1] : live_d[db];

			for (gint i = 0; i < (gint) pages->len; i++) {
				PdfDocumentPage *p = g_ptr_array_index(pages, i);
				gboolean in_t = i >= tv && i <= tb;

				in_s[i] = !p->on_disk_deleted && !(p->deleted && in_t) && (!p->inserted || in_t);
			}
			ev = rang(in_s, pages->len, ev_live);
			eb = rang(in_s, pages->len, eb_live);

			x = ganze_seiten(dv, db);
			live = x;
			anbindung_aktualisieren(d, &live);
			gespeichert = speichern_nachfuehren(teil, x);

			geprueft++;
			if (live.von.seite != ev_live || live.bis.seite != eb_live) {
				abweichungen++;
				g_test_message("Q1 z%d x=%d-%d T=%d-%d: live %d-%d erwartet %d-%d", run, dv, db, pv,
						pb, live.von.seite, live.bis.seite, ev_live, eb_live);
			}
			if (gespeichert.von.seite != ev || gespeichert.bis.seite != eb) {
				abweichungen++;
				g_test_message("Q2 z%d x=%d-%d T=%d-%d: gespeichert %d-%d erwartet %d-%d", run, dv,
						db, pv, pb, gespeichert.von.seite, gespeichert.bis.seite, ev, eb);
			}
		}

		g_free(teil);
		dokument_frei(d);
	}

	g_test_message("Eingefügte Blöcke: %d Prüfungen, %d Abweichungen", geprueft, abweichungen);
	g_rand_free(r);
	g_assert_cmpint(geprueft, >, 0);
	g_assert_cmpint(abweichungen, ==, 0);
}

void test_anbindung_zaehlung_register(void) {
	g_test_add_func("/anbindung_zaehlung/loeschen_speichern", test_loeschen_speichern);
	g_test_add_func("/anbindung_zaehlung/nach_dem_speichern", test_nach_dem_speichern);
	g_test_add_func("/anbindung_zaehlung/neustart", test_neustart);
	g_test_add_func("/anbindung_zaehlung/loeschung_ausserhalb", test_loeschung_ausserhalb);
	g_test_add_func("/anbindung_zaehlung/alte_und_neue_loeschung", test_alte_und_neue_loeschung);
	g_test_add_func("/anbindung_zaehlung/phantom_seite", test_phantom_seite);
	g_test_add_func("/anbindung_zaehlung/eingefuegt_randseite", test_eingefuegt_randseite);
	g_test_add_func("/anbindung_zaehlung/eigenschaften", test_eigenschaften);
	g_test_add_func("/anbindung_zaehlung/eingefuegte_bloecke", test_eingefuegte_bloecke);
}
