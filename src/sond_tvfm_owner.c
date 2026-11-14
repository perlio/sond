/*
 sond (sond_tvfm_owner.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_tvfm_owner.h"

G_DEFINE_INTERFACE(SondTVFMOwner, sond_tvfm_owner, G_TYPE_OBJECT)

static void sond_tvfm_owner_default_init(SondTVFMOwnerInterface *iface) {
	(void) iface;
}

#define OWNER_IFACE(o) SOND_TVFM_OWNER_GET_IFACE(o)

gchar const* sond_tvfm_owner_get_root(SondTVFMOwner *owner) {
	g_return_val_if_fail(SOND_IS_TVFM_OWNER(owner), NULL);

	return OWNER_IFACE(owner)->get_root ? OWNER_IFACE(owner)->get_root(owner) : NULL;
}

gboolean sond_tvfm_owner_is_seadrive_path(SondTVFMOwner *owner) {
	g_return_val_if_fail(SOND_IS_TVFM_OWNER(owner), FALSE);

	return OWNER_IFACE(owner)->is_seadrive_path ?
			OWNER_IFACE(owner)->is_seadrive_path(owner) : FALSE;
}

gboolean sond_tvfm_owner_supports_sections(SondTVFMOwner *owner) {
	g_return_val_if_fail(SOND_IS_TVFM_OWNER(owner), FALSE);

	return OWNER_IFACE(owner)->supports_sections &&
			OWNER_IFACE(owner)->supports_sections(owner) &&
			OWNER_IFACE(owner)->has_sections;
}

gboolean sond_tvfm_owner_has_sections(SondTVFMOwner *owner, SondTVFMItem *item) {
	if (!sond_tvfm_owner_supports_sections(owner))
		return FALSE;

	return OWNER_IFACE(owner)->has_sections(owner, item);
}

gboolean sond_tvfm_owner_supports_load_sections(SondTVFMOwner *owner) {
	g_return_val_if_fail(SOND_IS_TVFM_OWNER(owner), FALSE);

	return OWNER_IFACE(owner)->supports_load_sections &&
			OWNER_IFACE(owner)->supports_load_sections(owner) &&
			OWNER_IFACE(owner)->load_sections;
}

gint sond_tvfm_owner_load_sections(SondTVFMOwner *owner, SondTVFMItem *item,
		GPtrArray **arr_children, GError **error) {
	if (!sond_tvfm_owner_supports_load_sections(owner))
		return -1;

	return OWNER_IFACE(owner)->load_sections(owner, item, arr_children, error);
}

gboolean sond_tvfm_owner_supports_delete_section(SondTVFMOwner *owner) {
	g_return_val_if_fail(SOND_IS_TVFM_OWNER(owner), FALSE);

	return OWNER_IFACE(owner)->supports_delete_section &&
			OWNER_IFACE(owner)->supports_delete_section(owner) &&
			OWNER_IFACE(owner)->delete_section;
}

gint sond_tvfm_owner_delete_section(SondTVFMOwner *owner, SondTVFMItem *item,
		GError **error) {
	if (!sond_tvfm_owner_supports_delete_section(owner))
		return 0;

	return OWNER_IFACE(owner)->delete_section(owner, item, error);
}

gint sond_tvfm_owner_before_move(SondTVFMOwner *owner, SondTVFMItem *item,
		SondTVFMItem *parent, gchar const *base, gint index_to, GError **error,
		gpointer *ctx) {
	g_return_val_if_fail(SOND_IS_TVFM_OWNER(owner), 0);

	if (!OWNER_IFACE(owner)->before_move)
		return 0;

	return OWNER_IFACE(owner)->before_move(owner, item, parent, base, index_to,
			error, ctx);
}

gint sond_tvfm_owner_before_delete(SondTVFMOwner *owner, SondTVFMItem *item,
		GError **error, gpointer *ctx) {
	g_return_val_if_fail(SOND_IS_TVFM_OWNER(owner), 0);

	if (!OWNER_IFACE(owner)->before_delete)
		return 0;

	return OWNER_IFACE(owner)->before_delete(owner, item, error, ctx);
}

gint sond_tvfm_owner_before_insert(SondTVFMOwner *owner, SondTVFMItem *item,
		SondTVFMItem *parent, gchar const *base, gint index_to, GError **error) {
	g_return_val_if_fail(SOND_IS_TVFM_OWNER(owner), 0);

	if (!OWNER_IFACE(owner)->before_insert)
		return 0;

	return OWNER_IFACE(owner)->before_insert(owner, item, parent, base,
			index_to, error);
}

void sond_tvfm_owner_after(SondTVFMOwner *owner, gboolean success, gpointer ctx) {
	g_return_if_fail(SOND_IS_TVFM_OWNER(owner));

	if (OWNER_IFACE(owner)->after)
		OWNER_IFACE(owner)->after(owner, success, ctx);
}
