/*
 sond (gtk3/sond_wait_cursor.h) - Akten, Beweisstücke, Unterlagen
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
 * @file gtk3/sond_wait_cursor.h
 * @brief Wartecursor für Arbeit, die synchron im UI-Thread läuft (GTK3)
 */

#ifndef SOND_GTK3_WAIT_CURSOR_H_INCLUDED
#define SOND_GTK3_WAIT_CURSOR_H_INCLUDED

#include <gtk/gtk.h>

/* Wartecursor (Sanduhr) auf dem Fenster von widget, für Arbeit, die synchron im
 * UI-Thread läuft (die Oberfläche reagiert währenddessen nicht). set
 * leert die Event-Warteschlange, damit der Cursor schon sichtbar ist, bevor
 * die Arbeit beginnt; jedes set braucht ein reset (auf allen Ausstiegen). */
void wait_cursor_set(GtkWidget *widget);

/* Wie wait_cursor_set(), arbeitet aber keine Ereignisse ab: der Cursor wird
 * nur gesetzt und an die Anzeige geschickt. Für Stellen, an denen ein
 * Ereignis (z.B. ein Klick) den Zustand nicht stören darf. Ob die Sanduhr
 * schon vor der Arbeit erscheint, hängt vom Fenstersystem ab. Reset wie
 * bei wait_cursor_set(). */
void wait_cursor_set_quick(GtkWidget *widget);

void wait_cursor_reset(GtkWidget *widget);

#endif // SOND_GTK3_WAIT_CURSOR_H_INCLUDED
