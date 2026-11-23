/*
 zond (seiten.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2020  pelo america

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

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <tesseract/capi.h>

#include "../../sond_log_and_error.h"
#include "../../sond_fileparts.h"
#include "../../sond_pdf_helper.h"
#include "../../gtk3/sond_dialoge.h"
#include "../../misc_stdlib.h"


#include "../zond_pdf_document.h"


#include "../../znd/anbindung.h"


#include "viewer.h"
#include "seiten.h"
#include "viewer_render.h"
#include "viewer_search.h"
#include "viewer_save.h"
#include "document.h"

GPtrArray*
seiten_get_document_pages(PdfViewer *pv, GArray *arr_seiten_pv) {
	GPtrArray *arr_document_page = NULL;

	if (arr_seiten_pv) {
		arr_document_page = g_ptr_array_new();

		for (gint i = 0; i < arr_seiten_pv->len; i++) {
			ViewerPageNew *viewer_page = g_ptr_array_index(pv->arr_pages,
					g_array_index( arr_seiten_pv, gint, i ));

			if (!g_ptr_array_find(arr_document_page,
					viewer_page->pdf_document_page, NULL))
				g_ptr_array_add(arr_document_page,
						(gpointer) viewer_page->pdf_document_page);
		}
	}

	return arr_document_page;
}

GArray*
seiten_markierte_thumbs(PdfViewer *pv) {
	GList *selected = NULL;
	GList *list = NULL;
	GArray *arr_page_pv = NULL;
	gint *index = NULL;

	selected = gtk_tree_selection_get_selected_rows(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(pv->tree_thumb)), NULL);

	if (!selected)
		return NULL;

	arr_page_pv = g_array_new( FALSE, FALSE, sizeof(gint));
	list = selected;
	do {
		index = gtk_tree_path_get_indices(list->data);
		g_array_append_val(arr_page_pv, index[0]);
	} while ((list = list->next));

	g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);

	return arr_page_pv;
}

/*
 **      Seiten drehen
 */
static void seiten_page_tilt(ViewerPageNew *viewer_page) {
	float x1_tmp = 0.0;
	float y1_tmp = 0.0;

	x1_tmp = viewer_page->crop.x0
			+ (viewer_page->crop.y1 - viewer_page->crop.y0);
	y1_tmp = viewer_page->crop.y0
			+ (viewer_page->crop.x1 - viewer_page->crop.x0);

	viewer_page->crop.x1 = x1_tmp;
	viewer_page->crop.y1 = y1_tmp;

	return;
}

gint seiten_viewer_drehen(PdfViewer *pv, ViewerPageNew* viewer_page,
		gint page_pv, gpointer data) {
	gint winkel = 0;
	GtkTreeIter iter = { 0 };
	winkel = GPOINTER_TO_INT(data);

	viewer_close_thread_pool_and_transfer(pv);

	//damit in Seite gezeichnete Markierungen nach dem Drehen nicht an falscher Stelle sind
	pv->clicked_annot = NULL;
	pv->highlight.page[0] = -1;

	if (viewer_page->image_page)
		gtk_image_clear(GTK_IMAGE(viewer_page->image_page));
	viewer_page->pixbuf_page = NULL;

	viewer_get_iter_thumb(pv, page_pv, &iter);

	gtk_list_store_set(
			GTK_LIST_STORE(
					gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) )),
			&iter, 0, NULL, -1);
	viewer_page->pixbuf_thumb = NULL;

	if (winkel == 90 || winkel == -90) {
		seiten_page_tilt(viewer_page);
		g_object_set_data(G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1));
	}

	viewer_page->thread = 0;
	//g_signal_emit_by_name nicht erforderlich. da layout refreshed wird

	return 1;
}

static gint seiten_drehen_pdf(PdfDocumentPage *pdf_document_page, gint winkel,
		GError **error) {
	gint rotate = 0;
	pdf_obj* obj = NULL;

	fz_context *ctx = zond_pdf_document_get_ctx(pdf_document_page->document);

	obj = pdf_document_page_get_page_obj(pdf_document_page, error);
	if (!obj)
		return -1;

	rotate = pdf_page_rotate(ctx, obj, winkel, error);
	if (rotate == -1)
		return -1;

	pdf_document_page->rotate = rotate;

	return 0;
}

/* Führt den Zustand nach einer Drehung der Seite nach: /Rotate im Seitenobjekt ist schon geändert
 * und pdf_document_page->rotate gesetzt. Seite neu laden, Anzeige-Daten verwerfen, Journal-Eintrag
 * (damit die Drehung beim Speichern in die Datei kommt), Layout der Viewer anpassen. Gilt für
 * das Drehen im Viewer wie für die Drehung, die die OCR selbst vornimmt. winkel: -90, 90 oder 180. */
void seiten_drehung_nachfuehren(PdfViewer *pv, PdfDocumentPage *pdf_document_page, gint winkel) {
	GError *error = NULL;
	JournalEntry entry = { 0 };
	GArray* arr_journal = NULL;
	gint rc = 0;

	viewer_render_wait_for_transfer(pdf_document_page);

	/* War die Seite noch nie geladen (kein page, kein display_list/
	 * stext_page/arr_annots - lazy load), gibt es hier nichts zu
	 * invalidieren: /Rotate steht bereits im pdf_obj, und beim späteren
	 * ersten Rendern wird die Seite ganz normal frisch geladen und liest
	 * die Rotation dann schon korrekt. Nur wenn die Seite schon geladen
	 * war, muss sie neu geladen werden, weil pdf_page_rotate() nur den
	 * pdf_obj ändert, nicht ein bereits geladenes pdf_page-Objekt. */
	if (pdf_document_page->page) {
		fz_drop_display_list(
				zond_pdf_document_get_ctx(pdf_document_page->document),
				pdf_document_page->display_list);
		pdf_document_page->display_list = NULL;

		fz_drop_stext_page(
				zond_pdf_document_get_ctx(pdf_document_page->document),
				pdf_document_page->stext_page);
		pdf_document_page->stext_page = NULL;

		/* altes arr_annots freigeben (mit Free-Func je Element) - danach
		 * legt zond_pdf_document_load_page() ein neues an. Nur hier
		 * nötig: dort wird sie sonst nur beim erstmaligen Laden
		 * aufgerufen, wo arr_annots ohnehin noch NULL ist.
		 * page und arr_annots werden immer zusammen in
		 * zond_pdf_document_load_page() gesetzt - da wir hier bereits
		 * wissen, dass page gesetzt ist, ist es auch arr_annots. */
		g_ptr_array_unref(pdf_document_page->arr_annots);
		pdf_document_page->arr_annots = NULL;

		rc = zond_pdf_document_load_page(pdf_document_page,
				zond_pdf_document_get_ctx(pdf_document_page->document), &error);
		if (rc) {
			/* page/display_list/stext_page/arr_annots wurden oben bereits
			 * verworfen, page ist jetzt NULL (zond_pdf_document_load_page()
			 * setzt das bei einem gescheiterten Reload) - Flags müssen das
			 * widerspiegeln, sonst hält der Renderer die Seite fälschlich
			 * für geladen und griffe auf page (NULL) zu. Diese eine Seite
			 * bleibt ungedreht (physisch/im pdf_obj zwar schon rotiert,
			 * aber ohne Journal-Eintrag - geht beim nächsten Speichern
			 * wieder verloren). */
			pdf_document_page->thread &= ~(2 | 4 | 8);

			LOG_WARN("Seite %i konnte nach Drehung nicht neu geladen werden: %s",
					pdf_document_page->page_akt + 1, error->message);
			g_clear_error(&error);

			return;
		}
	}

	pdf_document_page->thread &= 2;

	arr_journal = zond_pdf_document_get_arr_journal(pdf_document_page->document);
	entry.pdf_document_page = pdf_document_page;
	entry.type = JOURNAL_TYPE_ROTATE;
	entry.rotate.winkel = winkel;
	g_array_append_val(arr_journal, entry);

	zond_pdf_document_notify_page_changed(pdf_document_page->document, pdf_document_page,
			ZPDFD_PART_CHANGE_ROTATED, GINT_TO_POINTER(winkel));
}

static gint seiten_drehen(PdfViewer *pv, GPtrArray *arr_document_page,
		gint winkel, GError **error) {
	for (gint i = 0; i < arr_document_page->len; i++) {
		gint rc = 0;

		PdfDocumentPage *pdf_document_page = g_ptr_array_index(
				arr_document_page, i);

		zond_pdf_document_mutex_lock(pdf_document_page->document);
		rc = seiten_drehen_pdf(pdf_document_page, winkel, error);
		zond_pdf_document_mutex_unlock(pdf_document_page->document);
		if (rc == -1)
			return -1;

		seiten_drehung_nachfuehren(pv, pdf_document_page, winkel);
	}

	return 0;
}

void cb_pv_seiten_drehen(GtkMenuItem *item, gpointer data) {
	gint rc = 0;
	GError *error = NULL;
	gint winkel = 0;
	gchar *title = NULL;
	GPtrArray *arr_document_page = NULL;

	PdfViewer *pv = (PdfViewer*) data;

	//zu drehende Seiten holen
	title = g_strdup_printf("Seiten drehen (1 - %i):", pv->arr_pages->len);
	arr_document_page = seiten_abfrage_seiten(pv, title, &winkel, TRUE);
	g_free(title);

	if (!arr_document_page)
		return;

	rc = seiten_drehen(pv, arr_document_page, winkel, &error);
	g_ptr_array_unref(arr_document_page);
	if (rc) {
		display_message(pv->vf, "Fehler in Seiten drehen -\n\nBei Aufruf "
				"seiten_drehen\n", error->message, NULL);
		g_error_free(error);

		return;
	}

	return;
}

/*
 **      Seiten löschen
 */
/* Kann nicht scheitern (kein Rückgabewert/GError**, im Unterschied zu
 * seiten_anbindung()) - Seiten werden hier immer entweder gelöscht oder
 * (letzte verbliebene Seite der Datei) übersprungen, beides ohne
 * Fehlerfall. */
static void seiten_loeschen(PdfViewer *pv, GPtrArray *arr_document_page) {
	gboolean page_deleted = FALSE;

	for (gint i = 0; i < arr_document_page->len; i++) {
		PdfDocumentPage* pdf_document_page = NULL;
		JournalEntry entry = { 0, };
		GArray* arr_journal = NULL;
		GPtrArray* arr_pages = NULL;
		gint count = 0;

		pdf_document_page = g_ptr_array_index(arr_document_page, i);

		//Prüfen, ob letzte (nicht gelöschte) Seite des Dokuments
		arr_pages = zond_pdf_document_get_arr_pages(
				pdf_document_page->document);
		for (gint j = 0; j < arr_pages->len; j++) {
			PdfDocumentPage* pdf_document_page_tmp = NULL;

			pdf_document_page_tmp = g_ptr_array_index(arr_pages, j);
			if (!pdf_document_page_tmp || !pdf_document_page_tmp->deleted)
				count++;
		}

		if (count == 1) continue;

		pdf_document_page->deleted = TRUE; //als zu löschend markieren
		page_deleted = TRUE;

		arr_journal = zond_pdf_document_get_arr_journal(pdf_document_page->document);

		entry.pdf_document_page = pdf_document_page;
		entry.type = JOURNAL_TYPE_PAGE_DELETED;

		g_array_append_val(arr_journal, entry);

		//Abschnitte, die die Seite umfassen, nehmen sie aus ihren Viewern
		zond_pdf_document_notify_page_changed(pdf_document_page->document, pdf_document_page,
				ZPDFD_PART_CHANGE_DELETED, NULL);
	}

	if (page_deleted) {
		gtk_tree_selection_unselect_all(
				gtk_tree_view_get_selection(GTK_TREE_VIEW(pv->tree_thumb)));
	}

	return;
}

//Host fragen, ob die Seiten gelöscht werden dürfen (Anbindungen); sonst Meldung
static gboolean seiten_loeschen_erlaubt(PdfViewer *pv, GPtrArray *arr_document_page) {
	gint rc = 0;
	GError *error = NULL;

	rc = pdf_viewer_session_pages_deleting(pv->session, pv, arr_document_page, &error);
	if (rc) {
		if (rc == -1)
			display_message(pv->vf, "Fehler Seiten Löschen\n\n", error->message, NULL);
		else
			display_message(pv->vf, error->message, NULL);
		g_error_free(error);

		return FALSE;
	}

	return TRUE;
}

void cb_pv_seiten_loeschen(GtkMenuItem *item, gpointer data) {
	gchar *title = NULL;
	GPtrArray *arr_document_page = NULL;

	PdfViewer *pv = (PdfViewer*) data;

	//zu löschende Seiten holen
	title = g_strdup_printf("Seiten löschen (1 - %i):", pv->arr_pages->len);
	arr_document_page = seiten_abfrage_seiten(pv, title, NULL, FALSE);
	g_free(title);

	if (!arr_document_page)
		return;

	if (!seiten_loeschen_erlaubt(pv, arr_document_page)) {
		g_ptr_array_unref(arr_document_page);

		return;
	}

	seiten_loeschen(pv, arr_document_page);
	g_ptr_array_unref(arr_document_page);

	return;
}

/*
 **      Seiten einfügen
 */
gint seiten_viewer_einfuegen(PdfViewer *pv, ViewerPageNew* viewer_page,
		gint page_pv, gpointer data) {
	DataInsert* data_insert = (DataInsert*) data;

	//Wenn vor erster oder nach letzter Seite des vorliegenden dd eingefügt werden soll:
	//Prüfen, ob dd so "weit" ist wie das dd, in das eingefügt wurde
	if (viewer_page->dd->zpdfd_part->first_page == viewer_page->pdf_document_page &&
			!data_insert->after_last) { //Seite liegt am Anfang ...
		gint last_page_pv_dd = 0;
		gint last_page_pv_entry = 0;

		//wenn vorliegendes dd "unterseitig" anfängt:
		if (viewer_page->dd->zpdfd_part->first_index)
			return 0;

		//sonst weiter untersuchen:
		last_page_pv_dd = viewer_page->dd->zpdfd_part->last_page->page_akt;
		last_page_pv_entry = data_insert->dd->zpdfd_part->last_page->page_akt;

		//vorliegendes dd kürzer: Ende!
		if (last_page_pv_dd < last_page_pv_entry) return 0;
		else if (last_page_pv_dd == last_page_pv_entry)
			if (viewer_page->dd->zpdfd_part->last_index < data_insert->dd->zpdfd_part->last_index)
				return 0;

		//dd soll jetzt auch eingefügte Seiten umfassen
		//d.h. erste Seite anpassen
		viewer_page->dd->zpdfd_part->first_page =
				zond_pdf_document_get_pdf_document_page(viewer_page->dd->zpdfd_part->zond_pdf_document,
				data_insert->page_doc);
	} else if (viewer_page->dd->zpdfd_part->last_page ==
			viewer_page->pdf_document_page && data_insert->after_last) {
		gint first_page_pv_dd = 0;
		gint first_page_pv_entry = 0;

		if (viewer_page->dd->zpdfd_part->last_index < EOP) return 0;

		first_page_pv_dd = viewer_page->dd->zpdfd_part->first_page->page_akt;
		first_page_pv_entry = data_insert->dd->zpdfd_part->first_page->page_akt;

		if (first_page_pv_dd > first_page_pv_entry) return 0;
		else if (first_page_pv_dd == first_page_pv_entry)
			if (viewer_page->dd->zpdfd_part->first_index > data_insert->dd->zpdfd_part->last_index)
				return 0;

		viewer_page->dd->zpdfd_part->last_page =
				zond_pdf_document_get_pdf_document_page(viewer_page->dd->zpdfd_part->zond_pdf_document,
				data_insert->page_doc + data_insert->count - 1);
	}

	/* Ab hier ändert sich pv->arr_pages: wartende Render-Tasks kennen ihre Seite über den Index
	 * und lösen ihn im Worker über arr_pages auf. Vorläufig anhalten, bis die Tasks die
	 * viewer_page selbst bekommen. */
	viewer_close_thread_pool_and_transfer(pv);

	//highlights und Markierungen Text löschen
	pv->highlight.page[0] = -1;
	pv->text_occ.index_act = -1;
	//page_act mit zurücksetzen - Seitennummern verschieben sich durch das
	//Einfügen, arr_quad/page_act könnten sich sonst auf die falsche Seite
	//beziehen
	pv->text_occ.page_act = -1;
	viewer_text_search_cancel(pv);
	gtk_label_set_text(GTK_LABEL(pv->label_search), "");

	//jetzt in viewer einfügen
	for (gint u = 0; u < data_insert->count; u++) {
		ViewerPageNew *viewer_page_insert = NULL;
		GtkTreeIter iter_tmp;

		viewer_page_insert = viewer_new_page (pv, viewer_page->dd,
				viewer_page->pdf_document_page->page_akt + u -
				((data_insert->after_last) ? -1 : data_insert->count));

		g_ptr_array_insert(pv->arr_pages, page_pv + u + ((data_insert->after_last) ? 1 : 0),
				viewer_page_insert);

		gtk_list_store_insert(GTK_LIST_STORE(
				gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) )),
				&iter_tmp, page_pv + u + ((data_insert->after_last) ? 1 : 0));
	}

	g_object_set_data(G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1));

	return 1;
}

//Einfügen vor der ersten oder nach der letzten Seite nur, wenn der Abschnitt am Rand der Seite liegt
static gboolean seiten_einfuegen_pos_pruefen(PdfViewer *pv, guint pos, ViewerPageNew *viewer_page) {
	if (pos == 0) {
		if (viewer_page->dd->zpdfd_part->first_index != 0) {
			display_message(pv->vf,
					"Abschnitt beginnt nicht am Beginn der Seite -\n"
							"Einfügen vor erster Seite daher nicht möglich",
					NULL);

			return FALSE;
		}
	}
	else if (pos == pv->arr_pages->len) {
		if (viewer_page->dd->zpdfd_part->last_index != EOP) {//einfügen nach letzter Seite
			display_message(pv->vf, "Abschnitt Ende nicht am Schluß der Seite -\n"
					"Einfügen nach Ende eines Abschnitts daher nicht möglich", NULL);

			return FALSE;
		}
	}

	return TRUE;
}

//Datei wählen, laden und als PDF öffnen; NULL bei Abbruch oder Fehler (Meldung ist ausgegeben)
static pdf_document* seiten_einfuegen_quelle_datei(PdfViewer *pv, SondFilePart **sfp) {
	gchar *path_merge = NULL;
	pdf_document *doc_merge = NULL;
	GError *error = NULL;

	//Datei auswählen
	path_merge = filename_oeffnen(GTK_WINDOW(pv->vf), NULL);
	if (!path_merge) //Dialog abgebrochen
		return NULL;

	//nicht geladene SeaDrive-Datei: erst laden (Abbruch: nichts einfügen)
	if (!pdf_viewer_session_load_file(pv->session,
			GTK_WINDOW(pv->vf), path_merge)) {
		g_free(path_merge);

		return NULL;
	}

	*sfp = sond_file_part_from_filepart(path_merge, &error);
	g_free(path_merge);
	if (!*sfp) {
		display_message(pv->vf, "Datei einfügen\n\n", error->message, NULL);
		g_error_free(error);

		return NULL;
	}

	if (!SOND_IS_FILE_PART_PDF(*sfp)) {
		display_message(pv->vf, "Keine PDF-Datei", NULL);
		g_object_unref(*sfp);
		*sfp = NULL;

		return NULL;
	}

	doc_merge = sond_file_part_pdf_open_document(pv->session->ctx,
			SOND_FILE_PART_PDF(*sfp), TRUE, &error);
	if (!doc_merge) {
		display_message(pv->vf, "Datei einfügen\n\n"
				"Einzufügende Datei konnte nicht geöffnet werden:\n",
				error->message, NULL);
		g_error_free(error);
		g_object_unref(*sfp);
		*sfp = NULL;

		return NULL;
	}

	return doc_merge;
}

//Seitenzahl der Quelle; bei Fehler werden Quelle und SondFilePart freigegeben
static gint seiten_einfuegen_zaehlen(PdfViewer *pv, pdf_document *doc_merge, SondFilePart *sfp,
		gint ret, gint *count) {
	fz_try(pv->session->ctx)
		*count = pdf_count_pages(pv->session->ctx, doc_merge);
	fz_catch( pv->session->ctx) {
		gchar* err_message = NULL;

		pdf_drop_document(pv->session->ctx, doc_merge);
		if (ret == 1)
			g_object_unref(sfp);

		err_message = g_strdup_printf("%s\n%s", __func__, fz_caught_message(pv->session->ctx));
		display_message(pv->vf, "Einfügen nicht möglich\n\n", err_message, NULL);
		g_free(err_message);

		return -1;
	}

	return 0;
}

/* Seiten ins Dokument einfügen, die Viewer-Seiten ergänzen, Layouts aktualisieren und ins Journal
 * eintragen. Gibt Quelldokument und SondFilePart frei; bei Fehler wird der Viewer geschlossen. */
static void seiten_einfuegen_ausfuehren(PdfViewer *pv, ViewerPageNew *viewer_page, guint pos,
		pdf_document *doc_merge, SondFilePart *sfp, gint ret, gint count) {
	gint rc = 0;
	gint page_doc = 0;
	GError *error = NULL;
	JournalEntry entry = { 0 };
	DataInsert data_insert = { 0 };

	page_doc = viewer_page->pdf_document_page->page_akt;
	if (pos == pv->arr_pages->len) page_doc++;

	//Seiten werden eingefügt, so daß Seite an Position page_doc nach hinten geschoben wird;
	//page_doc dieser Seite ist hinterher page_doc+count
	rc = zond_pdf_document_insert_pages(viewer_page->dd->zpdfd_part->zond_pdf_document,
			page_doc, viewer_page->dd->zpdfd_part, doc_merge, &error);
	pdf_drop_document(pv->session->ctx, doc_merge);
	if (ret == 1)
		g_object_unref(sfp);
	if (rc) {
		display_message(pv->vf, "Fehler Einfügen\n\n", error->message,
				"\n\nViewer wird geschlossen", NULL);
		g_error_free(error);
		viewer_schliessen(pv);

		return;
	}

	//betroffene viewer-seiten einfügen - kann keinen Fehler zurückgeben
	//vorangehende pdf_document_page wird übergeben - außer wenn Einfügen nach letzter Seite
	data_insert.dd = viewer_page->dd;
	data_insert.page_doc = page_doc; //ist die Position der ersten eingefügten Seite!
	data_insert.count = count;
	data_insert.after_last = (pos == pv->arr_pages->len) ? TRUE : FALSE;

	entry.type = JOURNAL_TYPE_PAGES_INSERTED;
	entry.pages_inserted.count = count;
	entry.pages_inserted.zpdfd_part = zpdfd_part_ref(viewer_page->dd->zpdfd_part);

	//viewer_page->pdf_document_page ist - wenn nicht Einfügen nach letzter Seite -
	//im zond_pdf_document nach hinten gerutscht; die Abschnitte, die sie umfassen, fügen ein
	zond_pdf_document_notify_page_changed(viewer_page->dd->zpdfd_part->zond_pdf_document,
			viewer_page->pdf_document_page, ZPDFD_PART_CHANGE_INSERTED, &data_insert);

	//erste eingefügte Seite!
	entry.pdf_document_page =
			zond_pdf_document_get_pdf_document_page(viewer_page->dd->zpdfd_part->zond_pdf_document, page_doc);

	g_array_append_val(zond_pdf_document_get_arr_journal(viewer_page->dd->zpdfd_part->zond_pdf_document), entry);

	return;
}

void cb_pv_seiten_einfuegen(GtkMenuItem *item, gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;
	gint ret = 0;
	guint pos = 0;
	pdf_document *doc_merge = NULL;
	ViewerPageNew* viewer_page = NULL;
	gint count = 0;
	SondFilePart* sfp = NULL;

	if (pv->dd->next) {
		display_message(pv->vf,
				"Virtuelles PDF -\n\n"
						"Einfügen noch nicht implementiert",
				NULL);

		return;
	}

	ret = seiten_abfrage_seitenzahl(pv, &pos);
	if (ret == -1)
		return;

	if (pos > pv->arr_pages->len)
		pos = pv->arr_pages->len;
	//kein "if (pos < 0) pos = 0;" mehr - pos ist guint, kann nie negativ sein

	viewer_page = g_ptr_array_index(pv->arr_pages, (pos == pv->arr_pages->len) ? pos - 1 : pos);

	//verschiedene Tests...
	if (!seiten_einfuegen_pos_pruefen(pv, pos, viewer_page))
		return;

	//komplette Datei wird eingefügt
	if (ret == 1) {
		doc_merge = seiten_einfuegen_quelle_datei(pv, &sfp);
		if (!doc_merge)
			return;
	} else if (ret == 2)
		doc_merge = pdf_keep_document(pv->session->ctx, pv->session->pv_clip); //Clipboard

	if (seiten_einfuegen_zaehlen(pv, doc_merge, sfp, ret, &count))
		return;

	seiten_einfuegen_ausfuehren(pv, viewer_page, pos, doc_merge, sfp, ret, count);

	return;
}

static pdf_document*
seiten_create_document(PdfViewer *pv, GArray *arr_page_pv, GError **error) {
	pdf_document *doc_dest = NULL;
	gint rc = 0;

	fz_try( pv->session->ctx )
		doc_dest = pdf_create_document(pv->session->ctx);
fz_catch	( pv->session->ctx ) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("MUPDF"),
					fz_caught(pv->session->ctx), "%s\n%s", __func__,
					fz_caught_message(pv->session->ctx));

		return NULL;
	}

	for (gint i = 0; i < arr_page_pv->len; i++) {
		ViewerPageNew *viewer_page = NULL;
		PdfDocumentPage *pdf_document_page = NULL;

		gint page_pv = g_array_index(arr_page_pv, gint, i);
		viewer_page = g_ptr_array_index(pv->arr_pages, page_pv);
		pdf_document_page = viewer_page->pdf_document_page;

		//gelöschte (versteckte) Annotationen sollen nicht in die Zwischenablage
		zond_pdf_document_mutex_lock(pdf_document_page->document);
		rc = pdf_copy_page(pv->session->ctx,
				zond_pdf_document_get_pdf_doc(pdf_document_page->document),
				pdf_document_page->page_akt, pdf_document_page->page_akt, doc_dest, -1,
				FALSE, error);
		zond_pdf_document_mutex_unlock(pdf_document_page->document);
		if (rc) {
			pdf_drop_document(pv->session->ctx, doc_dest);

			return NULL;
		}
	}

	return doc_dest;
}

static gint seiten_set_clipboard(PdfViewer *pv, GArray *arr_page_pv,
		GError **error) {
	pdf_drop_document(pv->session->ctx, pv->session->pv_clip);
	pv->session->pv_clip = NULL;

	pv->session->pv_clip = seiten_create_document(pv, arr_page_pv, error);
	if (!pv->session->pv_clip)
		return -1;

	return 0;
}

void cb_seiten_kopieren(GtkMenuItem *item, gpointer data) {
	gint rc = 0;
	GError *error = NULL;

	PdfViewer *pv = (PdfViewer*) data;

	GArray *arr_page_pv = seiten_markierte_thumbs(pv);
	if (!arr_page_pv)
		return;

	rc = seiten_set_clipboard(pv, arr_page_pv, &error);
	if (rc) {
		display_message(pv->vf, "Fehler Kopieren Seiten\n\n", error->message, NULL);
		g_error_free(error);
	}

	g_array_unref(arr_page_pv);

	return;
}

void cb_seiten_ausschneiden(GtkMenuItem *item, gpointer data) {
	gint rc = 0;
	GError *error = NULL;

	PdfViewer *pv = (PdfViewer*) data;

	GArray *arr_page_pv = seiten_markierte_thumbs(pv);
	if (!arr_page_pv)
		return;

	GPtrArray *arr_document_page = seiten_get_document_pages(pv, arr_page_pv);

	//wie beim Löschen: der Host entscheidet, ob die Seiten (Anbindungen) entfernt werden dürfen
	if (!seiten_loeschen_erlaubt(pv, arr_document_page)) {
		g_ptr_array_unref(arr_document_page);
		g_array_unref(arr_page_pv);

		return;
	}

	rc = seiten_set_clipboard(pv, arr_page_pv, &error);
	g_array_unref(arr_page_pv);
	if (rc) {
		display_message(pv->vf, "Fehler Kopieren Seiten\n\n", error->message, NULL);
		g_error_free(error);
		g_ptr_array_unref(arr_document_page);

		return;
	}

	seiten_loeschen(pv, arr_document_page);
	g_ptr_array_unref(arr_document_page);

	return;
}
