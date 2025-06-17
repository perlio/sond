/*
 zond (ziele.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2020  pelo america

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

#include <glib/gstdio.h>
#include <sqlite3.h>
#include <gtk/gtk.h>

#include "../../misc.h"
#include "../../sond_fileparts.h"

#include "../zond_pdf_document.h"

#include "../global_types.h"
#include "../zond_dbase.h"
#include "../zond_treeviewfm.h"

#include "../99conv/general.h"
#include "../99conv/pdf.h"

#include "../40viewer/viewer.h"
#include "../40viewer/document.h"

#include "../zond_treeview.h"
#include "../zond_tree_store.h"

static gint zond_anbindung_verschieben_kinder(Projekt *zond, gint node_id,
		GtkTreeIter *iter, Anbindung anbindung, GError **error) {
	gboolean child = TRUE;
	gint anchor_id = node_id;
	GtkTreeIter iter_anchor = { 0 };

	if (iter)
		iter_anchor = *iter;

	do {
		gint rc = 0;
		gint younger_sibling = 0;
		Anbindung anbindung_y = { 0 };
		gchar *section = NULL;

		rc = zond_dbase_get_younger_sibling(zond->dbase_zond->zond_dbase_work,
				node_id, &younger_sibling, error);
		if (rc)
			ERROR_Z

		if (younger_sibling == 0)
			break;

		rc = zond_dbase_get_node(zond->dbase_zond->zond_dbase_work,
				younger_sibling, NULL, NULL, NULL, &section, NULL, NULL, NULL,
				error);
		if (rc)
			ERROR_Z

		if (!section) {
			if (error)
				*error = g_error_new( ZOND_ERROR, 0,
						"%s\nKnoten enthält keine section", __func__);

			return -1;
		}

		anbindung_parse_file_section(section, &anbindung_y);
		g_free(section);

		if (anbindung_1_eltern_von_2(anbindung, anbindung_y)) {
			gint rc = 0;
			GtkTreeIter iter_younger_sibling = { 0 };
			GtkTreeIter iter_new = { 0 };

			rc = zond_dbase_verschieben_knoten(
					zond->dbase_zond->zond_dbase_work, younger_sibling,
					anchor_id, child, error);
			if (rc)
				ERROR_Z

			if (iter) {
				iter_younger_sibling = *iter;

				if (!gtk_tree_model_iter_next(
						GTK_TREE_MODEL(zond_tree_store_get_tree_store(iter)),
						&iter_younger_sibling)) {
					if (error)
						*error = g_error_new( ZOND_ERROR, 0,
								"Kein iter für jüngeres Geschwister");

					return -1;
				}

				zond_tree_store_move_node(&iter_younger_sibling,
						ZOND_TREE_STORE(
								gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) )),
						&iter_anchor, child, &iter_new);
				iter_anchor = iter_new;
			}

			anchor_id = younger_sibling;
			child = FALSE;
		} else
			break;
	} while (1);

	return 0;
}

static gint zond_anbindung_baum_inhalt(Projekt *zond, gint anchor_id,
		gboolean child, gint id_inserted, Anbindung anbindung,
		gchar const *rel_path, gchar const *node_text, GError **error) {
	gint rc = 0;
	GtkTreeIter *iter = NULL;
	GtkTreeIter iter_inserted = { 0 };
	gint baum_inhalt_file = 0;

	//Gucken, ob Anbindung im Baum-Inhalt aufscheint
	rc = zond_dbase_find_baum_inhalt_file(zond->dbase_zond->zond_dbase_work,
			id_inserted, &baum_inhalt_file, NULL, NULL, error);
	if (rc)
		ERROR_Z

	if (baum_inhalt_file) //in Baum Inhalt angebunden - muß in tree eingefügt werden
	{
		//eingefügtes ziel in Baum
		iter = zond_treeview_abfragen_iter(
				ZOND_TREEVIEW(zond->treeview[BAUM_INHALT]), anchor_id);
		if (!iter) {
			if (error)
				*error = g_error_new( ZOND_ERROR, 0,
						"%s\nzond_treeview_abfragen_iter gibt NULL zurück",
						__func__);

			return -1;
		}

		zond_tree_store_insert(zond_tree_store_get_tree_store(iter), iter,
				child, &iter_inserted);
		gtk_tree_iter_free(iter);
		zond_tree_store_set(&iter_inserted,
				zond->icon[ICON_ANBINDUNG].icon_name, node_text, id_inserted);

		sond_treeview_expand_row(zond->treeview[BAUM_INHALT], &iter_inserted);
		sond_treeview_set_cursor_on_text_cell(zond->treeview[BAUM_INHALT],
				&iter_inserted);
		gtk_widget_grab_focus(GTK_WIDGET(zond->treeview[BAUM_INHALT]));
	}

	rc = zond_anbindung_verschieben_kinder(zond, id_inserted,
			(baum_inhalt_file) ? &iter_inserted : NULL, anbindung, error);
	if (rc)
		ERROR_Z

	return (baum_inhalt_file) ? 0 : 1;
}

gint ziele_abfragen_anker_rek(ZondDBase *zond_dbase, Anbindung anbindung,
		gint anchor_id, gint *anchor_id_new, gboolean *kind, GError **error) {
	gint rc = 0;
	Anbindung anbindung_anchor = { 0 };
	gchar *section = NULL;

	rc = zond_dbase_get_node(zond_dbase, anchor_id, NULL, NULL, NULL, &section,
	NULL, NULL, NULL, error);
	if (rc)
		ERROR_Z

	if (section) {
		anbindung_parse_file_section(section, &anbindung_anchor);
		g_free(section);
	}

	//ziele auf Identität prüfen
	if (anbindung_1_gleich_2(anbindung_anchor, anbindung)) {
		if (error)
			*error = g_error_new( ZOND_ERROR, 0,
					"%s\nIdentischer Abschnitt wurde bereits angebunden",
					__func__);

		return -1;
	}
	//Knoten kommt ist kind - anchor "weiter" oder root
	else if (anbindung_1_eltern_von_2(anbindung_anchor, anbindung)
			|| (anbindung_anchor.von.seite == 0
					&& anbindung_anchor.von.index == 0
					&& anbindung_anchor.bis.seite == 0
					&& anbindung_anchor.bis.index == 0)) {
		gint rc = 0;
		gint first_child_id = 0;

		*kind = TRUE;
		*anchor_id_new = anchor_id;

		rc = zond_dbase_get_first_child(zond_dbase, anchor_id, &first_child_id,
				error);
		if (rc)
			ERROR_Z

		if (first_child_id > 0) //hat kind
				{
			gint rc = 0;

			rc = ziele_abfragen_anker_rek(zond_dbase, anbindung, first_child_id,
					anchor_id_new, kind, error);
			if (rc) {
				g_prefix_error(error, "%s\n", __func__);

				return -1;
			}
		}
	}
	//Seiten oder Punkte vor den einzufügenden Punkt
	else if (anbindung_1_vor_2(anbindung_anchor, anbindung)) {
		gint rc = 0;
		gint younger_sibling_id = 0;

		rc = zond_dbase_get_younger_sibling(zond_dbase, anchor_id,
				&younger_sibling_id, error);
		if (rc)
			ERROR_Z

		*kind = FALSE;
		*anchor_id_new = anchor_id;

		if (younger_sibling_id > 0) {
			gint rc = 0;

			rc = ziele_abfragen_anker_rek(zond_dbase, anbindung,
					younger_sibling_id, anchor_id_new, kind, error);
			if (rc)
				ERROR_Z
		}
	}
	// wenn nicht danach, bleibt ja nur Überschneidung!!
	else if (!anbindung_1_vor_2(anbindung, anbindung_anchor)
			&& !anbindung_1_eltern_von_2(anbindung, anbindung_anchor)) {
		if (error)
			*error = g_error_new( ZOND_ERROR, 0,
					"Eingegebenes Ziel überschneidet "
							"sich mit bereits bestehendem Ziel");

		return -1;
	}

	return 0;
}

static gint zond_anbindung_insert_pdf_abschnitt_in_dbase(Projekt *zond,
		const gchar *file_part, Anbindung anbindung, gint *anchor_pdf_abschnitt,
		gboolean *child, gint *node_inserted, gchar **node_text, GError **error) {
	gint pdf_root = 0;
	gint node_id_new = 0;
	gint rc = 0;
	gint anchor_id_dbase = 0;
	gchar *file_section = NULL;

	rc = zond_dbase_get_file_part_root(zond->dbase_zond->zond_dbase_work,
			file_part, &pdf_root, error);
	if (rc)
		ERROR_Z

	if (!pdf_root) {
		gint rc = 0;

		rc = zond_treeview_insert_file_part_in_db(zond, file_part, "anbindung",
				NULL, error);
		if (rc)
			ERROR_Z

		rc = zond_dbase_get_file_part_root(zond->dbase_zond->zond_dbase_work,
				file_part, &pdf_root, error);
		if (rc)
			ERROR_Z
	}

	//jetzt vergleichen,
	rc = ziele_abfragen_anker_rek(zond->dbase_zond->zond_dbase_work, anbindung,
			pdf_root, &anchor_id_dbase, child, error);
	if (rc)
		ERROR_Z

	if (anchor_pdf_abschnitt)
		*anchor_pdf_abschnitt = anchor_id_dbase;

	*node_text = g_strdup_printf("S. %i", anbindung.von.seite + 1);
	if (anbindung.von.index)
		*node_text = add_string(*node_text,
				g_strdup_printf(", Index %d", anbindung.von.index));
	if (anbindung.bis.seite || anbindung.bis.index)
		*node_text = add_string(*node_text,
				g_strdup_printf(" - S. %d", anbindung.bis.seite + 1));
	if (anbindung.bis.index != EOP)
		*node_text = add_string(*node_text,
				g_strdup_printf(", Index %d", anbindung.bis.index));

	//file_section zusammensetzen
	anbindung_build_file_section(anbindung, &file_section);

	node_id_new = zond_dbase_insert_node(zond->dbase_zond->zond_dbase_work,
			anchor_id_dbase, *child, ZOND_DBASE_TYPE_FILE_PART, 0, file_part,
			file_section, zond->icon[ICON_ANBINDUNG].icon_name, *node_text,
			NULL, error);
	g_free(file_section);
	if (node_id_new == -1)
		ERROR_Z

	if (node_inserted)
		*node_inserted = node_id_new;

	return 0;
}

static gint zond_anbindung_fm(Projekt *zond, gint node_inserted,
		gchar const *file_part, Anbindung anbindung, gchar const *node_text,
		gboolean open, GError **error) {
	gint rc = 0;
	gint parent_id = 0;
	gchar *file_part_parent = NULL;
	gchar *section_parent = NULL;
	gboolean visible = FALSE;
	GtkTreeIter iter = { 0 };
	gboolean children = FALSE;
	gboolean opened = FALSE;

	//Eltern sichtbar?
	rc = zond_dbase_get_parent(zond->dbase_zond->zond_dbase_work, node_inserted,
			&parent_id, error);
	if (rc)
		ERROR_Z

	rc = zond_dbase_get_node(zond->dbase_zond->zond_dbase_work, parent_id, NULL,
			NULL, &file_part_parent, &section_parent, NULL, NULL, NULL, error);
	if (rc)
		ERROR_Z

	rc = zond_treeviewfm_section_visible(
			ZOND_TREEVIEWFM(zond->treeview[BAUM_FS]), file_part_parent,
			section_parent, open, &visible, &iter, &children, &opened, error);
	g_free(file_part_parent);
	g_free(section_parent);

	if (!visible)
		return 0;

	if (!opened) //erstes Kind - einfach
	{
		if (!children) // dummy einfügen;
		{
			GtkTreeIter iter_inserted = { 0 };

			gtk_tree_store_insert(
					GTK_TREE_STORE(
							gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_FS]) )),
					&iter_inserted, &iter, 0);
		}

		if (open) {
			GtkTreeIter iter_new = { 0 };

			sond_treeview_expand_row(zond->treeview[BAUM_FS], &iter);
			gtk_tree_model_iter_children(
					gtk_tree_view_get_model(
							GTK_TREE_VIEW(zond->treeview[BAUM_FS])), &iter_new,
					&iter);
			sond_treeview_set_cursor(zond->treeview[BAUM_FS], &iter_new);
		}
	} else {
		GtkTreeIter iter_test = { 0 };
		GtkTreeIter iter_new = { 0 };
		GtkTreeIter iter_anchor = { 0 };
		gint pos = 0;
		gboolean child = TRUE;

		gtk_tree_model_iter_children(
				gtk_tree_view_get_model(GTK_TREE_VIEW(zond->treeview[BAUM_FS])),
				&iter_test, &iter);

		do {
			Anbindung anbindung_test = { 0 };
			GObject *object = NULL;

			gtk_tree_model_get(
					gtk_tree_view_get_model(
							GTK_TREE_VIEW(zond->treeview[BAUM_FS])), &iter_test,
					0, &object, -1);
			if (!object) {
				if (error)
					*error = g_error_new( ZOND_ERROR, 0,
							"%s\nKnoten enthält kein object", __func__);

				return -1;
			}

			if (!SOND_IS_FILE_PART_PDF_PAGE_TREE(object)) {
				if (error)
					*error = g_error_new( ZOND_ERROR, 0,
							"%s\nKnoten enthält keinen PDF-Abschnitt",
							__func__);
				g_object_unref(object);

				return -1;
			}

//			sond_file_part_pdf_page_tree_get_anbindung(
//					SOND_FILE_PART_PDF_PAGE_TREE(object), NULL, NULL,
//					&anbindung_test, NULL, NULL);
			g_object_unref(object);

			if (anbindung_1_vor_2(anbindung, anbindung_test))
				break;
			else if (anbindung_1_eltern_von_2(anbindung, anbindung_test))
				break;
			else if (anbindung_1_vor_2(anbindung_test, anbindung)) {
				pos++;
				if (!gtk_tree_model_iter_next(
						gtk_tree_view_get_model(
								GTK_TREE_VIEW(zond->treeview[BAUM_FS])),
						&iter_test))
					break;
//                    else break;
			}

			else if (anbindung_1_gleich_2(anbindung, anbindung_test)) {
				if (error)
					*error = g_error_new( ZOND_ERROR, 0,
							"%s\nAnbindung existiert schon im FS-Baum",
							__func__);

				return -1;
			} else //darf nicht sein!
			{
				if (error)
					*error = g_error_new( ZOND_ERROR, 0, "%s\nBAUM_FS korrupt",
							__func__);

				return -1;
			}
		} while (1);

//		zpda = g_object_new( ZOND_TYPE_PDF_ABSCHNITT, NULL);
//		zond_pdf_abschnitt_set(zpda, node_inserted, file_part, anbindung,
//				zond->icon[ICON_ANBINDUNG].icon_name, node_text);

		gtk_tree_store_insert(
				GTK_TREE_STORE(
						gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_FS]) )),
				&iter_new, &iter, pos);
//		gtk_tree_store_set(
//				GTK_TREE_STORE(
//						gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_FS]) )),
//				&iter_new, 0, G_OBJECT(zpda), -1);
//		g_object_unref(zpda);

		iter_anchor = iter_new;
		iter_test = iter_new;

		while (gtk_tree_model_iter_next(
				gtk_tree_view_get_model(GTK_TREE_VIEW(zond->treeview[BAUM_FS])),
				&iter_test)) {
			Anbindung anbindung_test = { 0 };
			GObject *object = NULL;

			gtk_tree_model_get(
					gtk_tree_view_get_model(
							GTK_TREE_VIEW(zond->treeview[BAUM_FS])), &iter_test,
					0, &object, -1);
			if (!object) {
				if (error)
					*error = g_error_new( ZOND_ERROR, 0,
							"%s\nKnoten enthält kein object", __func__);

				return -1;
			}

			if (!SOND_IS_FILE_PART_PDF_PAGE_TREE(object)) {
				if (error)
					*error = g_error_new( ZOND_ERROR, 0,
							"%s\nKnoten enthält keinen PDF-Abschnitt",
							__func__);
				g_object_unref(object);

				return -1;
			}

//			zond_pdf_abschnitt_get(ZOND_PDF_ABSCHNITT(object), NULL, NULL,
//					&anbindung_test, NULL, NULL);
			g_object_unref(object);

			if (anbindung_1_eltern_von_2(anbindung, anbindung_test)) {
				zond_treeviewfm_move_node(
						gtk_tree_view_get_model(
								GTK_TREE_VIEW(zond->treeview[BAUM_FS])),
						&iter_test, &iter_anchor, child);
				child = FALSE;
				iter_anchor = iter_test;
				iter_test = iter_new;
			} else if (anbindung_1_vor_2(anbindung, anbindung_test))
				break;
			else //darf's nicht geben
			{
				if (error)
					*error = g_error_new( ZOND_ERROR, 0,
							"%s\nTreeview Baum-FS kaputt", __func__);

				return -1;
			}
		}

		if (open)
			sond_treeview_set_cursor(zond->treeview[BAUM_FS], &iter_new);

		//wenn Geschwister als Kind umkopiert wurde, soll Elternknoten geöffnet werden
		if (!child)
			sond_treeview_expand_row(zond->treeview[BAUM_FS], &iter_new);
	} //irre

	return 0;
}

static gint zond_anbindung_trees(Projekt *zond, gint anchor_pdf_abschnitt,
		gboolean child, gint node_inserted, Anbindung anbindung,
		gchar const *file_part, gchar const *node_text, GError **error) {
	gint rc = 0;
	gboolean open = FALSE;

	rc = zond_anbindung_baum_inhalt(zond, anchor_pdf_abschnitt, child,
			node_inserted, anbindung, file_part, node_text, error);
	if (rc == -1)
		ERROR_Z

	if (rc == 1)
		open = TRUE;

	rc = zond_anbindung_fm(zond, node_inserted, file_part, anbindung, node_text,
			open, error);
	if (rc)
		ERROR_Z

	return 0;
}

gint zond_anbindung_erzeugen(PdfViewer *pv, GError **error) {
	gint rc = 0;
	gboolean child = TRUE;
	gint node_inserted = 0;
	gchar *node_text = NULL;
	gint anchor_pdf_abschnitt = 0;

	//ToDo: Wollen wir später schon!
	if (pv->dd->next) {
		if (error)
			*error =
					g_error_new( ZOND_ERROR, 0,
							"%s\nAnbindung kann nicht in virtuelles Dokument eingefügt werden",
							__func__);

		return -1;
	}

	rc = zond_anbindung_insert_pdf_abschnitt_in_dbase(pv->zond,
			zond_pdf_document_get_file_part(pv->dd->zond_pdf_document),
			pv->anbindung, &anchor_pdf_abschnitt, &child, &node_inserted,
			&node_text, error);
	if (rc)
		ERROR_Z

	rc = zond_anbindung_trees(pv->zond, anchor_pdf_abschnitt, child,
			node_inserted, pv->anbindung,
			zond_pdf_document_get_file_part(pv->dd->zond_pdf_document),
			node_text, error);
	g_free(node_text);
	if (rc)
		ERROR_Z

	return 0;
}

