/*
 sond (sond_tvfm_preflight.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_tvfm_preflight.h
 * @brief Passwort-Vorprüfung vor Kopieren und Verschieben (GTK-frei)
 *
 * Stellt für alle gesperrten ZIP-Einträge unter den Quellen, deren Inhalt gelesen werden muss,
 * das Passwort sicher (still mit gemerkten, sonst über den Dialog von sond_password.h), bevor
 * irgendetwas kopiert oder verschoben wird. Nicht lesbare Einträge werden beim Kopieren
 * vorgemerkt (sond_password_action_skip_add()), beim Verschieben bricht die Aktion ab.
 * Der Aufrufer umschließt den Ablauf mit sond_password_action_begin(!move) und
 * sond_password_action_end().
 */

#ifndef SOND_TVFM_PREFLIGHT_H_INCLUDED
#define SOND_TVFM_PREFLIGHT_H_INCLUDED

#include <glib.h>

#include "sond_tvfm_item.h"

G_BEGIN_DECLS

typedef struct {
	gboolean aborted; //Nutzer hat abgebrochen bzw. beim Verschieben übersprungen
	gchar *failure;   //Fehlermeldung, wenn nicht gefragt werden konnte (g_free)
} SondTVFMPreflightResult;

/* items: Quellen (GPtrArray von SondTVFMItem), parent: Ziel (Verzeichnis oder Container;
 * NULL = Wurzel), move: Verschieben statt Kopieren. TRUE: weitermachen. FALSE: nichts
 * kopieren/verschieben (res->aborted bzw. res->failure beachten). res ist vorher mit
 * {FALSE, NULL} zu belegen; res->failure gibt der Aufrufer frei. */
gboolean sond_tvfm_preflight(GPtrArray *items, SondTVFMItem *parent, gboolean move,
		SondTVFMPreflightResult *res);

G_END_DECLS

#endif // SOND_TVFM_PREFLIGHT_H_INCLUDED
