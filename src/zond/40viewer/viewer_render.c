#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <mupdf/fitz.h>
#include <cairo.h>

#include "../../sond_log_and_error.h"
#include "../../sond_fileparts.h"
#include "../../sond_pdf_helper.h"
#include "../../misc.h"

#include "../zond_pdf_document.h"

#include "viewer.h"


static void viewer_render_mark_quad(cairo_t *cr, fz_quad quad,
		fz_matrix transform) {
	fz_rect rect = fz_transform_rect(fz_rect_from_quad(quad), transform);

	float x = rect.x0;
	float y = rect.y0;
	float width = rect.x1 - x;
	float heigth = rect.y1 - y;

	cairo_rectangle(cr, x, y, width, heigth);
	cairo_set_source_rgba(cr, 0, .1, .8, 0.5);
	cairo_fill(cr);
}

static gboolean viewer_draw_image_page(GtkWidget *image, cairo_t *cr,
		gpointer data) {
	fz_matrix transform = { 0, };

	ViewerPageNew *viewer_page = (ViewerPageNew*) data;

	if (!gtk_widget_is_visible(image))
		return FALSE;

	transform = fz_translate(0.0, -viewer_page->crop.y0);
	transform = fz_post_scale(transform, viewer_page->pdfv->zoom / 100,
			viewer_page->pdfv->zoom / 100);

	//wenn annot angeclickt wurde
	if (viewer_page->pdfv->clicked_annot && //selbe Seite
			viewer_page->pdfv->clicked_annot->pdf_document_page
			== viewer_page->pdf_document_page) {
		if (viewer_page->pdfv->clicked_annot->annot.type == PDF_ANNOT_HIGHLIGHT
				|| viewer_page->pdfv->clicked_annot->annot.type
						== PDF_ANNOT_UNDERLINE) {
			GArray *arr_quads =
					viewer_page->pdfv->clicked_annot->annot.annot_text_markup.arr_quads;

			for (gint i = 0; i < arr_quads->len; i++) {
				fz_quad quad = g_array_index(arr_quads, fz_quad, i);
				quad = fz_transform_quad(quad, transform);
				cairo_move_to(cr, quad.ul.x, quad.ul.y);
				cairo_line_to(cr, quad.ur.x, quad.ur.y);
				cairo_line_to(cr, quad.lr.x, quad.lr.y);
				cairo_line_to(cr, quad.ll.x, quad.ll.y);
				cairo_line_to(cr, quad.ul.x, quad.ul.y);
				cairo_set_source_rgb(cr, 0, 1, 0);
				cairo_stroke(cr);
			}
		} else if (viewer_page->pdfv->clicked_annot->annot.type == PDF_ANNOT_TEXT) {
			fz_rect rect = { 0, };

			rect = fz_transform_rect(
					viewer_page->pdfv->clicked_annot->annot.annot_text.rect,
					transform);

			cairo_move_to(cr, rect.x0, rect.y0);
			cairo_line_to(cr, rect.x1, rect.y0);
			cairo_line_to(cr, rect.x1, rect.y1);
			cairo_line_to(cr, rect.x0, rect.y1);
			cairo_line_to(cr, rect.x0, rect.y0);
			cairo_set_source_rgb(cr, 0, 1, 0);
			cairo_stroke(cr);
		}
	} else //ansonsten etwaige highlights zeichnen
	{
		gint i = 0;
		while (viewer_page->pdfv->highlight.page[i] >= 0) {
			if (viewer_page
					== g_ptr_array_index(viewer_page->pdfv->arr_pages,
							viewer_page->pdfv->highlight.page[i]))
				viewer_render_mark_quad(cr, viewer_page->pdfv->highlight.quad[i],
						transform);

			i++;
		}
	}

	return FALSE;
}

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
			viewer_page->image_page = gtk_image_new_from_pixbuf(
					viewer_page->pixbuf_page);
			gtk_widget_show(viewer_page->image_page);
			g_signal_connect_after(viewer_page->image_page, "draw",
					G_CALLBACK(viewer_draw_image_page), viewer_page);

			gtk_widget_get_size_request(pdfv->layout, &width, NULL);
			x_pos = (width
					- (viewer_page->crop.x1 - viewer_page->crop.x0) * pdfv->zoom
							/ 100) / 2;

			gtk_layout_put(GTK_LAYOUT(pdfv->layout),
					GTK_WIDGET(viewer_page->image_page), x_pos,
					viewer_page->y_pos);
			g_object_unref(viewer_page->pixbuf_page);

			viewer_page->thread |= 2; //bit 2: image gerendert
		}

		if (!(viewer_page->thread & 4) && render_response.error == 0) {
			//tree_thumb
			GtkTreeIter iter = { 0 };

			viewer_get_iter_thumb(pdfv, render_response.page_pv, &iter);
			gtk_list_store_set(GTK_LIST_STORE(
					gtk_tree_view_get_model(GTK_TREE_VIEW(pdfv->tree_thumb))),
					&iter, 0, viewer_page->pixbuf_thumb, -1);
			g_object_unref(viewer_page->pixbuf_thumb);

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
		g_thread_pool_free(pdfv->thread_pool_page, TRUE, TRUE);
		pdfv->thread_pool_page = NULL;
	}

	viewer_render_transfer_rendered(pdfv, FALSE);

	return;
}

void viewer_render_response_free(gpointer data) {
	RenderResponse *render_response = (RenderResponse*) data;

	g_free(render_response->error_message);

	return;
}

static gboolean viewer_render_check(gpointer data) {
	gboolean protect = FALSE;

	PdfViewer *pv = (PdfViewer*) data;

	if (pv->thread_pool_page
			&& g_thread_pool_unprocessed(pv->thread_pool_page) != 0)
		protect = TRUE;
	viewer_render_transfer_rendered(pv, protect);

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

	viewer_pixbuf_priv = g_try_malloc0(sizeof(ViewerPixbufPrivate));
	if (!viewer_pixbuf_priv)
		return NULL;

	viewer_pixbuf_priv->ctx = ctx;

	viewer_pixbuf_priv->pixmap = pixmap;

	pixbuf = gdk_pixbuf_new_from_data(pixmap->samples, GDK_COLORSPACE_RGB,
			FALSE, 8, pixmap->w, pixmap->h, pixmap->stride,
			viewer_render_pixbuf_finalize, viewer_pixbuf_priv);

	return pixbuf;
}

static gint viewer_render_thumb(fz_context *ctx, ViewerPageNew *viewer_page,
		gchar **errmsg) {
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
		ERROR_MUPDF("fz_new_pixmap_with_bbox")

	fz_try( ctx)
		fz_clear_pixmap_with_value(ctx, pixmap, 255);
	fz_catch( ctx ) {
		fz_drop_pixmap(ctx, pixmap);
		ERROR_MUPDF("fz_clear_pixmap")
	}

	fz_device *draw_device = NULL;
	fz_try( ctx )
		draw_device = fz_new_draw_device(ctx, fz_identity, pixmap);
	fz_catch	( ctx ) {
		fz_drop_pixmap(ctx, pixmap);
		ERROR_MUPDF("fz_new_draw_device")
	}

	fz_try( ctx )
		fz_run_display_list(ctx, viewer_page->pdf_document_page->display_list,
				draw_device, transform, rect, NULL);
	fz_always	( ctx ) {
		fz_close_device(ctx, draw_device);
		fz_drop_device(ctx, draw_device);
	}fz_catch( ctx ) {
		fz_drop_pixmap(ctx, pixmap);
		ERROR_MUPDF("fz_run_display_list")
	}

	pixbuf = viewer_render_pixbuf_from_pixmap(
			zond_pdf_document_get_ctx(viewer_page->pdf_document_page->document),
			pixmap);
	if (!pixbuf) {
		fz_drop_pixmap(ctx, pixmap);
		if (errmsg)
			*errmsg = g_strdup("Bei Aufruf render_pixbuf_new_from_pixmap:\n"
					"Out of memory");

		return -1;
	}

	viewer_page->pixbuf_thumb = pixbuf;

	return 0;
}

static gint viewer_render_pixmap(fz_context *ctx, ViewerPageNew *viewer_page,
		gdouble zoom, gchar **errmsg) {
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
		ERROR_MUPDF("fz_new_pixmap_with_bbox")

	fz_try( ctx)
		fz_clear_pixmap_with_value(ctx, pixmap, 255);
	fz_catch( ctx ) {
		fz_drop_pixmap(ctx, pixmap);
		ERROR_MUPDF("fz_clear_pixmap")
	}

	fz_device *draw_device = NULL;
	fz_try( ctx )
		draw_device = fz_new_draw_device(ctx, fz_identity, pixmap);
	fz_catch( ctx ) {
		fz_drop_pixmap(ctx, pixmap);
		ERROR_MUPDF("fz_new_draw_device")
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
		ERROR_MUPDF("fz_run_display_list")
	}

	pixbuf = viewer_render_pixbuf_from_pixmap(
			zond_pdf_document_get_ctx(viewer_page->pdf_document_page->document),
			pixmap);
	if (!pixbuf) {
		fz_drop_pixmap(ctx, pixmap);
		if (errmsg)
			*errmsg = g_strdup("Bei Aufruf render_pixbuf_new_from_pixmap:\n"
					"Out of memory");

		return -1;
	}

	viewer_page->pixbuf_page = pixbuf;

	return 0;
}

static gint viewer_render_stext_page_from_display_list(fz_context *ctx,
		PdfDocumentPage *pdf_document_page, gchar **errmsg) {
	fz_stext_page *stext_page = NULL;
	fz_device *s_t_device = NULL;

	fz_stext_options opts = { FZ_STEXT_DEHYPHENATE };

	fz_try( ctx )
		stext_page = fz_new_stext_page(ctx, pdf_document_page->rect);
fz_catch	( ctx )
		ERROR_MUPDF("fz_new_stext_page")

	//structured text-device
	fz_try( ctx )
		s_t_device = fz_new_stext_device(ctx, stext_page, &opts);
fz_catch	( ctx ) {
		fz_drop_stext_page(ctx, stext_page);
		ERROR_MUPDF("fz_new_stext_device")
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
		ERROR_MUPDF("fz_run_display_list")
	}

	pdf_document_page->stext_page = stext_page;

	return 0;
}

static gint viewer_render_stext_page_from_page(
		PdfDocumentPage *pdf_document_page, gchar **errmsg) {
	fz_device *s_t_device = NULL;
	fz_stext_page *stext_page = NULL;

	fz_stext_options opts = { FZ_STEXT_DEHYPHENATE };

	fz_context *ctx = zond_pdf_document_get_ctx(
			pdf_document_page->document);

	fz_try(ctx)
		stext_page = fz_new_stext_page(ctx, pdf_document_page->rect);
	fz_catch(ctx)
		ERROR_MUPDF("fz_new_stext_page")

	//structured text-device
	fz_try(ctx)
		s_t_device = fz_new_stext_device(ctx, stext_page, &opts);
	fz_catch(ctx) {
		fz_drop_stext_page(ctx, stext_page);
		ERROR_MUPDF("fz_new_stext_device")
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
	fz_catch( ctx )
		ERROR_MUPDF("pdf_run_page")

	pdf_document_page->stext_page = stext_page;

	return 0;
}

gint viewer_render_stext_page_fast(fz_context *ctx,
		PdfDocumentPage *pdf_document_page, gchar **errmsg) {
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
			rc = zond_pdf_document_load_page(pdf_document_page, ctx, errmsg);
			zond_pdf_document_mutex_unlock(pdf_document_page->document);
			if (rc)
				ERROR_S

			pdf_document_page->thread |= 2;
		}

		rc = viewer_render_stext_page_from_page(pdf_document_page, errmsg);
		if (rc)
			ERROR_S

	} else //display_list fertig
	{
		gint rc = 0;

		rc = viewer_render_stext_page_from_display_list(ctx, pdf_document_page,
				errmsg);
		if (rc)
			ERROR_S
	}

	pdf_document_page->thread |= 8;

	return 0;
}

static gint viewer_render_display_list(fz_context *ctx,
		PdfDocumentPage *pdf_document_page, gchar **errmsg) {
	fz_display_list *display_list = NULL;
	fz_device *list_device = NULL;

	fz_try( ctx )
		display_list = fz_new_display_list(ctx, pdf_document_page->rect);
	fz_catch(ctx)
		ERROR_MUPDF("fz_new_display_list")

	//list_device für die Seite erzeugen
	fz_try(ctx)
		list_device = fz_new_list_device(ctx, display_list);
	fz_catch(ctx) {
		fz_drop_display_list(ctx, display_list);
		ERROR_MUPDF("fz_new_list_device")
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
		ERROR_MUPDF("fz_drop_display_list")
	}

	pdf_document_page->display_list = display_list;

	return 0;
}

void viewer_render_page(gpointer data, gpointer user_data) {
	gint rc = 0;
	gchar *errmsg = NULL;
	ViewerPageNew *viewer_page = NULL;
	fz_context *ctx = NULL;
	gint thread_data = 0;
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
		render_response.error_message = g_strconcat("Bei Aufruf ", __func__,
				":\n", errmsg, NULL);
		g_free(errmsg);

		g_mutex_lock(&pv->mutex_arr_rendered);
		g_array_append_val(pv->arr_rendered, render_response);
		g_mutex_unlock(&pv->mutex_arr_rendered);

		return;
	}

	if (thread_data & 1) {
		gint rc = 0;

		zond_pdf_document_mutex_lock(viewer_page->pdf_document_page->document);
		rc = zond_pdf_document_load_page(viewer_page->pdf_document_page, ctx, &errmsg);
		zond_pdf_document_mutex_unlock(
				viewer_page->pdf_document_page->document);
		if (rc == -1) {
			render_response.error = 2;
			render_response.error_message = g_strconcat("Bei Aufruf ", __func__,
					":\n", errmsg, NULL);
			g_free(errmsg);

			g_mutex_lock(&pv->mutex_arr_rendered);
			g_array_append_val(pv->arr_rendered, render_response);
			g_mutex_unlock(&pv->mutex_arr_rendered);

			return;
		}
	}

	if (thread_data & 2) {
		rc = viewer_render_display_list(ctx, viewer_page->pdf_document_page, &errmsg);
		if (rc == -1) {
			render_response.error = 3;
			render_response.error_message = g_strconcat("Bei Aufruf ", __func__,
					":\n", errmsg, NULL);
			g_free(errmsg);

			g_mutex_lock(&pv->mutex_arr_rendered);
			g_array_append_val(pv->arr_rendered, render_response);
			g_mutex_unlock(&pv->mutex_arr_rendered);

			return;
		}
	}

	if (thread_data & 4) {
		rc = viewer_render_stext_page_from_display_list(ctx,
				viewer_page->pdf_document_page, &errmsg);
		if (rc == -1) {
			render_response.error = 4;
			render_response.error_message = g_strconcat("Bei Aufruf ", __func__,
					":\n", errmsg, NULL);
			g_free(errmsg);

			g_mutex_lock(&pv->mutex_arr_rendered);
			g_array_append_val(pv->arr_rendered, render_response);
			g_mutex_unlock(&pv->mutex_arr_rendered);

			return;
		}
	}

	if (thread_data & 8) {
		gint rc = 0;

		rc = viewer_render_pixmap(ctx, viewer_page, pv->zoom, &errmsg);
		if (rc == -1) {
			render_response.error = 5;
			render_response.error_message = g_strconcat("Bei Aufruf ", __func__,
					":\n", errmsg, NULL);
			g_free(errmsg);

			g_mutex_lock(&pv->mutex_arr_rendered);
			g_array_append_val(pv->arr_rendered, render_response);
			g_mutex_unlock(&pv->mutex_arr_rendered);

			return;
		}
	}

	if (thread_data & 16) {
		gint rc = 0;

		rc = viewer_render_thumb(ctx, viewer_page, &errmsg);
		if (rc == -1) {
			render_response.error = 6;
			render_response.error_message = g_strconcat("Bei Aufruf ", __func__,
					":\n", errmsg, NULL);
			g_free(errmsg);

			g_mutex_lock(&pv->mutex_arr_rendered);
			g_array_append_val(pv->arr_rendered, render_response);
			g_mutex_unlock(&pv->mutex_arr_rendered);

			return;
		}
	}

	g_mutex_lock(&pv->mutex_arr_rendered);
	g_array_append_val(pv->arr_rendered, render_response);
	g_mutex_unlock(&pv->mutex_arr_rendered);

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

static void cb_viewer_render_page_for_printing(GtkPrintOperation *op,
		GtkPrintContext *context, gint page_nr, gpointer user_data) {
	PdfViewer *pdfv = (PdfViewer*) user_data;
	ViewerPageNew *viewer_page = NULL;
	fz_context *ctx = NULL;
	gdouble width = 0;
	gdouble height = 0;
	gdouble zoom_x = 0;
	gdouble zoom_y = 0;
	gdouble zoom = 0;

	viewer_page = g_ptr_array_index(pdfv->arr_pages, page_nr);

	viewer_render_thread(pdfv, page_nr);
	viewer_render_wait_for_transfer(viewer_page->pdf_document_page);

	width = gtk_print_context_get_width(context);
	height = gtk_print_context_get_height(context);

	zoom_x = width / (viewer_page->crop.x1 - viewer_page->crop.x0);
	zoom_y = height / (viewer_page->crop.y1 - viewer_page->crop.y0);
	zoom = (zoom_x <= zoom_y) ? zoom_x : zoom_y;

	ctx = fz_clone_context(zond_pdf_document_get_ctx(viewer_page->pdf_document_page->document));
	if (!ctx) {
		gchar *errmsg = g_strdup_printf("Seite Nr. %i", page_nr);
		display_message(pdfv->vf, "Fehler Drucken ", errmsg,
				" -\n\nfz_context konnte nicht geklont werden", NULL);
		g_free(errmsg);
		return;
	}

	fz_pixmap *pixmap = NULL;
	fz_matrix transform = fz_scale(zoom, zoom);
	fz_rect rect = fz_transform_rect(viewer_page->crop, transform);
	fz_irect irect = fz_round_rect(rect);

	fz_try(ctx)
		pixmap = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), irect, NULL, 0);
	fz_catch(ctx) {
		gchar *errmsg = g_strdup_printf("Seite Nr. %i", page_nr);
		display_message(pdfv->vf, "Fehler Drucken ", errmsg,
				" -\n\nBei Aufruf fz_new_pixmap_with_bbox:\n", fz_caught_message(ctx), NULL);
		g_free(errmsg);
		fz_drop_context(ctx);
		return;
	}

	fz_try(ctx)
		fz_clear_pixmap_with_value(ctx, pixmap, 255);
	fz_catch(ctx) {
		gchar *errmsg = g_strdup_printf("Seite Nr. %i", page_nr);
		display_message(pdfv->vf, "Fehler Drucken ", errmsg,
				" -\n\nBei Aufruf fz_pixmap_with_value:\n", fz_caught_message(ctx), NULL);
		g_free(errmsg);
		fz_drop_context(ctx);
		return;
	}

	fz_device *draw_device = NULL;
	fz_try(ctx)
		draw_device = fz_new_draw_device(ctx, fz_identity, pixmap);
	fz_catch(ctx) {
		fz_drop_pixmap(ctx, pixmap);
		gchar *errmsg = g_strdup_printf("Seite Nr. %i", page_nr);
		display_message(pdfv->vf, "Fehler Drucken ", errmsg,
				" -\n\nBei Aufruf fz_new_draw_device:\n", fz_caught_message(ctx), NULL);
		g_free(errmsg);
		fz_drop_context(ctx);
		return;
	}

	fz_try(ctx)
		fz_run_display_list(ctx, viewer_page->pdf_document_page->display_list,
				draw_device, transform, rect, NULL);
	fz_always(ctx) {
		fz_close_device(ctx, draw_device);
		fz_drop_device(ctx, draw_device);
	}
	fz_catch(ctx) {
		fz_drop_pixmap(ctx, pixmap);
		gchar *errmsg = g_strdup_printf("Seite Nr. %i", page_nr);
		display_message(pdfv->vf, "Fehler Drucken ", errmsg,
				" -\n\nBei Aufruf fz_run_display_list:\n", fz_caught_message(ctx), NULL);
		g_free(errmsg);
		fz_drop_context(ctx);
		return;
	}

	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(pixmap->samples, GDK_COLORSPACE_RGB,
			FALSE, 8, pixmap->w, pixmap->h, pixmap->stride, NULL, NULL);

	cairo_t *cr = gtk_print_context_get_cairo_context(context);
	gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
	g_object_unref(pixbuf);
	fz_drop_pixmap(ctx, pixmap);
	fz_drop_context(ctx);

	cairo_paint(cr);
	cairo_fill(cr);

	return;
}

void cb_viewer_render_print(GtkButton *button, gpointer data) {
	GtkPrintOperation *print = NULL;
	GtkPrintOperationResult res;
	GtkPageSetup *page_setup = NULL;
	GError *error = NULL;
	PdfViewer *pdfv = (PdfViewer*) data;

	print = gtk_print_operation_new();
	gtk_print_operation_set_n_pages(print, pdfv->arr_pages->len);

	page_setup = gtk_page_setup_new();
	gtk_page_setup_set_top_margin(page_setup, 0, GTK_UNIT_POINTS);
	gtk_page_setup_set_bottom_margin(page_setup, 0, GTK_UNIT_POINTS);
	gtk_page_setup_set_left_margin(page_setup, 0, GTK_UNIT_POINTS);
	gtk_page_setup_set_right_margin(page_setup, 0, GTK_UNIT_POINTS);
	gtk_print_operation_set_default_page_setup(print, page_setup);
	g_object_unref(page_setup);

	g_signal_connect(print, "draw_page",
			G_CALLBACK(cb_viewer_render_page_for_printing), pdfv);

	res = gtk_print_operation_run(print,
			GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, GTK_WINDOW(pdfv->vf), &error);
	g_object_unref(print);

	if (res == GTK_PRINT_OPERATION_RESULT_ERROR) {
		display_message(pdfv->vf,
				"Fehler Ausdruck -\n\nBei Aufruf gtk_print_operation_run:\n",
				error->message, NULL);
		g_clear_error(&error);
	}

	return;
}
