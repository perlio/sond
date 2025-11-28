/*
 zond (render.c) - Akten, Beweisstücke, Unterlagen
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

#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "../zond_pdf_document.h"
#include "../global_types.h"

#include "../99conv/general.h"
#include "../99conv/pdf.h"

#include "render.h"
#include "viewer.h"
#include "document.h"

typedef struct {
	fz_context *ctx;
	fz_pixmap *pixmap;
} ViewerPixbufPrivate;

static void render_pixbuf_finalize(guchar *pixels, gpointer data) {
	ViewerPixbufPrivate *viewer_pixbuf_priv = (ViewerPixbufPrivate*) data;

	fz_drop_pixmap(viewer_pixbuf_priv->ctx, viewer_pixbuf_priv->pixmap);

	g_free(viewer_pixbuf_priv);

	return;
}

static GdkPixbuf*
render_pixbuf_new_from_pixmap(fz_context *ctx, fz_pixmap *pixmap) {
	GdkPixbuf *pixbuf = NULL;
	ViewerPixbufPrivate *viewer_pixbuf_priv = NULL;

	viewer_pixbuf_priv = g_try_malloc0(sizeof(ViewerPixbufPrivate));
	if (!viewer_pixbuf_priv)
		return NULL;

	viewer_pixbuf_priv->ctx = ctx;

	viewer_pixbuf_priv->pixmap = pixmap;

	pixbuf = gdk_pixbuf_new_from_data(pixmap->samples, GDK_COLORSPACE_RGB,
			FALSE, 8, pixmap->w, pixmap->h, pixmap->stride,
			render_pixbuf_finalize, viewer_pixbuf_priv);

	return pixbuf;
}

static gint render_thumbnail(fz_context *ctx, ViewerPageNew *viewer_page,
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
	fz_catch	( ctx )
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

	pixbuf = render_pixbuf_new_from_pixmap(
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

static gint render_pixmap(fz_context *ctx, ViewerPageNew *viewer_page,
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
	fz_always	( ctx ) {
		fz_close_device(ctx, draw_device);
		fz_drop_device(ctx, draw_device);
	}fz_catch( ctx ) {
		fz_drop_pixmap(ctx, pixmap);
		ERROR_MUPDF("fz_run_display_list")
	}

	pixbuf = render_pixbuf_new_from_pixmap(
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

gint render_stext_page_from_display_list(fz_context *ctx,
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

static gint render_display_list(fz_context *ctx,
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

void render_page_thread(gpointer data, gpointer user_data) {
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
		rc = render_display_list(ctx, viewer_page->pdf_document_page, &errmsg);
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
		rc = render_stext_page_from_display_list(ctx,
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

		rc = render_pixmap(ctx, viewer_page, pv->zoom, &errmsg);
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

		rc = render_thumbnail(ctx, viewer_page, &errmsg);
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

