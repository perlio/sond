/*
 zond (verwendung.c) - Akten, Beweisstücke, Unterlagen
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

/* "Herkunft und Verwendung" (#186): zeigt zu einem beliebigen Knoten
 * seinen Ursprung und alles, was davon abgeleitet ist.
 *
 * Ursprung ist entweder ein Strukturpunkt oder die Wurzel eines file_part
 * (FILE_PART-Knoten mit parent_ID=0). FILE_PART-Knoten bilden einen eigenen
 * Baum (Datei -> Sections -> Unter-Sections). Auf jeden FILE_PART können
 * zeigen: 0-1 Anker (BAUM_INHALT_FILE = Anbindung), 0-n Copies und 0-n
 * Links; auf jede Copy und jeden Strukturpunkt wiederum 0-n Links. */

#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>

#include "../../misc.h"
#include "../zond_init.h"
#include "../zond_dbase.h"
#include "../zond_tree_store.h"

#include "project.h"
#include "suchen.h"
#include "verwendung.h"

//Tiefe, ab der ein Elternpfad bzw. eine Section-Rekursion abgebrochen wird
#define VERWENDUNG_MAX_TIEFE 64

typedef enum {
	KIND_ENTHALTEN_IN,
	KIND_DATEI,
	KIND_SECTION,
	KIND_ANBINDUNG,
	KIND_IN_ANBINDUNG,
	KIND_COPY,
	KIND_LINK,
	KIND_STRUKT,
	KIND_INDIREKT_GRUPPE,
	KIND_INDIREKT,
	KIND_NICHT_VERWENDET
} VerwendungKind;

enum {
	COL_KIND,
	COL_ICON,
	COL_ART,
	COL_LABEL,
	COL_TEXT,
	COL_TOOLTIP,
	COL_INFO,
	COL_BAUM, //Sprungziel: Baum (KEIN_BAUM = kein Sprung)
	COL_NODE_ID, //Sprungziel in BAUM_INHALT/BAUM_AUSWERTUNG
	COL_FILE_PART, //Sprungziel in BAUM_FS
	COL_SECTION,
	COL_KEY_ID, //knoten-ID, für die die Zeile steht (Hervorhebung)
	COL_WEIGHT,
	COL_LINK_ID, //nur Links: knoten-ID des Links (= head_nr der Kopfzeile)
	COL_TARGET_ID, //nur Links: Ziel des Links
	COL_TARGET_BAUM, //nur Links: Baum, in dem das Ziel angezeigt wird
	COL_ABSTIEG, //nur indirekt: IDs "a,b,x" vom Link-Ziel abwärts bis x
	COL_STYLE,
	NUM_COLS
};

typedef struct {
	Projekt *zond;
	GtkWidget *window;
	GtkWidget *treeview;
	GtkTreeStore *store;
	gint node_id;
	gchar *file_part;
	gchar *section;
} Verwendung;

static void verwendung_free(gpointer data) {
	Verwendung *v = data;

	g_object_unref(v->store);
	g_free(v->file_part);
	g_free(v->section);
	g_free(v);

	return;
}

static ZondDBase* verwendung_db(Verwendung *v) {
	return v->zond->dbase_zond->zond_dbase_work;
}

//Erste Zeile von text, auf 120 Zeichen gekürzt
static gchar* verwendung_text_kurz(gchar const *text) {
	gchar *kurz = NULL;
	gchar const *nl = NULL;

	if (!text || !*text)
		return NULL;

	nl = strchr(text, '\n');
	kurz = nl ? g_strndup(text, nl - text) : g_strdup(text);

	if (g_utf8_strlen(kurz, -1) > 120) {
		gchar *ende = g_utf8_offset_to_pointer(kurz, 119);

		*ende = '\0';
		ende = g_strconcat(kurz, "…", NULL);
		g_free(kurz);
		kurz = ende;
	} else if (nl) {
		gchar *mit = g_strconcat(kurz, " …", NULL);

		g_free(kurz);
		kurz = mit;
	}

	return kurz;
}

static void verwendung_add_row(Verwendung *v, GtkTreeIter *parent,
		GtkTreeIter *out, VerwendungKind kind, gchar const *icon,
		gchar const *art, gchar const *label, gchar const *text, gint baum,
		gint node_id, gchar const *file_part, gchar const *section,
		gint key_id) {
	GtkTreeIter iter = { 0 };
	gchar *kurz = verwendung_text_kurz(text);
	gchar *tooltip = (text && *text) ? g_markup_escape_text(text, -1) : NULL;

	gtk_tree_store_append(v->store, &iter, parent);
	gtk_tree_store_set(v->store, &iter, COL_KIND, kind, COL_ICON, icon,
			COL_ART, art, COL_LABEL, label, COL_TEXT, kurz, COL_TOOLTIP,
			tooltip, COL_INFO, NULL, COL_BAUM, baum, COL_NODE_ID, node_id,
			COL_FILE_PART, file_part, COL_SECTION, section, COL_KEY_ID, key_id,
			COL_WEIGHT, PANGO_WEIGHT_NORMAL, COL_STYLE, PANGO_STYLE_NORMAL, -1);

	g_free(kurz);
	g_free(tooltip);

	if (out)
		*out = iter;

	return;
}

/* Beschriftung eines Knotens für die Pfadanzeige - ein Anker
 * (BAUM_INHALT_FILE) trägt selbst keinen Text, dann zählt der FILE_PART. */
static gint verwendung_node_text(Verwendung *v, gint node_id, gchar **node_text,
		GError **error) {
	gint rc = 0;
	gint type = 0;
	gint link = 0;

	rc = zond_dbase_get_node(verwendung_db(v), node_id, &type, &link, NULL,
			NULL, NULL, node_text, NULL, error);
	if (rc)
		return -1;

	if (type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE) {
		g_free(*node_text);
		*node_text = NULL;

		rc = zond_dbase_get_node(verwendung_db(v), link, NULL, NULL, NULL,
				NULL, NULL, node_text, NULL, error);
		if (rc)
			return -1;
	}

	return 0;
}

//Pfad "A › B › C" von der obersten Ebene bis einschließlich node_id
static gint verwendung_pfad(Verwendung *v, gint node_id, gchar **pfad,
		GError **error) {
	GString *str = g_string_new(NULL);
	gint id = node_id;
	gint tiefe = 0;

	while (id > BAUM_AUSWERTUNG && tiefe++ < VERWENDUNG_MAX_TIEFE) {
		gchar *node_text = NULL;
		gint rc = 0;

		rc = verwendung_node_text(v, id, &node_text, error);
		if (rc) {
			g_string_free(str, TRUE);
			return -1;
		}

		if (str->len)
			g_string_prepend(str, " › ");
		g_string_prepend(str, node_text ? node_text : "?");
		g_free(node_text);

		rc = zond_dbase_get_parent(verwendung_db(v), id, &id, error);
		if (rc) {
			g_string_free(str, TRUE);
			return -1;
		}
	}

	*pfad = g_string_free(str, FALSE);

	return 0;
}

/* Alle Links, die auf target_id zeigen, als Kinder von parent. target_baum:
 * Baum, in dem das Ziel angezeigt wird (dort hängt die Kopfzeile des Links
 * in der links-Liste der Zielzeile). */
static gint verwendung_add_links(Verwendung *v, GtkTreeIter *parent,
		gint target_id, gint target_baum, GError **error) {
	GArray *arr = NULL;
	gint rc = 0;

	rc = zond_dbase_get_referrers(verwendung_db(v), target_id,
			ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK, &arr, error);
	if (rc)
		return -1;

	for (guint i = 0; i < arr->len; i++) {
		gint link_id = g_array_index(arr, gint, i);
		gint parent_id = 0;
		gint root = 0;
		gchar *pfad = NULL;
		gchar *label = NULL;
		GtkTreeIter iter_link = { 0 };

		rc = zond_dbase_get_parent(verwendung_db(v), link_id, &parent_id,
				error);
		if (rc)
			break;

		rc = zond_dbase_get_tree_root(verwendung_db(v), link_id, &root,
				error);
		if (rc)
			break;

		if (parent_id > BAUM_AUSWERTUNG) {
			rc = verwendung_pfad(v, parent_id, &pfad, error);
			if (rc)
				break;
			label = g_strconcat("unter: ", pfad, NULL);
		} else {
			label = g_strdup("oberste Ebene");
			parent_id = 0; //Wurzel ist keine Zeile im Baum
		}

		//Sprung auf die Kopfzeile des Links; node_id = Elternknoten als
		//Ersatz, falls die Kopfzeile nicht gefunden wird
		verwendung_add_row(v, parent, &iter_link, KIND_LINK,
				"emblem-symbolic-link", "Link", label, NULL,
				(root == BAUM_INHALT || root == BAUM_AUSWERTUNG) ?
						root : KEIN_BAUM, parent_id, NULL, NULL, link_id);
		gtk_tree_store_set(v->store, &iter_link, COL_LINK_ID, link_id,
				COL_TARGET_ID, target_id, COL_TARGET_BAUM, target_baum, -1);

		g_free(pfad);
		g_free(label);
	}

	g_array_unref(arr);

	return rc ? -1 : 0;
}

/* Vorfahren von node_id, wie sie im Baum angezeigt werden (ohne node_id
 * selbst, von unten nach oben). arr_anzeige: ID der Baumzeile (bei einer
 * Anbindung der FILE_PART), arr_alt: zusätzlich die Anker-ID (Altbestand:
 * Links auf den Anker) oder 0. Section: erst übergeordnete Sections bis zur
 * angebundenen, dann die Eltern von deren Anker in BAUM_INHALT. */
static gint verwendung_vorfahren(Verwendung *v, gint node_id,
		GArray **arr_anzeige, GArray **arr_alt, GError **error) {
	gint rc = 0;
	gint type = 0;
	gint id = node_id;
	gint tiefe = 0;
	gint null = 0;

	*arr_anzeige = g_array_new(FALSE, FALSE, sizeof(gint));
	*arr_alt = g_array_new(FALSE, FALSE, sizeof(gint));

	rc = zond_dbase_get_type_and_link(verwendung_db(v), node_id, &type, NULL,
			error);
	if (rc)
		return -1;

	if (type == ZOND_DBASE_TYPE_FILE_PART) {
		gint fp = node_id;

		id = 0;
		while (tiefe++ < VERWENDUNG_MAX_TIEFE) {
			GArray *arr_anker = NULL;
			gint parent = 0;
			gint type_parent = 0;

			rc = zond_dbase_get_referrers(verwendung_db(v), fp,
					ZOND_DBASE_TYPE_BAUM_INHALT_FILE, &arr_anker, error);
			if (rc)
				return -1;
			if (arr_anker->len)
				id = g_array_index(arr_anker, gint, 0);
			g_array_unref(arr_anker);
			if (id) {
				//angebundene übergeordnete Section: auch Links auf den Anker
				if (fp != node_id)
					g_array_index(*arr_alt, gint, (*arr_alt)->len - 1) = id;
				break; //weiter mit den Eltern des Ankers
			}

			rc = zond_dbase_get_parent(verwendung_db(v), fp, &parent, error);
			if (rc)
				return -1;
			if (!parent)
				break;

			rc = zond_dbase_get_type_and_link(verwendung_db(v), parent,
					&type_parent, NULL, error);
			if (rc)
				return -1;
			if (type_parent != ZOND_DBASE_TYPE_FILE_PART)
				break;

			g_array_append_val(*arr_anzeige, parent);
			g_array_append_val(*arr_alt, null);
			fp = parent;
		}

		if (!id) //nirgends angebunden
			return 0;
	}

	while (tiefe++ < VERWENDUNG_MAX_TIEFE) {
		gint parent = 0;
		gint type_parent = 0;
		gint link_parent = 0;

		rc = zond_dbase_get_parent(verwendung_db(v), id, &parent, error);
		if (rc)
			return -1;
		if (parent <= BAUM_AUSWERTUNG)
			break;

		rc = zond_dbase_get_type_and_link(verwendung_db(v), parent,
				&type_parent, &link_parent, error);
		if (rc)
			return -1;

		if (type_parent == ZOND_DBASE_TYPE_BAUM_INHALT_FILE) {
			g_array_append_val(*arr_anzeige, link_parent);
			g_array_append_val(*arr_alt, parent);
		} else {
			g_array_append_val(*arr_anzeige, parent);
			g_array_append_val(*arr_alt, null);
		}

		id = parent;
	}

	return 0;
}

//Baum, in dem Knoten id angezeigt wird (FILE_PART: BAUM_INHALT)
static gint verwendung_anzeige_baum(Verwendung *v, gint id, gint *baum,
		GError **error) {
	gint rc = 0;
	gint type = 0;
	gint root = 0;

	rc = zond_dbase_get_type_and_link(verwendung_db(v), id, &type, NULL, error);
	if (rc)
		return -1;

	if (type == ZOND_DBASE_TYPE_FILE_PART) {
		*baum = BAUM_INHALT;
		return 0;
	}

	rc = zond_dbase_get_tree_root(verwendung_db(v), id, &root, error);
	if (rc)
		return -1;

	*baum = (root == BAUM_INHALT || root == BAUM_AUSWERTUNG) ?
			root : KEIN_BAUM;

	return 0;
}

/* Indirekte Fundstellen (Stufe 1): node_id ist mit sichtbar, wo ein Link
 * auf einen seiner Vorfahren steht. Als zugeklappte Gruppe unter parent. */
static gint verwendung_add_indirekt(Verwendung *v, GtkTreeIter *parent,
		gint node_id, GError **error) {
	GArray *arr_anzeige = NULL;
	GArray *arr_alt = NULL;
	GtkTreeIter iter_gruppe = { 0 };
	gboolean gruppe = FALSE;
	gint rc = 0;

	rc = verwendung_vorfahren(v, node_id, &arr_anzeige, &arr_alt, error);
	if (rc)
		goto end;

	for (guint i = 0; i < arr_anzeige->len && !rc; i++) {
		gint vorfahr = g_array_index(arr_anzeige, gint, i);
		gint ziele[2] = { vorfahr, g_array_index(arr_alt, gint, i) };
		gint target_baum = KEIN_BAUM;
		gchar *name_vorfahr = NULL;
		GString *abstieg = g_string_new(NULL);

		//IDs unterhalb des Vorfahren bis node_id
		for (gint k = (gint) i - 1; k >= 0; k--)
			g_string_append_printf(abstieg, "%d,",
					g_array_index(arr_anzeige, gint, k));
		g_string_append_printf(abstieg, "%d", node_id);

		rc = verwendung_node_text(v, vorfahr, &name_vorfahr, error);
		if (!rc)
			rc = verwendung_anzeige_baum(v, vorfahr, &target_baum, error);

		for (gint z = 0; z < 2 && !rc; z++) {
			GArray *arr_links = NULL;

			if (!ziele[z])
				continue;

			rc = zond_dbase_get_referrers(verwendung_db(v), ziele[z],
					ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK, &arr_links, error);
			if (rc)
				break;

			for (guint j = 0; j < arr_links->len && !rc; j++) {
				gint link_id = g_array_index(arr_links, gint, j);
				gint link_parent = 0;
				gint root = 0;
				gchar *pfad = NULL;
				gchar *label = NULL;
				GtkTreeIter iter = { 0 };

				rc = zond_dbase_get_parent(verwendung_db(v), link_id,
						&link_parent, error);
				if (!rc)
					rc = zond_dbase_get_tree_root(verwendung_db(v), link_id,
							&root, error);
				if (!rc && link_parent > BAUM_AUSWERTUNG)
					rc = verwendung_pfad(v, link_parent, &pfad, error);
				if (rc)
					break;

				if (!gruppe) {
					verwendung_add_row(v, parent, &iter_gruppe,
							KIND_INDIREKT_GRUPPE, "emblem-symbolic-link",
							"indirekt sichtbar", NULL, NULL, KEIN_BAUM, 0, NULL,
							NULL, 0);
					gtk_tree_store_set(v->store, &iter_gruppe, COL_STYLE,
							PANGO_STYLE_ITALIC, -1);
					gruppe = TRUE;
				}

				label = g_strdup_printf("über Link auf „%s“ – %s",
						name_vorfahr ? name_vorfahr : "?",
						pfad ? pfad : "oberste Ebene");

				verwendung_add_row(v, &iter_gruppe, &iter, KIND_INDIREKT,
						"emblem-symbolic-link", "indirekt", label, NULL,
						(root == BAUM_INHALT || root == BAUM_AUSWERTUNG) ?
								root : KEIN_BAUM,
						(link_parent > BAUM_AUSWERTUNG) ? link_parent : 0,
						NULL, NULL, 0);
				gtk_tree_store_set(v->store, &iter, COL_LINK_ID, link_id,
						COL_TARGET_ID, vorfahr, COL_TARGET_BAUM, target_baum,
						COL_ABSTIEG, abstieg->str, COL_STYLE,
						PANGO_STYLE_ITALIC, -1);

				g_free(pfad);
				g_free(label);
			}

			g_array_unref(arr_links);
		}

		g_free(name_vorfahr);
		g_string_free(abstieg, TRUE);
	}

	end:
	if (arr_anzeige)
		g_array_unref(arr_anzeige);
	if (arr_alt)
		g_array_unref(arr_alt);

	return rc ? -1 : 0;
}

//Alle Copies (samt Links darauf), die auf target_id zeigen
static gint verwendung_add_copies(Verwendung *v, GtkTreeIter *parent,
		gint target_id, GError **error) {
	GArray *arr = NULL;
	gint rc = 0;

	rc = zond_dbase_get_referrers(verwendung_db(v), target_id,
			ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY, &arr, error);
	if (rc)
		return -1;

	for (guint i = 0; i < arr->len; i++) {
		gint copy_id = g_array_index(arr, gint, i);
		gchar *icon_name = NULL;
		gchar *node_text = NULL;
		gchar *text = NULL;
		GtkTreeIter iter_copy = { 0 };

		rc = zond_dbase_get_node(verwendung_db(v), copy_id, NULL, NULL, NULL,
				NULL, &icon_name, &node_text, &text, error);
		if (rc)
			break;

		verwendung_add_row(v, parent, &iter_copy, KIND_COPY, icon_name, "Copy",
				node_text, text, BAUM_AUSWERTUNG, copy_id, NULL, NULL,
				copy_id);

		g_free(icon_name);
		g_free(node_text);
		g_free(text);

		rc = verwendung_add_links(v, &iter_copy, copy_id, BAUM_AUSWERTUNG,
				error);
		if (rc)
			break;

		rc = verwendung_add_indirekt(v, &iter_copy, copy_id, error);
		if (rc)
			break;
	}

	g_array_unref(arr);

	return rc ? -1 : 0;
}

/* FILE_PART-Knoten mit allem, was daran hängt, dann rekursiv seine
 * Sections. in_anbindung: ein Vorfahr ist angebunden - dann ist der Knoten
 * in BAUM_INHALT sichtbar, auch ohne eigenen Anker. */
static gint verwendung_add_file_part(Verwendung *v, GtkTreeIter *parent,
		gint file_part_id, gboolean is_root, gboolean in_anbindung, gint tiefe,
		GError **error) {
	gint rc = 0;
	gchar *file_part = NULL;
	gchar *section = NULL;
	gchar *icon_name = NULL;
	gchar *node_text = NULL;
	gchar *text = NULL;
	GArray *arr_anker = NULL;
	GtkTreeIter iter_fp = { 0 };
	GtkTreeIter iter_abl = { 0 };
	gboolean sichtbar = FALSE;
	gint child = 0;

	if (tiefe > VERWENDUNG_MAX_TIEFE)
		return 0;

	rc = zond_dbase_get_node(verwendung_db(v), file_part_id, NULL, NULL,
			&file_part, &section, &icon_name, &node_text, &text, error);
	if (rc)
		return -1;

	rc = zond_dbase_get_referrers(verwendung_db(v), file_part_id,
			ZOND_DBASE_TYPE_BAUM_INHALT_FILE, &arr_anker, error);
	if (rc)
		goto end;

	sichtbar = (arr_anker->len > 0) || in_anbindung;

	verwendung_add_row(v, parent, &iter_fp,
			is_root ? KIND_DATEI : KIND_SECTION,
			is_root ? icon_name : "edit-select-all",
			is_root ? "Datei" : "Section",
			is_root ? file_part : (section ? section : "?"), NULL, BAUM_FS, 0,
			file_part, section, file_part_id);

	//Anbindung, Copies und Links hängen an der BAUM_INHALT-Zeile, falls
	//sichtbar, sonst direkt am FILE_PART.
	iter_abl = iter_fp;
	if (sichtbar)
		verwendung_add_row(v, &iter_fp, &iter_abl,
				(arr_anker->len > 0) ? KIND_ANBINDUNG : KIND_IN_ANBINDUNG,
				icon_name,
				(arr_anker->len > 0) ? "Anbindung" : "in Anbindung", node_text,
				text, BAUM_INHALT, file_part_id, NULL, NULL, file_part_id);

	rc = verwendung_add_copies(v, &iter_abl, file_part_id, error);
	if (rc)
		goto end;

	rc = verwendung_add_links(v, &iter_abl, file_part_id, BAUM_INHALT, error);
	if (rc)
		goto end;

	//Altbestand: Copies/Links können auch auf den Anker statt auf den
	//FILE_PART zeigen
	for (guint i = 0; i < arr_anker->len; i++) {
		gint anker = g_array_index(arr_anker, gint, i);

		rc = verwendung_add_copies(v, &iter_abl, anker, error);
		if (rc)
			goto end;

		rc = verwendung_add_links(v, &iter_abl, anker, BAUM_INHALT, error);
		if (rc)
			goto end;
	}

	if (sichtbar) {
		rc = verwendung_add_indirekt(v, &iter_abl, file_part_id, error);
		if (rc)
			goto end;
	}

	//Sections
	rc = zond_dbase_get_first_child(verwendung_db(v), file_part_id, &child,
			error);
	if (rc)
		goto end;

	while (child) {
		gint type = 0;

		rc = zond_dbase_get_type_and_link(verwendung_db(v), child, &type, NULL,
				error);
		if (rc)
			goto end;

		if (type == ZOND_DBASE_TYPE_FILE_PART) {
			rc = verwendung_add_file_part(v, &iter_fp, child, FALSE, sichtbar,
					tiefe + 1, error);
			if (rc)
				goto end;
		}

		rc = zond_dbase_get_younger_sibling(verwendung_db(v), child, &child,
				error);
		if (rc)
			goto end;
	}

	end:
	if (arr_anker)
		g_array_unref(arr_anker);
	g_free(file_part);
	g_free(section);
	g_free(icon_name);
	g_free(node_text);
	g_free(text);

	return rc ? -1 : 0;
}

static gint verwendung_add_strukt(Verwendung *v, gint strukt_id,
		GError **error) {
	gint rc = 0;
	gint root = 0;
	gchar *icon_name = NULL;
	gchar *node_text = NULL;
	gchar *text = NULL;
	GtkTreeIter iter = { 0 };

	rc = zond_dbase_get_node(verwendung_db(v), strukt_id, NULL, NULL, NULL,
			NULL, &icon_name, &node_text, &text, error);
	if (rc)
		return -1;

	rc = zond_dbase_get_tree_root(verwendung_db(v), strukt_id, &root, error);
	if (!rc) {
		verwendung_add_row(v, NULL, &iter, KIND_STRUKT, icon_name,
				(root == BAUM_INHALT) ?
						"Strukturpunkt (Bestand)" :
						"Strukturpunkt (Auswertung)", node_text, text,
				(root == BAUM_INHALT || root == BAUM_AUSWERTUNG) ?
						root : KEIN_BAUM, strukt_id, NULL, NULL, strukt_id);

		rc = verwendung_add_links(v, &iter, strukt_id, root, error);
		if (!rc)
			rc = verwendung_add_indirekt(v, &iter, strukt_id, error);
	}

	g_free(icon_name);
	g_free(node_text);
	g_free(text);

	return rc ? -1 : 0;
}

/* Für "x.zip//a.pdf//b.pdf": angebundene Container ("x.zip",
 * "x.zip//a.pdf") als eigene Zeilen oberhalb des Ursprungs */
static gint verwendung_add_enthalten_in(Verwendung *v, gchar const *file_part,
		GError **error) {
	gchar **teile = NULL;
	guint n = 0;

	if (!file_part || !strstr(file_part, "//"))
		return 0;

	teile = g_strsplit(file_part, "//", -1);
	n = g_strv_length(teile);

	for (guint k = 1; k < n; k++) {
		gchar *praefix = NULL;
		gchar *ende = teile[k];
		gint id = 0;
		gint anker = 0;
		gint rc = 0;

		teile[k] = NULL;
		praefix = g_strjoinv("//", teile);
		teile[k] = ende;

		rc = zond_dbase_get_section(verwendung_db(v), praefix, NULL, &id,
				error);
		if (!rc && id) {
			GArray *arr_anker = NULL;

			rc = zond_dbase_get_referrers(verwendung_db(v), id,
					ZOND_DBASE_TYPE_BAUM_INHALT_FILE, &arr_anker, error);
			if (!rc) {
				anker = arr_anker->len;
				g_array_unref(arr_anker);
			}
		}

		if (rc) {
			g_free(praefix);
			g_strfreev(teile);
			return -1;
		}

		if (anker)
			verwendung_add_row(v, NULL, NULL, KIND_ENTHALTEN_IN,
					"package-x-generic", "Enthalten in", praefix, NULL,
					BAUM_INHALT, id, praefix, NULL, id);

		g_free(praefix);
	}

	g_strfreev(teile);

	return 0;
}

/* Führt node_id auf seinen Ursprung zurück: *strukt_id (Strukturpunkt) oder
 * *file_part_id (Wurzel-FILE_PART). *key_id ist die ID, für die im Baum die
 * Zeile des Ausgangsknotens steht. */
static gint verwendung_ursprung(Verwendung *v, gint node_id, gint *strukt_id,
		gint *file_part_id, gint *key_id, GError **error) {
	gint rc = 0;
	gint type = 0;
	gint link = 0;
	gint fp = 0;
	gint tiefe = 0;

	*strukt_id = 0;
	*file_part_id = 0;
	*key_id = node_id;

	rc = zond_dbase_get_type_and_link(verwendung_db(v), node_id, &type, &link,
			error);
	if (rc)
		return -1;

	if (type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK) {
		//Link auf Link gibt es nicht - eine Stufe genügt
		node_id = link;
		rc = zond_dbase_get_type_and_link(verwendung_db(v), node_id, &type,
				&link, error);
		if (rc)
			return -1;
	}

	if (type == ZOND_DBASE_TYPE_BAUM_STRUKT) {
		*strukt_id = node_id;
		return 0;
	} else if (type == ZOND_DBASE_TYPE_FILE_PART)
		fp = node_id;
	else if (type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE) {
		fp = link;
		if (*key_id == node_id)
			*key_id = link; //Anker hat keine eigene Zeile
	} else if (type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY) {
		gint type_ziel = 0;
		gint link_ziel = 0;

		rc = zond_dbase_get_type_and_link(verwendung_db(v), link, &type_ziel,
				&link_ziel, error);
		if (rc)
			return -1;

		if (type_ziel == ZOND_DBASE_TYPE_FILE_PART)
			fp = link;
		else if (type_ziel == ZOND_DBASE_TYPE_BAUM_INHALT_FILE)
			fp = link_ziel;
	}

	if (!fp) {
		if (error)
			*error = g_error_new(ZOND_ERROR, 0,
					"%s\nKnoten %d hat keinen Ursprung (Typ %d)", __func__,
					node_id, type);
		return -1;
	}

	//Hoch bis zur Wurzel-FILE_PART (parent_ID=0)
	while (tiefe++ < VERWENDUNG_MAX_TIEFE) {
		gint parent = 0;
		gint type_parent = 0;

		rc = zond_dbase_get_parent(verwendung_db(v), fp, &parent, error);
		if (rc)
			return -1;
		if (!parent)
			break;

		rc = zond_dbase_get_type_and_link(verwendung_db(v), parent,
				&type_parent, NULL, error);
		if (rc)
			return -1;
		if (type_parent != ZOND_DBASE_TYPE_FILE_PART)
			break;

		fp = parent;
	}

	*file_part_id = fp;

	return 0;
}

//Zeile "Enthält: 2 Sections · 3 Copies · …" für jede Zeile mit Kindern
static void verwendung_zaehlen(GtkTreeStore *store, GtkTreeIter *parent,
		gint zaehler[]) {
	GtkTreeIter iter = { 0 };
	gboolean valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &iter,
			parent);

	while (valid) {
		gint kind = 0;
		gint eigene[KIND_NICHT_VERWENDET + 1] = { 0 };

		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_KIND, &kind, -1);
		zaehler[kind]++;

		if (gtk_tree_model_iter_has_child(GTK_TREE_MODEL(store), &iter)) {
			GString *info = g_string_new(NULL);
			struct {
				VerwendungKind kind;
				gchar const *einzahl;
				gchar const *mehrzahl;
			} const art[] = { { KIND_SECTION, "Section", "Sections" }, {
					KIND_ANBINDUNG, "Anbindung", "Anbindungen" }, { KIND_COPY,
					"Copy", "Copies" }, { KIND_LINK, "Link", "Links" }, {
					KIND_INDIREKT, "indirekt", "indirekt" } };

			verwendung_zaehlen(store, &iter, eigene);

			for (guint i = 0; i < G_N_ELEMENTS(art); i++) {
				gint n = eigene[art[i].kind];

				if (!n)
					continue;
				if (info->len)
					g_string_append(info, " · ");
				g_string_append_printf(info, "%d %s", n,
						(n == 1) ? art[i].einzahl : art[i].mehrzahl);
			}

			gtk_tree_store_set(store, &iter, COL_INFO, info->str, -1);
			g_string_free(info, TRUE);

			for (gint k = 0; k <= KIND_NICHT_VERWENDET; k++)
				zaehler[k] += eigene[k];
		}

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
	}

	return;
}

typedef struct {
	gint key_id;
	GtkTreePath *letzter;
} Hervorheben;

static gboolean verwendung_hervorheben_foreach(GtkTreeModel *model,
		GtkTreePath *path, GtkTreeIter *iter, gpointer data) {
	Hervorheben *h = data;
	gint key_id = 0;

	gtk_tree_model_get(model, iter, COL_KEY_ID, &key_id, -1);
	if (key_id != h->key_id)
		return FALSE;

	gtk_tree_store_set(GTK_TREE_STORE(model), iter, COL_WEIGHT,
			PANGO_WEIGHT_BOLD, -1);

	if (h->letzter)
		gtk_tree_path_free(h->letzter);
	h->letzter = gtk_tree_path_copy(path);

	return FALSE;
}

//Ausgangsknoten fett, Pfad dorthin aufgeklappt und ausgewählt
static void verwendung_hervorheben(Verwendung *v, gint key_id) {
	Hervorheben h = { key_id, NULL };

	gtk_tree_model_foreach(GTK_TREE_MODEL(v->store),
			verwendung_hervorheben_foreach, &h);

	if (!h.letzter) {
		//nichts gefunden: oberste Ebene aufklappen
		GtkTreePath *path = gtk_tree_path_new_first();

		gtk_tree_view_expand_row(GTK_TREE_VIEW(v->treeview), path, FALSE);
		gtk_tree_path_free(path);

		return;
	}

	//Nur bis zum Ausgangsknoten aufklappen - seine Kinder bleiben zu
	if (gtk_tree_path_get_depth(h.letzter) > 1) {
		GtkTreePath *parent = gtk_tree_path_copy(h.letzter);

		gtk_tree_path_up(parent);
		gtk_tree_view_expand_to_path(GTK_TREE_VIEW(v->treeview), parent);
		gtk_tree_path_free(parent);
	}
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(v->treeview), h.letzter, NULL,
			FALSE);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(v->treeview), h.letzter, NULL,
			TRUE, 0.3, 0.0);
	gtk_tree_path_free(h.letzter);

	return;
}

static gint verwendung_aufbauen(Verwendung *v, GError **error) {
	gint rc = 0;
	gint start = v->node_id;
	gint strukt_id = 0;
	gint file_part_id = 0;
	gint key_id = 0;
	gint zaehler[KIND_NICHT_VERWENDET + 1] = { 0 };

	gtk_tree_store_clear(v->store);

	if (!start && v->file_part) {
		rc = zond_dbase_get_section(verwendung_db(v), v->file_part,
				v->section, &start, error);
		if (rc)
			return -1;
	}

	if (!start) {
		//Aus BAUM_FS aufgerufen, Datei kommt in der Projektdatei nicht vor
		gchar *label = v->section ?
				g_strdup_printf("%s (%s)", v->file_part, v->section) :
				g_strdup(v->file_part);

		rc = verwendung_add_enthalten_in(v, v->file_part, error);
		if (rc) {
			g_free(label);
			return -1;
		}

		verwendung_add_row(v, NULL, NULL, KIND_NICHT_VERWENDET,
				"text-x-generic", "Datei", label, "nicht angebunden/verwendet",
				BAUM_FS, 0, v->file_part, v->section, 0);
		g_free(label);

		return 0;
	}

	rc = verwendung_ursprung(v, start, &strukt_id, &file_part_id, &key_id,
			error);
	if (rc)
		return -1;

	if (strukt_id)
		rc = verwendung_add_strukt(v, strukt_id, error);
	else {
		gchar *file_part = NULL;

		rc = zond_dbase_get_node(verwendung_db(v), file_part_id, NULL, NULL,
				&file_part, NULL, NULL, NULL, NULL, error);
		if (!rc)
			rc = verwendung_add_enthalten_in(v, file_part, error);
		g_free(file_part);

		if (!rc)
			rc = verwendung_add_file_part(v, NULL, file_part_id, TRUE, FALSE,
					0, error);
	}
	if (rc)
		return -1;

	verwendung_zaehlen(v->store, NULL, zaehler);
	verwendung_hervorheben(v, key_id);

	return 0;
}

static void verwendung_aktualisieren(Verwendung *v) {
	GError *error = NULL;
	gint rc = 0;

	rc = verwendung_aufbauen(v, &error);
	if (rc) {
		display_message(v->window, "Herkunft und Verwendung konnte nicht "
				"ermittelt werden\n\n", error->message, NULL);
		g_error_free(error);
	}

	return;
}

static void cb_verwendung_aktualisieren(GtkButton *button, gpointer data) {
	verwendung_aktualisieren((Verwendung*) data);

	return;
}

/* Von der Kopfzeile eines Links durch die gespiegelten Zeilen abwärts
 * (IDs in abstieg, verglichen mit Spalte 2) - lädt dabei nach. Bleibt bei
 * der tiefsten gefundenen Zeile stehen. */
static void verwendung_abstieg(GtkTreeModel *model, GtkTreeIter *iter,
		gchar const *abstieg) {
	gchar **ids = g_strsplit(abstieg, ",", -1);

	for (gint k = 0; ids[k]; k++) {
		gint id = atoi(ids[k]);
		GtkTreeIter child = { 0 };
		gboolean found = FALSE;

		if (zond_tree_store_link_is_unloaded(iter))
			zond_tree_store_load_link(iter);

		for (gboolean valid = gtk_tree_model_iter_children(model, &child, iter);
				valid; valid = gtk_tree_model_iter_next(model, &child)) {
			gint child_id = 0;

			gtk_tree_model_get(model, &child, 2, &child_id, -1);
			if (child_id == id) {
				*iter = child;
				found = TRUE;
				break;
			}
		}

		if (!found)
			break;
	}

	g_strfreev(ids);

	return;
}

static void cb_verwendung_row_activated(GtkTreeView *tree_view,
		GtkTreePath *path, GtkTreeViewColumn *column, gpointer data) {
	Verwendung *v = data;
	GtkTreeIter iter = { 0 };
	gint baum = KEIN_BAUM;
	gint node_id = 0;
	gint link_id = 0;
	gint target_id = 0;
	gint target_baum = KEIN_BAUM;
	gchar *file_part = NULL;
	gchar *section = NULL;

	if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(v->store), &iter, path))
		return;

	gtk_tree_model_get(GTK_TREE_MODEL(v->store), &iter, COL_BAUM, &baum,
			COL_NODE_ID, &node_id, COL_FILE_PART, &file_part, COL_SECTION,
			&section, COL_LINK_ID, &link_id, COL_TARGET_ID, &target_id,
			COL_TARGET_BAUM, &target_baum, -1);

	if (link_id && (target_baum == BAUM_INHALT
			|| target_baum == BAUM_AUSWERTUNG)) {
		GtkTreeIter *iter_link = zond_tree_store_get_iter_link(
				ZOND_TREE_STORE(gtk_tree_view_get_model(
						GTK_TREE_VIEW(v->zond->treeview[target_baum]))),
				target_id, link_id);

		if (iter_link) {
			gchar *abstieg = NULL;

			gtk_tree_model_get(GTK_TREE_MODEL(v->store), &iter, COL_ABSTIEG,
					&abstieg, -1);
			if (abstieg)
				verwendung_abstieg(
						gtk_tree_view_get_model(
								GTK_TREE_VIEW(v->zond->treeview[baum])),
						iter_link, abstieg);
			g_free(abstieg);

			suchen_springe_zu_iter(v->zond, baum, iter_link);
			gtk_tree_iter_free(iter_link);
			g_free(file_part);
			g_free(section);

			return;
		}
	}

	if (baum == BAUM_FS)
		suchen_springe_zu_baum_fs(v->zond, file_part, section);
	else if (baum == BAUM_INHALT || baum == BAUM_AUSWERTUNG)
		suchen_springe_zu_knoten(v->zond, baum, node_id);

	g_free(file_part);
	g_free(section);

	return;
}

static void verwendung_spalte(GtkWidget *treeview, gchar const *titel,
		gint col_text, gboolean mit_icon, gboolean expand) {
	GtkTreeViewColumn *column = gtk_tree_view_column_new();
	GtkCellRenderer *renderer = NULL;

	gtk_tree_view_column_set_title(column, titel);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_column_set_expand(column, expand);

	if (mit_icon) {
		renderer = gtk_cell_renderer_pixbuf_new();
		gtk_tree_view_column_pack_start(column, renderer, FALSE);
		gtk_tree_view_column_add_attribute(column, renderer, "icon-name",
				COL_ICON);
	}

	renderer = gtk_cell_renderer_text_new();
	g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", col_text);
	gtk_tree_view_column_add_attribute(column, renderer, "weight", COL_WEIGHT);
	gtk_tree_view_column_add_attribute(column, renderer, "style", COL_STYLE);
	if (col_text == COL_INFO)
		g_object_set(renderer, "foreground", "gray", NULL);

	gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

	return;
}

gint verwendung_anzeigen(Projekt *zond, gint node_id, gchar const *file_part,
		gchar const *section, GError **error) {
	Verwendung *v = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *swindow = NULL;
	GtkWidget *bbox = NULL;
	GtkWidget *button_aktualisieren = NULL;
	GtkWidget *button_schliessen = NULL;
	gint rc = 0;

	v = g_new0(Verwendung, 1);
	v->zond = zond;
	v->node_id = node_id;
	v->file_part = g_strdup(file_part);
	v->section = g_strdup(section);
	v->store = gtk_tree_store_new(NUM_COLS, G_TYPE_INT, G_TYPE_STRING,
			G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING,
			G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT,
			G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT);

	v->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(v->window), "Herkunft und Verwendung");
	gtk_window_set_default_size(GTK_WINDOW(v->window), 900, 450);
	gtk_window_set_transient_for(GTK_WINDOW(v->window),
			GTK_WINDOW(zond->app_window));
	g_object_set_data_full(G_OBJECT(v->window), "verwendung", v,
			verwendung_free);

	v->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(v->store));
	gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(v->treeview), COL_TOOLTIP);
	verwendung_spalte(v->treeview, "Element", COL_ART, TRUE, FALSE);
	verwendung_spalte(v->treeview, "Bezeichnung", COL_LABEL, FALSE, TRUE);
	verwendung_spalte(v->treeview, "Text", COL_TEXT, FALSE, TRUE);
	verwendung_spalte(v->treeview, "Enthält", COL_INFO, FALSE, FALSE);
	g_signal_connect(v->treeview, "row-activated",
			G_CALLBACK(cb_verwendung_row_activated), v);

	swindow = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(swindow), v->treeview);

	button_aktualisieren = gtk_button_new_with_label("Aktualisieren");
	button_schliessen = gtk_button_new_with_label("Schließen");
	g_signal_connect(button_aktualisieren, "clicked",
			G_CALLBACK(cb_verwendung_aktualisieren), v);
	g_signal_connect_swapped(button_schliessen, "clicked",
			G_CALLBACK(gtk_widget_destroy), v->window);

	bbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(bbox), 6);
	gtk_container_set_border_width(GTK_CONTAINER(bbox), 6);
	gtk_container_add(GTK_CONTAINER(bbox), button_aktualisieren);
	gtk_container_add(GTK_CONTAINER(bbox), button_schliessen);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox), swindow, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(v->window), vbox);

	rc = verwendung_aufbauen(v, error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);
		gtk_widget_destroy(v->window);

		return -1;
	}

	gtk_widget_show_all(v->window);

	return 0;
}
