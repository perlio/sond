/*
 zond (sond_projekt_index.h) - Akten, Beweisstücke, Unterlagen
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

/*
 * Index-Auskünfte zu Dingen der Projektdatei (Anbindungen und ihre Abschnitte). Frei von GTK und
 * von den Bäumen; zond (GTK3) und der Projekt-Baum des Clients (GTK4) nutzen dieselben Funktionen.
 */

#ifndef SOND_PROJEKT_INDEX_H_INCLUDED
#define SOND_PROJEKT_INDEX_H_INCLUDED

#include <glib.h>

#include "../sond_index.h"
#include "../sond_process_file.h"

G_BEGIN_DECLS

/* Index-Status eines Abschnitts (einer Anbindung): des Seitenbereichs section ({{von}{bis}} oder
 * {Punkt}, s. anbindung.h) in der Datei filepart ("a.pdf", "x.zip//b.pdf"). NONE, wenn nicht
 * indiziert oder wenn Kontext, Filepart oder Abschnitt fehlen. Abschnitte gibt es nur zu PDF; ob
 * der Dateityp indizierbar ist, prüft der Aufrufer nicht, PDF ist es immer. */
SondIndexStatus sond_projekt_index_status_abschnitt(SondIndexCtx *index_ctx,
		gchar const *filepart, gchar const *section);

/* Löscht den Index der Auswahl ht (Schlüssel SondFilePart, Wert SondPageRange* oder NULL für die
 * ganze Datei, s. tvfm/sond_tvfm_fileparts.h) in einer Transaktion. Es ändert nichts am
 * Dateisystem und nichts an Anbindungen. project_dir: Projektverzeichnis (für die Pfade der
 * Index-Löschung). Ein Fehler bei einem Eintrag betrifft die anderen nicht; Rückgabe: Zahl der
 * Einträge, die sich nicht löschen ließen (0 = alles gelöscht). */
gint sond_projekt_index_loeschen(SondIndexCtx *index_ctx, GHashTable *ht, gchar const *project_dir);

G_END_DECLS

#endif /* SOND_PROJEKT_INDEX_H_INCLUDED */
