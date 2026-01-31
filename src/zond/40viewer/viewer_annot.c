/*
 zond (viewer_annot.c) - Akten, Beweisstücke, Unterlagen
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

#include "../../misc.h"

#include "../99conv/pdf.h"

#include "viewer.h"
#include "viewer_render.h"
#include "document.h"


gboolean viewer_annot_is_in_rect(Annot* annot, fz_rect rect) {
	if (annot->type == PDF_ANNOT_TEXT) {
		fz_rect annot_rect = annot->annot_text.rect;

		if (fz_is_valid_rect(fz_intersect_rect(annot_rect, rect)))
			return TRUE;
	}
	else if (annot->type == PDF_ANNOT_HIGHLIGHT
			|| annot->type == PDF_ANNOT_UNDERLINE) {
		for (gint i = 0; i < annot->annot_text_markup.arr_quads->len; i++) {
			fz_quad quad = { 0 };

			quad = g_array_index(annot->annot_text_markup.arr_quads, fz_quad, i);
			if (fz_is_point_inside_rect(quad.ul, rect) ||
					fz_is_point_inside_rect(quad.lr, rect))
				return TRUE;
		}
	}

	return FALSE;
}

static gint viewer_annot_foreach_changed(PdfViewer *pv, ViewerPageNew* viewer_page,
		gint page_pv, gpointer data) {
	//Falls erste oder letzte Seite dd: prüfen, ob nicht weggecropt
	if ((viewer_page->pdf_document_page == viewer_page->dd->zpdfd_part->first_page ||
			viewer_page->pdf_document_page == viewer_page->dd->zpdfd_part->last_page) &&
			!viewer_annot_is_in_rect((Annot*) data, viewer_page->crop))
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

static gint viewer_annot_delete(PdfDocumentPageAnnot *pdf_document_page_annot,
		GError** error) {
	fz_context *ctx = NULL;
	pdf_annot *pdf_annot = NULL;
	GArray* arr_journal = NULL;
	JournalEntry entry = { 0, };

	ctx = zond_pdf_document_get_ctx(pdf_document_page_annot->pdf_document_page->document);

	zond_pdf_document_mutex_lock(pdf_document_page_annot->pdf_document_page->document);
	pdf_annot = pdf_document_page_annot_get_pdf_annot(pdf_document_page_annot);
	if (!pdf_annot) {
		zond_pdf_document_mutex_unlock(pdf_document_page_annot->pdf_document_page->document);
		*error = g_error_new(ZOND_ERROR, 0, "%s\nAnnotation nicht gefunden", __func__);
		return -1;
	}

	fz_try(ctx) {
		gint flags = 0;

		flags = pdf_annot_flags(ctx, pdf_annot);
		pdf_set_annot_flags(ctx, pdf_annot, flags | 2);
	}
	fz_always(ctx)
		zond_pdf_document_mutex_unlock(pdf_document_page_annot->pdf_document_page->document);
	fz_catch(ctx) {
		if (error) *error = g_error_new( g_quark_from_static_string("mupdf"),
				fz_caught(ctx), "%s\n%s", __func__, fz_caught_message(ctx));

		return -1;
	}

	pdf_document_page_annot->deleted = TRUE;

	//Entry fettig machen
	entry.pdf_document_page = pdf_document_page_annot->pdf_document_page;
	entry.type = JOURNAL_TYPE_ANNOT_DELETED;
	entry.annot_changed.pdf_document_page_annot = pdf_document_page_annot;
	entry.annot_changed.annot_before = annot_deep_copy(pdf_document_page_annot->annot);

	arr_journal = zond_pdf_document_get_arr_journal(
			pdf_document_page_annot->pdf_document_page->document);
	g_array_append_val(arr_journal, entry);

	return 0;
}

gint viewer_annot_handle_delete(PdfViewer* pv, GError** error) {
	gint rc = 0;

	ViewerPageNew *viewer_page = g_ptr_array_index(pv->arr_pages,
			pv->click_pdf_punkt.seite);

	viewer_render_wait_for_transfer(viewer_page->pdf_document_page);

	gtk_popover_popdown(GTK_POPOVER(pv->annot_pop_edit));

	rc = viewer_annot_delete(pv->clicked_annot, error);
	if (rc)
		ERROR_Z

	fz_drop_display_list(
			zond_pdf_document_get_ctx(
					viewer_page->pdf_document_page->document),
			viewer_page->pdf_document_page->display_list);
	viewer_page->pdf_document_page->display_list = NULL;
	viewer_page->pdf_document_page->thread &= 10; //4 löschen

	viewer_foreach(pv, viewer_page->pdf_document_page,
			viewer_annot_foreach_changed, &pv->clicked_annot->annot);

	pv->clicked_annot = NULL;

	return 0;
}

gint viewer_annot_handle_edit_closed(PdfViewer* pdfv, GtkWidget *popover, GError** error) {
	gchar *text = NULL;
	PdfDocumentPageAnnot *pdf_document_page_annot = NULL;
	GtkTextIter start = { 0, };
	GtkTextIter end = { 0, };
	GtkTextBuffer *text_buffer = NULL;
	JournalEntry entry = { 0, };
	GArray* arr_journal = NULL;
	gint rc = 0;
	pdf_annot *pdf_annot = NULL;
	gchar* text_old = NULL;

	pdf_document_page_annot = g_object_get_data(G_OBJECT(popover),
			"pdf-document-page-annot");

	text_buffer = gtk_text_view_get_buffer(
			GTK_TEXT_VIEW(pdfv->annot_textview));
	gtk_text_buffer_get_bounds(text_buffer, &start, &end);

	text = gtk_text_buffer_get_text(text_buffer, &start, &end, TRUE);

	if (!g_strcmp0(text, pdf_document_page_annot->annot.annot_text.content)) {
		g_free(text);
		return 0;
	}

	//Ist-Zustand festhalten
	text_old = pdf_document_page_annot->annot.annot_text.content; //ref übernehmen

	zond_pdf_document_mutex_lock(pdf_document_page_annot->pdf_document_page->document);
	pdf_annot = pdf_document_page_annot_get_pdf_annot(pdf_document_page_annot);
	if (!pdf_annot) {
		zond_pdf_document_mutex_unlock(pdf_document_page_annot->pdf_document_page->document);
		g_free(text);
		g_set_error(error, ZOND_ERROR, 0,
				"%s\nAnnotation nicht gefunden", __func__);

		return -1;
	}

	//Annot selbst ändern
	pdf_document_page_annot->annot.annot_text.content = text; //ref übernommen

	//in pdf_doc einspielen
	rc = pdf_annot_change(zond_pdf_document_get_ctx(
			pdf_document_page_annot->pdf_document_page->document), pdf_annot,
			pdf_document_page_annot->pdf_document_page->rotate,
			pdf_document_page_annot->annot, error);
	zond_pdf_document_mutex_unlock(pdf_document_page_annot->pdf_document_page->document);
	if (rc) {
		pdf_document_page_annot->annot.annot_text.content = text_old;
		g_free(text);

		ERROR_Z
	}

	//Jetzt entry machen
	entry.type = JOURNAL_TYPE_ANNOT_CHANGED;
	entry.pdf_document_page = pdf_document_page_annot->pdf_document_page;
	entry.annot_changed.pdf_document_page_annot = pdf_document_page_annot;

	//alte annot
	entry.annot_changed.annot_before = annot_deep_copy(pdf_document_page_annot->annot);
	//allerdings content ändern
	g_free(entry.annot_changed.annot_before.annot_text.content); //ref aufgebraucht
	entry.annot_changed.annot_before.annot_text.content = text_old; //ref aufgebraucht

	entry.annot_changed.annot_after = annot_deep_copy(pdf_document_page_annot->annot);

	arr_journal = zond_pdf_document_get_arr_journal(pdf_document_page_annot->pdf_document_page->document);
	g_array_append_val(arr_journal, entry);

	gtk_text_buffer_set_text(text_buffer, "", -1);

	//soll nur in sämtlichen betroffenen viewern Speichern aktivieren
	//ansonsten muß nichts gemacht werden
	viewer_foreach(pdfv, pdf_document_page_annot->pdf_document_page,
			viewer_annot_foreach_changed, &entry.annot_changed.annot_after);

	return 0;
}

static gboolean viewer_annot_check_diff(DisplayedDocument* dd,
		PdfDocumentPage *pdf_document_page, fz_rect rect_old,
		fz_rect rect_new) {
	fz_rect crop = pdf_document_page->rect;

	if (pdf_document_page != dd->zpdfd_part->first_page &&
			pdf_document_page != dd->zpdfd_part->last_page) return FALSE;

	crop.y0 = dd->zpdfd_part->first_index;
	crop.y1 = dd->zpdfd_part->last_index;

	if (fz_is_valid_rect(fz_intersect_rect(rect_old, crop)) !=
			fz_is_valid_rect(fz_intersect_rect(rect_new, crop)))
		return TRUE;

	return FALSE;
}

static fz_rect viewer_annot_clamp_page(ViewerPageNew *viewer_page,
		fz_rect rect) {
	fz_rect rect_cropped = { 0, };

	//clamp
	rect_cropped = rect;

	if (rect_cropped.x0 < viewer_page->crop.x0)
		rect_cropped.x0 = viewer_page->crop.x0;
	if (rect_cropped.x0 + ANNOT_ICON_WIDTH > viewer_page->crop.x1)
		rect_cropped.x0 = viewer_page->crop.x1 - ANNOT_ICON_WIDTH;

	if (rect_cropped.y0 < viewer_page->crop.y0)
		rect_cropped.y0 = viewer_page->crop.y0;
	if (rect_cropped.y0 + ANNOT_ICON_HEIGHT > viewer_page->crop.y1)
		rect_cropped.y0 = viewer_page->crop.y1 - ANNOT_ICON_HEIGHT;

	rect_cropped.x1 = rect_cropped.x0 + ANNOT_ICON_WIDTH;
	rect_cropped.y1 = rect_cropped.y0 + ANNOT_ICON_HEIGHT;

	return rect_cropped;
}




gint viewer_annot_handle_release_clicked_annot(PdfViewer* pv,
		ViewerPageNew* viewer_page, PdfPunkt pdf_punkt, GError** error) {
	//verschoben?
	if (!(pv->click_pdf_punkt.seite == pdf_punkt.seite
			&& pv->click_pdf_punkt.punkt.x == pdf_punkt.punkt.x
			&& pv->click_pdf_punkt.punkt.y == pdf_punkt.punkt.y)) {
		fz_context *ctx = NULL;
		JournalEntry entry = { 0, };
		GArray* arr_journal = NULL;
		gint rc = 0;
		GError* error = NULL;
		pdf_annot *pdf_annot = NULL;
		fz_rect rect_old = fz_empty_rect;

		if (pv->click_pdf_punkt.seite != pdf_punkt.seite) { //viewer_page neu holen
			viewer_page = g_ptr_array_index(pv->arr_pages,
					pv->click_pdf_punkt.seite);
			viewer_render_wait_for_transfer(
					viewer_page->pdf_document_page);

			if (!(viewer_page->pdf_document_page->thread & 2))
				return TRUE;
		}

		ctx = zond_pdf_document_get_ctx(
				viewer_page->pdf_document_page->document);

		zond_pdf_document_mutex_lock(
				viewer_page->pdf_document_page->document);

		pdf_annot = pdf_document_page_annot_get_pdf_annot(pv->clicked_annot);
		if (!pdf_annot) {
			zond_pdf_document_mutex_unlock(
					viewer_page->pdf_document_page->document);
			display_message(pv->vf, "Fehler - Annotation editieren\n\n",
					"Bei Aufruf pdf_annot_get_pdf_annot", NULL);

			return TRUE;
		}
		//clicked_annot->rect wurde beim Ziehen laufend angepaßt
		//im JournalEntry soll der bisherige Zustand gespeichert werden
		//der muß dann aus der annot selbt geholt werden
		fz_try(ctx)
			rect_old = pdf_annot_rect(ctx, pdf_annot);
		fz_always(ctx)
			zond_pdf_document_mutex_unlock(viewer_page->pdf_document_page->document);
		fz_catch(ctx) {
			display_message(pv->vf, "Fehler Annot ändern-\n\n",
					"Bei Aufruf pdf_annot_rect: ", fz_caught_message(ctx),
					NULL);

			return TRUE;
		}

		//neues rect kommt ja schon so an, aber clamp machen
		pv->clicked_annot->annot.annot_text.rect =
				viewer_annot_clamp_page(viewer_page, pv->clicked_annot->annot.annot_text.rect);

		//erst einmal prüfen, ob Verschiebung dazu führt, daß Annot in anderem
		//geöffneten Abschnitt aufscheint oder verschwindet
		for (gint i = 0; i < pv->zond->arr_pv->len; i++) {
			DisplayedDocument *dd = NULL;

			PdfViewer *pv_loop = g_ptr_array_index(pv->zond->arr_pv, i);
			dd = pv_loop->dd;
			do {
				if (viewer_annot_check_diff(dd, viewer_page->pdf_document_page,
						rect_old, pv->clicked_annot->annot.annot_text.rect)) {
					display_message(pv->vf,
							"Fehler - Annotation verschieben\n\n",
							"Annotation würde in geöffnerem Abschnitt entfernt oder hinzugefügt\n\n"
							"Bitte Abschnitt schließen und erneut versuchen",
							NULL);

					pv->clicked_annot->annot.annot_text.rect = rect_old;

					//Fenster hervorholen
					gtk_window_present(GTK_WINDOW(pv_loop->vf));

					return TRUE;
				}

			} while ((dd = dd->next) != NULL);
		}

		zond_pdf_document_mutex_lock(
				viewer_page->pdf_document_page->document);
		rc = pdf_annot_change(ctx, pdf_annot, viewer_page->pdf_document_page->rotate,
				pv->clicked_annot->annot, &error);
		zond_pdf_document_mutex_unlock(
				viewer_page->pdf_document_page->document);
		if (rc) {
			display_message(pv->vf, "Fehler Annot ändern-\n\n",
					error->message, NULL);
			g_error_free(error);
			pv->clicked_annot->annot.annot_text.rect = rect_old;

			return TRUE;
		}

		//ins Journal
		entry.pdf_document_page = viewer_page->pdf_document_page;
		entry.type = JOURNAL_TYPE_ANNOT_CHANGED;
		entry.annot_changed.pdf_document_page_annot = pv->clicked_annot;
		entry.annot_changed.annot_before = annot_deep_copy(pv->clicked_annot->annot);
		//rect anpassen
		entry.annot_changed.annot_before.annot_text.rect = rect_old;

		entry.annot_changed.annot_after = annot_deep_copy(pv->clicked_annot->annot);

		arr_journal = zond_pdf_document_get_arr_journal(viewer_page->pdf_document_page->document);
		g_array_append_val(arr_journal, entry);

		fz_drop_display_list(ctx,
				viewer_page->pdf_document_page->display_list);
		viewer_page->pdf_document_page->display_list = NULL;
		viewer_page->pdf_document_page->thread &= 10;

		viewer_foreach(pv, viewer_page->pdf_document_page,
				viewer_annot_foreach_changed, &entry.annot_changed.annot_after);
	} else if (pv->clicked_annot->annot.annot_text.open) {//nicht verschoben, edit-popup geöffnet
		//angeklickt -> textview öffnen
		GdkRectangle gdk_rectangle = { 0, };
		gint x = 0, y = 0, width = 0, height = 0;

		gtk_container_child_get(GTK_CONTAINER(pv->layout),
				GTK_WIDGET(viewer_page->image_page), "y", &y, NULL);
		y += (gint) (pv->clicked_annot->annot.annot_text.rect.y0 * pv->zoom
				/ 100);
		y -= gtk_adjustment_get_value(pv->v_adj);

		gtk_container_child_get(GTK_CONTAINER(pv->layout),
				GTK_WIDGET(viewer_page->image_page), "x", &x, NULL);
		x += (gint) (pv->clicked_annot->annot.annot_text.rect.x0 * pv->zoom
				/ 100);
		x -= gtk_adjustment_get_value(pv->h_adj);

		height = (gint) ((pv->clicked_annot->annot.annot_text.rect.y1
				- pv->clicked_annot->annot.annot_text.rect.y0) * pv->zoom
				/ 100);
		width = (gint) ((pv->clicked_annot->annot.annot_text.rect.x1
				- pv->clicked_annot->annot.annot_text.rect.x0) * pv->zoom
				/ 100);

		gdk_rectangle.x = x;
		gdk_rectangle.y = y;
		gdk_rectangle.width = width;
		gdk_rectangle.height = height;

		gtk_popover_popdown(GTK_POPOVER(pv->annot_pop));

		g_object_set_data(G_OBJECT(pv->annot_pop_edit),
				"pdf-document-page-annot", pv->clicked_annot);
		gtk_popover_set_pointing_to(GTK_POPOVER(pv->annot_pop_edit),
				&gdk_rectangle);
		if (pv->clicked_annot->annot.annot_text.content)
			gtk_text_buffer_set_text(gtk_text_view_get_buffer(
					GTK_TEXT_VIEW(pv->annot_textview)),
					pv->clicked_annot->annot.annot_text.content, -1);
		gtk_popover_popup(GTK_POPOVER(pv->annot_pop_edit));
		gtk_widget_grab_focus(pv->annot_pop_edit);
	}

	return 0;
}

gint viewer_annot_create(ViewerPageNew *viewer_page, gchar **errmsg) {
	pdf_annot *pdf_annot = NULL;
	PdfDocumentPageAnnot *pdf_document_page_annot = NULL;
	JournalEntry entry = { 0, };
	Annot annot = { 0 };
	GError* error = NULL;

	fz_context *ctx = zond_pdf_document_get_ctx(
			viewer_page->pdf_document_page->document);

	if (viewer_page->pdfv->state == 1)
		annot.type = PDF_ANNOT_HIGHLIGHT;
	else if (viewer_page->pdfv->state == 2)
		annot.type = PDF_ANNOT_UNDERLINE;
	else if (viewer_page->pdfv->state == 3)
		annot.type = PDF_ANNOT_TEXT;

	if (annot.type == PDF_ANNOT_HIGHLIGHT || annot.type == PDF_ANNOT_UNDERLINE) {
		gint i = 0;

		annot.annot_text_markup.arr_quads = g_array_new(FALSE, FALSE, sizeof(fz_quad));

		while (viewer_page->pdfv->highlight.page[i] != -1) {
			if (viewer_page ==
					g_ptr_array_index(viewer_page->pdfv->arr_pages, viewer_page->pdfv->highlight.page[i])) {
				g_array_append_val(annot.annot_text_markup.arr_quads, viewer_page->pdfv->highlight.quad[i]);
			}
			i++;
		}
	}
	else if (viewer_page->pdfv->state == 3)
	{
		annot.annot_text.rect.x0 = viewer_page->pdfv->click_pdf_punkt.punkt.x;
		annot.annot_text.rect.y0 = viewer_page->pdfv->click_pdf_punkt.punkt.y;
		annot.annot_text.rect.x1 = annot.annot_text.rect.x0 + ANNOT_ICON_WIDTH;
		annot.annot_text.rect.y1 = annot.annot_text.rect.y0 + ANNOT_ICON_HEIGHT;

		annot.annot_text.rect = viewer_annot_clamp_page(viewer_page, annot.annot_text.rect);
	}

	zond_pdf_document_mutex_lock(viewer_page->pdf_document_page->document);
	pdf_annot = pdf_annot_create(ctx, viewer_page->pdf_document_page->page,
			viewer_page->pdf_document_page->rotate, annot, &error);
	zond_pdf_document_mutex_unlock(viewer_page->pdf_document_page->document);
	if (!pdf_annot) {
		if (errmsg) *errmsg = g_strdup_printf("%s\n%s", __func__, error->message);
		g_error_free(error);
		annot_free(&annot);

		return -1;
	}

	pdf_document_page_annot = g_malloc0(sizeof(PdfDocumentPageAnnot));
	pdf_document_page_annot->pdf_document_page = viewer_page->pdf_document_page;
	pdf_document_page_annot->annot = annot;

	g_ptr_array_add(viewer_page->pdf_document_page->arr_annots,
			pdf_document_page_annot);

	fz_drop_display_list(
			zond_pdf_document_get_ctx(
					viewer_page->pdf_document_page->document),
			viewer_page->pdf_document_page->display_list);
	viewer_page->pdf_document_page->display_list = NULL;
	viewer_page->pdf_document_page->thread &= 10;

	viewer_foreach(viewer_page->pdfv, viewer_page->pdf_document_page,
			viewer_annot_foreach_changed, &annot);

	entry.pdf_document_page = viewer_page->pdf_document_page;
	entry.type = JOURNAL_TYPE_ANNOT_CREATED;
	entry.annot_changed.pdf_document_page_annot = pdf_document_page_annot;
	entry.annot_changed.annot_after = annot_deep_copy(annot);
	g_array_append_val(zond_pdf_document_get_arr_journal(viewer_page->pdf_document_page->document), entry);

	return 0;
}

gint viewer_annot_create_markup(PdfViewer *pv, ViewerPageNew* viewer_page,
		PdfPunkt pdf_punkt, GError **error) {
	gint von = 0;
	gint bis = 0;

	if (pv->click_pdf_punkt.seite < pdf_punkt.seite) {
		von = pv->click_pdf_punkt.seite;
		bis = pdf_punkt.seite;
	} else if (pv->click_pdf_punkt.seite > pdf_punkt.seite) {
		von = pdf_punkt.seite;
		bis = pv->click_pdf_punkt.seite;
	} else //gleiche Seite
	{
		von = pdf_punkt.seite;
		bis = pdf_punkt.seite;
	}

	for (gint page = von; page <= bis; page++) {
		ViewerPageNew *viewer_page_loop = NULL;
		gint rc = 0;
		gchar* errmsg = NULL;

		if (page == pdf_punkt.seite)
			viewer_page_loop = viewer_page;
		else {
			viewer_page_loop = g_ptr_array_index(pv->arr_pages,
					page);
			viewer_render_wait_for_transfer(
					viewer_page_loop->pdf_document_page);
		}

		if (!(viewer_page_loop->pdf_document_page->thread & 2))
			return TRUE;

		rc = viewer_annot_create(viewer_page_loop, &errmsg);
		if (rc) {
			g_set_error(error, ZOND_ERROR, 0,
					"%s", errmsg);
			g_free(errmsg);

			return -1;
		}
	}

	return 0;
}
