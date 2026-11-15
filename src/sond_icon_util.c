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

#include "sond_gtk_compat.h" /* SOND_ICON_THEME_FOR_WIDGET */
#include "sond_log_and_error.h"

#if GTK_MAJOR_VERSION < 4
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
#endif /* GTK3: gtk_icon_theme_load_icon() gibt es in GTK4 nicht */

/* Cairo-Fläche in ein GdkPixbuf umsetzen (GTK4 kennt gdk_pixbuf_get_from_surface()
 * nicht mehr) */
static GdkPixbuf* pixbuf_from_surface(cairo_surface_t *surface, gint width,
		gint height) {
#if GTK_MAJOR_VERSION < 4
	return gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);
#else
	GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
	const guchar *src = NULL;
	guchar *dst = NULL;
	gint src_stride = 0;
	gint dst_stride = 0;

	if (!pixbuf)
		return NULL;

	cairo_surface_flush(surface);
	src = cairo_image_surface_get_data(surface);
	src_stride = cairo_image_surface_get_stride(surface);
	dst = gdk_pixbuf_get_pixels(pixbuf);
	dst_stride = gdk_pixbuf_get_rowstride(pixbuf);

	for (gint y = 0; y < height; y++) {
		for (gint x = 0; x < width; x++) {
			guint32 px = *(const guint32*) (src + y * src_stride + x * 4);
			guint a = (px >> 24) & 0xff;
			guint r = (px >> 16) & 0xff;
			guint g = (px >> 8) & 0xff;
			guint b = px & 0xff;
			guchar *out = dst + y * dst_stride + x * 4;

			//Cairo liefert vormultiplizierte Werte
			if (a > 0 && a < 255) {
				r = MIN(r * 255 / a, 255);
				g = MIN(g * 255 / a, 255);
				b = MIN(b * 255 / a, 255);
			}
			out[0] = (guchar) r;
			out[1] = (guchar) g;
			out[2] = (guchar) b;
			out[3] = (guchar) a;
		}
	}

	return pixbuf;
#endif
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

	pixbuf = pixbuf_from_surface(surface, size, size);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	return pixbuf;
}

GdkPixbuf* sond_icon_util_status_badge_pixbuf(SondIndexStatus status, gint size) {
	if (status == SOND_INDEX_STATUS_FULL)
		return draw_circle_badge(0.20, 0.66, 0.33, size); /* Grün */
	if (status == SOND_INDEX_STATUS_PARTIAL)
		return draw_circle_badge(0.55, 0.55, 0.55, size); /* Grau - wie SEADRIVE_DIR_STATUS_MIXED */

	return NULL;
}

GdkPixbuf* sond_icon_util_seadrive_badge_pixbuf(SondSeadriveBadge badge, gint size) {
	switch (badge) {
	case SOND_SEADRIVE_BADGE_OFFLINE:
		return draw_circle_badge(0.55, 0.25, 0.75, size); /* Violett */
	case SOND_SEADRIVE_BADGE_PENDING:
		return draw_circle_badge(0.95, 0.61, 0.07, size); /* Orange */
	case SOND_SEADRIVE_BADGE_PINNED:
		return draw_circle_badge(0.20, 0.66, 0.33, size); /* Grün */
	default:
		return NULL;
	}
}

GdkPixbuf* sond_icon_util_seadrive_dir_badge_pixbuf(SondSeadriveDirStatus status, gint size) {
	switch (status) {
	case SOND_SEADRIVE_DIR_STATUS_FULL_OFFLINE:
		return draw_circle_badge(0.55, 0.25, 0.75, size); /* Violett - wie BADGE_OFFLINE */
	case SOND_SEADRIVE_DIR_STATUS_FULL_HYDRATED_PINNED:
		return draw_circle_badge(0.20, 0.66, 0.33, size); /* Grün - wie BADGE_PINNED */
	case SOND_SEADRIVE_DIR_STATUS_MIXED:
		return draw_circle_badge(0.55, 0.55, 0.55, size); /* Grau - nur Ordner, kein Datei-Äquivalent */
	default:
		return NULL;
	}
}

#if GTK_MAJOR_VERSION < 4
GdkPixbuf* sond_icon_util_attachment_badge_pixbuf(GtkWidget *widget, gint size) {
	return sond_icon_util_load_pixbuf(widget, "mail-attachment-symbolic", size);
}

GdkPixbuf* sond_icon_util_locked_badge_pixbuf(GtkWidget *widget, gint size,
		gboolean unlocked) {
	GtkIconInfo *info = NULL;
	GdkPixbuf *pixbuf = NULL;
	GdkRGBA green = { 0.20, 0.66, 0.33, 1.0 };

	if (!unlocked)
		return sond_icon_util_load_pixbuf(widget, "changes-prevent-symbolic", size);

	//dasselbe Symbol, grün eingefärbt
	info = gtk_icon_theme_lookup_icon(SOND_ICON_THEME_FOR_WIDGET(widget),
			"changes-prevent-symbolic", size, GTK_ICON_LOOKUP_USE_BUILTIN);
	if (!info)
		return NULL;

	pixbuf = gtk_icon_info_load_symbolic(info, &green, NULL, NULL, NULL, NULL,
			NULL);
	g_object_unref(info);

	return pixbuf;
}
#else
/* GTK4: Büroklammer und Schloss per Cairo gezeichnet (das GTK3-Verfahren, ein
 * Symbol aus dem Icon-Theme als Pixbuf zu laden, gibt es nicht mehr) */
static cairo_surface_t* new_badge_surface(gint size, cairo_t **cr) {
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);

	*cr = cairo_create(surface);
	cairo_set_operator(*cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(*cr);
	cairo_set_operator(*cr, CAIRO_OPERATOR_OVER);

	return surface;
}

GdkPixbuf* sond_icon_util_attachment_badge_pixbuf(GtkWidget *widget, gint size) {
	cairo_t *cr = NULL;
	cairo_surface_t *surface = NULL;
	GdkPixbuf *pixbuf = NULL;

	(void) widget;

	if (size < 4)
		size = 4;
	surface = new_badge_surface(size, &cr);

	//Büroklammer: zwei ineinanderliegende Schleifen
	cairo_set_source_rgb(cr, 0.30, 0.30, 0.30);
	cairo_set_line_width(cr, MAX(size * 0.11, 0.8));
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to(cr, size * 0.62, size * 0.30);
	cairo_line_to(cr, size * 0.62, size * 0.72);
	cairo_arc_negative(cr, size * 0.48, size * 0.72, size * 0.14, 0, G_PI);
	cairo_line_to(cr, size * 0.34, size * 0.28);
	cairo_arc(cr, size * 0.47, size * 0.28, size * 0.13, G_PI, 0);
	cairo_line_to(cr, size * 0.60, size * 0.62);
	cairo_stroke(cr);

	pixbuf = pixbuf_from_surface(surface, size, size);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	return pixbuf;
}

GdkPixbuf* sond_icon_util_locked_badge_pixbuf(GtkWidget *widget, gint size,
		gboolean unlocked) {
	cairo_t *cr = NULL;
	cairo_surface_t *surface = NULL;
	GdkPixbuf *pixbuf = NULL;

	(void) widget;

	if (size < 4)
		size = 4;
	surface = new_badge_surface(size, &cr);

	if (unlocked)
		cairo_set_source_rgb(cr, 0.20, 0.66, 0.33); //grün: entsperrt
	else
		cairo_set_source_rgb(cr, 0.12, 0.12, 0.12); //schwarz: gesperrt

	//Bügel
	cairo_set_line_width(cr, MAX(size * 0.12, 0.8));
	cairo_arc(cr, size * 0.5, size * 0.42, size * 0.20, G_PI, 2 * G_PI);
	cairo_move_to(cr, size * 0.30, size * 0.42);
	cairo_line_to(cr, size * 0.30, size * 0.52);
	cairo_move_to(cr, size * 0.70, size * 0.42);
	cairo_line_to(cr, size * 0.70, size * 0.52);
	cairo_stroke(cr);

	//Körper
	cairo_rectangle(cr, size * 0.18, size * 0.50, size * 0.64, size * 0.42);
	cairo_fill(cr);

	pixbuf = pixbuf_from_surface(surface, size, size);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	return pixbuf;
}
#endif /* GTK3: Theme-Symbole, GTK4: Cairo */

#if GTK_MAJOR_VERSION < 4
gint sond_icon_util_renderer_get_size(GtkCellRenderer *renderer) {
	gint icon_size = 0;
	gint px = 0;

	g_object_get(renderer, "stock-size", &icon_size, NULL);
	gtk_icon_size_lookup((GtkIconSize) icon_size, &px, NULL);
	if (px <= 0) px = 16;

	return px;
}
#endif

gchar const* sond_icon_util_normalize_name(gchar const *name) {
	gchar *lower = NULL;
	gchar const *result = "text-x-generic";

	//Theme-Namen enthalten weder Pfadtrenner noch Komma
	if (!name || !*name
			|| (!strchr(name, '\\') && !strchr(name, '/') && !strchr(name, ',')))
		return name;

	lower = g_ascii_strdown(name, -1);
	if (strstr(lower, "thunderbird") || strstr(lower, "outlook"))
		result = "mail-read";
	else if (strstr(lower, "irfanview") || strstr(lower, "i_view")
			|| strstr(lower, "photo"))
		result = "image-x-generic";
	else if (strstr(lower, "excel"))
		result = "x-office-spreadsheet";
	else if (strstr(lower, "powerpnt"))
		result = "x-office-presentation";
	else if (strstr(lower, "wordpad") || strstr(lower, "winword")
			|| strstr(lower, "soffice"))
		result = "x-office-document";
	else if (strstr(lower, ".ico") || strstr(lower, "acro")
			|| strstr(lower, "pdf"))
		result = "pdf";
	g_free(lower);

	return result;
}

#if GTK_MAJOR_VERSION < 4
gboolean sond_icon_util_render_with_overlays(GtkWidget *widget,
		GtkCellRenderer *renderer, gchar const *base_icon_name,
		SondIconOverlay const *overlays, guint n_overlays) {
	gint px = sond_icon_util_renderer_get_size(renderer);
	gint overlay_px = MAX(px / 2, 8);
	GdkPixbuf *main_pb = NULL;

	base_icon_name = sond_icon_util_normalize_name(base_icon_name);
	main_pb = sond_icon_util_load_pixbuf(widget, base_icon_name, px);

	if (!main_pb) {
		//je Name nur einmal melden: der Baum wird ständig neu gezeichnet
		static GHashTable *logged = NULL;
		gchar const *key = base_icon_name ? base_icon_name : "(null)";

		if (!logged)
			logged = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
		if (!g_hash_table_contains(logged, key)) {
			g_hash_table_add(logged, g_strdup(key));
			LOG_WARN("sond_icon_util_load_pixbuf(\"%s\", %d) fehlgeschlagen - "
					"Overlay(s) können nicht gezeichnet werden", key, px);
		}
		g_object_set(G_OBJECT(renderer), "icon-name",
				base_icon_name ? base_icon_name : "image-missing", NULL);
		return FALSE;
	}

	/* WICHTIG: gtk_icon_theme_load_icon() (in sond_icon_util_load_pixbuf())
	 * kann - je nach GTK-/Theme-Implementierung - ein INTERN GECACHTES,
	 * gemeinsam genutztes GdkPixbuf zurückgeben, keine frische Kopie pro
	 * Aufruf. gdk_pixbuf_composite() unten zeichnet direkt IN main_pb
	 * hinein - ohne diese Kopie hier würde das Overlay-Badge dauerhaft in
	 * den Cache-Eintrag für "base_icon_name" "hineingebrannt" und danach
	 * bei JEDER anderen Zeile mit demselben Basis-Icon (z.B. jedem anderen
	 * Ordner - "folder") mit angezeigt, unabhängig von deren eigenem
	 * Status (Bug "Ordner faelschlich gruen/violett"). Nur bei tatsächlich vorhandenen Overlays kopieren
	 * (sonst unnötiger Allokations-Overhead für den Normalfall). */
	if (n_overlays > 0) {
		GdkPixbuf *copy = gdk_pixbuf_copy(main_pb);
		if (copy) {
			g_object_unref(main_pb);
			main_pb = copy;
		} else {
			LOG_WARN("%s: gdk_pixbuf_copy() fehlgeschlagen - Overlay(s) "
					"würden das gecachte Basis-Icon \"%s\" verändern, "
					"werden deshalb übersprungen", __func__,
					base_icon_name ? base_icon_name : "(null)");
			n_overlays = 0;
		}
	}

	for (guint i = 0; i < n_overlays; i++) {
		gint dest_x = 0;
		gint dest_y = 0;

		if (!overlays[i].pixbuf)
			continue;

		dest_y = (overlays[i].corner == SOND_ICON_CORNER_TOP_LEFT ||
				overlays[i].corner == SOND_ICON_CORNER_TOP_RIGHT) ?
						0 : px - overlay_px;
		dest_x = (overlays[i].corner == SOND_ICON_CORNER_BOTTOM_RIGHT ||
				overlays[i].corner == SOND_ICON_CORNER_TOP_RIGHT) ?
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
#endif /* GTK3: GtkCellRenderer */
