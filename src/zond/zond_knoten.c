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

#include <string.h>

#include "../sond_log_and_error.h"
#include "../znd/zond_error.h"

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

gint zond_knoten_extern_verlinkt(ZondDBase *dbase, gint node_id, GError **error) {
	gboolean found = FALSE;

	if (zond_dbase_has_external_links(dbase, node_id, &found, error))
		return -1;

	return found ? 2 : 0;
}

gint zond_knoten_loeschpruefung(ZondDBase *dbase, gint node_id, gint type,
		gint *baum_inhalt_file, GError **error) {
	gint baum_auswertung_copy = 0;
	gint rc = 0;

	*baum_inhalt_file = 0;

	if (zond_dbase_get_baum_auswertung_copy(dbase, node_id, &baum_auswertung_copy, error))
		return -1;

	if (baum_auswertung_copy)
		return 1;

	//Prüfen, ob mitzulöschendes Kind von PDF_ABSCHNITT als BAUM_AUSWERTUNG_COPY angebunden ist
	if (type == ZOND_DBASE_TYPE_FILE_PART) {
		gboolean copied = FALSE;

		if (zond_dbase_is_file_part_copied(dbase, node_id, &copied, error))
			return -1;

		if (copied)
			return 1;
	}

	//Ist node_id ein Abschnitt, der über baum_inhalt_file angebunden ist, wird letztere
	//gelöscht, also die Anbindung im Inhaltsbaum
	if (zond_dbase_get_baum_inhalt_file_from_file_part(dbase, node_id, baum_inhalt_file,
			error))
		return -1;

	if (*baum_inhalt_file)
		node_id = *baum_inhalt_file;

	//node_id ist jetzt der Knoten, der tatsächlich gelöscht wird
	rc = zond_knoten_extern_verlinkt(dbase, node_id, error);

	return rc;
}

gint zond_knoten_aufloesen(ZondDBase *dbase, gint node_id, gint type, gint *baum_inhalt_file_out,
		GError **error) {
	gint baum_inhalt_file = 0;
	gint id_anchor = 0;
	gint (*fp_relative)(ZondDBase*, gint, gint*, GError**) = zond_dbase_get_first_child;
	gint relative = 0;

	if (baum_inhalt_file_out)
		*baum_inhalt_file_out = 0;

	if (type != ZOND_DBASE_TYPE_BAUM_STRUKT) {
		gint baum_auswertung_copy = 0;

		//Test, ob als Copy angebunden: dann nicht aufgelöst
		if (zond_dbase_get_baum_auswertung_copy(dbase, node_id, &baum_auswertung_copy, error))
			return -1;

		if (baum_auswertung_copy)
			return 1;

		//Test, ob direkt angebunden
		if (zond_dbase_get_baum_inhalt_file_from_file_part(dbase, node_id, &baum_inhalt_file,
				error))
			return -1;
	}

	if (zond_dbase_begin(dbase, error))
		return -1;

	id_anchor = baum_inhalt_file ? baum_inhalt_file : node_id;

	do {
		if (fp_relative(dbase, node_id, &relative, error))
			ERROR_ROLLBACK_Z(dbase)

		if (relative == 0)
			break;

		if (baum_inhalt_file) {
			gint node_inserted = zond_dbase_insert_node(dbase, id_anchor, FALSE,
					ZOND_DBASE_TYPE_BAUM_INHALT_FILE, relative, NULL, NULL, NULL, NULL, NULL,
					error);

			if (node_inserted == -1)
				ERROR_ROLLBACK_Z(dbase)

			//wenn baum_inhalt_file, dann werden die Kinder von node_id nicht verschoben;
			//ab dem zweiten Durchgang sind die jüngeren Geschwister dran
			fp_relative = zond_dbase_get_younger_sibling;
			//und zwar die des ersten Kindes
			node_id = relative;

			id_anchor = node_inserted;
		} else {
			//Kind verschieben
			if (zond_dbase_verschieben_knoten(dbase, relative, id_anchor, FALSE, error))
				ERROR_ROLLBACK_Z(dbase)

			id_anchor = relative;
		}
	} while (1);

	if (zond_dbase_remove_node(dbase, baum_inhalt_file ? baum_inhalt_file : node_id, error))
		ERROR_ROLLBACK_Z(dbase)

	if (zond_dbase_commit(dbase, error))
		ERROR_ROLLBACK_Z(dbase)

	if (baum_inhalt_file_out)
		*baum_inhalt_file_out = baum_inhalt_file;

	return 0;
}

//Wurzel-IDs der beiden Bäume in der Datenbank (BAUM_INHALT, BAUM_AUSWERTUNG in zond_init.h)
#define WURZEL_INHALT 1
#define WURZEL_AUSWERTUNG 2

void zond_knoten_kopie_clear(ZondKopie *kopie) {
	g_clear_pointer(&kopie->icon_name, g_free);
	g_clear_pointer(&kopie->node_text, g_free);
	g_clear_pointer(&kopie->text, g_free);
}

gint zond_knoten_kopie_vorbereiten(ZondDBase *dbase, gint node_id, ZondKopie *kopie,
		GError **error) {
	gint type = 0;
	gint link = 0;

	memset(kopie, 0, sizeof(ZondKopie));

	if (zond_dbase_get_node(dbase, node_id, &type, &link, NULL, NULL, &kopie->icon_name,
			&kopie->node_text, &kopie->text, error))
		return -1;

	//Verschachtelter Link (Kind eines kopierten Punktes): bleibt Link auf dasselbe Ziel
	if (type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK) {
		gint root = 0;

		zond_knoten_kopie_clear(kopie);

		if (zond_knoten_get_root(dbase, link, &root, error))
			return -1;

		if (root != WURZEL_INHALT && root != WURZEL_AUSWERTUNG) {
			if (error)
				*error = g_error_new(ZOND_ERROR, 0,
						"%s\nLink-Ziel (Knoten %d) hat keine gültige "
						"Baum-Wurzel (root=%d)", __func__, link, root);

			return -1;
		}

		kopie->type = ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK;
		kopie->link = link;
		kopie->link_root = root;

		return 0;
	}

	if (type == ZOND_DBASE_TYPE_BAUM_STRUKT) {
		kopie->type = ZOND_DBASE_TYPE_BAUM_STRUKT;

		return 0;
	}

	kopie->type = ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY;
	kopie->link = (type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY
			|| type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE) ? link : node_id;

	/* BAUM_INHALT_FILE ist der reine Anker der Anbindung und trägt selbst kein icon_name/
	 * node_text/text. Angezeigt werden muss die verlinkte, dauerhafte file_part-Zeile, sonst
	 * bleibt die Kopie unbeschriftet. */
	if (type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE) {
		zond_knoten_kopie_clear(kopie);

		if (zond_dbase_get_node(dbase, link, NULL, NULL, NULL, NULL, &kopie->icon_name,
				&kopie->node_text, &kopie->text, error)) {
			zond_knoten_kopie_clear(kopie);

			return -1;
		}

		if (zond_dbase_get_first_child(dbase, link, &kopie->erstes_kind, error)) {
			zond_knoten_kopie_clear(kopie);

			return -1;
		}
	}

	return 0;
}

gint zond_knoten_kopie_anlegen(ZondDBase *dbase, ZondKopie const *kopie, gint anchor_id,
		gboolean child, gint *id, GError **error) {
	gint node_id_new = 0;

	if (kopie->type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK)
		return zond_knoten_neuer_link(dbase, anchor_id, child, kopie->link, id, error);

	node_id_new = zond_dbase_insert_node(dbase, anchor_id, child, kopie->type,
			kopie->link, NULL, NULL, kopie->icon_name, kopie->node_text, kopie->text,
			error);

	if (node_id_new == -1) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	if (id)
		*id = node_id_new;

	return 0;
}

gint zond_knoten_neuer_link(ZondDBase *dbase, gint anchor_id, gboolean child, gint ziel_id,
		gint *id, GError **error) {
	gint node_id_new = zond_dbase_insert_node(dbase, anchor_id, child,
			ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK, ziel_id, NULL, NULL, NULL, NULL, NULL,
			error);

	if (node_id_new == -1) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	if (id)
		*id = node_id_new;

	return 0;
}
