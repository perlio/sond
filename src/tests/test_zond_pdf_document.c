/*
 sond (test_zond_pdf_document.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests des Dokumentmodells des Viewers (zond/zond_pdf_document.c, zond_pdf_part.c): Öffnen eines
 * Dokuments über Abschnitte (zpdfd_part_peek), Lebensdauer von Dokument und Abschnitten, nur
 * teilweise initialisierte Seiten, Einfügen von Seiten, Zuordnung eingefügter Seiten zu ihrem
 * Abschnitt und die Signale der Abschnitte. Die PDF-Dateien legt der Test selbst an.
 */

#include <glib.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "../sond_fileparts.h"
#include "../znd/anbindung.h"
#include "../zond/zond_pdf_document.h"
#include "tvfm_test_support.h"

/* ---------------------------------------------------------------------------------------- */
/* Rahmen                                                                                    */

typedef struct {
	Fix *fix;
	SondFilePart *sfp; //seiten.pdf mit 6 Seiten
} Ctx;

static void gone_cb(gpointer data, GObject *object) {
	(void) object;
	*(gboolean*) data = TRUE;
}

static Ctx* ctx_new(void) {
	Ctx *c = g_new0(Ctx, 1);
	GError *error = NULL;

	c->fix = fix_new();
	schreibe_pdf_seiten(c->fix, "seiten.pdf", 6);
	c->sfp = sond_file_part_create(NULL, "seiten.pdf", &error);
	g_assert_no_error(error);
	g_assert_nonnull(c->sfp);
	g_assert_true(SOND_IS_FILE_PART_PDF(c->sfp));

	return c;
}

static void ctx_free(Ctx *c) {
	g_object_unref(c->sfp);
	fix_free(c->fix);
	g_free(c);
}

static ZPDFDPart* peek(Ctx *c, Anbindung *a) {
	GError *error = NULL;
	ZPDFDPart *part = zpdfd_part_peek(SOND_FILE_PART_PDF(c->sfp), a, &error);

	g_assert_no_error(error);
	g_assert_nonnull(part);

	return part;
}

static gint seiten_zahl(ZondPdfDocument *d) {
	return zond_pdf_document_get_number_of_pages(d);
}

//Pfad der Arbeitskopie des geöffneten Dokuments
static gchar* arbeitskopie(ZondPdfDocument *d) {
	pdf_document *pd = zond_pdf_document_get_pdf_doc(d);
	fz_context *ctx = zond_pdf_document_get_ctx(d);

	return g_strdup(fz_stream_filename(ctx, pd->file));
}

/* ---------------------------------------------------------------------------------------- */
/* Öffnen und Lebensdauer                                                                    */

static void test_ganz_oeffnen(void) {
	Ctx *c = ctx_new();
	ZPDFDPart *part = peek(c, NULL);
	ZondPdfDocument *d = part->zond_pdf_document;
	gboolean frei = FALSE;
	gchar *kopie = NULL;

	g_assert_cmpint(seiten_zahl(d), ==, 6);
	for (gint i = 0; i < 6; i++) {
		PdfDocumentPage *p = zond_pdf_document_get_pdf_document_page(d, i);

		g_assert_nonnull(p);
		g_assert_cmpint(p->page_akt, ==, i);
		g_assert_true(zpdfd_part_umfasst_seite(part, p));
	}
	g_assert_false(part->has_anbindung);
	g_assert_cmpint(part->first_page->page_akt, ==, 0);
	g_assert_cmpint(part->last_page->page_akt, ==, 5);
	g_assert_cmpint(part->first_index, ==, 0);
	g_assert_cmpint(part->last_index, ==, EOP);
	g_assert_true(zond_pdf_document_is_open(SOND_FILE_PART_PDF(c->sfp)) == d);
	g_assert_cmpuint(zond_pdf_document_get_arr_parts(d)->len, ==, 1);

	//das Dokument ist freigegeben, wenn der letzte Abschnitt freigegeben ist; die Arbeitskopie ist weg
	kopie = arbeitskopie(d);
	g_assert_true(g_file_test(kopie, G_FILE_TEST_EXISTS));
	g_object_weak_ref(G_OBJECT(d), gone_cb, &frei);
	zpdfd_part_drop(part);
	g_assert_true(frei);
	g_assert_false(g_file_test(kopie, G_FILE_TEST_EXISTS));
	g_assert_null(zond_pdf_document_is_open(SOND_FILE_PART_PDF(c->sfp)));
	g_free(kopie);

	ctx_free(c);
}

/* Dasselbe Dokument zweimal über denselben Abschnitt geöffnet: derselbe Abschnitt, das Dokument
 * wird nur einmal gehalten (der Abschnitt hält es) und ist nach beiden Freigaben frei. */
static void test_zweimal_dieselbe_ansicht(void) {
	Ctx *c = ctx_new();
	ZPDFDPart *p1 = peek(c, NULL);
	ZPDFDPart *p2 = peek(c, NULL);
	ZondPdfDocument *d = p1->zond_pdf_document;
	gboolean frei = FALSE;

	g_assert_true(p1 == p2);
	g_assert_cmpuint(G_OBJECT(p1)->ref_count, ==, 2);
	g_assert_cmpuint(G_OBJECT(d)->ref_count, ==, 1);
	g_object_weak_ref(G_OBJECT(d), gone_cb, &frei);

	zpdfd_part_drop(p2);
	g_assert_false(frei);
	zpdfd_part_drop(p1);
	g_assert_true(frei);

	ctx_free(c);
}

//Abschnitte desselben Dokuments: das Dokument lebt, solange einer lebt
static void test_mehrere_abschnitte(void) {
	Ctx *c = ctx_new();
	Anbindung a = { { 1, 0 }, { 3, EOP } };
	Anbindung zurueck = { { 0, 0 }, { 0, 0 } };
	ZPDFDPart *ganz = peek(c, NULL);
	ZPDFDPart *teil = peek(c, &a);
	ZPDFDPart *teil2 = peek(c, &a);
	ZondPdfDocument *d = ganz->zond_pdf_document;
	gboolean frei = FALSE;

	g_assert_true(teil != ganz);
	g_assert_true(teil == teil2); //gleiche Anbindung: derselbe Abschnitt
	g_assert_true(teil->zond_pdf_document == d);
	g_assert_true(teil->has_anbindung);
	g_assert_cmpint(teil->first_page->page_akt, ==, 1);
	g_assert_cmpint(teil->last_page->page_akt, ==, 3);
	zpdfd_part_get_anbindung(teil, &zurueck);
	g_assert_true(anbindung_1_gleich_2(a, zurueck));
	g_assert_cmpuint(zond_pdf_document_get_arr_parts(d)->len, ==, 2);
	g_assert_cmpuint(G_OBJECT(d)->ref_count, ==, 2); //eine Referenz je Abschnitt

	g_object_weak_ref(G_OBJECT(d), gone_cb, &frei);
	zpdfd_part_drop(teil2);
	zpdfd_part_drop(teil);
	g_assert_cmpuint(zond_pdf_document_get_arr_parts(d)->len, ==, 1);
	g_assert_false(frei);
	zpdfd_part_drop(ganz);
	g_assert_true(frei);

	ctx_free(c);
}

//Wird ein Abschnitt zuerst geöffnet, sind nur seine Seiten initialisiert
static void test_nur_abschnitt_initialisiert(void) {
	Ctx *c = ctx_new();
	Anbindung a = { { 1, 0 }, { 2, EOP } };
	ZPDFDPart *teil = peek(c, &a);
	ZondPdfDocument *d = teil->zond_pdf_document;

	g_assert_cmpint(seiten_zahl(d), ==, 6);
	g_assert_null(zond_pdf_document_get_pdf_document_page(d, 0));
	g_assert_nonnull(zond_pdf_document_get_pdf_document_page(d, 1));
	g_assert_nonnull(zond_pdf_document_get_pdf_document_page(d, 2));
	g_assert_null(zond_pdf_document_get_pdf_document_page(d, 3));
	g_assert_null(zond_pdf_document_get_pdf_document_page(d, 5));

	//ein weiterer Abschnitt holt die übrigen Seiten nach, mit der Nummer, die sie jetzt haben
	{
		Anbindung b = { { 4, 0 }, { 5, EOP } };
		ZPDFDPart *teil_b = peek(c, &b);

		g_assert_cmpint(zond_pdf_document_get_pdf_document_page(d, 4)->page_akt, ==, 4);
		g_assert_cmpint(zond_pdf_document_get_pdf_document_page(d, 5)->page_akt, ==, 5);
		g_assert_null(zond_pdf_document_get_pdf_document_page(d, 3));
		zpdfd_part_drop(teil_b);
	}

	zpdfd_part_drop(teil);
	ctx_free(c);
}

static void test_oeffnen_fehler(void) {
	Ctx *c = ctx_new();
	SondFilePart *sfp = NULL;
	GError *error = NULL;
	ZPDFDPart *part = NULL;

	schreibe(c->fix, "kaputt.pdf", "%PDF-1.4\nkeine Struktur\n");
	//schon beim Anlegen des SondFilePart wird die Datei geprüft und eine Warnung ausgegeben
	g_test_expect_message("unknown", G_LOG_LEVEL_WARNING, "*konnte nicht geöffnet werden*");
	sfp = sond_file_part_create(NULL, "kaputt.pdf", &error);
	g_test_assert_expected_messages();
	if (!sfp || !SOND_IS_FILE_PART_PDF(sfp)) {
		g_clear_error(&error);
		if (sfp)
			g_object_unref(sfp);
		ctx_free(c);
		g_test_skip("die beschädigte Datei wird nicht als PDF erkannt");

		return;
	}

	//eine beschädigte Datei: Fehler mit Meldung, kein Absturz, nichts bleibt offen
	part = zpdfd_part_peek(SOND_FILE_PART_PDF(sfp), NULL, &error);
	g_assert_null(part);
	g_assert_nonnull(error);
	g_clear_error(&error);
	g_assert_null(zond_pdf_document_is_open(SOND_FILE_PART_PDF(sfp)));

	g_object_unref(sfp);
	ctx_free(c);
}

/* ---------------------------------------------------------------------------------------- */
/* Einfügen                                                                                  */

static pdf_document* quelle_oeffnen(Ctx *c, fz_context *ctx, gint n) {
	SondFilePart *sfp_q = NULL;
	pdf_document *doc = NULL;
	GError *error = NULL;

	schreibe_pdf_seiten(c->fix, "quelle.pdf", n);
	sfp_q = sond_file_part_create(NULL, "quelle.pdf", &error);
	g_assert_no_error(error);
	doc = sond_file_part_pdf_open_document(ctx, SOND_FILE_PART_PDF(sfp_q), FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(doc);
	g_object_unref(sfp_q);

	return doc;
}

/* Einfügen in ein Dokument, das nur über einen Abschnitt geöffnet ist: dahinter liegende Seiten
 * sind noch nicht initialisiert (NULL). Die übrigen Seiten behalten ihre Nummer, die
 * eingefügten sind dem Abschnitt zugeordnet. */
static void test_einfuegen_in_abschnitt(void) {
	Ctx *c = ctx_new();
	Anbindung a = { { 1, 0 }, { 2, EOP } };
	ZPDFDPart *teil = peek(c, &a);
	ZondPdfDocument *d = teil->zond_pdf_document;
	fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	pdf_document *quelle = quelle_oeffnen(c, ctx, 2);
	GError *error = NULL;

	g_assert_cmpint(zond_pdf_document_insert_pages(d, 2, teil, quelle, &error), ==, 0);
	g_assert_no_error(error);
	pdf_drop_document(ctx, quelle);
	fz_drop_context(ctx);

	g_assert_cmpint(seiten_zahl(d), ==, 8);

	//eingefügt: Seiten 2 und 3, dem Abschnitt zugeordnet
	for (gint i = 2; i <= 3; i++) {
		PdfDocumentPage *p = zond_pdf_document_get_pdf_document_page(d, i);

		g_assert_nonnull(p);
		g_assert_cmpint(p->page_akt, ==, i);
		g_assert_true(p->inserted);
		g_assert_true(p->inserted_in == teil);
	}

	//die frühere Seite 2 ist jetzt Seite 4
	g_assert_cmpint(zond_pdf_document_get_pdf_document_page(d, 1)->page_akt, ==, 1);
	g_assert_false(zond_pdf_document_get_pdf_document_page(d, 1)->inserted);

	//nicht initialisierte bleiben NULL und bekommen beim Nachladen die Nummer ihrer Stelle
	g_assert_null(zond_pdf_document_get_pdf_document_page(d, 0));
	g_assert_null(zond_pdf_document_get_pdf_document_page(d, 5));
	{
		Anbindung b = { { 5, 0 }, { 7, EOP } };
		ZPDFDPart *teil_b = peek(c, &b);

		for (gint i = 5; i <= 7; i++)
			g_assert_cmpint(zond_pdf_document_get_pdf_document_page(d, i)->page_akt, ==, i);
		zpdfd_part_drop(teil_b);
	}

	zpdfd_part_drop(teil);
	ctx_free(c);
}

/* Der Abschnitt, in den eingefügt wurde, wird freigegeben, das Dokument lebt weiter: die
 * eingefügten Seiten bleiben "eingefügt", zeigen aber nicht mehr auf den freigegebenen Abschnitt. Mit
 * dem letzten Abschnitt ist das Dokument frei (kein Zyklus über das Journal). */
static void test_eingefuegt_nach_freigabe(void) {
	Ctx *c = ctx_new();
	Anbindung a = { { 1, 0 }, { 2, EOP } };
	ZPDFDPart *teil = peek(c, &a);
	ZPDFDPart *ganz = peek(c, NULL);
	ZondPdfDocument *d = teil->zond_pdf_document;
	fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	pdf_document *quelle = quelle_oeffnen(c, ctx, 2);
	GError *error = NULL;
	gboolean frei = FALSE;
	JournalEntry entry = { 0 };

	g_assert_cmpint(zond_pdf_document_insert_pages(d, 2, teil, quelle, &error), ==, 0);
	pdf_drop_document(ctx, quelle);
	fz_drop_context(ctx);

	//wie im Viewer: ein Journal-Eintrag zur Einfügung (hält keinen Abschnitt fest)
	entry.type = JOURNAL_TYPE_PAGES_INSERTED;
	entry.pdf_document_page = zond_pdf_document_get_pdf_document_page(d, 2);
	entry.pages_inserted.count = 2;
	g_array_append_val(zond_pdf_document_get_arr_journal(d), entry);

	g_assert_true(zond_pdf_document_get_pdf_document_page(d, 2)->inserted_in == teil);

	g_object_weak_ref(G_OBJECT(d), gone_cb, &frei);
	zpdfd_part_drop(teil);
	g_assert_false(frei); //der ganze Abschnitt hält das Dokument
	for (gint i = 2; i <= 3; i++) {
		PdfDocumentPage *p = zond_pdf_document_get_pdf_document_page(d, i);

		g_assert_true(p->inserted);
		g_assert_null(p->inserted_in);
	}

	zpdfd_part_drop(ganz);
	g_assert_true(frei); //auch mit dem Journal-Eintrag

	ctx_free(c);
}

/* ---------------------------------------------------------------------------------------- */
/* Signale                                                                                   */

typedef struct {
	gint n;
	gint art;
	gpointer daten;
	gpointer seite;
} Seen;

static void page_cb(ZPDFDPart *part, gpointer page, gint art, gpointer daten, gpointer user) {
	Seen *s = user;

	(void) part;
	s->n++;
	s->art = art;
	s->daten = daten;
	s->seite = page;
}

static void annot_cb(ZPDFDPart *part, gpointer page, gpointer annot, gpointer user) {
	Seen *s = user;

	(void) part;
	s->n++;
	s->seite = page;
	s->daten = annot;
}

static void dirty_cb(ZPDFDPart *part, gpointer user) {
	(void) part;
	((Seen*) user)->n++;
}

static void test_signale_seiten(void) {
	Ctx *c = ctx_new();
	Anbindung a = { { 0, 0 }, { 2, EOP } };
	Anbindung b = { { 2, 0 }, { 5, EOP } };
	ZPDFDPart *pa = peek(c, &a);
	ZPDFDPart *pb = peek(c, &b);
	ZondPdfDocument *d = pa->zond_pdf_document;
	Seen sa = { 0 }, sb = { 0 };

	g_signal_connect(pa, "page-changed", G_CALLBACK(page_cb), &sa);
	g_signal_connect(pb, "page-changed", G_CALLBACK(page_cb), &sb);

	//Seite 1 liegt nur in a, Seite 2 in beiden (gemeinsame Randseite), Seite 4 nur in b
	zond_pdf_document_notify_page_changed(d, zond_pdf_document_get_pdf_document_page(d, 1),
			ZPDFD_PART_CHANGE_ROTATED, GINT_TO_POINTER(90));
	g_assert_cmpint(sa.n, ==, 1);
	g_assert_cmpint(sb.n, ==, 0);
	g_assert_cmpint(sa.art, ==, ZPDFD_PART_CHANGE_ROTATED);
	g_assert_cmpint(GPOINTER_TO_INT(sa.daten), ==, 90);

	zond_pdf_document_notify_page_changed(d, zond_pdf_document_get_pdf_document_page(d, 2),
			ZPDFD_PART_CHANGE_DELETED, NULL);
	g_assert_cmpint(sa.n, ==, 2);
	g_assert_cmpint(sb.n, ==, 1);
	g_assert_cmpint(sb.art, ==, ZPDFD_PART_CHANGE_DELETED);

	zond_pdf_document_notify_page_changed(d, zond_pdf_document_get_pdf_document_page(d, 4),
			ZPDFD_PART_CHANGE_OCR, NULL);
	g_assert_cmpint(sa.n, ==, 2);
	g_assert_cmpint(sb.n, ==, 2);

	zpdfd_part_drop(pa);
	zpdfd_part_drop(pb);
	ctx_free(c);
}

static void test_signale_annot_und_dirty(void) {
	Ctx *c = ctx_new();
	ZPDFDPart *part = peek(c, NULL);
	ZondPdfDocument *d = part->zond_pdf_document;
	Seen sa = { 0 }, sd = { 0 };
	Annot annot = { 0 };

	//ein Abschnitt wird angezeigt, wenn jemand "annot-changed" abonniert hat
	g_assert_false(zpdfd_part_wird_angezeigt(part));
	g_signal_connect(part, "annot-changed", G_CALLBACK(annot_cb), &sa);
	g_signal_connect(part, "dirty-changed", G_CALLBACK(dirty_cb), &sd);
	g_assert_true(zpdfd_part_wird_angezeigt(part));

	zond_pdf_document_notify_annot_changed(d, zond_pdf_document_get_pdf_document_page(d, 3), &annot);
	g_assert_cmpint(sa.n, ==, 1);
	g_assert_true(sa.daten == &annot);
	g_assert_true(sa.seite == zond_pdf_document_get_pdf_document_page(d, 3));

	//dirty-changed nur bei einer Änderung
	g_assert_false(part->dirty);
	zpdfd_part_set_dirty(part, TRUE);
	g_assert_true(part->dirty);
	g_assert_cmpint(sd.n, ==, 1);
	zpdfd_part_set_dirty(part, TRUE);
	g_assert_cmpint(sd.n, ==, 1);
	zpdfd_part_set_dirty(part, FALSE);
	g_assert_cmpint(sd.n, ==, 2);

	zpdfd_part_drop(part);
	ctx_free(c);
}

void test_zond_pdf_document_register(void) {
	g_test_add_func("/zond_pdf_document/ganz_oeffnen", test_ganz_oeffnen);
	g_test_add_func("/zond_pdf_document/zweimal_dieselbe_ansicht", test_zweimal_dieselbe_ansicht);
	g_test_add_func("/zond_pdf_document/mehrere_abschnitte", test_mehrere_abschnitte);
	g_test_add_func("/zond_pdf_document/nur_abschnitt_initialisiert", test_nur_abschnitt_initialisiert);
	g_test_add_func("/zond_pdf_document/oeffnen_fehler", test_oeffnen_fehler);
	g_test_add_func("/zond_pdf_document/einfuegen_in_abschnitt", test_einfuegen_in_abschnitt);
	g_test_add_func("/zond_pdf_document/eingefuegt_nach_freigabe", test_eingefuegt_nach_freigabe);
	g_test_add_func("/zond_pdf_document/signale_seiten", test_signale_seiten);
	g_test_add_func("/zond_pdf_document/signale_annot_und_dirty", test_signale_annot_und_dirty);
}
