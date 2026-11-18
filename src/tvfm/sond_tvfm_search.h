/*
 sond (sond_tvfm_search.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_tvfm_search.h
 * @brief Dateisuche nach Namen über SondTVFMItem (GTK-frei)
 *
 * Durchsucht Verzeichnisse und (wahlweise) Container unterhalb von Startitems.
 * Der Aufruf ist synchron und für einen Hintergrundthread gedacht; der Abbruch
 * läuft über ein atomares Flag (sond_tvfm_search_cancel()). Dialog, Fortschritt
 * und Ergebnisanzeige sind Sache des Baums.
 *
 * Frei von GTK.
 */

#ifndef SOND_TVFM_SEARCH_H_INCLUDED
#define SOND_TVFM_SEARCH_H_INCLUDED

#include <glib.h>

#include "sond_tvfm_item.h"
#include "sond_tvfm_owner.h"

G_BEGIN_DECLS

typedef enum {
	SOND_TVFM_SEARCH_CONTAINS, //Name enthält Text
	SOND_TVFM_SEARCH_WHOLE_NAME, //Name ist gleich Text
	SOND_TVFM_SEARCH_WILDCARD //Text ist Muster mit * und ?
} SondTVFMSearchMode;

typedef struct {
	gchar const *text;
	SondTVFMSearchMode mode;
	gboolean case_sensitive;
	gboolean match_files; //Dateien (auch Container) als Treffer
	gboolean match_dirs; //Verzeichnisse als Treffer
	gboolean in_containers; //ZIP, PDF-Anhänge, E-Mails durchsuchen
} SondTVFMSearchOpts;

typedef struct _SondTVFMSearch SondTVFMSearch;

/* owner: Baum, zu dem die Items gehören (für Wurzel und SeaDrive-Prüfung).
 * opts->text darf nicht leer sein; die Optionen werden kopiert. */
SondTVFMSearch* sond_tvfm_search_new(SondTVFMOwner *owner,
		SondTVFMSearchOpts const *opts);

/* Durchsucht die Items in start (GPtrArray von SondTVFMItem). 0 oder -1 mit
 * error (nur Lesefehler eines Startitems). Bei Abbruch 0 mit den bis dahin
 * gefundenen Treffern. */
gint sond_tvfm_search_run(SondTVFMSearch*, GPtrArray *start, GError **error);

/* Thread-sicher; die laufende Suche endet beim nächsten Item. */
void sond_tvfm_search_cancel(SondTVFMSearch*);

/* Treffer als Fileparts (GPtrArray mit g_free) in der Form, die
 * sond_treeviewfm_file_part_visible() erwartet. Der Aufrufer löst sie mit
 * g_ptr_array_unref(); gilt nur einmal, danach NULL. */
GPtrArray* sond_tvfm_search_take_hits(SondTVFMSearch*);

/* Zahl nicht lesbarer, übersprungener Verzeichnisse */
guint sond_tvfm_search_get_n_skipped(SondTVFMSearch*);

/* Zahl nicht geladener SeaDrive-Container (ZIP, E-Mail), die nicht durchsucht
 * wurden (die Suche löst keinen Download aus). */
guint sond_tvfm_search_get_n_not_loaded(SondTVFMSearch*);

void sond_tvfm_search_free(SondTVFMSearch*);

G_END_DECLS

#endif // SOND_TVFM_SEARCH_H_INCLUDED
