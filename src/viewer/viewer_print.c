/*
 zond (viewer_print.c) - Akten, Beweisstücke, Unterlagen
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

//Drucken der angezeigten Seiten

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <mupdf/fitz.h>
#include <cairo.h>

#include "../gtk3/sond_dialoge.h"

#include "zond_pdf_document.h"

#include "viewer.h"
#include "viewer_render.h"

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

	/* viewer_render_wait_for_transfer() wartet nur, bis der Render-Task
	 * fertig ist (erfolgreich oder nicht) - ein Fehlschlag wird dabei nur
	 * intern geloggt (LOG_WARN in viewer_render_transfer_rendered()), aber
	 * nicht nach hier zurückgemeldet. Ohne diese Prüfung würde
	 * fz_run_display_list() unten mit display_list == NULL aufgerufen -
	 * ein direkter Nullpointer-Zugriff tief in mupdf, der NICHT über
	 * fz_throw() läuft und damit auch vom umgebenden fz_try/fz_catch nicht
	 * abgefangen wird (Absturz statt Fehlerdialog). */
	if (!viewer_page->pdf_document_page->display_list) {
		gchar *errmsg = g_strdup_printf("Seite Nr. %i", page_nr);
		display_message(viewer_parent(pdfv), "Fehler Drucken ", errmsg,
				" -\n\nSeite konnte nicht gerendert werden", NULL);
		g_free(errmsg);

		return;
	}

	width = gtk_print_context_get_width(context);
	height = gtk_print_context_get_height(context);

	zoom_x = width / (viewer_page->crop.x1 - viewer_page->crop.x0);
	zoom_y = height / (viewer_page->crop.y1 - viewer_page->crop.y0);
	zoom = (zoom_x <= zoom_y) ? zoom_x : zoom_y;

	ctx = fz_clone_context(zond_pdf_document_get_ctx(viewer_page->pdf_document_page->document));
	if (!ctx) {
		gchar *errmsg = g_strdup_printf("Seite Nr. %i", page_nr);
		display_message(viewer_parent(pdfv), "Fehler Drucken ", errmsg,
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
		display_message(viewer_parent(pdfv), "Fehler Drucken ", errmsg,
				" -\n\nBei Aufruf fz_new_pixmap_with_bbox:\n", fz_caught_message(ctx), NULL);
		g_free(errmsg);
		fz_drop_context(ctx);
		return;
	}

	fz_try(ctx)
		fz_clear_pixmap_with_value(ctx, pixmap, 255);
	fz_catch(ctx) {
		gchar *errmsg = g_strdup_printf("Seite Nr. %i", page_nr);
		display_message(viewer_parent(pdfv), "Fehler Drucken ", errmsg,
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
		display_message(viewer_parent(pdfv), "Fehler Drucken ", errmsg,
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
		display_message(viewer_parent(pdfv), "Fehler Drucken ", errmsg,
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
			GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, GTK_WINDOW(viewer_parent(pdfv)), &error);
	g_object_unref(print);

	if (res == GTK_PRINT_OPERATION_RESULT_ERROR) {
		display_message(viewer_parent(pdfv),
				"Fehler Ausdruck -\n\nBei Aufruf gtk_print_operation_run:\n",
				error->message, NULL);
		g_clear_error(&error);
	}

	return;
}
