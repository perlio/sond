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

/* Hilfsmakros für GTK3/GTK4-Kompatibilität (aus sond_treeview.h herausgelöst, damit
 * sond_icon_util.c sie ohne den GTK3-Baum einbinden kann) */

#ifndef SOND_GTK_COMPAT_H_INCLUDED
#define SOND_GTK_COMPAT_H_INCLUDED

#include <gtk/gtk.h>

#if GTK_MAJOR_VERSION >= 4
#  define SOND_GET_TOPLEVEL(w) \
		GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(w)))
#  define SOND_ICON_THEME_FOR_WIDGET(w) \
		gtk_icon_theme_get_for_display(gtk_widget_get_display(GTK_WIDGET(w)))
#else
#  define SOND_GET_TOPLEVEL(w) \
		gtk_widget_get_toplevel(GTK_WIDGET(w))
#  define SOND_ICON_THEME_FOR_WIDGET(w) \
		gtk_icon_theme_get_for_screen(gtk_widget_get_screen(GTK_WIDGET(w)))
#endif

#endif // SOND_GTK_COMPAT_H_INCLUDED
