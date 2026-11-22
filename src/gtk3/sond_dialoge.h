/*
 sond (gtk3/sond_dialoge.h) - Akten, Beweisstücke, Unterlagen
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
 * @file gtk3/sond_dialoge.h
 * @brief Meldungen, Abfragen und Dateiauswahl (GTK3)
 */

#ifndef SOND_GTK3_DIALOGE_H_INCLUDED
#define SOND_GTK3_DIALOGE_H_INCLUDED

#include <gtk/gtk.h>

gint my_dialog_run(GtkDialog*);

void display_message(GtkWidget*, ...);

gint dialog_with_buttons(GtkWidget*, const gchar*, const gchar*, gchar**, ...);

gint abfrage_frage(GtkWidget*, const gchar*, const gchar*, gchar**);

gchar* filename_speichern(GtkWindow*, const gchar*, const gchar*);

/* start_path (kann NULL sein): Ordner, in dem der Dialog initial öffnet -
 * s. Kommentar an filename_oeffnen() (sond_dialoge.c) zum Hintergrund (langsames
 * Öffnen des Datei-Dialogs bei SeaDrive-Projekten). */
gchar* filename_oeffnen(GtkWindow*, const gchar *start_path);

GtkWidget* result_listbox_new(GtkWindow*, const gchar*);

#endif // SOND_GTK3_DIALOGE_H_INCLUDED
