/*
 sond (sond_module.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_module.h
 * @brief Gemeinsame Regeln für die Module des Clients (Seiten des Stacks)
 *
 * Jedes Modul, das beim Server etwas offen hält (Sperren, nicht fertig angelegte
 * Knoten, laufende Bearbeitungen), muss an seinem Widget einen Abbruch-Handler
 * anmelden (sond_module_set_abort()). Er tut, was die Schaltfläche "Abbrechen"
 * des Moduls tut: Nicht Gespeichertes verwerfen, Sperren zurückgeben.
 *
 * Das Hauptfenster ruft ihn beim Schließen des Clients für alle Seiten des Stacks
 * auf (sond_module_abort_stack()); beim Zerstören des Moduls ruft das Modul ihn
 * selbst auf, falls das Fenster nicht über "Schließen" ging (z. B. Beenden der
 * Anwendung). Der Handler läuft höchstens einmal.
 */

#ifndef SOND_MODULE_H
#define SOND_MODULE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef void (*SondModuleAbortFunc)(GtkWidget *module, gpointer user_data);

/* Meldet den Abbruch-Handler des Moduls an (module: das Widget, das in den Stack
 * eingehängt wird). Der Handler soll kurze Zeitgrenzen für Serveraufrufe
 * verwenden, damit das Beenden nicht an einem toten Server hängt. */
void sond_module_set_abort(GtkWidget *module, SondModuleAbortFunc func,
        gpointer user_data);

/* Führt den Abbruch-Handler des Moduls aus, falls es einen hat und er noch nicht
 * gelaufen ist. */
void sond_module_abort(GtkWidget *module);

/* Führt die Abbruch-Handler aller Module im Stack aus, die aktive Seite zuerst. */
void sond_module_abort_stack(GtkStack *stack);

G_END_DECLS

#endif /* SOND_MODULE_H */
