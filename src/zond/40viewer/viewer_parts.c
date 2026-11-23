/*
 zond (viewer_parts.c) - Akten, Beweisstücke, Unterlagen
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

/* Der Viewer abonniert die Signale der Abschnitte (ZPDFDPart) seiner angezeigten Dokumente:
 * dirty-changed (Speichern-Knopf), annot-changed und page-changed (Seiten drehen, löschen,
 * einfügen, OCR). Die Handler führen die Änderung in diesem Viewer nach; was mit der einzelnen
 * Seite geschieht, steht in seiten.c. */

#include "viewer.h"

#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "../zond_pdf_document.h"

#include "document.h"
#include "pdf_annot_ops.h"
#include "seiten.h"
#include "viewer_parts.h"
#include "viewer_render.h"
#include "viewer_search.h"

/* Layout neu aufbauen, wenn es als geändert gemerkt ist, und die sichtbaren Seiten neu rendern
 * lassen: auch ohne Neuaufbau (Drehen um 180 Grad, OCR) sind die Bilder der Seiten geleert. */
static void seiten_refresh_layout(PdfViewer *pv) {
	if (g_object_get_data(G_OBJECT(pv->layout), "dirty")) {
		viewer_refresh_layout(pv, 0);
		g_object_set_data(G_OBJECT(pv->layout), "dirty", NULL);
	}

	g_signal_emit_by_name(pv->v_adj, "value-changed", NULL);

	return;
}

static gboolean cb_seiten_layout_idle(gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;

	pv->idle_layout = 0;
	if (pv->dd) //Datei im eigenständigen Viewer kann inzwischen geschlossen sein
		seiten_refresh_layout(pv);

	return G_SOURCE_REMOVE;
}

//Neuaufbau des Layouts und Neurendern einmalig im Idle einplanen, nach allen Handlern
static void seiten_layout_planen(PdfViewer *pv) {
	if (!pv->idle_layout)
		pv->idle_layout = g_idle_add(cb_seiten_layout_idle, pv);

	return;
}


static gint annot_seite_neu_darstellen(PdfViewer *pv, ViewerPageNew* viewer_page,
		gint page_pv, gpointer data) {
	//Falls erste oder letzte Seite dd: prüfen, ob nicht weggecropt
	if ((viewer_page->pdf_document_page == viewer_page->dd->zpdfd_part->first_page ||
			viewer_page->pdf_document_page == viewer_page->dd->zpdfd_part->last_page) &&
			!pdf_annot_ops_is_in_rect((Annot*) data, viewer_page->crop))
		return 0;

	if (viewer_page->thread & 2) {
		gtk_image_clear(GTK_IMAGE(viewer_page->image_page));
		viewer_page->pixbuf_page = NULL;
	}

	if (viewer_page->thread & 4) {
		GtkTreeIter iter = { 0 };

		viewer_get_iter_thumb(viewer_page->pdfv, page_pv, &iter);

		//thumb löschen
		gtk_list_store_set(GTK_LIST_STORE(gtk_tree_view_get_model(
				GTK_TREE_VIEW(pv->tree_thumb) )), &iter, 0, NULL, -1);
		viewer_page->pixbuf_thumb = NULL;
	}

	viewer_page->thread = 0;

	g_signal_emit_by_name(pv->v_adj, "value-changed", NULL);

	return 1;
}

//Handler von "annot-changed" der Abschnitte des Viewers: Seite des Abschnitts neu darstellen lassen
static void cb_viewer_part_annot_changed(ZPDFDPart *zpdfd_part, gpointer pdf_document_page,
		gpointer annot, gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;

	for (gint i = 0; i < pv->arr_pages->len; i++) {
		ViewerPageNew *viewer_page = g_ptr_array_index(pv->arr_pages, i);

		if (viewer_page->pdf_document_page == pdf_document_page
				&& viewer_page->dd->zpdfd_part == zpdfd_part) {
			if (annot_seite_neu_darstellen(pv, viewer_page, i, annot)) //sichtbar geändert
				zpdfd_part_set_dirty(zpdfd_part, TRUE);

			break;
		}
	}

	return;
}

//Seite aus dem Viewer nehmen (Seitenfläche, Vorschau, Suche); das Layout wird gemerkt, nicht neu aufgebaut
static void seiten_viewer_seite_entfernen(PdfViewer *pv, ViewerPageNew *viewer_page, gint page_pv) {
	GtkTreeIter iter = { 0 };

	//Markierungen und Textsuche zurücksetzen; die Seitennummern verschieben sich
	pv->highlight.page[0] = -1;
	pv->text_occ.index_act = -1;
	pv->text_occ.page_act = -1;
	viewer_text_search_cancel(pv);
	gtk_label_set_text(GTK_LABEL(pv->label_search), "");

	viewer_close_thread_pool_and_transfer(pv);

	if (viewer_page->image_page)
		gtk_widget_destroy(viewer_page->image_page);

	g_ptr_array_remove_index(pv->arr_pages, page_pv); //viewer_page wird g_freed!

	viewer_get_iter_thumb(pv, page_pv, &iter);
	gtk_list_store_remove(GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(pv->tree_thumb))),
			&iter);

	g_object_set_data(G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1));

	return;
}

static gboolean cb_seiten_schliessen_idle(gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;

	pv->idle_schliessen = 0;
	if (pv->arr_pages->len == 0)
		viewer_schliessen(pv);

	return G_SOURCE_REMOVE;
}

/* Handler von "page-changed" der Abschnitte des Viewers: sucht die Viewer-Seite des Abschnitts und
 * führt die Änderung nach. Bei INSERTED ist daten ein DataInsert, die Seite die Bezugsseite. */
static void cb_viewer_part_page_changed(ZPDFDPart *zpdfd_part, gpointer pdf_document_page, gint art,
		gpointer daten, gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;
	ViewerPageNew *viewer_page = NULL;
	gint page_pv = 0;

	for (gint i = 0; i < pv->arr_pages->len; i++) {
		ViewerPageNew *viewer_page_loop = g_ptr_array_index(pv->arr_pages, i);

		if (viewer_page_loop->pdf_document_page == pdf_document_page
				&& viewer_page_loop->dd->zpdfd_part == zpdfd_part) {
			viewer_page = viewer_page_loop;
			page_pv = i;

			break;
		}
	}

	if (!viewer_page)
		return;

	switch (art) {
	case ZPDFD_PART_CHANGE_ROTATED:
		seiten_viewer_drehen(pv, viewer_page, page_pv, daten);
		zpdfd_part_set_dirty(zpdfd_part, TRUE);
		break;
	case ZPDFD_PART_CHANGE_OCR:
		zpdfd_part_set_dirty(zpdfd_part, TRUE);
		break;
	case ZPDFD_PART_CHANGE_DELETED:
		seiten_viewer_seite_entfernen(pv, viewer_page, page_pv);
		zpdfd_part_set_dirty(zpdfd_part, TRUE);
		//ein Viewer ohne Seiten ist sinnlos; im Signal selbst nicht schließen, daher im Idle
		if (pv->arr_pages->len == 0 && !pv->idle_schliessen)
			pv->idle_schliessen = g_idle_add(cb_seiten_schliessen_idle, pv);
		break;
	case ZPDFD_PART_CHANGE_INSERTED:
		if (seiten_viewer_einfuegen(pv, viewer_page, page_pv, daten))
			zpdfd_part_set_dirty(zpdfd_part, TRUE);
		break;
	default:
		break;
	}

	seiten_layout_planen(pv);

	return;
}

//Speichern-Knopf ist aktiv, wenn ein angezeigter Abschnitt geändert ist
void viewer_speichern_aktualisieren(PdfViewer *pv) {
	gtk_widget_set_sensitive(pv->button_speichern, document_has_dirty(pv));

	return;
}

static void cb_viewer_part_dirty_changed(ZPDFDPart *zpdfd_part, gpointer data) {
	viewer_speichern_aktualisieren((PdfViewer*) data);

	return;
}

//Abschnitte des Viewers abonnieren
void viewer_dd_signale_verbinden(PdfViewer *pv) {
	for (DisplayedDocument *dd = pv->dd; dd; dd = dd->next) {
		g_signal_connect(dd->zpdfd_part, "dirty-changed",
				G_CALLBACK(cb_viewer_part_dirty_changed), pv);
		g_signal_connect(dd->zpdfd_part, "annot-changed",
				G_CALLBACK(cb_viewer_part_annot_changed), pv);
		g_signal_connect(dd->zpdfd_part, "page-changed",
				G_CALLBACK(cb_viewer_part_page_changed), pv);
	}

	return;
}

void viewer_part_praesentieren(PdfViewerSession *session, ZPDFDPart *zpdfd_part) {
	for (guint i = 0; i < session->arr_pv->len; i++) {
		PdfViewer *pv = g_ptr_array_index(session->arr_pv, i);

		for (DisplayedDocument *dd = pv->dd; dd; dd = dd->next)
			if (dd->zpdfd_part == zpdfd_part) {
				gtk_window_present(GTK_WINDOW(pv->vf));

				return;
			}
	}

	return;
}

//vor dem Freigeben der DisplayedDocuments aufrufen
void viewer_dd_signale_trennen(PdfViewer *pv) {
	if (pv->idle_layout) {
		g_source_remove(pv->idle_layout);
		pv->idle_layout = 0;
	}
	if (pv->idle_schliessen) {
		g_source_remove(pv->idle_schliessen);
		pv->idle_schliessen = 0;
	}

	for (DisplayedDocument *dd = pv->dd; dd; dd = dd->next)
		g_signal_handlers_disconnect_by_data(dd->zpdfd_part, pv);

	return;
}
