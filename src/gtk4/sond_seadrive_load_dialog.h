/*
 sond (gtk4/sond_seadrive_load_dialog.h) - Akten, Beweisstücke, Unterlagen
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
 * @file gtk4/sond_seadrive_load_dialog.h
 * @brief Lädt nicht geladene SeaDrive-Dateien mit Fortschrittsfenster (GTK4)
 *
 * Gegenstück zu sond_seadrive_load_sync() der GTK3-Fassung (seadrive_gtk3). Der Zustand
 * kommt aus dem GTK-freien Lade-Auftrag (sond_seadrive_core.h), hier liegt nur die
 * Anzeige.
 */

#ifndef SOND_GTK4_SEADRIVE_LOAD_DIALOG_H_INCLUDED
#define SOND_GTK4_SEADRIVE_LOAD_DIALOG_H_INCLUDED

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Lädt alle full_paths, die als nicht geladene Platzhalter vorliegen: Downloads
 * anstoßen, ein modales Fenster zeigt den Fortschritt (Dateien und Volumen) mit
 * "Abbrechen" und wartet in einer eigenen Hauptschleife, bis alle Dateien geladen sind.
 * TRUE: alles lokal, der Aufrufer macht weiter. FALSE: Abbruch durch den Nutzer oder
 * gescheiterte Downloads (schon gemeldet), der Aufrufer bricht still ab. Ohne
 * SeaDrive-Laufwerk (und außerhalb von Windows) immer TRUE. Nur im Hauptthread. */
gboolean sond_seadrive_load_sync(GtkWindow *parent, GPtrArray *full_paths);

/* Wie oben für einen einzelnen absoluten Pfad. */
gboolean sond_seadrive_load_file_sync(GtkWindow *parent, const gchar *full_path);

G_END_DECLS

#endif /* SOND_GTK4_SEADRIVE_LOAD_DIALOG_H_INCLUDED */
