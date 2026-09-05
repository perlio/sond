/*
 sond (sond_icon_util.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_icon_util.h"

#include "sond_treeview.h" /* SOND_ICON_THEME_FOR_WIDGET */
#include "sond_log_and_error.h"

GdkPixbuf* sond_icon_util_load_pixbuf(GtkWidget *widget,
		gchar const *icon_name, gint size) {
	GtkIconTheme *theme = NULL;
	GdkPixbuf *pixbuf = NULL;

	if (!icon_name) return NULL;

	theme = SOND_ICON_THEME_FOR_WIDGET(widget);
	pixbuf = gtk_icon_theme_load_icon(theme, icon_name, size,
			GTK_ICON_LOOKUP_USE_BUILTIN, NULL);

	return pixbuf;
}

/* Gemeinsamer Kern für alle per Cairo gezeichneten Kreis-Badges: gefüllter
 * Kreis in der angegebenen Farbe mit dünnem weißem Rand für Kontrast auf
 * dunklem wie hellem Untergrund. */
static GdkPixbuf* draw_circle_badge(gdouble r, gdouble g, gdouble b, gint size) {
	cairo_surface_t *surface = NULL;
	cairo_t *cr = NULL;
	GdkPixbuf *pixbuf = NULL;
	gdouble cx = 0, cy = 0, radius = 0;

	if (size < 4) size = 4;

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
	cr = cairo_create(surface);

	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cx = size / 2.0;
	cy = size / 2.0;
	radius = size / 2.0 - 0.4;

	cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
	cairo_set_source_rgba(cr, r, g, b, 1.0);
	cairo_fill_preserve(cr);

	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9); /* dünner weißer Rand für Kontrast */
	cairo_set_line_width(cr, MAX(size * 0.10, 0.6));
	cairo_stroke(cr);

	pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, size, size);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	return pixbuf;
}

GdkPixbuf* sond_icon_util_status_badge_pixbuf(SondIndexStatus status, gint size) {
	if (status == SOND_INDEX_STATUS_FULL)
		return draw_circle_badge(0.20, 0.66, 0.33, size); /* Grün */
	if (status == SOND_INDEX_STATUS_PARTIAL)
		return draw_circle_badge(0.95, 0.61, 0.07, size); /* Orange */

	return NULL;
}

GdkPixbuf* sond_icon_util_seadrive_badge_pixbuf(SondSeadriveBadge badge, gint size) {
	switch (badge) {
	case SOND_SEADRIVE_BADGE_OFFLINE:
		return draw_circle_badge(0.55, 0.25, 0.75, size); /* Violett */
	case SOND_SEADRIVE_BADGE_PINNED:
		return draw_circle_badge(0.20, 0.66, 0.33, size); /* Grün */
	default:
		return NULL;
	}
}

gint sond_icon_util_renderer_get_size(GtkCellRenderer *renderer) {
	gint icon_size = 0;
	gint px = 0;

	g_object_get(renderer, "stock-size", &icon_size, NULL);
	gtk_icon_size_lookup((GtkIconSize) icon_size, &px, NULL);
	if (px <= 0) px = 16;

	return px;
}

gboolean sond_icon_util_render_with_overlays(GtkWidget *widget,
		GtkCellRenderer *renderer, gchar const *base_icon_name,
		SondIconOverlay const *overlays, guint n_overlays) {
	gint px = sond_icon_util_renderer_get_size(renderer);
	gint overlay_px = MAX(px / 2, 8);
	GdkPixbuf *main_pb = sond_icon_util_load_pixbuf(widget, base_icon_name, px);

	if (!main_pb) {
		LOG_WARN("%s: sond_icon_util_load_pixbuf(\"%s\", %d) fehlgeschlagen - "
				"Overlay(s) können nicht gezeichnet werden",
				__func__, base_icon_name ? base_icon_name : "(null)", px);
		g_object_set(G_OBJECT(renderer), "icon-name",
				base_icon_name ? base_icon_name : "image-missing", NULL);
		return FALSE;
	}

	for (guint i = 0; i < n_overlays; i++) {
		gint dest_x = 0;
		gint dest_y = 0;

		if (!overlays[i].pixbuf)
			continue;

		dest_y = px - overlay_px;
		dest_x = (overlays[i].corner == SOND_ICON_CORNER_BOTTOM_RIGHT) ?
				px - overlay_px : 0;

		gdk_pixbuf_composite(overlays[i].pixbuf, main_pb,
				dest_x, dest_y, overlay_px, overlay_px,
				dest_x, dest_y, 1.0, 1.0,
				GDK_INTERP_BILINEAR, 255);
	}

	g_object_set(G_OBJECT(renderer), "pixbuf", main_pb, NULL);
	g_object_unref(main_pb);

	return TRUE;
}
