/*
 sond (sond_tvfm_badges.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_tvfm_badges.h
 * @brief Welche Overlay-Symbole (Badges) zeigt ein Eintrag des Dateibaums?
 *
 * Aus sond_treeviewfm_render_file_icon() herausgelöste Entscheidungslogik, damit
 * die GTK3-Fassung (Zellrenderer) und die GTK4-Fassung (Zellwidgets) dieselben
 * Regeln anwenden. Zeichnen und Zusammensetzen der Symbole bleibt Sache der
 * Ansicht (sond_icon_util.h für die Badge-Pixbufs).
 *
 * Die Zustände, die ein Baum selbst vorhält (Datei-Badges und Ordner-Statistik des
 * SeaDrive-Watchers, Indexstatus), liefert er über SondTVFMBadgeSources.
 */

#ifndef SOND_TVFM_BADGES_H_INCLUDED
#define SOND_TVFM_BADGES_H_INCLUDED

#include <glib.h>

#include "../sond_icon_util.h"
#include "../sond_fileparts.h"
#include "sond_tvfm_item.h"
#include "sond_tvfm_owner.h"

G_BEGIN_DECLS

typedef struct {
	SondSeadriveBadge seadrive;       //unten rechts: Datei bzw. Ordner selbst
	SondSeadriveDirStatus dir_status; //unten rechts: Ordner-Teilbaum (nur wenn seadrive NONE)
	SondIndexStatus index;            //unten links: Indizierung
	gboolean attachment;              //oben rechts: Anhang einer E-Mail
	SondLock lock;                    //oben links: passwortgeschützt/entsperrt
} SondTVFMBadges;

/* Quellen, die der Baum liefert (jede darf NULL sein = nichts bekannt) */
typedef struct {
	/* SeaDrive-Zustand einer Datei (voller Pfad) */
	SondSeadriveBadge (*file_badge)(gpointer data, gchar const *full_path);
	/* aggregierter SeaDrive-Zustand eines Ordners (voller Pfad) */
	SondSeadriveDirStatus (*dir_status)(gpointer data, gchar const *full_path);
	/* Indizierungsstatus eines Eintrags */
	SondIndexStatus (*index_status)(gpointer data, SondTVFMItem *item);
} SondTVFMBadgeSources;

/* Ermittelt die Badges des Eintrags. Der SeaDrive-Zustand gehört zur realen Datei:
 * bei Einträgen in Containern (ZIP-Einträge, Mimeparts, PDF-Seiten) gilt der der
 * obersten Datei. Nur unter Windows und nur auf SeaDrive-Wurzeln gibt es einen
 * SeaDrive-Zustand. */
void sond_tvfm_item_get_badges(SondTVFMItem *item, SondTVFMOwner *owner,
		SondTVFMBadgeSources const *sources, gpointer sources_data,
		SondTVFMBadges *badges);

/* Zustand einer Datei direkt aus den Dateiattributen (ohne Watcher), mit derselben
 * Priorität wie sonst: gepinnt und nicht geladen = PENDING, nicht geladen =
 * OFFLINE, gepinnt und geladen = PINNED, sonst NONE. Nur unter Windows, sonst NONE. */
SondSeadriveBadge sond_tvfm_badge_seadrive_live(gchar const *full_path);

/* Kurze Beschreibung für einen Hinweistext; NULL, wenn es nichts zu sagen gibt */
gchar const* sond_tvfm_badge_seadrive_text(SondSeadriveBadge badge);

gchar const* sond_tvfm_badge_dir_status_text(SondSeadriveDirStatus status);

G_END_DECLS

#endif // SOND_TVFM_BADGES_H_INCLUDED
