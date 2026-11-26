/*
 zond (zond_pdf_part.c) - Akten, Beweisstücke, Unterlagen
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

/* Abschnitt (ZPDFDPart) eines geöffneten Dokuments: der Bereich, den ein Viewer oder eine
 * Anbindung zeigt, mit den Signalen, über die die Viewer Änderungen am Dokument erfahren. */

#include "zond_pdf_document.h"

#include "../sond_fileparts.h"
#include "../sond_log_and_error.h"

#include "../znd/anbindung.h"

/**
 * ZPDF_PART ist das Objekt, das Zugriff auf zond_pdf_document hat
 */
enum {
	PART_PAGE_CHANGED,
	PART_ANNOT_CHANGED,
	PART_DIRTY_CHANGED,
	PART_N_SIGNALS
};

static guint part_signals[PART_N_SIGNALS] = { 0 };

G_DEFINE_TYPE(ZPDFDPart, zond_pdf_part, G_TYPE_OBJECT)

static void zond_pdf_part_finalize(GObject *self) {
	ZPDFDPart *zpdfd_part = (ZPDFDPart*) self;

	if (zpdfd_part->zond_pdf_document) {
		GPtrArray *arr_pages = zond_pdf_document_get_arr_pages(zpdfd_part->zond_pdf_document);

		//eingefügte Seiten verweisen nicht mehr auf den Abschnitt (kein Zeiger auf freigegebenen Speicher)
		for (guint i = 0; i < arr_pages->len; i++) {
			PdfDocumentPage *pdf_document_page = g_ptr_array_index(arr_pages, i);

			if (pdf_document_page && pdf_document_page->inserted_in == zpdfd_part)
				pdf_document_page->inserted_in = NULL;
		}

		if (!g_ptr_array_remove_fast(zond_pdf_document_get_arr_parts(zpdfd_part->zond_pdf_document),
				zpdfd_part))
			LOG_WARN("zpdfd_part nicht in Array vorhanden");
		g_object_unref(zpdfd_part->zond_pdf_document);
	}

	G_OBJECT_CLASS(zond_pdf_part_parent_class)->finalize(self);

	return;
}

static void zond_pdf_part_class_init(ZPDFDPartClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = zond_pdf_part_finalize;

	part_signals[PART_PAGE_CHANGED] = g_signal_new("page-changed",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 3, G_TYPE_POINTER, G_TYPE_INT, G_TYPE_POINTER);
	part_signals[PART_ANNOT_CHANGED] = g_signal_new("annot-changed",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);
	part_signals[PART_DIRTY_CHANGED] = g_signal_new("dirty-changed",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 0);

	return;
}

static void zond_pdf_part_init(ZPDFDPart *self) {
	return;
}

void zpdfd_part_drop(ZPDFDPart* zpdfd_part) {
	g_object_unref(zpdfd_part);

	return;
}

ZPDFDPart* zpdfd_part_ref(ZPDFDPart* zpdfd_part) {
	return g_object_ref(zpdfd_part);
}

gboolean zpdfd_part_umfasst_seite(ZPDFDPart *zpdfd_part, PdfDocumentPage *pdf_document_page) {
	return pdf_document_page->page_akt >= zpdfd_part->first_page->page_akt
			&& pdf_document_page->page_akt <= zpdfd_part->last_page->page_akt;
}

gboolean zpdfd_part_wird_angezeigt(ZPDFDPart *zpdfd_part) {
	return g_signal_has_handler_pending(zpdfd_part, part_signals[PART_ANNOT_CHANGED], 0, TRUE);
}

void zpdfd_part_set_dirty(ZPDFDPart *zpdfd_part, gboolean dirty) {
	if (zpdfd_part->dirty == dirty)
		return;

	zpdfd_part->dirty = dirty;
	g_signal_emit(zpdfd_part, part_signals[PART_DIRTY_CHANGED], 0);

	return;
}

/* Sendet an alle Abschnitte, die die Seite umfassen. Über eine Kopie mit eigenen Referenzen, damit
 * ein Handler Abschnitte freigeben darf. */
static GPtrArray* zond_pdf_document_parts_der_seite(ZondPdfDocument *zond_pdf_document,
		PdfDocumentPage *pdf_document_page) {
	GPtrArray *arr_parts = zond_pdf_document_get_arr_parts(zond_pdf_document);
	GPtrArray *arr = g_ptr_array_new_with_free_func(g_object_unref);

	for (guint i = 0; i < arr_parts->len; i++) {
		ZPDFDPart *zpdfd_part = g_ptr_array_index(arr_parts, i);

		if (zpdfd_part_umfasst_seite(zpdfd_part, pdf_document_page))
			g_ptr_array_add(arr, g_object_ref(zpdfd_part));
	}

	return arr;
}

void zond_pdf_document_notify_page_changed(ZondPdfDocument *zond_pdf_document,
		PdfDocumentPage *pdf_document_page, ZPDFDPartChange art, gpointer data) {
	GPtrArray *arr = zond_pdf_document_parts_der_seite(zond_pdf_document, pdf_document_page);

	for (guint i = 0; i < arr->len; i++)
		g_signal_emit(g_ptr_array_index(arr, i), part_signals[PART_PAGE_CHANGED], 0,
				pdf_document_page, (gint) art, data);
	g_ptr_array_unref(arr);

	return;
}

void zond_pdf_document_notify_annot_changed(ZondPdfDocument *zond_pdf_document,
		PdfDocumentPage *pdf_document_page, Annot *annot) {
	GPtrArray *arr = zond_pdf_document_parts_der_seite(zond_pdf_document, pdf_document_page);

	for (guint i = 0; i < arr->len; i++)
		g_signal_emit(g_ptr_array_index(arr, i), part_signals[PART_ANNOT_CHANGED], 0,
				pdf_document_page, annot);
	g_ptr_array_unref(arr);

	return;
}

void zpdfd_part_get_anbindung(ZPDFDPart* zpdfd_part, Anbindung* anbindung) {
	anbindung->von.seite = zpdfd_part->first_page->page_akt;
	anbindung->von.index = zpdfd_part->first_index;
	anbindung->bis.seite = zpdfd_part->last_page->page_akt;
	anbindung->bis.index = zpdfd_part->last_index;

	return;
}

ZPDFDPart* zpdfd_part_peek(SondFilePartPDF* sfp_pdf, Anbindung* anbindung,
		GError** error) {
	ZondPdfDocument* zpdfd = NULL;
	GPtrArray* arr_parts = NULL;
	GPtrArray* arr_pages = NULL;
	ZPDFDPart* zpdfd_part = NULL;

	if (anbindung && anbindung_is_empty(anbindung))
		anbindung = NULL;

	zpdfd = zond_pdf_document_open(sfp_pdf,
			(anbindung) ? anbindung->von.seite : 0,
					(anbindung) ? anbindung->bis.seite : -1,
							error);
	if (!zpdfd)
		return NULL;

	arr_parts = zond_pdf_document_get_arr_parts(zpdfd);
	arr_pages = zond_pdf_document_get_arr_pages(zpdfd);

	//in Array von Parts suchen, ob schon vorhanden
	for (guint i = 0; i < arr_parts->len; i++) {
		ZPDFDPart* zpdfd_part_loop = NULL;

		zpdfd_part_loop = g_ptr_array_index(arr_parts, i);

		if (!anbindung) {
			if (!zpdfd_part_loop->has_anbindung) {
				g_object_unref(zpdfd); //der Abschnitt hält das Dokument schon
				return zpdfd_part_ref(zpdfd_part_loop);
			}
		}
		else {
			Anbindung anbindung_part = { 0 };

			zpdfd_part_get_anbindung(zpdfd_part_loop, &anbindung_part);

			if (anbindung_1_gleich_2(*anbindung, anbindung_part)) {
				g_object_unref(zpdfd);
				return zpdfd_part_ref(zpdfd_part_loop);
			}
		}
	}

	//nicht gefunden, dann neu
	zpdfd_part = g_object_new(ZOND_TYPE_PDF_PART, NULL);

	zpdfd_part->zond_pdf_document = zpdfd;

	zpdfd_part->first_page = g_ptr_array_index(arr_pages,
			(anbindung) ? anbindung->von.seite : 0);
	zpdfd_part->first_index = (anbindung) ? anbindung->von.index : 0;
	zpdfd_part->last_page = g_ptr_array_index(arr_pages,
			(anbindung) ? anbindung->bis.seite : arr_pages->len - 1);
	zpdfd_part->last_index = (anbindung) ? anbindung->bis.index : EOP;

	zpdfd_part->has_anbindung = (anbindung) ? TRUE : FALSE;

	g_ptr_array_add(arr_parts, zpdfd_part);

	return zpdfd_part;
}

