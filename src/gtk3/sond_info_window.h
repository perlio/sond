/*
 sond (gtk3/sond_info_window.h) - Akten, Beweisstücke, Unterlagen
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

/**
 * @file gtk3/sond_info_window.h
 * @brief Fenster mit Meldungen und Fortschrittsbalken für lange Abläufe (GTK3)
 */

#ifndef SOND_GTK3_INFO_WINDOW_H_INCLUDED
#define SOND_GTK3_INFO_WINDOW_H_INCLUDED

#include <gtk/gtk.h>

typedef struct _Info_Window {
	GtkWidget *dialog;
	/* Ein GtkTextView/GtkTextBuffer (text_view/end_mark) statt eines
	 * GtkLabels pro Nachricht: bei sehr vielen Nachrichten (z.B. eine Zeile
	 * pro Datei beim Anbinden tausender Dateien) wäre die
	 * GtkBox-Größenberechnung O(Anzahl Kinder) pro neuer Nachricht,
	 * verschärft durch das UI-Pumping (gtk_main_iteration() nach jeder
	 * Nachricht erzwingt den Resize sofort statt ihn zu bündeln) - in Summe
	 * O(n²). Text anhängen ist beim GtkTextView dafür gebaut und bleibt auch
	 * bei vielen Zeilen günstig. */
	GtkWidget *text_view;
	GtkTextMark *end_mark;
	GtkWidget *progress_bar;
	gint* cancel;
} InfoWindow;

void info_window_kill(InfoWindow*);

void info_window_close(InfoWindow*);

void info_window_set_progress_bar_fraction(InfoWindow*, gdouble);

void info_window_set_progress_bar(InfoWindow*);

void info_window_display_progress(InfoWindow*, gint);

void info_window_set_message(InfoWindow*, const gchar*, ...);

void info_window_set_message_thread_safe(InfoWindow *info_window, const gchar *format, ...);

InfoWindow* info_window_open(GtkWidget*, gint*, const gchar*);

#endif // SOND_GTK3_INFO_WINDOW_H_INCLUDED
