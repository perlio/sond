/*
 sond (gtk4/sond_eingang_dialog.h) - Akten, Beweisstücke, Unterlagen
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
 * @file gtk4/sond_eingang_dialog.h
 * @brief Dialog "Eingang setzen": Datum (Feld und Kalender) und Art
 */

#ifndef SOND_GTK4_EINGANG_DIALOG_H_INCLUDED
#define SOND_GTK4_EINGANG_DIALOG_H_INCLUDED

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Wird mit OK gerufen: iso ist das Datum ("JJJJ-MM-TT"), art die gewählte Art oder NULL (keine) */
typedef void (*SondEingangDialogFunc)(gchar const *iso, gchar const *art, gpointer data);

/* Zeigt den Dialog (modal zu parent, kehrt sofort zurück). zusammenfassung: Zeile über den Feldern
 * ("Eingang für ..."), vorbelegung: bisheriges Datum als JJJJMMTT, 0 = heute. Ein ungültiges Datum
 * wird im Dialog beanstandet; bei Abbrechen wird func nicht gerufen. */
void sond_eingang_dialog_show(GtkWindow *parent, gchar const *zusammenfassung, gint vorbelegung,
		SondEingangDialogFunc func, gpointer data);

G_END_DECLS

#endif /* SOND_GTK4_EINGANG_DIALOG_H_INCLUDED */
