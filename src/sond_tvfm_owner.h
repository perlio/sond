/*
 sond (sond_tvfm_owner.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_tvfm_owner.h
 * @brief Schnittstelle zwischen SondTVFMItem und dem Baum, der es anzeigt
 *
 * Ein SondTVFMItem braucht vom Baum, zu dem es gehört, nur wenige Angaben und
 * Rückmeldungen: die Wurzel des Projektverzeichnisses, ob sie in SeaDrive liegt,
 * die Behandlung von Abschnitten (Anbindungen) und die Signale vor und nach dem
 * Verschieben, Löschen und Einfügen. Diese Schnittstelle trennt das Item (GTK-frei)
 * vom konkreten Baum (GTK3: SondTreeviewFM, GTK4: die neue Ansicht). Der Baum
 * implementiert sie.
 *
 * Frei von GTK.
 */

#ifndef SOND_TVFM_OWNER_H_INCLUDED
#define SOND_TVFM_OWNER_H_INCLUDED

#include <glib-object.h>

#include "sond_tvfm_item.h"

G_BEGIN_DECLS

#define SOND_TYPE_TVFM_OWNER sond_tvfm_owner_get_type( )
G_DECLARE_INTERFACE(SondTVFMOwner, sond_tvfm_owner, SOND, TVFM_OWNER, GObject)

/* Alle Einträge außer get_root und is_seadrive_path sind optional (NULL = nicht
 * unterstützt, Rückgabe wie "nichts zu tun"). */
struct _SondTVFMOwnerInterface {
	GTypeInterface parent_iface;

	/* Wurzel des Projektverzeichnisses (nicht freigeben) */
	gchar const* (*get_root)(SondTVFMOwner*);
	/* Liegt die Wurzel im SeaDrive-Laufwerk? */
	gboolean (*is_seadrive_path)(SondTVFMOwner*);
	/* Soll das Einlesen eines Verzeichnisses den Inhalt der Dateien lesen (Typ
	 * nach den ersten Bytes, PDF auf Anhänge prüfen)? NULL/TRUE: ja. FALSE: der
	 * Typ ergibt sich nur aus der Endung, keine Datei wird geöffnet (schnell, löst
	 * keinen Download aus). */
	gboolean (*read_file_contents)(SondTVFMOwner*);

	/* Abschnitte (Anbindungen) eines Items: ob sie unterstützt werden, ob das Item
	 * welche hat, sie laden, einen löschen */
	gboolean (*supports_sections)(SondTVFMOwner*);
	gboolean (*has_sections)(SondTVFMOwner*, SondTVFMItem*);
	gboolean (*supports_load_sections)(SondTVFMOwner*);
	gint (*load_sections)(SondTVFMOwner*, SondTVFMItem*, GPtrArray**, GError**);
	gboolean (*supports_delete_section)(SondTVFMOwner*);
	gint (*delete_section)(SondTVFMOwner*, SondTVFMItem*, GError**);

	/* Signale vor und nach Änderungen. Rückgabe 0 = weiter, -1 = Fehler,
	 * 1 = (vor dem Löschen) nichts weiter tun; *ctx ist der Kontext einer
	 * Transaktion, der an after() zurückgegeben wird. */
	gint (*before_move)(SondTVFMOwner*, SondTVFMItem *item, SondTVFMItem *parent,
			gchar const *base, gint index_to, GError **error, gpointer *ctx);
	gint (*before_delete)(SondTVFMOwner*, SondTVFMItem *item, GError **error,
			gpointer *ctx);
	gint (*before_insert)(SondTVFMOwner*, SondTVFMItem *item, SondTVFMItem *parent,
			gchar const *base, gint index_to, GError **error);
	void (*after)(SondTVFMOwner*, gboolean success, gpointer ctx);
};

/* Aufrufhilfen: prüfen auf nicht gesetzte optionale Einträge */
gchar const* sond_tvfm_owner_get_root(SondTVFMOwner*);

gboolean sond_tvfm_owner_is_seadrive_path(SondTVFMOwner*);

gboolean sond_tvfm_owner_read_file_contents(SondTVFMOwner*);

gboolean sond_tvfm_owner_supports_sections(SondTVFMOwner*);

gboolean sond_tvfm_owner_has_sections(SondTVFMOwner*, SondTVFMItem*);

gboolean sond_tvfm_owner_supports_load_sections(SondTVFMOwner*);

gint sond_tvfm_owner_load_sections(SondTVFMOwner*, SondTVFMItem*, GPtrArray**,
		GError**);

gboolean sond_tvfm_owner_supports_delete_section(SondTVFMOwner*);

gint sond_tvfm_owner_delete_section(SondTVFMOwner*, SondTVFMItem*, GError**);

gint sond_tvfm_owner_before_move(SondTVFMOwner*, SondTVFMItem *item,
		SondTVFMItem *parent, gchar const *base, gint index_to, GError **error,
		gpointer *ctx);

gint sond_tvfm_owner_before_delete(SondTVFMOwner*, SondTVFMItem *item,
		GError **error, gpointer *ctx);

gint sond_tvfm_owner_before_insert(SondTVFMOwner*, SondTVFMItem *item,
		SondTVFMItem *parent, gchar const *base, gint index_to, GError **error);

void sond_tvfm_owner_after(SondTVFMOwner*, gboolean success, gpointer ctx);

G_END_DECLS

#endif // SOND_TVFM_OWNER_H_INCLUDED
