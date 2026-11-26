#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <mupdf/fitz.h>
#include <cairo.h>

#include "../sond_log_and_error.h"
#include "../sond_pdf_helper.h"
#include "../sond_fileparts.h"
#include "../gtk3/sond_dialoge.h"

#include "zond_pdf_document.h"

#include "viewer.h"
#include "viewer_draw.h"


static void viewer_render_transfer_rendered(PdfViewer *pdfv, gboolean protect) {
	gint idx = 0;

	if (protect)
		g_mutex_lock(&pdfv->mutex_arr_rendered);

	if ((idx = pdfv->arr_rendered->len - 1) < 0) {
		if (protect)
			g_mutex_unlock(&pdfv->mutex_arr_rendered);

		return;
	}

	while (idx >= 0) {
		RenderResponse render_response = { 0 };
		ViewerPageNew *viewer_page = NULL;

		render_response = g_array_index(pdfv->arr_rendered, RenderResponse,
				idx);

		viewer_page = g_ptr_array_index(pdfv->arr_pages, render_response.page_pv);

		//thread von viewer_page und pdf_document_page fertig: bit 0 löschen
		viewer_page->thread &= 6;
		viewer_page->pdf_document_page->thread &= 14;
		viewer_page->pdf_document_page->thread_pv = NULL;

		//Beim rendern ein Fehler aufgetreten
		if (render_response.error)
			LOG_WARN("Fehler beim Rendern Seite %i: %s", render_response.page_pv,
					render_response.error_message);

		if (render_response.error == 0 || render_response.error > 2)
			viewer_page->pdf_document_page->thread |= 2;

		if (render_response.error == 0 || render_response.error > 3)
			viewer_page->pdf_document_page->thread |= 4;

		if (render_response.error == 0 || render_response.error > 4)
			viewer_page->pdf_document_page->thread |= 8;

		if (!(viewer_page->thread & 2)
				&& (render_response.error == 0 || render_response.error > 5)) {
			gint x_pos = 0;
			gint width = 0;

			//Kein mutex erforderlich, weil nur Zugriff, wenn schon gerendert
			//Dann ist aber ausgeschlossen, daß ein thread auf viewer_page->pixbuf_page zugreift
			gtk_widget_get_size_request(pdfv->layout, &width, NULL);
			x_pos = (width
					- (viewer_page->crop.x1 - viewer_page->crop.x0) * pdfv->zoom
							/ 100) / 2;

			if (viewer_page->image_page) { //nach Zoom oder Drehen: Widget weiterverwenden
				gtk_image_set_from_pixbuf(GTK_IMAGE(viewer_page->image_page),
						viewer_page->pixbuf_page);
				gtk_layout_move(GTK_LAYOUT(pdfv->layout), viewer_page->image_page, x_pos,
						viewer_page->y_pos);
			} else {
				viewer_page->image_page = gtk_image_new_from_pixbuf(
						viewer_page->pixbuf_page);
				gtk_widget_show(viewer_page->image_page);
				g_signal_connect_after(viewer_page->image_page, "draw",
						G_CALLBACK(viewer_draw_image_page), viewer_page);

				gtk_layout_put(GTK_LAYOUT(pdfv->layout),
						GTK_WIDGET(viewer_page->image_page), x_pos,
						viewer_page->y_pos);
			}
			//g_clear_object() statt g_object_unref(): GtkImage hält seine
			//eigene Referenz, viewer_page->pixbuf_page danach auf NULL
			//setzen statt als dangling Pointer stehen zu lassen
			g_clear_object(&viewer_page->pixbuf_page);

			viewer_page->thread |= 2; //bit 2: image gerendert
		}

		if (!(viewer_page->thread & 4) && render_response.error == 0) {
			//tree_thumb
			GtkTreeIter iter = { 0 };

			viewer_get_iter_thumb(pdfv, render_response.page_pv, &iter);
			gtk_list_store_set(GTK_LIST_STORE(
					gtk_tree_view_get_model(GTK_TREE_VIEW(pdfv->tree_thumb))),
					&iter, 0, viewer_page->pixbuf_thumb, -1);
			//g_clear_object() statt g_object_unref(): GtkListStore hält
			//seine eigene Referenz, viewer_page->pixbuf_thumb danach auf
			//NULL setzen statt als dangling Pointer stehen zu lassen
			g_clear_object(&viewer_page->pixbuf_thumb);

			viewer_page->thread |= 4; //bit 2: thumb gerendert
		}

		g_array_remove_index_fast(pdfv->arr_rendered, idx);
		idx--;

		pdfv->count_active_thread--;
	}

	if (protect)
		g_mutex_unlock(&pdfv->mutex_arr_rendered);

	return;
}

void viewer_render_wait_for_transfer(PdfDocumentPage* pdf_document_page) {
	while (pdf_document_page->thread & 1)
		viewer_render_transfer_rendered(pdf_document_page->thread_pv, TRUE);

	return;
}

void viewer_close_thread_pool_and_transfer(PdfViewer *pdfv) {
	if (pdfv->thread_pool_page) {
		/* g_thread_pool_free() mit immediate=TRUE verwirft alle noch nicht
		 * einem Worker zugeteilten (wartenden) Tasks - für jede davon wurde
		 * count_active_thread aber bereits beim Einreihen (g_thread_pool_push())
		 * hochgezählt und wird nie mehr heruntergezählt, da nie ein
		 * RenderResponse dafür entsteht. Vor dem Free abfangen. */
		guint unprocessed = g_thread_pool_unprocessed(pdfv->thread_pool_page);

		g_thread_pool_free(pdfv->thread_pool_page, TRUE, TRUE);
		pdfv->thread_pool_page = NULL;

		pdfv->count_active_thread -= (gint) unprocessed;
	}

	viewer_render_transfer_rendered(pdfv, FALSE);

	/* Für die verworfenen Tasks kommt nie eine Antwort: ihre Flags (Seite wird gerendert, Viewer)
	 * würden sonst für immer gesetzt bleiben und jedes Warten darauf endlos laufen lassen. Nach
	 * dem Transfer gehört jedes noch gesetzte Flag dieses Viewers zu einem verworfenen Task. */
	for (guint i = 0; i < pdfv->arr_pages->len; i++) {
		ViewerPageNew *viewer_page = g_ptr_array_index(pdfv->arr_pages, i);

		viewer_page->thread &= ~1;
		if (viewer_page->pdf_document_page->thread_pv == pdfv) {
			viewer_page->pdf_document_page->thread &= ~1;
			viewer_page->pdf_document_page->thread_pv = NULL;
		}
	}

	return;
}

void viewer_render_response_free(gpointer data) {
	RenderResponse *render_response = (RenderResponse*) data;

	g_free(render_response->error_message);

	return;
}

static gboolean viewer_render_check(gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;

	/* Immer mit Lock: g_thread_pool_unprocessed() zählt nur noch nicht
	 * gestartete Tasks in der Warteschlange, nicht die bis zu 3 parallel
	 * laufenden Worker (g_thread_pool_new(..., 3, ...)). Bei leerer
	 * Warteschlange, aber (noch) laufendem Worker, würde ohne Lock
	 * pv->arr_rendered hier (Hauptthread) gleichzeitig mit dem unter Lock
	 * anhängenden Worker (viewer_render_page()) gelesen/verändert -
	 * Daten-Race auf einem GArray. Der Overhead eines unumkämpften Mutex
	 * ist demgegenüber vernachlässigbar. */
	viewer_render_transfer_rendered(pv, TRUE);

	if (pv->count_active_thread == 0) {
		pv->idle_source = 0;
		return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}

typedef struct {
	fz_context *ctx;
	fz_pixmap *pixmap;
} ViewerPixbufPrivate;

static void viewer_render_pixbuf_finalize(guchar *pixels, gpointer data) {
	ViewerPixbufPrivate *viewer_pixbuf_priv = (ViewerPixbufPrivate*) data;

	fz_drop_pixmap(viewer_pixbuf_priv->ctx, viewer_pixbuf_priv->pixmap);

	g_free(viewer_pixbuf_priv);

	return;
}

static GdkPixbuf*
viewer_render_pixbuf_from_pixmap(fz_context *ctx, fz_pixmap *pixmap) {
	GdkPixbuf *pixbuf = NULL;
	ViewerPixbufPrivate *viewer_pixbuf_priv = NULL;

	viewer_pixbuf_priv = g_malloc0(sizeof(ViewerPixbufPrivate));

	viewer_pixbuf_priv->ctx = ctx;

	viewer_pixbuf_priv->pixmap = pixmap;

	pixbuf = gdk_pixbuf_new_from_data(pixmap->samples, GDK_COLORSPACE_RGB,
			FALSE, 8, pixmap->w, pixmap->h, pixmap->stride,
			viewer_render_pixbuf_finalize, viewer_pixbuf_priv);

	return pixbuf;
}

static gint viewer_render_thumb(fz_context *ctx, ViewerPageNew *viewer_page,
		GError **error) {
	fz_pixmap *pixmap = NULL;
	GdkPixbuf *pixbuf = NULL;

	fz_matrix transform = fz_scale(0.15, 0.15);

	fz_rect rect = fz_transform_rect(viewer_page->crop, transform);
	fz_irect irect = fz_round_rect(rect);

//per draw-device to pixmap
	fz_try( ctx )
		pixmap = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), irect, NULL,
				0);
	fz_catch(ctx)
		ERROR_PDF

	fz_try( ctx)
		fz_clear_pixmap_with_value(ctx, pixmap, 255);
	fz_catch( ctx ) {
		fz_drop_pixmap(ctx, pixmap);
		ERROR_PDF
	}

	fz_device *draw_device = NULL;
	fz_try( ctx )
		draw_device = fz_new_draw_device(ctx, fz_identity, pixmap);
	fz_catch	( ctx ) {
		fz_drop_pixmap(ctx, pixmap);
		ERROR_PDF
	}

	fz_try( ctx )
		fz_run_display_list(ctx, viewer_page->pdf_document_page->display_list,
				draw_device, transform, rect, NULL);
	fz_always	( ctx ) {
		fz_close_device(ctx, draw_device);
		fz_drop_device(ctx, draw_device);
	}fz_catch( ctx ) {
		fz_drop_pixmap(ctx, pixmap);
		ERROR_PDF
	}

	pixbuf = viewer_render_pixbuf_from_pixmap(
			zond_pdf_document_get_ctx(viewer_page->pdf_document_page->document),
			pixmap);
	if (!pixbuf) {
		fz_drop_pixmap(ctx, pixmap);
		g_set_error(error, SOND_ERROR, 0,
				"Bei Aufruf render_pixbuf_new_from_pixmap: Out of memory");

		return -1;
	}

	viewer_page->pixbuf_thumb = pixbuf;

	return 0;
}

static gint viewer_render_pixmap(fz_context *ctx, ViewerPageNew *viewer_page,
		gdouble zoom, GError **error) {
	fz_pixmap *pixmap = NULL;
	GdkPixbuf *pixbuf = NULL;

	fz_matrix transform = fz_scale(zoom / 100, zoom / 100);

	fz_rect rect = fz_transform_rect(viewer_page->crop, transform);
	fz_irect irect = fz_round_rect(rect);

	//per draw-device to pixmap
	fz_try( ctx )
		pixmap = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), irect, NULL,
				0);
	fz_catch( ctx )
		ERROR_PDF

	fz_try( ctx)
		fz_clear_pixmap_with_value(ctx, pixmap, 255);
	fz_catch( ctx ) {
		fz_drop_pixmap(ctx, pixmap);
		ERROR_PDF
	}

	fz_device *draw_device = NULL;
	fz_try( ctx )
		draw_device = fz_new_draw_device(ctx, fz_identity, pixmap);
	fz_catch( ctx ) {
		fz_drop_pixmap(ctx, pixmap);
		ERROR_PDF
	}

	fz_try( ctx )
		fz_run_display_list(ctx, viewer_page->pdf_document_page->display_list,
				draw_device, transform, rect, NULL);
	fz_always(ctx) {
		fz_close_device(ctx, draw_device);
		fz_drop_device(ctx, draw_device);
	}
	fz_catch(ctx) {
		fz_drop_pixmap(ctx, pixmap);
		ERROR_PDF
	}

	pixbuf = viewer_render_pixbuf_from_pixmap(
			zond_pdf_document_get_ctx(viewer_page->pdf_document_page->document),
			pixmap);
	if (!pixbuf) {
		fz_drop_pixmap(ctx, pixmap);
		g_set_error(error, SOND_ERROR, 0,
				"Bei Aufruf render_pixbuf_new_from_pixmap: Out of memory");

		return -1;
	}

	viewer_page->pixbuf_page = pixbuf;

	return 0;
}

static gint viewer_render_stext_page_from_display_list(fz_context *ctx,
		PdfDocumentPage *pdf_document_page, GError **error) {
	fz_stext_page *stext_page = NULL;
	fz_device *s_t_device = NULL;

	fz_stext_options opts = { FZ_STEXT_DEHYPHENATE };

	fz_try( ctx )
		stext_page = fz_new_stext_page(ctx, pdf_document_page->rect);
fz_catch	( ctx )
		ERROR_PDF

	//structured text-device
	fz_try( ctx )
		s_t_device = fz_new_stext_device(ctx, stext_page, &opts);
fz_catch	( ctx ) {
		fz_drop_stext_page(ctx, stext_page);
		ERROR_PDF
	}

	//und durchs stext-device laufen lassen
	fz_try( ctx )
		fz_run_display_list(ctx, pdf_document_page->display_list, s_t_device,
				fz_identity, pdf_document_page->rect, NULL);
fz_always	( ctx ) {
		fz_close_device(ctx, s_t_device);
		fz_drop_device(ctx, s_t_device);
	}fz_catch( ctx ) {
		fz_drop_stext_page(ctx, stext_page);
		ERROR_PDF
	}

	pdf_document_page->stext_page = stext_page;

	return 0;
}

static gint viewer_render_stext_page_from_page(
		PdfDocumentPage *pdf_document_page, GError **error) {
	fz_device *s_t_device = NULL;
	fz_stext_page *stext_page = NULL;

	fz_stext_options opts = { FZ_STEXT_DEHYPHENATE };

	fz_context *ctx = zond_pdf_document_get_ctx(
			pdf_document_page->document);

	fz_try(ctx)
		stext_page = fz_new_stext_page(ctx, pdf_document_page->rect);
	fz_catch(ctx)
		ERROR_PDF

	//structured text-device
	fz_try(ctx)
		s_t_device = fz_new_stext_device(ctx, stext_page, &opts);
	fz_catch(ctx) {
		fz_drop_stext_page(ctx, stext_page);
		ERROR_PDF
	}

	//doc-lock muß gesetzt werden, da _load_page auf document zugreift
	zond_pdf_document_mutex_lock(pdf_document_page->document);

	//page durchs list-device laufen lassen
	fz_try( ctx )
		pdf_run_page(ctx, pdf_document_page->page, s_t_device, fz_identity, NULL);
	fz_always(ctx) {
		zond_pdf_document_mutex_unlock(pdf_document_page->document);
		fz_close_device(ctx, s_t_device);
		fz_drop_device(ctx, s_t_device);
	}
	fz_catch( ctx ) {
		fz_drop_stext_page(ctx, stext_page);
		ERROR_PDF
	}

	pdf_document_page->stext_page = stext_page;

	return 0;
}

gint viewer_render_stext_page_fast(fz_context *ctx,
		PdfDocumentPage *pdf_document_page, GError **error) {
	//thread für Seite gestartet?
	viewer_render_wait_for_transfer(pdf_document_page);

	if (pdf_document_page->thread & 8)
		return 0;

	//page oder display_list nicht geladen
	if (!(pdf_document_page->thread & 4)) {
		gint rc = 0;

		if (!(pdf_document_page->thread & 2)) {
			gint rc = 0;

			zond_pdf_document_mutex_lock(pdf_document_page->document);
			rc = zond_pdf_document_load_page(pdf_document_page, ctx, error);
			zond_pdf_document_mutex_unlock(pdf_document_page->document);
			if (rc)
				return -1;

			pdf_document_page->thread |= 2;
		}

		rc = viewer_render_stext_page_from_page(pdf_document_page, error);
		if (rc)
			return -1;

	} else //display_list fertig
	{
		gint rc = 0;

		rc = viewer_render_stext_page_from_display_list(ctx, pdf_document_page,
				error);
		if (rc)
			return -1;
	}

	pdf_document_page->thread |= 8;

	return 0;
}

static gint viewer_render_display_list(fz_context *ctx,
		PdfDocumentPage *pdf_document_page, GError **error) {
	fz_display_list *display_list = NULL;
	fz_device *list_device = NULL;

	fz_try( ctx )
		display_list = fz_new_display_list(ctx, pdf_document_page->rect);
	fz_catch(ctx)
		ERROR_PDF

	//list_device für die Seite erzeugen
	fz_try(ctx)
		list_device = fz_new_list_device(ctx, display_list);
	fz_catch(ctx) {
		fz_drop_display_list(ctx, display_list);
		ERROR_PDF
	}

	zond_pdf_document_mutex_lock(pdf_document_page->document);
	//page durchs list-device laufen lassen
	fz_try( ctx )
		pdf_run_page(ctx, pdf_document_page->page, list_device, fz_identity,
				NULL);
	fz_always(ctx) {
		zond_pdf_document_mutex_unlock(pdf_document_page->document);
		fz_close_device(ctx, list_device);
		fz_drop_device(ctx, list_device);
	}fz_catch(ctx) {
		fz_drop_display_list(ctx, display_list);
		ERROR_PDF
	}

	pdf_document_page->display_list = display_list;

	return 0;
}

//Antwort des Workers für den Hauptthread bereitstellen
static void render_antwort_anhaengen(PdfViewer *pv, RenderResponse *render_response) {
	g_mutex_lock(&pv->mutex_arr_rendered);
	g_array_append_val(pv->arr_rendered, *render_response);
	g_mutex_unlock(&pv->mutex_arr_rendered);

	return;
}

/* Führt die über thread_data angeforderten Schritte der Reihe nach aus (1 Seite laden, 2 Displayliste,
 * 4 Textseite, 8 Pixmap, 16 Vorschaubild). Liefert 0 oder den Fehlercode des gescheiterten Schritts
 * (2 bis 6). */
static gint render_schritte(PdfViewer *pv, ViewerPageNew *viewer_page, fz_context *ctx,
		gint thread_data, GError **error) {
	gint rc = 0;

	if (thread_data & 1) {
		zond_pdf_document_mutex_lock(viewer_page->pdf_document_page->document);
		rc = zond_pdf_document_load_page(viewer_page->pdf_document_page, ctx, error);
		zond_pdf_document_mutex_unlock(viewer_page->pdf_document_page->document);
		if (rc == -1)
			return 2;
	}

	if (thread_data & 2) {
		rc = viewer_render_display_list(ctx, viewer_page->pdf_document_page, error);
		if (rc == -1)
			return 3;
	}

	if (thread_data & 4) {
		rc = viewer_render_stext_page_from_display_list(ctx, viewer_page->pdf_document_page, error);
		if (rc == -1)
			return 4;
	}

	if (thread_data & 8) {
		rc = viewer_render_pixmap(ctx, viewer_page, pv->zoom, error);
		if (rc == -1)
			return 5;
	}

	if (thread_data & 16) {
		rc = viewer_render_thumb(ctx, viewer_page, error);
		if (rc == -1)
			return 6;
	}

	return 0;
}

/* Worker des Pools. Die Seite kommt als Index in pv->arr_pages (in thread_data), der Worker löst
 * ihn ohne Sperre auf. Das ist nur sicher, solange sich pv->arr_pages und die Seiten nicht ändern,
 * während Tasks warten oder laufen: Wer das Array ändert oder eine Seite verändert oder freigibt
 * (Drehen, Löschen, Einfügen, Schließen), hält vorher den Pool des Viewers an
 * (viewer_close_thread_pool_and_transfer). */
void viewer_render_page(gpointer data, gpointer user_data) {
	GError *error = NULL;
	ViewerPageNew *viewer_page = NULL;
	fz_context *ctx = NULL;
	gint thread_data = 0;
	gint fehler = 0;
	RenderResponse render_response = { 0 };

	PdfViewer *pv = (PdfViewer*) user_data;

	thread_data = GPOINTER_TO_INT(data);
	render_response.page_pv = thread_data >> 5;
	viewer_page = g_ptr_array_index(pv->arr_pages, render_response.page_pv);

	ctx = fz_clone_context(
			zond_pdf_document_get_ctx(
					viewer_page->pdf_document_page->document));
	if (!ctx) {
		render_response.error = 1;
		render_response.error_message = g_strdup_printf(
				"Bei Aufruf %s:\nfz_clone_context: Out of memory", __func__);
		render_antwort_anhaengen(pv, &render_response);

		return;
	}

	fehler = render_schritte(pv, viewer_page, ctx, thread_data, &error);
	if (fehler) {
		render_response.error = fehler;
		render_response.error_message = g_strconcat("Bei Aufruf ", __func__,
				":\n", error->message, NULL);
		g_error_free(error);
	}

	render_antwort_anhaengen(pv, &render_response);

	fz_drop_context(ctx);

	return;
}

void viewer_render_thread(PdfViewer *pv, gint page) {
	ViewerPageNew *viewer_page = NULL;
	gint thread_data = 0;
	GError *error = NULL;

	viewer_page = g_ptr_array_index(pv->arr_pages, page);

	if (viewer_page->thread == 6
			&& viewer_page->pdf_document_page->thread == 14)
		return; //alles fertig, nix zu tun

	if (viewer_page->thread & 1)
		return; //Seite läuft schon

	if (viewer_page->pdf_document_page->thread & 1) { //pdf_document_page wird gerade in anderem viewer gerendert
		if (viewer_page->thread == 6)
			return; //im viewer alles gerendert, pdf_document_page läuft-> nix zu tun
		else
			while (viewer_page->pdf_document_page->thread & 1)
				viewer_render_transfer_rendered(
						viewer_page->pdf_document_page->thread_pv,
						TRUE);
	}

	if (!pv->idle_source)
		pv->idle_source = g_idle_add(G_SOURCE_FUNC(viewer_render_check), pv);

	if (!pv->thread_pool_page) {
		GError *error = NULL;

		if (!(pv->thread_pool_page = g_thread_pool_new(
				(GFunc) viewer_render_page, pv, 3, FALSE, &error))) {
			LOG_WARN("Thread-Pool kann nicht erzeugt werden: %s",
					error->message);
			g_error_free(error);

			return;
		}
	}

	if (!(viewer_page->pdf_document_page->thread & 2))
		thread_data += 1;
	if (!(viewer_page->pdf_document_page->thread & 4))
		thread_data += 2;
	if (!(viewer_page->pdf_document_page->thread & 8))
		thread_data += 4;
	if (!(viewer_page->thread & 2))
		thread_data += 8;
	if (!(viewer_page->thread & 4))
		thread_data += 16;

	thread_data += page << 5;

	if (!g_thread_pool_push(pv->thread_pool_page, GINT_TO_POINTER(thread_data),
			&error)) {
		LOG_WARN("Fehler bei Start Thread für Seite %i: %s", page,
				error->message);
		g_error_free(error);

		return;
	}
	g_thread_pool_move_to_front(pv->thread_pool_page,
			GINT_TO_POINTER(thread_data));
	pv->count_active_thread++;

	viewer_page->thread |= 1; //bit 1: thread gestartet
	viewer_page->pdf_document_page->thread |= 1;
	viewer_page->pdf_document_page->thread_pv = pv;

	return;
}

static gint viewer_render_get_visible_thumbs(PdfViewer *pv, gint *start, gint *end) {
	GtkTreePath *path_start = NULL;
	GtkTreePath *path_end = NULL;
	gint *index_start = NULL;
	gint *index_end = NULL;

	if (pv->arr_pages->len == 0)
		return 1;

	if (!gtk_tree_view_get_visible_range(GTK_TREE_VIEW(pv->tree_thumb),
			&path_start, &path_end))
		return 1;

	index_start = gtk_tree_path_get_indices(path_start);
	index_end = gtk_tree_path_get_indices(path_end);

	*start = index_start[0];
	*end = index_end[0];

	gtk_tree_path_free(path_start);
	gtk_tree_path_free(path_end);

	return 0;
}

void cb_viewer_render_visible_thumbs(PdfViewer *pv) {
	gint rc = 0;
	gint start = 0;
	gint end = 0;

	if (!gtk_widget_get_visible(pv->swindow_tree))
		return;

	rc = viewer_render_get_visible_thumbs(pv, &start, &end);
	if (rc == 0)
		for (gint i = start; i <= end; i++)
			viewer_render_thread(pv, i);

	return;
}

void viewer_render_get_visible_pages(PdfViewer *pv, gint *von,
		gint *bis) {
	gdouble value = gtk_adjustment_get_value(pv->v_adj);
	gdouble size = gtk_adjustment_get_page_size(pv->v_adj);

	*von = -1;
	*bis = -1;

	gdouble v_oben = 0.0;
	gdouble v_unten = -10.0;

	while (((value + size) > v_oben)
			&& ((*bis) < ((gint) pv->arr_pages->len - 1))) {
		ViewerPageNew *viewer_page = NULL;

		(*bis)++;
		viewer_page = g_ptr_array_index(pv->arr_pages, *bis);

		v_oben += ((viewer_page->crop.y1 - viewer_page->crop.y0) * pv->zoom
				/ 100) + 10;

		if (value > v_unten)
			(*von)++;
		v_unten += ((viewer_page->crop.y1 - viewer_page->crop.y0) * pv->zoom
				/ 100) + 10;
	}

	//Bildlauf-Bereich noch ohne Größe: mindestens die erste Seite
	if (pv->arr_pages->len > 0) {
		if (*bis < 0)
			*bis = 0;
		if (*von < 0)
			*von = 0;
	}

	return;
}

void cb_viewer_render_visible_pages(PdfViewer* pv) {
	GtkTreePath *path = NULL;
	ViewerPageNew *viewer_page = NULL;
	const gchar *path_doc = NULL;
	const gchar *file = NULL;
	gchar *dir = NULL;
	gint erste = 0;
	gint letzte = 0;
	gchar *title = NULL;

	if (pv->arr_pages->len == 0)
		return;

	viewer_render_get_visible_pages(pv, &erste, &letzte);

	viewer_page = g_ptr_array_index(pv->arr_pages, erste);

	//in Headerbar angezeigte Datei und Seite anzeigen
	SondFilePartPDF* sfp_pdf = zond_pdf_document_get_sfp_pdf(
			viewer_page->pdf_document_page->document);
	path_doc = sond_file_part_get_path(SOND_FILE_PART(sfp_pdf));
	file = g_strrstr(path_doc, "/");
	if (file)
		file++; // "/" entfernen
	else
		file = path_doc;
	dir = g_strndup(path_doc, strlen(path_doc) - strlen(file));

	title = g_strdup_printf("%s [Seite %i]", file,
			viewer_page->pdf_document_page->page_akt + 1);
	gtk_header_bar_set_title(GTK_HEADER_BAR(pv->headerbar), title);
	g_free(title);

	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(pv->headerbar), dir);
	g_free(dir);

	//Seite von oberen - unterem Rand im entry anzeigen
	gchar *text = g_strdup_printf("%i-%i", erste + 1, letzte + 1);
	gtk_entry_set_text(GTK_ENTRY(pv->entry), text);
	g_free(text);

	//rendern in Auftrag
	for (gint i = letzte; i >= erste; i--)
		viewer_render_thread(pv, i);

	//thumb-Leiste anpassen
	path = gtk_tree_path_new_from_indices(erste, -1);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(pv->tree_thumb), path, NULL,
	FALSE, 0, 0);
	gtk_tree_path_free(path);

	return;
}
