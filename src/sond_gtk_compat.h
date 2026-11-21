/*
 sond (sond_gtk_compat.h) - Akten, Beweisstücke, Unterlagen
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

/**
 * @file sond_gtk_compat.h
 * @brief Kleine Brücke, damit Fenstercode in GTK3 und GTK4 aus derselben Quelle läuft
 *
 * Enthält Hilfsmakros (aus sond_treeview.h herausgelöst, damit sond_icon_util.c sie ohne den
 * GTK3-Baum einbinden kann) und die Wrapper. Die Wrapper haben die GTK4-Form; unter GTK3 bilden sie das Verhalten nach. Nur für die wenigen
 * Aufrufe, die sich zwischen beiden Versionen unterscheiden, kein allgemeines Backport.
 */

#ifndef SOND_GTK_COMPAT_H_INCLUDED
#define SOND_GTK_COMPAT_H_INCLUDED

#include <gtk/gtk.h>

G_BEGIN_DECLS

#if GTK_MAJOR_VERSION >= 4
#  define SOND_GTK4 1
#  define SOND_GET_TOPLEVEL(w) 		GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(w)))
#  define SOND_ICON_THEME_FOR_WIDGET(w) 		gtk_icon_theme_get_for_display(gtk_widget_get_display(GTK_WIDGET(w)))
#else
#  define SOND_GET_TOPLEVEL(w) 		gtk_widget_get_toplevel(GTK_WIDGET(w))
#  define SOND_ICON_THEME_FOR_WIDGET(w) 		gtk_icon_theme_get_for_screen(gtk_widget_get_screen(GTK_WIDGET(w)))
#endif

/* ---- Dialoge (sond_gtk_compat.c) ---- */

/* Meldung mit Schließen-Knopf; kehrt unter GTK4 sofort zurück */
void sond_compat_message(GtkWindow *parent, gchar const *text);

/* path: Dateiname (g_free) oder NULL bei Abbruch. Unter GTK4 asynchron, unter GTK3 synchron. */
typedef void (*SondCompatSaveFunc)(gchar *path, gpointer data);
void sond_compat_save_file(GtkWindow *parent, gchar const *title, gchar const *initial_name,
		SondCompatSaveFunc func, gpointer data);

/* ---- Widgets ---- */

static inline GtkWidget* sond_compat_window_new(void) {
#ifdef SOND_GTK4
	return gtk_window_new();
#else
	return gtk_window_new(GTK_WINDOW_TOPLEVEL);
#endif
}

static inline void sond_compat_window_set_child(GtkWindow *window, GtkWidget *child) {
#ifdef SOND_GTK4
	gtk_window_set_child(window, child);
#else
	gtk_container_add(GTK_CONTAINER(window), child);
#endif
}

static inline void sond_compat_window_present(GtkWindow *window) {
#ifndef SOND_GTK4
	gtk_widget_show_all(GTK_WIDGET(window));
#endif
	gtk_window_present(window);
}

static inline void sond_compat_box_append(GtkBox *box, GtkWidget *child, gboolean expand) {
#ifdef SOND_GTK4
	if (expand) {
		gtk_widget_set_hexpand(child, gtk_orientable_get_orientation(GTK_ORIENTABLE(box))
				== GTK_ORIENTATION_HORIZONTAL);
		gtk_widget_set_vexpand(child, gtk_orientable_get_orientation(GTK_ORIENTABLE(box))
				== GTK_ORIENTATION_VERTICAL);
	}
	gtk_box_append(box, child);
#else
	gtk_box_pack_start(box, child, expand, expand, 0);
#endif
}

static inline GtkWidget* sond_compat_scrolled_window_new(void) {
#ifdef SOND_GTK4
	return gtk_scrolled_window_new();
#else
	return gtk_scrolled_window_new(NULL, NULL);
#endif
}

static inline void sond_compat_scrolled_window_set_child(GtkScrolledWindow *sw, GtkWidget *child) {
#ifdef SOND_GTK4
	gtk_scrolled_window_set_child(sw, child);
#else
	gtk_container_add(GTK_CONTAINER(sw), child);
#endif
}

static inline gchar const* sond_compat_entry_get_text(GtkEntry *entry) {
#ifdef SOND_GTK4
	return gtk_editable_get_text(GTK_EDITABLE(entry));
#else
	return gtk_entry_get_text(entry);
#endif
}

static inline void sond_compat_entry_set_width_chars(GtkEntry *entry, gint n_chars) {
#ifdef SOND_GTK4
	gtk_editable_set_width_chars(GTK_EDITABLE(entry), n_chars);
#else
	gtk_entry_set_width_chars(entry, n_chars);
#endif
}

/* Name eines Cursors ("grabbing", ...), NULL stellt den Standard wieder her */
static inline void sond_compat_widget_set_cursor(GtkWidget *widget, gchar const *name) {
#ifdef SOND_GTK4
	gtk_widget_set_cursor_from_name(widget, name);
#else
	GdkWindow *gw = gtk_widget_get_window(widget);
	GdkCursor *cursor = NULL;

	if (!gw)
		return;
	if (name)
		cursor = gdk_cursor_new_from_name(gdk_window_get_display(gw), name);
	gdk_window_set_cursor(gw, cursor);
	if (cursor)
		g_object_unref(cursor);
#endif
}

/* ---- Zeichnen ---- */

typedef void (*SondCompatDrawFunc)(GtkDrawingArea *area, cairo_t *cr, int width, int height,
		gpointer data);

#ifndef SOND_GTK4
typedef struct {
	SondCompatDrawFunc func;
	gpointer data;
} SondCompatDraw;

static inline gboolean sond_compat_draw_trampoline(GtkWidget *widget, cairo_t *cr, gpointer p) {
	SondCompatDraw *d = p;

	d->func(GTK_DRAWING_AREA(widget), cr, gtk_widget_get_allocated_width(widget),
			gtk_widget_get_allocated_height(widget), d->data);

	return FALSE;
}

static inline void sond_compat_draw_free(gpointer p, GClosure *closure) {
	(void) closure;
	g_free(p);
}
#endif

static inline void sond_compat_drawing_area_set_draw_func(GtkDrawingArea *area,
		SondCompatDrawFunc func, gpointer data) {
#ifdef SOND_GTK4
	gtk_drawing_area_set_draw_func(area, func, data, NULL);
#else
	SondCompatDraw *d = g_new(SondCompatDraw, 1);

	d->func = func;
	d->data = data;
	g_signal_connect_data(area, "draw", G_CALLBACK(sond_compat_draw_trampoline), d,
			sond_compat_draw_free, 0);
#endif
}

/* ---- Eingabe-Controller (Signatur wie GTK4; unter GTK3 gehört die Referenz dem Widget) ---- */

#ifndef SOND_GTK4
static inline void sond_compat_controller_keep(GtkWidget *widget, GtkEventController *controller) {
	g_signal_connect_swapped(widget, "destroy", G_CALLBACK(g_object_unref), controller);
}
#endif

typedef gboolean (*SondCompatKeyFunc)(GtkEventControllerKey *controller, guint keyval,
		guint keycode, GdkModifierType state, gpointer data);

static inline void sond_compat_add_key_handler(GtkWidget *widget, SondCompatKeyFunc func,
		gpointer data) {
#ifdef SOND_GTK4
	GtkEventController *c = gtk_event_controller_key_new();

	g_signal_connect(c, "key-pressed", G_CALLBACK(func), data);
	gtk_widget_add_controller(widget, c);
#else
	GtkEventController *c = gtk_event_controller_key_new(widget);

	g_signal_connect(c, "key-pressed", G_CALLBACK(func), data);
	sond_compat_controller_keep(widget, c);
#endif
}

/* Mausrad (senkrecht), vor den Kindern ausgewertet, damit ein Scrollbereich nicht zuerst zugreift.
 * dy < 0: nach oben. */
typedef gboolean (*SondCompatScrollFunc)(GtkEventControllerScroll *controller, gdouble dx,
		gdouble dy, gpointer data);

static inline void sond_compat_add_scroll_handler(GtkWidget *widget, SondCompatScrollFunc func,
		gpointer data) {
#ifdef SOND_GTK4
	GtkEventController *c = gtk_event_controller_scroll_new(
			GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);

	gtk_event_controller_set_propagation_phase(c, GTK_PHASE_CAPTURE);
	g_signal_connect(c, "scroll", G_CALLBACK(func), data);
	gtk_widget_add_controller(widget, c);
#else
	GtkEventController *c = NULL;

	gtk_widget_add_events(widget, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
	c = gtk_event_controller_scroll_new(widget, GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
	gtk_event_controller_set_propagation_phase(c, GTK_PHASE_CAPTURE);
	g_signal_connect(c, "scroll", G_CALLBACK(func), data);
	sond_compat_controller_keep(widget, c);
#endif
}

/* Modifier während des laufenden Scroll-Ereignisses */
static inline GdkModifierType sond_compat_scroll_state(GtkEventControllerScroll *controller) {
#ifdef SOND_GTK4
	return gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));
#else
	GdkModifierType state = 0;

	(void) controller;
	gtk_get_current_event_state(&state);

	return state;
#endif
}

/* Ziehen mit der angegebenen Maustaste (2 = mittlere). Die Signale "drag-begin", "drag-update"
 * und "drag-end" sind in GTK3 und GTK4 gleich; der Aufrufer verbindet sie mit dem Rückgabewert. */
static inline GtkGesture* sond_compat_add_drag(GtkWidget *widget, guint button) {
#ifdef SOND_GTK4
	GtkGesture *g = gtk_gesture_drag_new();

	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), button);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(g));
#else
	GtkGesture *g = NULL;

	gtk_widget_add_events(widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
			| GDK_POINTER_MOTION_MASK | GDK_BUTTON_MOTION_MASK);
	g = gtk_gesture_drag_new(widget);
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), button);
	sond_compat_controller_keep(widget, GTK_EVENT_CONTROLLER(g));
#endif

	return g;
}

G_END_DECLS

#endif /* SOND_GTK_COMPAT_H_INCLUDED */
