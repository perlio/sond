/*
 zond (zond_knoten.c) - Akten, Beweisstücke, Unterlagen
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

#include "zond_knoten.h"

#include "../sond_log_and_error.h"

void zond_knoten_free(ZondKnoten *node) {
	if (!node)
		return;

	g_free(node->icon_name);
	g_free(node->node_text);
	g_free(node);
}

/* Liest die Zeile node_id. 0: *node gesetzt, 1: defekte Anbindung, nicht anzeigen (*node NULL),
 * -1: Fehler */
static gint node_new(ZondDBase *dbase, gint node_id, ZondKnoten **node, GError **error) {
	ZondKnoten *n = g_new0(ZondKnoten, 1);
	gint link = 0;
	gint first_child = 0;

	*node = NULL;
	n->db_id = node_id;
	n->id = node_id;

	if (zond_dbase_get_node(dbase, node_id, &n->type, &link, NULL, NULL, &n->icon_name,
			&n->node_text, NULL, error)) {
		zond_knoten_free(n);

		return -1;
	}

	if (n->type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE) {
		GError *error_file = NULL;

		//angezeigt wird die Wurzel der Datei; Icon und Text kommen von ihr
		g_clear_pointer(&n->icon_name, g_free);
		g_clear_pointer(&n->node_text, g_free);
		n->id = link;

		if (zond_dbase_get_node(dbase, link, NULL, NULL, NULL, NULL, &n->icon_name,
				&n->node_text, NULL, &error_file)) {
			LOG_WARN("%s: Anbindung %d wird nicht angezeigt: %s", __func__, node_id,
					error_file ? error_file->message : "?");
			g_clear_error(&error_file);
			zond_knoten_free(n);

			return 1;
		}
	} else if (n->type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK) {
		g_clear_pointer(&n->icon_name, g_free);
		g_clear_pointer(&n->node_text, g_free);
		n->id = link;
		n->head_nr = node_id;
	}

	if (zond_dbase_get_first_child(dbase, n->id, &first_child, error)) {
		zond_knoten_free(n);

		return -1;
	}
	n->has_children = first_child > 0;

	*node = n;

	return 0;
}

gint zond_knoten_get_children(ZondDBase *dbase, gint parent_id, GPtrArray **nodes,
		GError **error) {
	GPtrArray *arr = g_ptr_array_new_with_free_func((GDestroyNotify) zond_knoten_free);
	gint node_id = 0;

	g_return_val_if_fail(dbase && nodes, -1);

	if (zond_dbase_get_first_child(dbase, parent_id, &node_id, error)) {
		g_ptr_array_unref(arr);

		return -1;
	}

	while (node_id > 0) {
		ZondKnoten *node = NULL;
		gint younger = 0;
		gint rc = node_new(dbase, node_id, &node, error);

		if (rc == -1) {
			g_ptr_array_unref(arr);

			return -1;
		}
		if (node)
			g_ptr_array_add(arr, node);

		if (zond_dbase_get_younger_sibling(dbase, node_id, &younger, error)) {
			g_ptr_array_unref(arr);

			return -1;
		}
		node_id = younger;
	}

	*nodes = arr;

	return 0;
}

gint zond_knoten_get_root(ZondDBase *dbase, gint node_id, gint *root, GError **error) {
	gint type = 0;

	if (zond_dbase_get_type_and_link(dbase, node_id, &type, NULL, error))
		return -1;

	if (type == ZOND_DBASE_TYPE_FILE_PART) {
		gint baum_inhalt_file = 0;

		//angebundener FILE_PART: der Baum seiner Anbindung
		if (zond_dbase_find_baum_inhalt_file(dbase, node_id, &baum_inhalt_file, NULL, NULL,
				error))
			return -1;

		if (baum_inhalt_file)
			node_id = baum_inhalt_file;
	}

	if (zond_dbase_get_tree_root(dbase, node_id, root, error))
		return -1;

	return 0;
}

gint zond_knoten_anker_pruefen(ZondDBase *dbase, gint *anchor_id, gboolean child,
		GError **error) {
	gint type = 0;

	if (*anchor_id == 0) {
		LOG_WARN("%s: anchor_id == 0", __func__);

		return 0;
	}

	if (zond_dbase_get_type_and_link(dbase, *anchor_id, &type, NULL, error))
		return -1;

	if (type == ZOND_DBASE_TYPE_FILE_PART) {
		gint baum_inhalt_file = 0;
		gint file_part_angebunden = 0;

		if (zond_dbase_find_baum_inhalt_file(dbase, *anchor_id, &baum_inhalt_file,
				&file_part_angebunden, NULL, error))
			return -1;

		if (baum_inhalt_file) { //anchor ist angebunden
			if (*anchor_id != file_part_angebunden || child) { //nicht unmittelbar, sondern Vorfahren
				*anchor_id = 0;

				return 1; //child egal
			}

			*anchor_id = baum_inhalt_file;
		}
	}

	return 0;
}

gint zond_knoten_neuer_punkt(ZondDBase *dbase, gint anchor_id, gboolean child,
		gchar const *icon_name, gchar const *node_text, gint *id, GError **error) {
	gint node_id_new = zond_dbase_insert_node(dbase, anchor_id, child,
			ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL, icon_name, node_text, NULL, error);

	if (node_id_new == -1)
		return -1;

	if (id)
		*id = node_id_new;

	return 0;
}

gint zond_knoten_verschieben(ZondDBase *dbase, gint node_id, gboolean in_inhalt,
		gint anchor_id, gboolean child, gint *moved_id, GError **error) {
	//im Inhaltsbaum wird das Ziel (der FILE_PART) nicht verschoben, wohl aber seine Anbindung
	if (in_inhalt) {
		gint type = 0;

		if (zond_dbase_get_type_and_link(dbase, node_id, &type, NULL, error) == -1)
			return -1;

		if (type == ZOND_DBASE_TYPE_FILE_PART) {
			gint baum_inhalt_file = 0;

			if (zond_dbase_get_baum_inhalt_file_from_file_part(dbase, node_id,
					&baum_inhalt_file, error) == -1)
				return -1;

			if (!baum_inhalt_file)
				return 1;

			node_id = baum_inhalt_file;
		}
	}

	if (zond_dbase_verschieben_knoten(dbase, node_id, anchor_id, child, error))
		return -1;

	if (moved_id)
		*moved_id = node_id;

	return 0;
}
