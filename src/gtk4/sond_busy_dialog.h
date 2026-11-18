/*
 sond (gtk4/sond_busy_dialog.h) - Akten, Beweisstücke, Unterlagen
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
 * @file gtk4/sond_busy_dialog.h
 * @brief Kleines modales Fortschrittsfenster mit Abbrechen (GTK4)
 *
 * Zeigt einen Text und einen Fortschrittsbalken (pulsierend, solange kein Anteil
 * gesetzt ist) und einen Knopf "Abbrechen". Das Fenster treibt nichts an: wer
 * eine längere Arbeit in einem Thread laufen lässt, arbeitet selbst die
 * Ereignisse ab (g_main_context_iteration) und prüft das Abbruch-Flag.
 */

#ifndef SOND_GTK4_BUSY_DIALOG_H_INCLUDED
#define SOND_GTK4_BUSY_DIALOG_H_INCLUDED

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _SondBusyDialog SondBusyDialog;

/* parent darf NULL sein. *cancelled (darf NULL sein: dann ein eigenes Flag, s.
 * sond_busy_dialog_is_cancelled()) wird auf 1 gesetzt, wenn der Nutzer auf
 * Abbrechen klickt oder das Fenster schließt. */
SondBusyDialog* sond_busy_dialog_new(GtkWindow *parent, gchar const *title,
		gchar const *text, gint *cancelled);

void sond_busy_dialog_set_text(SondBusyDialog*, gchar const *text);

/* 0..1; ein negativer Wert schaltet auf pulsierend zurück */
void sond_busy_dialog_set_fraction(SondBusyDialog*, gdouble fraction);

gboolean sond_busy_dialog_is_cancelled(SondBusyDialog*);

/* Schließt das Fenster und gibt es frei */
void sond_busy_dialog_close(SondBusyDialog*);

G_END_DECLS

#endif /* SOND_GTK4_BUSY_DIALOG_H_INCLUDED */
