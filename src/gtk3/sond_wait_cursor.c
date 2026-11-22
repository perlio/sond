/*
 sond (gtk3/sond_wait_cursor.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2023  pelo america

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

#include "sond_wait_cursor.h"

#include <gtk/gtk.h>


static void wait_cursor_apply(GtkWidget *widget, gchar const *name) {
	GtkWidget *toplevel = NULL;
	GdkWindow *gw = NULL;
	GdkDisplay *display = NULL;

	if (!widget)
		return;

	toplevel = gtk_widget_get_toplevel(widget);
	gw = gtk_widget_get_window(toplevel ? toplevel : widget);
	if (!gw)
		return;

	display = gdk_window_get_display(gw);
	if (name) {
		GdkCursor *cursor = gdk_cursor_new_from_name(display, name);

		gdk_window_set_cursor(gw, cursor);
		if (cursor)
			g_object_unref(cursor);
	} else
		gdk_window_set_cursor(gw, NULL);
	gdk_display_flush(display);
}

void wait_cursor_set(GtkWidget *widget) {
	wait_cursor_apply(widget, "wait");

	while (gtk_events_pending())
		gtk_main_iteration();

	return;
}

void wait_cursor_set_quick(GtkWidget *widget) {
	wait_cursor_apply(widget, "wait");

	return;
}

void wait_cursor_reset(GtkWidget *widget) {
	wait_cursor_apply(widget, NULL);

	return;
}
