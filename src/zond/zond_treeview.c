/*
 sond (zond_treeview.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2022  pelo america

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

#include "zond_treeview.h"

#include <gtk/gtk.h>
#include <glib-object.h>

#include "../misc.h"
#include "../sond_fileparts.h"
#include "../sond_treeview.h"
#include "../sond_log_and_error.h"
#include "../sond_process_file.h"

#include "zond_init.h"
#include "zond_dbase.h"
#include "zond_tree_store.h"
#include "zond_treeviewfm.h"
#include "zond_pdf_document.h"

#include "10init/app_window.h"
#include "10init/headerbar.h"

#include "20allgemein/project.h"

#include "40viewer/viewer.h"
#include "40viewer/viewer_ui.h"
#include "40viewer/document.h"

#include "99conv/general.h"
#include "zond_indexsuche.h"

typedef struct {
	Projekt *zond;
} ZondTreeviewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondTreeview, zond_treeview, SOND_TYPE_TREEVIEW)

static gint zond_treeview_get_root(ZondTreeview *ztv, gint node_id, gint *root,
		GError **error) {
	gint rc = 0;
	gint type = 0;

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	rc = zond_dbase_get_type_and_link(
			ztv_priv->zond->dbase_zond->zond_dbase_work, node_id, &type, NULL,
			error);
	if (rc)
		ERROR_Z

	if (type == ZOND_DBASE_TYPE_FILE_PART) {
		gint rc = 0;
		gint baum_inhalt_file = 0;

		//prüfen, ob angebundener file_part
		rc = zond_dbase_find_baum_inhalt_file(
				ztv_priv->zond->dbase_zond->zond_dbase_work, node_id,
				&baum_inhalt_file, NULL, NULL, error);
		if (rc)
			ERROR_Z

		if (baum_inhalt_file)
			node_id = baum_inhalt_file;
	}

	rc = zond_dbase_get_tree_root(ztv_priv->zond->dbase_zond->zond_dbase_work,
			node_id, root, error);
	if (rc)
		ERROR_Z

	return 0;
}

static gint zond_treeview_get_filepart_and_section(ZondTreeview *ztv, GtkTreeIter *iter,
		gchar **file_part, gchar** section, GError **error);

void zond_treeview_load_textview(Projekt* zond) {
	if (zond->node_id_act == 0)
        gtk_text_buffer_set_text(
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)), "", -1);
	else {
		GtkTextBuffer *buffer = NULL;
		GError* error = NULL;
		gint rc = 0;
		gchar* text = NULL;

		buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview));

		rc = zond_dbase_get_text(zond->dbase_zond->zond_dbase_work, zond->node_id_act,
				&text, &error);
		if (rc) {
			LOG_WARN("Node-Text konnte nicht geladen werden:\n%s", error->message);
			g_error_free(error);
		}

		if (text) {
			gtk_text_buffer_set_text(buffer, text, -1);
			gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(zond->textview),
					gtk_text_buffer_get_mark(
							gtk_text_view_get_buffer(
									GTK_TEXT_VIEW(zond->textview)),
							"ende-text"), 0.0,
					FALSE, 0.0, 0.0);
			g_free(text);
		} else
			gtk_text_buffer_set_text(buffer, "", -1);
	}

	return;
}

void zond_treeview_cursor_changed(ZondTreeview *treeview, gpointer user_data) {
	gint node_id = 0;
	GtkTreeIter iter = { 0, };

	Projekt *zond = (Projekt*) user_data;

    //wenn kein cursor gesetzt ist
    if (!sond_treeview_get_cursor(SOND_TREEVIEW(treeview), &iter)) {
        zond->node_id_act = 0;

        if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(zond->textview_pin_button))) {
            gtk_widget_set_sensitive(zond->textview, FALSE);
            gtk_widget_set_sensitive(zond->textview_pin_button, FALSE);
            gtk_widget_set_sensitive(zond->textview_jump_button, FALSE);
        }

		return;
	}

    gtk_widget_set_sensitive(zond->textview, TRUE);
    gtk_widget_set_sensitive(zond->textview_pin_button, TRUE);
    gtk_widget_set_sensitive(zond->textview_jump_button, TRUE);
	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)),
			&iter, 2, &node_id, -1);

	//Wenn gleicher Knoten: direkt zurück
	if (node_id == zond->node_id_act)
		return;

    // node_id_act setzen
    zond->node_id_act = node_id;

	// Wenn textview gepint: nicht aktualisieren
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(zond->textview_pin_button))) {
		zond_treeview_load_textview(zond);

		zond->node_id_textview = node_id;
    }

    return;
}

static void zond_treeview_row_expanded(GtkTreeView *tree_view,
		GtkTreeIter *iter, GtkTreePath *path, gpointer data) {
	//link hat Kind das ist noch dummy: link laden
	if (zond_tree_store_link_is_unloaded(iter))
		zond_tree_store_load_link(iter);

	return;
}

static void zond_treeview_text_edited(SondTreeview *stv, GtkTreeIter *iter,
		gchar const *new_text) {
	gint rc = 0;
	GError *error = NULL;
	gint node_id = 0;

	ZondTreeview *ztv = ZOND_TREEVIEW(stv);

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(ztv)), iter, 2,
			&node_id, -1);

	rc = zond_dbase_update_node_text(
			ztv_priv->zond->dbase_zond->zond_dbase_work, node_id, new_text,
			&error);
	if (rc) {
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(ztv)),
				"Knoten umbenennen nicht möglich\n\n", error->message, NULL);
		g_error_free(error);

		return;
	}

	zond_tree_store_set(iter, NULL, new_text, 0);

	return;
}

static void zond_treeview_render_node_text(GtkTreeViewColumn *column,
		GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter,
		gpointer data) {
	gint rc = 0;
	gint node_id = 0;
	gchar *text = NULL;
	GError *error = NULL;

	ZondTreeview *ztv = (ZondTreeview*) data;
	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	if (zond_tree_store_is_link(iter)) {
		gchar *label = NULL;
		gchar *markuptxt = NULL;

		// Retrieve the current label
		gtk_tree_model_get(model, iter, 1, &label, -1);

		markuptxt = g_strdup_printf("<i>%s</i>", label);
		g_free(label);

		if (zond_tree_store_get_link_head_nr(iter)) {
			markuptxt = add_string(g_strdup("<span foreground=\"purple\">"),
					markuptxt);
			markuptxt = add_string(markuptxt, g_strdup("</span>"));
		}

		g_object_set(renderer, "markup", markuptxt, NULL);
		g_free(markuptxt);

		/* Hintergrund für Links nicht setzen – fertig */
		return;
	}

	//Hintergrund icon rot wenn Text in textview
	gtk_tree_model_get(model, iter, 2, &node_id, -1);
	rc = zond_dbase_get_text(ztv_priv->zond->dbase_zond->zond_dbase_work,
			node_id, &text, &error);
	if (rc) {
		gchar *text_label = NULL;
		text_label = g_strconcat("Fehler in treeviews_render_node_text -\n\n"
				"Bei Aufruf zond_dbase_get_text:\n", error->message, NULL);
		g_error_free(error);
		gtk_label_set_text(ztv_priv->zond->label_status, text_label);
		g_free(text_label);
	} else if (!text || !g_strcmp0(text, ""))
		g_object_set(renderer, "background-set", FALSE, NULL);
	else
		g_object_set(renderer, "background-set", TRUE, NULL);

	g_free(text);

	return;
}

static void zond_treeview_class_init(ZondTreeviewClass *klass) {
	SOND_TREEVIEW_CLASS(klass)->render_text_cell =
			zond_treeview_render_node_text;
	SOND_TREEVIEW_CLASS(klass)->text_edited = zond_treeview_text_edited;

	SOND_TREEVIEW_CLASS(klass)->gmenu = g_menu_new();
	sond_treeview_add_base_menu(SOND_TREEVIEW_CLASS(klass)->gmenu);

	GMenu *gmenu = SOND_TREEVIEW_CLASS(klass)->gmenu;

	GMenu *sec_einf = g_menu_new();
	GMenu *sub_einf = g_menu_new();
	g_menu_append(sub_einf, "Gleiche Ebene", "stv.einf-ge");
	g_menu_append(sub_einf, "Unterebene",    "stv.einf-up");
	g_menu_append_submenu(sec_einf, "Punkt einf\u00fcgen", G_MENU_MODEL(sub_einf));
	g_object_unref(sub_einf);
	g_menu_prepend_section(gmenu, NULL, G_MENU_MODEL(sec_einf));
	g_object_unref(sec_einf);

	GMenu *sec_paste = g_menu_new();
	GMenu *sub_paste = g_menu_new();
	g_menu_append(sub_paste, "Gleiche Ebene", "stv.paste-ge");
	g_menu_append(sub_paste, "Unterebene",    "stv.paste-up");
	g_menu_append_submenu(sec_paste, "Einf\u00fcgen", G_MENU_MODEL(sub_paste));
	g_object_unref(sub_paste);
	GMenu *sub_paste_link = g_menu_new();
	g_menu_append(sub_paste_link, "Gleiche Ebene", "stv.paste-link-ge");
	g_menu_append(sub_paste_link, "Unterebene",    "stv.paste-link-up");
	g_menu_append_submenu(sec_paste, "Als Link einf\u00fcgen", G_MENU_MODEL(sub_paste_link));
	g_object_unref(sub_paste_link);
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_paste));
	g_object_unref(sec_paste);

	GMenu *sec_edit = g_menu_new();
	g_menu_append(sec_edit, "L\u00f6schen",            "stv.loeschen");
	g_menu_append(sec_edit, "Anbindung entfernen", "stv.anb-entf");
	g_menu_append(sec_edit, "Zu Ursprung springen","stv.jump");
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_edit));
	g_object_unref(sec_edit);

	/* Icon-Submenu: leer, wird bei erster Instanz befuellt */
	GMenu *sec_icon = g_menu_new();
	klass->gmenu_icons = g_menu_new();
	g_menu_append_submenu(sec_icon, "Icon \u00e4ndern", G_MENU_MODEL(klass->gmenu_icons));
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_icon));
	g_object_unref(sec_icon);

	GMenu *sec_oeffnen = g_menu_new();
	g_menu_append(sec_oeffnen, "Öffnen",        "stv.oeffnen");
	g_menu_append(sec_oeffnen, "Öffnen mit",    "stv.oeffnen-mit");
	g_menu_append(sec_oeffnen, "Auszug \u00f6ffnen", "stv.auszug");
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_oeffnen));
	g_object_unref(sec_oeffnen);

	return;
}

static gboolean on_query_tooltip(GtkWidget  *widget,
                                  gint        x,
                                  gint        y,
                                  gboolean    keyboard_mode,
                                  GtkTooltip *tooltip,
                                  gpointer    data)
{
    GtkTreeView  *tree_view = GTK_TREE_VIEW(widget);
    GtkTreeModel *model;
    GtkTreeIter   iter;
    GtkTreePath  *path = NULL;
    gint          bx, by;
    gint node_id = 0;
    gchar* file_part = NULL;
    gchar* section = NULL;
    Anbindung anbindung = { 0 };
    gchar* anb_string = NULL;
    GError* error = NULL;
    gchar* markup = NULL;

    // Koordinaten umrechnen
    gtk_tree_view_convert_widget_to_bin_window_coords(tree_view, x, y, &bx, &by);

    // Row unter der Maus ermitteln
    if (!gtk_tree_view_get_path_at_pos(tree_view, bx, by, &path, NULL, NULL, NULL))
        return FALSE;

    model = gtk_tree_view_get_model(tree_view);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);

	node_id = zond_treeview_get_filepart_and_section(
			ZOND_TREEVIEW(tree_view), &iter, &file_part, &section, &error);
	if (node_id == -1) {
		markup = g_strdup_printf("Fehler: %s", error->message);
		g_error_free(error);
	}
	else if (file_part) {
		if (section) {
			anbindung_parse_file_section(section, &anbindung);
			g_free(section);
			anb_string = anbindung_to_human_readable(&anbindung);
		}

		// Markup zusammenbauen
		markup = g_strdup_printf("<small><tt>%s</tt></small>\n",
				file_part);
		g_free(file_part);

		if (anb_string) {
			markup = add_string(markup,
					g_strdup_printf("<small><tt>%s</tt></small>",
			anb_string));
			g_free(anb_string);
		}

		gtk_tooltip_set_markup(tooltip, markup);
	    g_free(markup);
	}
	else
		return FALSE;

    return TRUE;
}

static void zond_treeview_init(ZondTreeview *ztv) {
	//Tree-Model erzeugen und verbinden
	ZondTreeStore *tree_store = g_object_new( ZOND_TYPE_TREE_STORE, NULL);

	gtk_tree_view_set_model(GTK_TREE_VIEW(ztv), GTK_TREE_MODEL(tree_store));
	g_object_unref(tree_store);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(ztv), FALSE);

	gtk_tree_view_column_set_attributes(
			gtk_tree_view_get_column(GTK_TREE_VIEW(ztv), 0),
			sond_treeview_get_cell_renderer_icon(SOND_TREEVIEW(ztv)),
			"icon-name", 0, NULL);
	gtk_tree_view_column_set_attributes(
			gtk_tree_view_get_column(GTK_TREE_VIEW(ztv), 0),
			sond_treeview_get_cell_renderer_text(SOND_TREEVIEW(ztv)), "text", 1,
			NULL);

	// Tooltip
	gtk_widget_set_has_tooltip(GTK_WIDGET(ztv), TRUE);
	g_signal_connect(ztv, "query-tooltip", G_CALLBACK(on_query_tooltip), NULL);

	// Spaltenbreite nach Expand/Collapse anpassen
	g_signal_connect(ztv, "row-expanded",
			G_CALLBACK(gtk_tree_view_columns_autosize), NULL);
	g_signal_connect(ztv, "row-collapsed",
			G_CALLBACK(gtk_tree_view_columns_autosize), NULL);

	return;
}

static gint zond_treeview_check_anchor_id(Projekt *zond,
		GtkTreeIter *iter_anchor, gint *anchor_id, gboolean child,
		GError **error) {
	gint rc = 0;
	gint type = 0;
	gint baum_inhalt_file = 0;
	gint file_part_angebunden = 0;

	if (*anchor_id == 0) {
		LOG_WARN("%s: anchor_id == 0", __func__);

		return 0;
	}

	rc = zond_dbase_get_type_and_link(zond->dbase_zond->zond_dbase_work,
			*anchor_id, &type, NULL, error);
	if (rc)
		ERROR_Z

	if (type == ZOND_DBASE_TYPE_FILE_PART) {
		gint rc = 0;

		rc = zond_dbase_find_baum_inhalt_file(
				zond->dbase_zond->zond_dbase_work, *anchor_id, &baum_inhalt_file,
				&file_part_angebunden, NULL, error);
		if (rc)
			ERROR_Z

		if (baum_inhalt_file) { //anchor ist angebunden
			if (*anchor_id != file_part_angebunden || child) {//nicht unmittelbar, sondern Vorfahren
				*anchor_id = 0;

				return 1; //child egal
			}
			else
				*anchor_id = baum_inhalt_file;
		}
	}

	return 0;
}

gint zond_treeview_get_anchor(Projekt *zond, gboolean* child,
		GtkTreeIter *iter_cursor, GtkTreeIter *iter_anchor,
		gint *anchor_id, gboolean* in_link, GError **error) {
	GtkTreeIter iter_cursor_intern = { 0 };
	GtkTreeIter iter_anchor_intern = { 0 };
	gint head_nr = 0;

	if (!sond_treeview_get_cursor(zond->treeview[zond->baum_active],
			&iter_cursor_intern)) {
		//Trick, weil wir keinen gültigen iter übergeben können->
		//setzten stamp auf stamp des "richtigen" tree_stores und
		//user_data auf root_node
		ZondTreeStore *store = NULL;

		store =
				ZOND_TREE_STORE(
						gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[zond->baum_active]) ));

		if (iter_cursor) {
			iter_cursor->stamp = zond_tree_store_get_stamp(store);
			iter_cursor->user_data = zond_tree_store_get_root_node(store);
		}

		if (iter_anchor) {
			iter_anchor->stamp = zond_tree_store_get_stamp(store);
			iter_anchor->user_data = zond_tree_store_get_root_node(store);
		}

		if (anchor_id)
			*anchor_id = zond_tree_store_get_root(store);

		if (child)
			*child = TRUE;

		return 0;
	}

	if (*child) {
		if (zond_tree_store_is_link(&iter_cursor_intern)) {
			zond_tree_store_get_iter_target(&iter_cursor_intern,
					&iter_anchor_intern);
			if (in_link)
				*in_link = TRUE;
		} else
			iter_anchor_intern = iter_cursor_intern;
	}
	else {
		if (zond_tree_store_is_link(&iter_cursor_intern) && // ist link
				((head_nr =
						zond_tree_store_get_link_head_nr(&iter_cursor_intern)) <= 0)) { //und kein head-link
			zond_tree_store_get_iter_target(&iter_cursor_intern,
					&iter_anchor_intern);
			if (in_link)
				*in_link = TRUE;
		}
		else //wenn iter_cursor_intern kein link oder head-link
			iter_anchor_intern = iter_cursor_intern; //weil kein child: anchor = head
	}

	if (iter_cursor)
		*iter_cursor = iter_cursor_intern;

	if (iter_anchor)
		*iter_anchor = iter_anchor_intern;

	if (anchor_id) {
		if (head_nr <= 0) {
			gint rc = 0;

			gtk_tree_model_get(
					GTK_TREE_MODEL(
							zond_tree_store_get_tree_store(
									&iter_anchor_intern)), &iter_anchor_intern,
					2, anchor_id, -1);

			rc = zond_treeview_check_anchor_id(zond, &iter_anchor_intern, anchor_id,
					*child, error);
			if (rc == -1)
				ERROR_Z
		} else
			*anchor_id = head_nr;
	}

	return 0;
}

static gint zond_treeview_insert_node(Projekt *zond, gboolean child,
		GError **error) {
	gint anchor_id = 0;
	gint node_id_new = 0;
	GtkTreeIter iter_cursor = { 0 };
	GtkTreeIter iter_anchor = { 0 };
	GtkTreeIter iter_new = { 0 };
	ZondTreeStore *tree_store = NULL;
	gboolean in_link = FALSE;
	gint rc = 0;

	g_return_val_if_fail(
			zond->baum_active == BAUM_INHALT
					|| zond->baum_active == BAUM_AUSWERTUNG, -1);

	rc = zond_treeview_get_anchor(zond, &child, &iter_cursor,
			&iter_anchor, &anchor_id, &in_link, error);
	if (rc)
		ERROR_Z

	if (!anchor_id)
		return 1; //Punkt darf nicht als Unterpunkt von Datei eingefügt werden
	else if (in_link)
		return 2; //Punkt darf nicht als Unterpunkt von Link eingefügt werden

	//Knoten in Datenbank einfügen
	node_id_new = zond_dbase_insert_node(zond->dbase_zond->zond_dbase_work,
			anchor_id, child, ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL,
			zond->icon[ICON_NORMAL].icon_name, "Neuer Punkt", NULL, error);
	if (node_id_new == -1)
		ERROR_Z

	//Knoten in baum_inhalt einfuegen
	tree_store = zond_tree_store_get_tree_store(&iter_anchor);
	zond_tree_store_insert(tree_store, (anchor_id > 2) ? &iter_anchor : NULL, child,
			&iter_new);

	//Standardinhalt setzen
	zond_tree_store_set(&iter_new, zond->icon[ICON_NORMAL].icon_name,
			"Neuer Punkt", node_id_new);

	if (child && anchor_id > 2)
		sond_treeview_expand_row(zond->treeview[zond->baum_active],
				&iter_cursor);
	sond_treeview_set_cursor(zond->treeview[zond->baum_active], &iter_new);

	return 0;
}

gint zond_treeview_walk_tree(ZondTreeview *ztv, gboolean with_younger_siblings,
		gint node_id, GtkTreeIter *iter_anchor, gboolean child,
		GtkTreeIter *iter_inserted, gint anchor_id, gint *node_id_inserted,
		gint (*walk_tree)(ZondTreeview*, gint, GtkTreeIter*, gboolean,
				GtkTreeIter*, gint, gint*, GError**), GError **error) {
	gint rc = 0;
	gint first_child = 0;
	gint node_id_new = 0;
	GtkTreeIter iter_new = { 0 };

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	rc = walk_tree(ztv, node_id, iter_anchor, child, &iter_new, anchor_id,
			&node_id_new, error);
	if (rc == -1)
		ERROR_Z

	rc = zond_dbase_get_first_child(
			ztv_priv->zond->dbase_zond->zond_dbase_work, node_id,
			&first_child, error);
	if (rc)
		ERROR_Z
	else if (first_child > 0) {
		gint rc = 0;

		rc = zond_treeview_walk_tree(ztv, TRUE, first_child, &iter_new,
				TRUE,
				NULL, node_id_new, NULL, walk_tree, error);
		if (rc)
			ERROR_Z
	}

	if (with_younger_siblings) {
		gint younger_sibling = 0;

		rc = zond_dbase_get_younger_sibling(
				ztv_priv->zond->dbase_zond->zond_dbase_work, node_id,
				&younger_sibling, error);
		if (rc)
			ERROR_Z
		else if (younger_sibling > 0) {
			rc = zond_treeview_walk_tree(ztv, TRUE, younger_sibling, &iter_new,
					FALSE,
					NULL, node_id_new, NULL, walk_tree, error);
			if (rc)
				ERROR_Z
		}
	}

	if (node_id_inserted)
		*node_id_inserted = node_id_new;
	if (iter_inserted)
		*iter_inserted = iter_new;

	return 0;
}

static gboolean zond_treeview_iter_foreach_node_id(GtkTreeModel *model,
		GtkTreePath *path, GtkTreeIter *iter, gpointer user_data) {
	GtkTreeIter **new_iter = (GtkTreeIter**) user_data;
	gint node_id = GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(model), "node_id" ));

	gint node_id_tree = 0;
	gtk_tree_model_get(model, iter, 2, &node_id_tree, -1);

	if (node_id == node_id_tree) {
		*new_iter = gtk_tree_iter_copy(iter);
		return TRUE;
	} else
		return FALSE;
}

GtkTreeIter*
zond_treeview_abfragen_iter(ZondTreeview *treeview, gint node_id) {
	GtkTreeIter *iter = NULL;
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));

	g_object_set_data(G_OBJECT(model), "node_id", GINT_TO_POINTER(node_id));
	gtk_tree_model_foreach(model,
			(GtkTreeModelForeachFunc) zond_treeview_iter_foreach_node_id,
			&iter);

	return iter;
}

static gint zond_treeview_insert_file_parts(ZondTreeview *ztv, gint node_id,
		GtkTreeIter *iter, gboolean child, GtkTreeIter *iter_inserted,
		gint anchor_id, gint *node_id_inserted, GError **error) {
	gint rc = 0;
	gchar *icon_name = NULL;
	gchar *node_text = NULL;
	GtkTreeIter iter_new = { 0 };
	GtkTreeIter *iter_pdf_abschnitt = NULL;

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	//Wenn Zweig schon vorhanden ist, weil etwa BAUM_INHALT_PDF_ABSCHNITT bestanden hat
	iter_pdf_abschnitt = zond_treeview_abfragen_iter(ztv, node_id);
	if (iter_pdf_abschnitt) {
		zond_tree_store_move_node(iter_pdf_abschnitt,
				ZOND_TREE_STORE(
						gtk_tree_view_get_model( GTK_TREE_VIEW(ztv_priv->zond->treeview[BAUM_INHALT]) )),
				iter, child, iter_inserted);

		gtk_tree_iter_free(iter_pdf_abschnitt);

		return 0;
	}

	//Ansonsten Einfügen
	rc = zond_dbase_get_node(ztv_priv->zond->dbase_zond->zond_dbase_work,
			node_id, NULL, NULL, NULL, NULL, &icon_name, &node_text, NULL,
			error);
	if (rc)
		ERROR_Z

	zond_tree_store_insert(
			ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) )),
			iter, child, &iter_new);
	zond_tree_store_set(&iter_new, icon_name, node_text, node_id);

	g_free(icon_name);
	g_free(node_text);

	if (iter_inserted)
		*iter_inserted = iter_new;

	return 0;
}

gint zond_treeview_insert_file_part_in_db(Projekt *zond, gchar const *file_part,
		gchar const* basename_item, gchar const *icon_name, gint *file_root,
		GError **error) {
	gint rc = 0;
	gchar const *basename = NULL;
	gint file_root_int = 0;

	if (basename_item)
		basename = basename_item;
	else {
		basename = g_strrstr(file_part, "/");
		if (basename)
			basename++; //basename ist der Teil nach dem letzten "/"
		else
			basename = file_part;
	}

	rc = zond_dbase_create_file_root(zond->dbase_zond->zond_dbase_work,
			file_part, icon_name, basename, NULL, &file_root_int, error);
	if (rc)
		ERROR_Z

	if (file_root)
		*file_root = file_root_int;

	return 0;
}

static gint zond_treeview_remove_childish_anbindungen(ZondTreeview *ztv,
		InfoWindow *info_window, gint ID, gint *anchor_id, gboolean *child,
		Anbindung *anbindung, GError **error) {
	gint resp = 0;

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	do {
		gint rc = 0;
		gint baum_inhalt_file = 0;

		rc = zond_dbase_get_first_baum_inhalt_file_child(
				ztv_priv->zond->dbase_zond->zond_dbase_work, ID,
				&baum_inhalt_file, NULL, error);
		if (rc)
			ERROR_Z

		if (!baum_inhalt_file)
			break;

		//noch nicht gefrägt gehabt?!
		if (!resp) {
			info_window_set_message(info_window,
					"...Abschnitt bereits angebunden");

			resp = abfrage_frage(ztv_priv->zond->app_window,
					"Mindestens ein Teil des PDF ist bereits angebunden",
					"Abschnitt hinzuziehen?", NULL);
			if (resp != GTK_RESPONSE_YES)
				return 1;
		}

		//Prüfen, ob man sich die anchor_id löschen würde...
		if (baum_inhalt_file == *anchor_id) {
			gint rc = 0;
			gint older_sibling = 0;

			//kann ja nicht child == TRUE sein, weil dann würde ja in Datei eingefügt, was sowieso verboten ist
			//also dann older sibling
			rc = zond_dbase_get_older_sibling(
					ztv_priv->zond->dbase_zond->zond_dbase_work,
					baum_inhalt_file, &older_sibling, error);
			if (rc)
				ERROR_Z

					//sonst parent
			if (!older_sibling) {
				gint rc = 0;
				gint parent = 0;

				rc = zond_dbase_get_parent(
						ztv_priv->zond->dbase_zond->zond_dbase_work,
						baum_inhalt_file, &parent, error);
				if (rc)
					ERROR_Z

				*anchor_id = parent;
				*child = TRUE;
			} else
				*anchor_id = older_sibling;
		}

		//BAUM_INHALT_PDF_ABSCHNITT löschen
		rc = zond_dbase_remove_node(ztv_priv->zond->dbase_zond->zond_dbase_work,
				baum_inhalt_file, error);
		if (rc)
			ERROR_Z
	} while (1);

	return 0;
}

/** Fehler: -1
 eingefügt: node_id
 nicht eingefügt, weil schon angebunden: 0 **/
static gint zond_treeview_leaf_anbinden(ZondTreeview *ztv,
		GtkTreeIter *anchor_iter, gint anchor_id, gboolean child,
		SondTVFMItem* stvfm_item, InfoWindow *info_window,
		gint *zaehler, GError** error) {
	gint new_node_id = 0;
	GtkTreeIter iter_new = { 0 };
	g_autofree gchar *filepart = NULL;
	gchar const* section = NULL;
	gint ID_file_part = 0;
	gint rc = 0;
	SondFilePart *sfp = NULL;
	gchar* info_text = NULL;
	gint baum_inhalt_file = 0;

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);
	filepart = sond_file_part_get_filepart(sfp);
	section = sond_tvfm_item_get_path_or_section(stvfm_item);

	info_text = (section) ? g_strdup_printf("Anbindung Abschnitt '%s' in '%s'",
			section, filepart) : g_strdup_printf("Anbindung Datei '%s'",
			filepart);
	info_window_set_message(info_window, info_text);
	g_free(info_text);

	rc = zond_dbase_get_section(ztv_priv->zond->dbase_zond->zond_dbase_work,
			filepart, section, &ID_file_part, error);
	if (rc)
		ERROR_Z

	if (!ID_file_part) { //Datei noch nicht in zond_dbase
		gint rc = 0;

		if (section) { //kann nicht sein!
			LOG_WARN("%s: Datei nicht in Datenbank, Abschnitt aber schon",
					__func__);

			return 0;
		}

		//Datei in zond_dbase einfügen
		rc = zond_treeview_insert_file_part_in_db(ztv_priv->zond, filepart,
				sond_tvfm_item_get_display_name(stvfm_item),
				sond_tvfm_item_get_icon_name(stvfm_item), &ID_file_part, error);
		if (rc)
			ERROR_Z
	}
	else { //nur wenn in db, dann möglicherweise in baum_inhalt
		//wenn schon pdf_root existiert, dann herausfinden, ob aktuell an Baum angebunden
		rc = zond_dbase_find_baum_inhalt_file(ztv_priv->zond->dbase_zond->zond_dbase_work,
				ID_file_part, &baum_inhalt_file, NULL, NULL, error);
		if (rc)
			ERROR_Z

		if (baum_inhalt_file) {
			gchar* message = NULL;

			message = g_strdup_printf("filepart '%s' bereits angebunden",
					filepart);
			info_window_set_message(info_window, message);
			g_free(message);

			return 0; //Wenn angebunden: nix machen
		}
	}

	//etwaige untergeordnete Anbindungen heranholen, falls gewünscht
	rc = zond_treeview_remove_childish_anbindungen(ztv, info_window,
			ID_file_part, &anchor_id, &child, NULL, error);
	if (rc == -1)
		ERROR_Z
	else if (rc == 1)
		return 0; //will nicht

	new_node_id = zond_dbase_insert_node(
			ztv_priv->zond->dbase_zond->zond_dbase_work, anchor_id, child,
			ZOND_DBASE_TYPE_BAUM_INHALT_FILE, ID_file_part, NULL, NULL,
			NULL, NULL, NULL, error);
	if (new_node_id == -1)
		ERROR_Z

	rc = zond_treeview_walk_tree(ztv, FALSE, ID_file_part,
			(zond_tree_store_get_root(ZOND_TREE_STORE(
			gtk_tree_view_get_model( GTK_TREE_VIEW(ztv)))) == anchor_id) ?
			NULL : anchor_iter, child, &iter_new, 0,
			NULL, zond_treeview_insert_file_parts, error);
	if (rc == -1)
		ERROR_Z

	*anchor_iter = iter_new;
	(*zaehler)++;

	return new_node_id;
}

/*  Fehler: werden im info_window angezeigt
 **  ansonsten: Id des zunächst erzeugten Knotens  */
static gint zond_treeview_anbinden_rekursiv(ZondTreeview *ztv,
		GtkTreeIter *anchor_iter, gint anchor_id, gboolean child, SondTVFMItem *stvfm_item,
		InfoWindow *info_window, gint *zaehler, gboolean* dir_inserted) {
	gint new_node_id = 0;
	GError* error = NULL;

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	if (*(info_window->cancel))
		return -1;

	if (sond_tvfm_item_get_item_type(stvfm_item) == SOND_TVFM_ITEM_TYPE_LEAF ||
			sond_tvfm_item_get_item_type(stvfm_item) == SOND_TVFM_ITEM_TYPE_LEAF_SECTION) {
		new_node_id = zond_treeview_leaf_anbinden(ztv, anchor_iter, anchor_id, child,
				stvfm_item, info_window, zaehler, &error);

		if (new_node_id == -1) {
			gchar* errmsg = NULL;
			gchar* filepart = NULL;
			SondFilePart* sfp = NULL;

			sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);
			filepart = sond_file_part_get_filepart(sfp);
			errmsg = g_strdup_printf("Fehler Anbindung filepart '%s':\n%s",
					filepart, error->message);
			g_free(filepart);
			g_error_free(error);
			info_window_set_message(info_window, errmsg);
			g_free(errmsg);

			return 0;
		}
	}
	else { //SOND_TVFM_ITEM_TYPE_DIR!
		gchar const* basename = NULL;
		GPtrArray* arr_children = NULL;
		gchar const* icon_name = NULL;
		gchar *text = 0;
		ZondTreeStore *tree_store = NULL;
		GtkTreeIter iter_new = { 0 };
		gint rc = 0;
		gint anchor_id_dir = 0;
		gint anchor_id_child = 0;
		gboolean child_anchor = TRUE;

		//icon_name ermitteln
		if (SOND_IS_FILE_PART_PDF(sond_tvfm_item_get_sond_file_part(stvfm_item)))
			icon_name = "pdf-folder";
		else if (SOND_IS_FILE_PART_ZIP(sond_tvfm_item_get_sond_file_part(stvfm_item)))
			if (!sond_tvfm_item_get_path_or_section(stvfm_item))
				icon_name = "package-x-generic";
			else
				icon_name = "folder";
		else if (SOND_IS_FILE_PART_GMESSAGE(sond_tvfm_item_get_sond_file_part(stvfm_item))) {
			if (!sond_tvfm_item_get_path_or_section(stvfm_item))
				icon_name = "mail-read";
			else
				icon_name = "folder";
		}
		else
			icon_name = "folder";

		basename = sond_tvfm_item_get_display_name(stvfm_item);

		anchor_id_dir = zond_dbase_insert_node(
				ztv_priv->zond->dbase_zond->zond_dbase_work, anchor_id, child,
				ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL, icon_name, basename,
				NULL, &error);
		if (anchor_id_dir == -1) {
			gchar* errmsg = NULL;
			gchar* filepart = NULL;
			SondFilePart* sfp = NULL;

			sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);

			filepart = sond_file_part_get_filepart(sfp);
			errmsg = g_strdup_printf("Fehler Anbindung filepart '%s':\n%s",
					filepart, error->message);
			g_free(filepart);
			g_error_free(error);
			info_window_set_message(info_window, errmsg);
			g_free(errmsg);

			return 0;
		}

		tree_store = ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ));
		zond_tree_store_insert(tree_store, (anchor_id > 2) ? anchor_iter : NULL,
				child, &iter_new);
		*anchor_iter = iter_new;
		*dir_inserted = TRUE;

		//Standardinhalt setzen
		zond_tree_store_set(&iter_new, icon_name, basename, anchor_id_dir);

		text = g_strconcat("Verzeichnis eingefügt: ", basename, NULL);
		info_window_set_message(info_window, text);
		g_free(text);

		rc = sond_tvfm_item_load_children(stvfm_item, &arr_children, &error);
		if (rc) {
			gchar* errmsg = NULL;
			gchar* filepart = NULL;
			SondFilePart* sfp = NULL;

			sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);

			filepart = sond_file_part_get_filepart(sfp);
			errmsg = g_strdup_printf("Kinder von filepart '%s' konnten "
					"nicht geladen werden:\n%s", filepart, error->message);
			g_free(filepart);
			g_error_free(error);
			info_window_set_message(info_window, errmsg);
			g_free(errmsg);

			return 0;
		}

		anchor_id_child = anchor_id_dir; //wird in Schleife für Kinder verwendet
		for (guint i = 0; i < arr_children->len; i++) {
			SondTVFMItem* stvfm_item_child = NULL;

			stvfm_item_child = g_ptr_array_index(arr_children, i);
			new_node_id = zond_treeview_anbinden_rekursiv(ztv, &iter_new, anchor_id_child,
					child_anchor, stvfm_item_child, info_window, zaehler, dir_inserted);
			if (new_node_id == -1) //abgebrochen
				break;

			if (new_node_id > 0) {
				child_anchor = FALSE;
				anchor_id_child = new_node_id;
			}
		}

		new_node_id = anchor_id_dir; //wird zurückgegeben

		g_ptr_array_unref(arr_children);
	}

	return new_node_id;
}

typedef struct {
	ZondTreeview *ztv;
	gint anchor_id;
	GtkTreeIter anchor_iter;
	gboolean child;
	gint zaehler;
	gboolean dir_inserted;
	InfoWindow *info_window;
} SSelectionAnbinden;

static gint zond_treeview_clipboard_anbinden_foreach(SondTreeview *stv,
		GtkTreeIter *iter, gpointer data, GError **error) {
	SondTVFMItem* stvfm_item = NULL;
	gint rc = 0;

	SSelectionAnbinden *s_selection = (SSelectionAnbinden*) data;

	//SondFilePart im ZondTreeviewFM holen
	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter, 0,
			&stvfm_item, -1);

	rc = zond_treeview_anbinden_rekursiv(s_selection->ztv,
			&s_selection->anchor_iter, s_selection->anchor_id,
			s_selection->child, stvfm_item, s_selection->info_window,
			&s_selection->zaehler, &s_selection->dir_inserted);
	g_object_unref(stvfm_item);
	if (rc == -1) //Kein Fehler, Cancel gedrückt!
		return 1; //sond_treeview_..._foreach bricht dann ab

	s_selection->child = FALSE;
	s_selection->anchor_id = rc;

	return 0;
}

static void zond_treeview_clipboard_anbinden(Projekt *zond, gint anchor_id,
		GtkTreeIter *anchor_iter, gboolean child, InfoWindow *info_window) {
	SSelectionAnbinden s_selection = { 0 };
	GError *error = NULL;

	s_selection.ztv = ZOND_TREEVIEW(zond->treeview[BAUM_INHALT]);
	s_selection.anchor_id = anchor_id;
	s_selection.anchor_iter = *anchor_iter;
	s_selection.child = child;
	s_selection.zaehler = 0;
	s_selection.dir_inserted = FALSE;
	s_selection.info_window = info_window;

	sond_treeview_clipboard_foreach(
			zond_treeview_clipboard_anbinden_foreach, &s_selection, &error);

	if (s_selection.zaehler || s_selection.dir_inserted) {
		sond_treeview_expand_to_row(zond->treeview[BAUM_INHALT],
				&s_selection.anchor_iter);
		sond_treeview_set_cursor(zond->treeview[BAUM_INHALT],
				&s_selection.anchor_iter);
	}

	gchar *text = g_strdup_printf("%i Anbindungen eingefügt",
			s_selection.zaehler);
	info_window_set_message(info_window, text);
	g_free(text);

	return;
}

typedef struct {
	Projekt *zond;
	GtkTreeIter *iter_anchor;
	gboolean child;
	gint anchor_id;
} SSelection;

static gint zond_treeview_clipboard_verschieben_foreach(SondTreeview *tree_view,
		GtkTreeIter *iter_src, gpointer data, GError **error) {
	gint node_id = 0;
	gint rc = 0;
	GtkTreeIter iter_new = { 0 };

	SSelection *s_selection = (SSelection*) data;

	//soll link verschoben werden? Nur wenn head
	if (zond_tree_store_is_link(iter_src)) {
		//nur packen, wenn head
		if ((node_id = zond_tree_store_get_link_head_nr(iter_src)) <= 0)
			return 0;
	} else
		gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view)),
				iter_src, 2, &node_id, -1);

	//soll Ziel verschoben werden? Nein!
	if (zond_tree_store_get_root(zond_tree_store_get_tree_store(iter_src))
			== BAUM_INHALT) {
		gint rc = 0;
		gint type = 0;

		rc = zond_dbase_get_type_and_link(
				s_selection->zond->dbase_zond->zond_dbase_work, node_id, &type,
				NULL, error);
		if (rc == -1)
			ERROR_Z

		if (type == ZOND_DBASE_TYPE_FILE_PART) { //Test, ob als BAUM_INHALT_PDF_ABSCHNITT angebunden - dann geht verschieben
			gint rc = 0;
			gint baum_inhalt_file = 0;

			rc = zond_dbase_get_baum_inhalt_file_from_file_part(
					s_selection->zond->dbase_zond->zond_dbase_work, node_id,
					&baum_inhalt_file, error);
			if (rc == -1)
				ERROR_Z

			if (!baum_inhalt_file)
				return 0;

			node_id = baum_inhalt_file;
		}
	}

	//Knoten verschieben verschieben
	rc = zond_dbase_verschieben_knoten(
			s_selection->zond->dbase_zond->zond_dbase_work, node_id,
			s_selection->anchor_id, s_selection->child, error);
	if (rc)
		ERROR_Z

	zond_tree_store_move_node(iter_src,
			zond_tree_store_get_tree_store(s_selection->iter_anchor),
			s_selection->iter_anchor, s_selection->child, &iter_new);

	s_selection->child = FALSE;
	*(s_selection->iter_anchor) = iter_new;
	s_selection->anchor_id = node_id;

	return 0;
}

static gint zond_treeview_clipboard_verschieben(Projekt *zond, gboolean child,
		GtkTreeIter *iter_cursor, GtkTreeIter *iter_anchor, gint anchor_id,
		GError **error) {
	gint rc = 0;
	Clipboard *clipboard = NULL;

	SSelection s_selection = { zond, iter_anchor, child, anchor_id };

	clipboard =
			((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW))->clipboard;

	if (zond_tree_store_get_tree_store(
			iter_cursor) !=
			ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(clipboard->tree_view) )))
		return 0;

	rc = sond_treeview_clipboard_foreach(
			zond_treeview_clipboard_verschieben_foreach, &s_selection, error);
	if (rc == -1)
		ERROR_Z

			//Alte Auswahl löschen
	if (clipboard->arr_ref->len > 0)
		g_ptr_array_remove_range(clipboard->arr_ref, 0,
				clipboard->arr_ref->len);

	gtk_widget_queue_draw(GTK_WIDGET(zond->treeview[zond->baum_active]));

	if (child
			&& (iter_cursor->user_data
					!= zond_tree_store_get_root_node(
							zond_tree_store_get_tree_store(iter_cursor))))
		sond_treeview_expand_to_row(zond->treeview[zond->baum_active],
				s_selection.iter_anchor);
	sond_treeview_set_cursor(zond->treeview[zond->baum_active],
			s_selection.iter_anchor);

	return 0;
}

static gint zond_treeview_copy_pdf_abschnitt(ZondTreeview *ztv, gint node_id,
		GtkTreeIter *iter, gboolean child, GtkTreeIter *iter_inserted,
		gint anchor_id, gint *node_id_inserted, GError **error) {
	gchar *icon_name = NULL;
	gchar *node_text = NULL;
	gchar *text = NULL;
	gint rc = 0;

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	rc = zond_dbase_get_node(ztv_priv->zond->dbase_zond->zond_dbase_work,
			node_id, NULL, NULL, NULL, NULL, &icon_name, &node_text, &text,
			error);
	if (rc)
		ERROR_Z

	*node_id_inserted = zond_dbase_insert_node(
			ztv_priv->zond->dbase_zond->zond_dbase_work, anchor_id, child,
			ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY, node_id, NULL, NULL,
			icon_name, node_text, text, error);
	if (*node_id_inserted == -1) {
		g_free(icon_name);
		g_free(node_text);
		g_free(text);

		ERROR_Z
	}

	zond_tree_store_insert(
			ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) )),
			iter, child, iter_inserted);
	zond_tree_store_set(iter_inserted, icon_name, node_text, *node_id_inserted);

	g_free(icon_name);
	g_free(node_text);
	g_free(text);

	return 0;
}

gint zond_treeview_copy_node_to_baum_auswertung(ZondTreeview *ztv, gint node_id,
		GtkTreeIter *iter, gboolean child, GtkTreeIter *iter_inserted,
		gint anchor_id, gint *node_id_inserted, GError **error) {
	gint type = 0;
	gint link = 0;
	gchar *icon_name = NULL;
	gchar *node_text = NULL;
	gchar *text = NULL;
	gint rc = 0;
	gint node_id_new = 0;
	gint type_new = 0;
	gint link_new = 0;

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	rc = zond_dbase_get_node(ztv_priv->zond->dbase_zond->zond_dbase_work,
			node_id, &type, &link, NULL, NULL, &icon_name, &node_text, &text,
			error);
	if (rc)
		ERROR_Z

	if (type != ZOND_DBASE_TYPE_BAUM_STRUKT) {
		if (type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY
				|| type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE)
			link_new = link;
		else
			link_new = node_id;

		type_new = ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY;
	} else
		type_new = ZOND_DBASE_TYPE_BAUM_STRUKT;

	node_id_new = zond_dbase_insert_node(
			ztv_priv->zond->dbase_zond->zond_dbase_work, anchor_id, child,
			type_new, link_new, NULL, NULL, icon_name, node_text, text, error);
	if (node_id_new == -1) {
		g_prefix_error(error, "%s\n", __func__);
		g_free(icon_name);
		g_free(node_text);
		g_free(text);

		return -1;
	}

	if (node_id_inserted)
		*node_id_inserted = node_id_new;

	zond_tree_store_insert(
			ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) )),
			iter, child, iter_inserted);
	zond_tree_store_set(iter_inserted, icon_name, node_text, node_id_new);

	g_free(icon_name);
	g_free(node_text);
	g_free(text);

	if (type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE) {
		gint first_child = 0;

		rc = zond_dbase_get_first_child(
				ztv_priv->zond->dbase_zond->zond_dbase_work, link, &first_child,
				error);
		if (rc)
			ERROR_Z

		if (first_child == 0)
			return 0;

		rc = zond_treeview_walk_tree(ztv, TRUE, first_child, iter_inserted,
				TRUE, NULL, node_id_new, NULL, zond_treeview_copy_pdf_abschnitt,
				error);
		if (rc)
			ERROR_Z
	}

	return 0;
}

static gint zond_treeview_clipboard_kopieren_foreach(SondTreeview *tree_view,
		GtkTreeIter *iter, gpointer data, GError **error) {
	gint rc = 0;
	gint node_id = 0;
	gint node_id_new = 0;
	GtkTreeIter iter_new = { 0 };

	SSelection *s_selection = (SSelection*) data;

	//soll durch etwaige links "hindurchgucken"
	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view)), iter,
			2, &node_id, -1);

	rc = zond_dbase_begin(s_selection->zond->dbase_zond->zond_dbase_work, error);
	if (rc)
		ERROR_Z

	rc = zond_treeview_walk_tree(
			ZOND_TREEVIEW(s_selection->zond->treeview[BAUM_AUSWERTUNG]),
			FALSE, node_id, s_selection->iter_anchor, s_selection->child,
			&iter_new, s_selection->anchor_id, &node_id_new,
			zond_treeview_copy_node_to_baum_auswertung, error);
	if (rc)
		ERROR_ROLLBACK_Z(s_selection->zond->dbase_zond->zond_dbase_work)

	rc = zond_dbase_commit(s_selection->zond->dbase_zond->zond_dbase_work,
			error);
	if (rc)
		ERROR_ROLLBACK_Z(s_selection->zond->dbase_zond->zond_dbase_work)

	s_selection->child = FALSE;
	*(s_selection->iter_anchor) = iter_new;
	s_selection->anchor_id = node_id_new;

	return 0;
}

static gint zond_treeview_clipboard_kopieren(Projekt *zond, gboolean child,
		GtkTreeIter *iter_cursor, GtkTreeIter *iter_anchor, gint anchor_id,
		GError **error) {
	SSelection s_selection = { zond, iter_anchor, child, anchor_id };

	if (zond_tree_store_get_tree_store(
			iter_cursor) ==
			ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG]) ))) {
		gint rc = 0;

		rc = sond_treeview_clipboard_foreach(
				zond_treeview_clipboard_kopieren_foreach, &s_selection, error);
		if (rc == -1)
			ERROR_Z
	} else
		return 0;

	if (child
			&& (iter_cursor->user_data
					!= zond_tree_store_get_root_node(
							zond_tree_store_get_tree_store(iter_cursor))))
		sond_treeview_expand_to_row(zond->treeview[zond->baum_active],
				s_selection.iter_anchor);
	sond_treeview_set_cursor(zond->treeview[zond->baum_active],
			s_selection.iter_anchor);

	return 0;
}

static gint zond_treeview_paste_clipboard_as_link_foreach(
		SondTreeview *tree_view, GtkTreeIter *iter, gpointer data,
		GError **error) {
	gint node_id_new = 0;
	gint node_id = 0;
	GtkTreeIter iter_target = { 0 };
	GtkTreeIter iter_new = { 0 };

	SSelection *s_selection = (SSelection*) data;

	//soll durch etwaige links "hindurchgucken"
	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view)), iter,
			2, &node_id, -1);

	//node ID, auf den link zeigen soll
	node_id_new = zond_dbase_insert_node(
			s_selection->zond->dbase_zond->zond_dbase_work,
			s_selection->anchor_id, s_selection->child,
			ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK, node_id, NULL, NULL, NULL,
			NULL, NULL, error);
	if (node_id_new < 0)
		ERROR_Z

	//falls link im clipboard: iter_target ermitteln, damit nicht link auf link zeigt
	zond_tree_store_get_iter_target(iter, &iter_target);
	zond_tree_store_insert_link(&iter_target, node_id_new,
			zond_tree_store_get_tree_store(s_selection->iter_anchor),
			(s_selection->anchor_id) ? s_selection->iter_anchor : NULL,
			s_selection->child, &iter_new);

	s_selection->anchor_id = node_id_new;
	s_selection->child = FALSE;
	*(s_selection->iter_anchor) = iter_new;

	return 0;
}

//Ziel ist immer BAUM_AUSWERTUNG
static gint zond_treeview_paste_clipboard_as_link(Projekt *zond, gboolean child,
		GtkTreeIter *iter_cursor, GtkTreeIter *iter_anchor, gint anchor_id,
		GError **error) {
	gint rc = 0;

	SSelection s_selection = { zond, iter_anchor, child, anchor_id };

	rc = sond_treeview_clipboard_foreach(
			zond_treeview_paste_clipboard_as_link_foreach, &s_selection,
			error);
	if (rc == -1)
		ERROR_Z

	if (child
			&& (iter_cursor->user_data
					!= zond_tree_store_get_root_node(
							zond_tree_store_get_tree_store(iter_cursor))))
		sond_treeview_expand_row(zond->treeview[zond->baum_active],
				iter_cursor);
	sond_treeview_set_cursor(zond->treeview[zond->baum_active],
			s_selection.iter_anchor);

	return 0;
}

static gint zond_treeview_paste_clipboard(Projekt *zond, gboolean child,
		gboolean link, GError **error) {
	Clipboard *clipboard = NULL;
	GtkTreeIter iter_cursor = { 0, };
	GtkTreeIter iter_anchor = { 0 };
	gint anchor_id = 0;
	gboolean in_link = FALSE;
	gint rc = 0;
	gint cancel = 0;

	if (zond->baum_active == KEIN_BAUM || zond->baum_active == BAUM_FS)
		return 0;

	clipboard =
			((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW))->clipboard;

	//Wenn clipboard leer - ganz am Anfang oder nach Einfügen von Ausschneiden
	if (clipboard->arr_ref->len == 0)
		return 0;

	//ist der Baum, der markiert wurde, egal ob link zu anderem Baum oder nicht
	Baum baum_selection = (Baum) sond_treeview_get_id(clipboard->tree_view);

	//verhindern, daß in Zweig unterhalb eingefügt wird
	if (zond->baum_active == baum_selection) {//wenn innerhalb des gleichen Baums
		if (sond_treeview_test_cursor_descendant(
				zond->treeview[zond->baum_active], child)) {
			if (error) *error = g_error_new(ZOND_ERROR, 0, "%s\n"
					"Unzulässiges Ziel: Abkömmling von einzufügendem Knoten", __func__);
			return -1;
		}
	}

	rc = zond_treeview_get_anchor(zond, &child, &iter_cursor, &iter_anchor,
			&anchor_id, &in_link, error);
	if (rc)
		ERROR_Z

	if (!anchor_id)
		return 1;
	else if (in_link)
		return 2;

	if (baum_selection == BAUM_FS) {
		InfoWindow *info_window = NULL;

		info_window = info_window_open(zond->app_window,
				&cancel, "Dateien anbinden");

		zond_treeview_clipboard_anbinden(zond, anchor_id,
				&iter_anchor, child, info_window);

		info_window_close(info_window);

		return 0;
	}

	if (clipboard->ausschneiden && !link) {
		gint rc = 0;

		rc = zond_treeview_clipboard_verschieben(zond, child, &iter_cursor,
				&iter_anchor, anchor_id, error);
		if (rc == -1)
			ERROR_Z
	} else if (!clipboard->ausschneiden && !link) {
		gint rc = 0;

		rc = zond_treeview_clipboard_kopieren(zond, child, &iter_cursor,
				&iter_anchor, anchor_id, error);
		if (rc == -1)
			ERROR_Z
	} else if (!clipboard->ausschneiden && link) {
		gint rc = 0;

		if (zond->baum_active == BAUM_INHALT)
			return 0; //nur in BAUM_AUSWERTUNG!

		rc = zond_treeview_paste_clipboard_as_link(zond, child, &iter_cursor,
				&iter_anchor, anchor_id, error);
		if (rc)
			ERROR_Z
	}
	//ausschneiden und link geht ja nicht...

	return 0;
}

static gint zond_treeview_selection_loeschen_foreach(SondTreeview *tree_view,
		GtkTreeIter *iter, gpointer data, GError **error) {
	gint node_id = 0;
	gint rc = 0;
	gboolean visible = FALSE;
	GtkTreeIter iter_fm = { 0 };

	Projekt *zond = (Projekt*) data;

	//node_id herausfinden - wenn kein Link-Head->raus
	if (zond_tree_store_is_link(iter)) {
		gint head_nr = 0;

		head_nr = zond_tree_store_get_link_head_nr(iter);

		if (head_nr == 0)
			return 0;
		else
			node_id = head_nr;
		//Prüfung auf Link von BAUM_AUSWERTUNG_COPY nicht erforderlich
		//nur Linkziel kann Ziel von Copy sein
		//Ebenso ob Link auf PDF-Abschnitt zeigt; nur Link wird entfernt
	}
	else
		gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view)),
				iter, 2, &node_id, -1);

	if (tree_view == zond->treeview[BAUM_INHALT]) {
		gint rc = 0;
		gint baum_auswertung_copy = 0;
		gint baum_inhalt_file = 0;
		gint type = 0;
		gchar *file_part = NULL;
		gchar *section = NULL;

		rc = zond_dbase_get_node(zond->dbase_zond->zond_dbase_work, node_id,
				&type, NULL, &file_part, &section, NULL, NULL, NULL, error);
		if (rc)
			ERROR_Z

		if (type == ZOND_DBASE_TYPE_BAUM_STRUKT) {
			gint num_children = 0;

			num_children = gtk_tree_model_iter_n_children(
					gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view)), iter);

			for (guint i = num_children; i > 0; i--) {
				GtkTreeIter iter_child = { 0 };
				gint rc = 0;

				gtk_tree_model_iter_nth_child(
						gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view)),
						&iter_child, iter, i - 1);

				rc = zond_treeview_selection_loeschen_foreach(
						tree_view, &iter_child, data, error);
				if (rc)
					ERROR_Z
			}
		}
		else {
			rc = zond_treeviewfm_section_visible(
					ZOND_TREEVIEWFM(zond->treeview[BAUM_FS]), file_part, section,
					FALSE, &visible, &iter_fm, NULL, NULL, error);
			g_free(file_part);
			g_free(section);
			if (rc == -1)
				ERROR_Z

			rc = zond_dbase_get_baum_auswertung_copy(
					zond->dbase_zond->zond_dbase_work, node_id,
					&baum_auswertung_copy, error);
			if (rc)
				ERROR_Z

			if (baum_auswertung_copy)
				return 0;

			//Prüfen, ob mitzulöschendes Kind von PDF_ABSCHNITT als BAUM_AUSWERTUNG_COPY angebunden ist
			if (type == ZOND_DBASE_TYPE_FILE_PART) {
				gint rc = 0;
				gboolean copied = FALSE;

				rc = zond_dbase_is_file_part_copied(
						zond->dbase_zond->zond_dbase_work, node_id, &copied,
						error);
				if (rc)
					ERROR_Z

				if (copied)
					return 0;
			}

			//Wenn node_id ein pdf_abschnitt ist, der über baum_inhalt_file angebunden ist,
			//dann soll letzterer gelöscht werden, also die Anbindung im Baum_inhalt
			rc = zond_dbase_get_baum_inhalt_file_from_file_part(
					zond->dbase_zond->zond_dbase_work, node_id, &baum_inhalt_file,
					error);
			if (rc)
				ERROR_Z

			if (baum_inhalt_file)
				node_id = baum_inhalt_file;

			//wenn Pdf-Abschnitt gelöscht wird - in ZondTreeviewFM umsetzen
			if (!baum_inhalt_file && visible)
				gtk_tree_store_remove(
						GTK_TREE_STORE(
								gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_FS]) )),
						&iter_fm);
			else if (!baum_inhalt_file) {
				GtkTreeIter iter_parent = { 0 };

				gtk_tree_model_iter_parent(
						gtk_tree_view_get_model(
								GTK_TREE_VIEW(zond->treeview[BAUM_INHALT])),
						&iter_parent, iter);

				//Einzelkind?
				if (gtk_tree_model_iter_n_children(
						gtk_tree_view_get_model(
								GTK_TREE_VIEW(zond->treeview[BAUM_INHALT])),
						&iter_parent) == 1) {
					gint rc = 0;
					gint parent_id = 0;
					gchar *file_part = NULL;
					gchar *section = NULL;
					gboolean visible = FALSE;
					GtkTreeIter iter_fm = { 0 };
					GtkTreeIter iter_child = { 0 };

					rc = zond_dbase_get_parent(zond->dbase_zond->zond_dbase_work,
							node_id, &parent_id, error);
					if (rc)
						ERROR_Z

					rc = zond_dbase_get_node(zond->dbase_zond->zond_dbase_work,
							parent_id, &type, NULL, &file_part, &section, NULL,
							NULL, NULL, error);
					if (rc)
						ERROR_Z

					rc = zond_treeviewfm_section_visible(
							ZOND_TREEVIEWFM(zond->treeview[BAUM_FS]), file_part,
							section,
							FALSE, &visible, &iter_fm, NULL, NULL, error);
					g_free(file_part);
					g_free(section);
					if (rc == -1)
						ERROR_Z

					if (!gtk_tree_model_iter_children(
							gtk_tree_view_get_model(
									GTK_TREE_VIEW(zond->treeview[BAUM_FS])),
							&iter_child, &iter_fm))
						LOG_WARN("%s\niter hat keine Kinder", __func__);
					else
						gtk_tree_store_remove(
								GTK_TREE_STORE(gtk_tree_view_get_model(
										GTK_TREE_VIEW(zond->treeview[BAUM_FS]) )),
										&iter_child);
				}
			}
		}
	}

	rc = zond_dbase_remove_node(zond->dbase_zond->zond_dbase_work, node_id,
			error);
	if (rc)
		ERROR_Z

	zond_tree_store_remove(iter);

	return 0;
}

static gint zond_treeview_selection_entfernen_anbindung_foreach(
		SondTreeview *stv, GtkTreeIter *iter, gpointer data, GError** error) {
	gint rc = 0;
	gint node_id = 0;
	gint type = 0;
	gint id_anchor = 0;
	gint baum_inhalt_file = 0;
	gint id_parent = 0;
	gint (*fp_relative)(ZondDBase*, gint, gint*,
			GError**) = zond_dbase_get_first_child;
	gint relative = 0;
	gchar *file_part = NULL;
	gchar *section = NULL;
	GtkTreeIter iter_fm = { 0 };
	gboolean visible = FALSE;
	gboolean children = FALSE;
	gboolean opened = FALSE;

	Projekt *zond = data;
	ZondTreeviewFM *ztvfm = ZOND_TREEVIEWFM(zond->treeview[BAUM_FS]);

	if (zond_tree_store_is_link(iter))
		return 0;

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter, 2,
			&node_id, -1);

	rc = zond_dbase_get_node(zond->dbase_zond->zond_dbase_work, node_id, &type,
			NULL, &file_part, &section, NULL, NULL, NULL, error);
	if (rc)
		ERROR_Z

	if (section) //wenn nicht, muß baum_inhalt_file != 0 sein, d.h. es wird im fm nix gelöscht
	{
		gint rc = 0;

		rc = zond_treeviewfm_section_visible(
				ZOND_TREEVIEWFM(zond->treeview[BAUM_FS]), file_part, section,
				FALSE, &visible, &iter_fm, &children, &opened, error);
		g_free(file_part);
		g_free(section);
		if (rc == -1)
			ERROR_Z

		//parent brauchen wir, um zu ermitteln, ob nach der Löschung parent noch Kinder hat
		rc = zond_dbase_get_parent(zond->dbase_zond->zond_dbase_work, node_id,
				&id_parent, error);
		if (rc)
			ERROR_Z
	} else
		g_free(file_part);

	if (type != ZOND_DBASE_TYPE_BAUM_STRUKT) {
		gint rc = 0;
		gint baum_auswertung_copy = 0;

		//Test, ob als Copy angebunden
		rc = zond_dbase_get_baum_auswertung_copy(
				zond->dbase_zond->zond_dbase_work, node_id,
				&baum_auswertung_copy, error);
		if (rc)
			ERROR_Z

		//dann nicht löschen
		if (baum_auswertung_copy)
			return 0;

		//Test, ob direkt angebunden
		rc = zond_dbase_get_baum_inhalt_file_from_file_part(
				zond->dbase_zond->zond_dbase_work, node_id, &baum_inhalt_file,
				error);
		if (rc)
			ERROR_Z
	}

	rc = zond_dbase_begin(zond->dbase_zond->zond_dbase_work, error);
	if (rc)
		ERROR_Z

	if (baum_inhalt_file)
		id_anchor = baum_inhalt_file;
	else
		id_anchor = node_id;

	do {
		gint rc = 0;

		rc = fp_relative(zond->dbase_zond->zond_dbase_work, node_id, &relative,
				error);
		if (rc)
			ERROR_ROLLBACK_Z(zond->dbase_zond->zond_dbase_work)

		if (relative == 0)
			break;

		if (baum_inhalt_file) {
			gint node_inserted = 0;

			node_inserted = zond_dbase_insert_node(
					zond->dbase_zond->zond_dbase_work, id_anchor, FALSE,
					ZOND_DBASE_TYPE_BAUM_INHALT_FILE, relative, NULL, NULL,
					NULL, NULL, NULL, error);
			if (node_inserted == -1)
				ERROR_ROLLBACK_Z(zond->dbase_zond->zond_dbase_work)

			//wenn baum_inhalt_file, dann werden die Kinder von node_id nicht verschoben
			//dann müssen ab dem zweiten Durchgang die jüngeren Geschwister durchgegangen werden
			fp_relative = zond_dbase_get_younger_sibling;
			//und jüngeres Geschwister des ersten Kindes
			node_id = relative;

			id_anchor = node_inserted;
		} else {
			//kind verschieben
			rc = zond_dbase_verschieben_knoten(
					zond->dbase_zond->zond_dbase_work, relative, id_anchor,
					FALSE, error);
			if (rc)
				ERROR_ROLLBACK_Z(zond->dbase_zond->zond_dbase_work)

			id_anchor = relative;
		}
	} while (1);

	rc = zond_dbase_remove_node(zond->dbase_zond->zond_dbase_work,
			(baum_inhalt_file) ? baum_inhalt_file : node_id, error);
	if (rc)
		ERROR_ROLLBACK_Z(zond->dbase_zond->zond_dbase_work)

	rc = zond_dbase_commit(zond->dbase_zond->zond_dbase_work, error);
	if (rc)
		ERROR_ROLLBACK_Z(zond->dbase_zond->zond_dbase_work)

	//Im Baum jedenfalls Punkt löschen und Kinder an die Stelle setzen
	zond_tree_store_kill_parent(iter);

	if (baum_inhalt_file)
		return 0; //dann wird in fm nix gelöscht

	if (id_parent == 0)
		return 0; //Datei-root, wird nicht gelöscht

	//In treeviewfm umsetzen
	if (visible) {
		if (!opened) //öffnen, damit Kinder sichtbar sind
			sond_treeview_expand_row(SOND_TREEVIEW(ztvfm), &iter_fm);

		zond_treeviewfm_kill_parent(ztvfm, &iter_fm);
	} else { //nicht visible - ggf. dummy löschen
		gint child = 0;
		gint rc = 0;

		rc = zond_dbase_get_first_child(zond->dbase_zond->zond_dbase_work,
				id_parent, &child, error);
		if (rc)
			ERROR_Z

		//Wenn letztes Kind von parent
		if (child == 0) {
			gint rc = 0;
			gboolean visible_parent = FALSE;
			GtkTreeIter iter_parent = { 0 };
			GtkTreeIter iter_child = { 0 };
			gint type = 0;
			gchar *file_part = NULL;
			gchar *section = NULL;

			rc = zond_dbase_get_node(zond->dbase_zond->zond_dbase_work,
					id_parent, &type, NULL, &file_part, &section, NULL, NULL,
					NULL, error);
			if (rc)
				ERROR_Z

			if (type != ZOND_DBASE_TYPE_FILE_PART) {
				if (error)
					*error = g_error_new(ZOND_ERROR, 0,
							"%s\nUngültiger Knotentyp", __func__);
				g_free(file_part);
				g_free(section);

				return -1;
			}

			//prüfen, ob parent sichtbar ist
			rc = zond_treeviewfm_section_visible(ztvfm, file_part, section,
					FALSE, &visible_parent, &iter_parent, NULL, NULL, error);
			g_free(file_part);
			g_free(section);
			if (rc == -1)
				ERROR_Z

			if (visible_parent) {
				if (!gtk_tree_model_iter_children(
						gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)),
						&iter_child, &iter_parent))
					LOG_WARN("%s\nKnoten hat keinen Abkömmling", __func__);
				else
					gtk_tree_store_remove(GTK_TREE_STORE(
							gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) )),
							&iter_child);
			}
		}
	}

	return 0;
}

static void zond_treeview_jump_to_iter(Projekt *zond, GtkTreeIter *iter) {
	Baum baum_target = KEIN_BAUM;

	baum_target = zond_tree_store_get_root(
			zond_tree_store_get_tree_store(iter));

	sond_treeview_expand_to_row(zond->treeview[baum_target], iter);
	sond_treeview_set_cursor(zond->treeview[baum_target], iter);

	return;
}

gint zond_treeview_jump_to_node_id(Projekt *zond, gint node_id) {
	GtkTreeIter *iter = NULL;

	iter = zond_treeview_abfragen_iter(ZOND_TREEVIEW(zond->treeview[BAUM_INHALT]), node_id);
	if (!iter)
		return 1;

	zond_treeview_jump_to_iter(zond, iter);
	gtk_tree_iter_free(iter);

	return 0;
}

static gint zond_treeview_jump_to_origin(ZondTreeview *ztv, GtkTreeIter *iter,
		GError **error) {
	gint node_id = 0;
	gint type = 0;
	gint link = 0;
	gint rc = 0;

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	gtk_tree_model_get(
			gtk_tree_view_get_model(
					GTK_TREE_VIEW(
							ztv_priv->zond->treeview[ztv_priv->zond->baum_active])),
			iter, 2, &node_id, -1);

	rc = zond_dbase_get_type_and_link(
			ztv_priv->zond->dbase_zond->zond_dbase_work, node_id, &type, &link,
			error);
	if (rc)
		ERROR_Z

	if (type == ZOND_DBASE_TYPE_BAUM_STRUKT)
		return 0;
	else if (type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY) {
		gint rc = 0;

		rc = zond_treeview_jump_to_node_id(ztv_priv->zond, link);
		if (rc == 1) {//nicht gefunden
			if (error) *error = g_error_new(ZOND_ERROR, 0,
					"%s\nlink-ID konnte in BAUM_INHALT nicht gefunden werden",
					__func__);

			return -1;
		}
	}
	else { //FILE_PART
		gint rc = 0;
		gchar *file_part = NULL;
		gchar *section = NULL;

		rc = zond_dbase_get_node(ztv_priv->zond->dbase_zond->zond_dbase_work,
				node_id, NULL, NULL, &file_part, &section, NULL, NULL, NULL,
				error);
		if (rc)
			ERROR_Z

		//wenn FS nicht angezeigt: erst einschalten, damit man was sieht
		if (!gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(ztv_priv->zond->fs_button)))
			gtk_toggle_button_set_active(
					GTK_TOGGLE_BUTTON(ztv_priv->zond->fs_button), TRUE);

		rc = zond_treeviewfm_set_cursor_on_section(
				ZOND_TREEVIEWFM(ztv_priv->zond->treeview[BAUM_FS]),
				file_part, section, error);
		g_free(file_part);
		g_free(section);
		if (rc)
			ERROR_Z
	}

	return 0;
}

static void zond_treeview_jump_to_link_target(Projekt *zond, GtkTreeIter *iter) {
	GtkTreeIter iter_target = { 0 };

	zond_tree_store_get_iter_target(iter, &iter_target);

	zond_treeview_jump_to_iter(zond, &iter_target);

	return;
}

gint zond_treeview_oeffnen_internal_viewer(Projekt *zond,
		DisplayedDocument* dd, PdfPos *pos_pdf, GError **error) {
	PdfPos pos_von = { 0 };

	//Neue Instanz oder bestehende?
	if (!(zond->state & GDK_SHIFT_MASK)) {
		//Testen, ob pv mit file_part schon geöffnet
		for (gint i = 0; i < zond->arr_pv->len; i++) {
			DisplayedDocument* dd_loop = dd;
			PdfViewer *pv = g_ptr_array_index(zond->arr_pv, i);

			DisplayedDocument* dd_int = pv->dd;

			do {
				if (dd_int->zpdfd_part != dd_loop->zpdfd_part)
					break;
				dd_int = dd_int->next;
				dd_loop = dd_loop->next;
			} while (dd_int);

			if (dd_int == NULL && dd_loop == NULL) {
				gtk_window_present(GTK_WINDOW(pv->vf));
				if (pos_pdf)
					pos_von = *pos_pdf;
				viewer_springen_zu_pos_pdf(pv, pos_von, 0);

				return 0;
			}
		}
	}

	if (pos_pdf)
		pos_von = *pos_pdf;

	PdfViewer *pv = viewer_init(zond);
	viewer_display_document(pv, dd, pos_von.seite, pos_von.index);

	return 0;
}

static gint get_anbindung_ges(Projekt *zond, gint node_id,
		Anbindung anbindung, Anbindung* anbindung_ges,
		GError **error) {
	if (zond->state & GDK_CONTROL_MASK) {
		if ((anbindung.bis.seite || anbindung.bis.index))
			*anbindung_ges = anbindung;
		else if (anbindung.von.seite || anbindung.von.index) { //Pdf_punkt
			//nächsthöheren Abschnitt ermitteln
			gint rc = 0;
			gint parent_id = 0;
			gchar *section_ges = NULL;

			rc = zond_dbase_get_parent(zond->dbase_zond->zond_dbase_work,
					node_id, &parent_id, error);
			if (rc)
				ERROR_Z

			rc = zond_dbase_get_node(zond->dbase_zond->zond_dbase_work,
					parent_id,
					NULL, NULL, NULL, &section_ges, NULL, NULL, NULL, error);
			if (rc)
				ERROR_Z

			if (section_ges) //nicht root
			{
				anbindung_parse_file_section(section_ges, anbindung_ges);
				g_free(section_ges);
			}
		}
	} else {
		//ermitteln, woran pdf_abschnitt angeknüpft ist
		//um Umfang des zu öffnenden PDF festzustellen
		gint rc = 0;
		gint file_part_id = 0;
		gchar *section_ges = NULL;

		rc = zond_dbase_find_baum_inhalt_file(zond->dbase_zond->zond_dbase_work,
				node_id, NULL, &file_part_id, NULL, error);
		if (rc)
			ERROR_Z

		rc = zond_dbase_get_node(zond->dbase_zond->zond_dbase_work,
				file_part_id, NULL, NULL, NULL, &section_ges, NULL, NULL, NULL,
				error);
		if (rc)
			ERROR_Z

		if (section_ges) {
			anbindung_parse_file_section(section_ges, anbindung_ges);
			g_free(section_ges);
		}
	}

	return 0;
}

static gint get_filepart_from_iter(ZondTreeview* ztv, GtkTreeIter* iter,
		SondFilePart** sfp, Anbindung* anbindung_node, gint* node_id, GError** error) {
	gchar *file_part = NULL;
	gchar* section = NULL;
	gint ret = 0;

	ret = zond_treeview_get_filepart_and_section(ztv,
			iter, &file_part, &section, error);
	if (ret == -1)
		ERROR_Z

	if (!file_part)
		return 0; //nichts zu öffnen

	if (section) {
		anbindung_parse_file_section(section, anbindung_node);
		g_free(section);
	}

	*sfp = sond_file_part_from_filepart(file_part, error);
	g_free(file_part);
	if (!(*sfp))
		ERROR_Z

	if (node_id)
		*node_id = ret;

	return 0;
}

static gint zond_treeview_open_auszug(Projekt* zond, GtkTreeIter* iter,
		DisplayedDocument** dd, PdfPos* pdf_pos, GError** error) {
	DisplayedDocument* dd_loop = NULL;
	GtkTreeIter iter_tmp = { 0 };

	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(zond->treeview[zond->baum_active]));

	dd_loop = *dd;
	iter_tmp = *iter;

	//jetzt im Baum nach oben gehen und etwaige dds prependen
	while (gtk_tree_model_iter_previous(model, &iter_tmp)) {
		gint rc = 0;
		DisplayedDocument* dd_tmp = NULL;
		SondFilePart* sfp = NULL;
		Anbindung anbindung_node = { 0 };
		Anbindung anbindung_ges = { 0 };
		PdfPos pdf_pos_tmp = { 0 };

		rc = get_filepart_from_iter(ZOND_TREEVIEW(zond->treeview[zond->baum_active]),
				&iter_tmp, &sfp, &anbindung_node, NULL, error);
		if (rc)
			ERROR_Z

		if (!sfp)
			continue;

		if (!SOND_IS_FILE_PART_PDF(sfp) || anbindung_is_pdf_punkt(anbindung_node)) {
			g_object_unref(sfp);

			continue;
		}
		anbindung_ges = anbindung_node;
		dd_tmp = document_new_displayed_document(SOND_FILE_PART_PDF(sfp),
				&anbindung_ges, &anbindung_node, TRUE, &pdf_pos_tmp, error);
		g_object_unref(sfp);
		if (!dd_tmp)
			ERROR_Z

		pdf_pos->seite += pdf_pos_tmp.seite + 1;

		dd_tmp->next = dd_loop;
		dd_loop = dd_tmp;
	}

	*dd = dd_loop;

	//dann runter
	iter_tmp = *iter;
	dd_loop = *dd;
	while (gtk_tree_model_iter_next(model, &iter_tmp)) {
		gint rc = 0;
		DisplayedDocument* dd_tmp = NULL;
		SondFilePart* sfp = NULL;
		Anbindung anbindung_node = { 0 };
		Anbindung anbindung_ges = { 0 };

		rc = get_filepart_from_iter(ZOND_TREEVIEW(zond->treeview[zond->baum_active]),
				&iter_tmp, &sfp, &anbindung_node, NULL, error);
		if (rc)
			ERROR_Z

		if (!sfp)
			continue;

		if (!SOND_IS_FILE_PART_PDF(sfp) || anbindung_is_pdf_punkt(anbindung_node)) {
			g_object_unref(sfp);

			continue;
		}
		anbindung_ges = anbindung_node;
		dd_tmp = document_new_displayed_document(SOND_FILE_PART_PDF(sfp),
				&anbindung_ges, NULL, TRUE, NULL, error);
		g_object_unref(sfp);

		if (!dd_tmp)
			ERROR_Z

		dd_loop->next = dd_tmp;
		dd_loop = dd_tmp;
	}

	return 0;
}

static gint zond_treeview_open_node(Projekt *zond, GtkTreeIter *iter,
		gboolean open_with, gboolean auszug, GError **error) {
	gint rc = 0;
	SondFilePart* sfp = NULL;
	gint node_id = 0;
	Anbindung anbindung_node = { 0 };
	PdfPos pdf_pos = { 0 };

	rc = get_filepart_from_iter(ZOND_TREEVIEW(zond->treeview[zond->baum_active]), iter,
			&sfp, &anbindung_node, &node_id, error);
	if (rc)
		ERROR_Z

	if (!sfp)
		return 0;

	//mit externem Programm oder mit renderer öffnen
	if (open_with || !SOND_IS_FILE_PART_PDF(sfp)) {//wenn kein pdf oder mit Programmauswahl zu öffnen:
		rc = sond_file_part_open(sfp, open_with, error);
		g_object_unref(sfp);
		if (rc)
			ERROR_Z
	}
	else if (!auszug) { //internen Viewer verwenden
		Anbindung anbindung_ges = { 0 };
		DisplayedDocument* dd = NULL;

		//Anbindung anpassen und Anfangsposition berechnen
		if (!anbindung_is_empty(&anbindung_node)) {
			rc = get_anbindung_ges(zond, node_id, anbindung_node,
					&anbindung_ges, error);
			if (rc) {
				g_object_unref(sfp);

				ERROR_Z
			}
		}

		dd = document_new_displayed_document(SOND_FILE_PART_PDF(sfp),
				&anbindung_ges, &anbindung_node, (zond->state & GDK_MOD1_MASK),
				&pdf_pos, error);
		g_object_unref(sfp);
		if (!dd)
			ERROR_Z

		rc = zond_treeview_oeffnen_internal_viewer(zond, dd,
				&pdf_pos, error);
		if (rc) {
			document_free_displayed_documents(dd);

			ERROR_Z
		}
	}
	else { //Auszug!
		Anbindung anbindung_ges = { 0 };
		DisplayedDocument* dd = NULL;
		PdfPos pdf_pos = { 0 };
		gint rc = 0;

		if (anbindung_is_pdf_punkt(anbindung_node)) {
			g_object_unref(sfp);

			return 0;
		}

		anbindung_ges = anbindung_node;

		dd = document_new_displayed_document(SOND_FILE_PART_PDF(sfp),
				&anbindung_ges, &anbindung_node, (zond->state & GDK_MOD1_MASK),
				&pdf_pos, error);
		g_object_unref(sfp);
		if (!dd)
			ERROR_Z

		rc = zond_treeview_open_auszug(zond, iter, &dd, &pdf_pos, error);
		if (rc) {
			document_free_displayed_documents(dd);

			ERROR_Z
		}

		rc = zond_treeview_oeffnen_internal_viewer(zond, dd,
				&pdf_pos, error);
		if (rc) {
			document_free_displayed_documents(dd);

			ERROR_Z
		}
	}

	return 0;
}

static void zond_treeview_row_activated(GtkTreeView *tree_view,
		GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data) {
	GActionGroup *ag = gtk_widget_get_action_group(
			GTK_WIDGET(tree_view), "stv");
	if (ag && g_action_group_has_action(ag, "oeffnen"))
		g_action_group_activate_action(ag, "oeffnen", NULL);
}

typedef struct _SSelectionChangeIcon {
	Projekt *zond;
	const gchar *icon_name;
} SSelectionChangeIcon;

static gint zond_treeview_selection_change_icon_foreach(SondTreeview *tree_view,
		GtkTreeIter *iter, gpointer data, GError **error) {
	gint rc = 0;
	gint node_id = 0;
	SSelectionChangeIcon *s_selection = NULL;

	s_selection = data;

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view)), iter,
			2, &node_id, -1);

	rc = zond_dbase_update_icon_name(
			s_selection->zond->dbase_zond->zond_dbase_work, node_id,
			s_selection->icon_name, error);
	if (rc)
		ERROR_Z

	//neuen icon_name im tree speichern
	zond_tree_store_set(iter, s_selection->icon_name, NULL, 0);

	return 0;
}

/* --------------------------------------------------------------------------
 * GSimpleAction-Wrapper fuer ZondTreeview-Kontextmenu
 * -------------------------------------------------------------------------- */
static void zond_treeview_action_einf_ge(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	gint rc = 0; GError *error = NULL;
	rc = zond_treeview_insert_node(zond, FALSE, &error);
	if (rc == -1) { display_message(zond->app_window, "Punkt einf\u00fcgen fehlgeschlagen\n\n", error->message, NULL); g_error_free(error); }
	else if (rc == 1) display_message(zond->app_window, "Punkt darf nicht als Unterpunkt von Datei eingef\u00fcgt werden", NULL);
	else if (rc == 2) display_message(zond->app_window, "Punkt darf nicht als Unterpunkt von Link eingef\u00fcgt werden", NULL);
}
static void zond_treeview_action_einf_up(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	gint rc = 0; GError *error = NULL;
	rc = zond_treeview_insert_node(zond, TRUE, &error);
	if (rc == -1) { display_message(zond->app_window, "Punkt einf\u00fcgen fehlgeschlagen\n\n", error->message, NULL); g_error_free(error); }
	else if (rc == 1) display_message(zond->app_window, "Punkt darf nicht als Unterpunkt von Datei eingef\u00fcgt werden", NULL);
	else if (rc == 2) display_message(zond->app_window, "Punkt darf nicht als Unterpunkt von Link eingef\u00fcgt werden", NULL);
}
static void zond_treeview_action_paste_ge(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d; gint rc = 0; GError *error = NULL;
	rc = zond_treeview_paste_clipboard(zond, FALSE, FALSE, &error);
	if (rc == -1) { display_message(zond->app_window, "Fehler Einf\u00fcgen Clipboard\n\n", error->message, NULL); g_error_free(error); }
	else if (rc == 1) display_message(zond->app_window, "Einf\u00fcgen als Unterpunkt einer Datei nicht zul\u00e4ssig", NULL);
}
static void zond_treeview_action_paste_up(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d; gint rc = 0; GError *error = NULL;
	rc = zond_treeview_paste_clipboard(zond, TRUE, FALSE, &error);
	if (rc == -1) { display_message(zond->app_window, "Fehler Einf\u00fcgen Clipboard\n\n", error->message, NULL); g_error_free(error); }
	else if (rc == 1) display_message(zond->app_window, "Einf\u00fcgen als Unterpunkt einer Datei nicht zul\u00e4ssig", NULL);
}
static void zond_treeview_action_paste_link_ge(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d; gint rc = 0; GError *error = NULL;
	rc = zond_treeview_paste_clipboard(zond, FALSE, TRUE, &error);
	if (rc == -1) { display_message(zond->app_window, "Fehler Einf\u00fcgen Clipboard\n\n", error->message, NULL); g_error_free(error); }
	else if (rc == 1) display_message(zond->app_window, "Einf\u00fcgen als Unterpunkt einer Datei nicht zul\u00e4ssig", NULL);
}
static void zond_treeview_action_paste_link_up(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d; gint rc = 0; GError *error = NULL;
	rc = zond_treeview_paste_clipboard(zond, TRUE, TRUE, &error);
	if (rc == -1) { display_message(zond->app_window, "Fehler Einf\u00fcgen Clipboard\n\n", error->message, NULL); g_error_free(error); }
	else if (rc == 1) display_message(zond->app_window, "Einf\u00fcgen als Unterpunkt einer Datei nicht zul\u00e4ssig", NULL);
}
static void zond_treeview_action_loeschen(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d; gint rc = 0; GError *error = NULL;
	rc = sond_treeview_selection_foreach(zond->treeview[zond->baum_active], zond_treeview_selection_loeschen_foreach, zond, &error);
	if (rc == -1) { display_message(zond->app_window, "L\u00f6schen fehlgeschlagen\n\n", error->message, NULL); g_error_free(error); }
}
static void zond_treeview_action_anb_entf(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d; gint rc = 0; GError *error = NULL;
	if (zond->baum_active != BAUM_INHALT) return;
	rc = sond_treeview_selection_foreach(zond->treeview[BAUM_INHALT], zond_treeview_selection_entfernen_anbindung_foreach, zond, &error);
	if (rc) { display_message(zond->app_window, "L\u00f6schen von Anbindungen fehlgeschlagen\n\n", error->message, NULL); g_error_free(error); }
}
static void zond_treeview_action_jump(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	GtkTreeIter iter = { 0 };
	if (!sond_treeview_get_cursor(zond->treeview[zond->baum_active], &iter)) return;
	if (zond_tree_store_is_link(&iter)) zond_treeview_jump_to_link_target(zond, &iter);
	else {
		gint rc = 0; GError *error = NULL;
		rc = zond_treeview_jump_to_origin(ZOND_TREEVIEW(zond->treeview[zond->baum_active]), &iter, &error);
		if (rc) { display_message(zond->app_window, "Fehler Sprung zu Herkunft\n\n", error->message, NULL); g_error_free(error); }
	}
}
static void zond_treeview_action_oeffnen(GSimpleAction *a,
		GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	GtkTreeIter iter = { 0 };
	GError *error = NULL;
	gint rc = 0;

	if (!sond_treeview_get_cursor(zond->treeview[zond->baum_active], &iter))
		return;

	rc = zond_treeview_open_node(zond, &iter, FALSE, FALSE, &error);
	if (rc) {
		display_message(zond->app_window, "Fehler beim \u00d6ffnen Knoten:\n\n",
				error->message, NULL);
		g_error_free(error);
	}

	return;
}

static void zond_treeview_action_oeffnen_mit(GSimpleAction *a,
		GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	GtkTreeIter iter = { 0 };
	GError *error = NULL;
	gint rc = 0;

	if (!sond_treeview_get_cursor(zond->treeview[zond->baum_active], &iter))
		return;

	rc = zond_treeview_open_node(zond, &iter, TRUE, FALSE, &error);
	if (rc) {
		display_message(zond->app_window, "Fehler beim \u00d6ffnen Knoten:\n\n",
				error->message, NULL);
		g_error_free(error);
	}

	return;
}

static void zond_treeview_action_auszug(GSimpleAction *a, GVariant *p, gpointer d) {
	GtkTreeIter iter = { 0 };
	GError *error = NULL;
	gint rc = 0;

	Projekt *zond = (Projekt*) d;

	if (!sond_treeview_get_cursor(zond->treeview[zond->baum_active], &iter))
		return;

	rc = zond_treeview_open_node(zond, &iter, FALSE, TRUE, &error);
	if (rc) {
		display_message(zond->app_window, "Fehler beim \u00d6ffnen des Auszugs\n\n",
				error->message, NULL);
		g_error_free(error);
	}

	return;
}

static void zond_treeview_action_icon(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d; gint rc = 0; GError *error = NULL;
	gint icon_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(a), "icon-id"));
	SSelectionChangeIcon s_selection = { zond, zond->icon[icon_id].icon_name };
	rc = sond_treeview_selection_foreach(zond->treeview[zond->baum_active], zond_treeview_selection_change_icon_foreach, (gpointer) &s_selection, &error);
	if (rc == -1) { display_message(zond->app_window, "Icon \u00e4ndern fehlgeschlagen\n\n", error->message, NULL); g_error_free(error); }
}

static void zond_treeview_init_contextmenu(ZondTreeview *ztv) {
	/* Instanzspezifische Aktionen registrieren.
	 * GMenu-Sections wurden bereits in class_init aufgebaut. */
	GSimpleActionGroup *ag = sond_treeview_get_action_group(SOND_TREEVIEW(ztv));
	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);
	Projekt *zond = ztv_priv->zond;

	struct { const gchar *name; GCallback cb; } acts[] = {
		{ "einf-ge",       G_CALLBACK(zond_treeview_action_einf_ge)      },
		{ "einf-up",       G_CALLBACK(zond_treeview_action_einf_up)      },
		{ "paste-ge",      G_CALLBACK(zond_treeview_action_paste_ge)     },
		{ "paste-up",      G_CALLBACK(zond_treeview_action_paste_up)     },
		{ "paste-link-ge", G_CALLBACK(zond_treeview_action_paste_link_ge)},
		{ "paste-link-up", G_CALLBACK(zond_treeview_action_paste_link_up)},
		{ "loeschen",      G_CALLBACK(zond_treeview_action_loeschen)     },
		{ "anb-entf",      G_CALLBACK(zond_treeview_action_anb_entf)     },
		{ "jump",          G_CALLBACK(zond_treeview_action_jump)         },
		{ "oeffnen",       G_CALLBACK(zond_treeview_action_oeffnen)      },
		{ "oeffnen-mit",   G_CALLBACK(zond_treeview_action_oeffnen_mit)  },
		{ "auszug",        G_CALLBACK(zond_treeview_action_auszug)       },
	};
	for (guint i = 0; i < G_N_ELEMENTS(acts); i++) {
		GSimpleAction *act = g_simple_action_new(acts[i].name, NULL);
		g_signal_connect(act, "activate", acts[i].cb, zond);
		g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act));
		g_object_unref(act);
	}

	/* Icon-Aktionen + Submenu befuellen (nur erste Instanz befuellt das Submenu) */
	GMenu *gmenu_icons = ZOND_TREEVIEW_GET_CLASS(ztv)->gmenu_icons;
	gboolean filled = (g_menu_model_get_n_items(G_MENU_MODEL(gmenu_icons)) > 0);
	for (gint i = 0; i < NUMBER_OF_ICONS; i++) {
		gchar *name = g_strdup_printf("icon-%d", i);
		GSimpleAction *act = g_simple_action_new(name, NULL);
		g_object_set_data(G_OBJECT(act), "icon-id", GINT_TO_POINTER(i));
		g_signal_connect(act, "activate", G_CALLBACK(zond_treeview_action_icon), zond);
		g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act));
		g_object_unref(act);
		if (!filled) {
			gchar *action = g_strdup_printf("stv.icon-%d", i);
			GMenuItem *item = g_menu_item_new(zond->icon[i].display_name, action);
			GIcon *icon = g_themed_icon_new(zond->icon[i].icon_name);
			g_menu_item_set_icon(item, icon);
			g_object_unref(icon);
			g_menu_append_item(gmenu_icons, item);
			g_object_unref(item);
			g_free(action);
		}
		g_free(name);
	}

	return;
}

ZondTreeview*
zond_treeview_new(Projekt *zond, gint root_node_id) {
	ZondTreeview *ztv = NULL;
	ZondTreeviewPrivate *ztv_priv = NULL;

	ztv = g_object_new(ZOND_TYPE_TREEVIEW, NULL);

	ztv_priv = zond_treeview_get_instance_private(ztv);
	ztv_priv->zond = zond;
	sond_treeview_set_id(SOND_TREEVIEW(ztv), root_node_id);
	zond_tree_store_set_root(
			ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) )),
			root_node_id);

	zond_treeview_init_contextmenu(ztv);

	/* Doppelklick = angebundene Datei öffnen */
	g_signal_connect(ztv, "row-activated",
			G_CALLBACK(zond_treeview_row_activated), zond);

	/* Zeile expandiert: Link nachladen + Spaltenbreite anpassen */
	g_signal_connect(ztv, "row-expanded",
			G_CALLBACK(zond_treeview_row_expanded), zond);

	return ztv;
}

static gint zond_treeview_load_node(ZondTreeview *ztv, gint node_id,
		GtkTreeIter *iter_anchor, gboolean child, GtkTreeIter *iter_inserted,
		gint anchor_id, gint *node_id_inserted, GError **error) {
	gint type = 0;
	gint link = 0;
	gint rc = 0;
	GtkTreeIter iter_new = { 0 };

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	rc = zond_dbase_get_type_and_link(
			ztv_priv->zond->dbase_zond->zond_dbase_work, node_id, &type, &link,
			error);
	if (rc)
		ERROR_Z

	if (type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE) {
		gint rc = 0;

		rc = zond_treeview_walk_tree(ztv, FALSE, link, iter_anchor, child,
				&iter_new, node_id, NULL, zond_treeview_insert_file_parts,
				error);
		if (rc)
			ERROR_Z
	}
	else //eigentlich nur link, copy oder strukt...
	{
		gchar *icon_name = NULL;
		gchar *node_text = NULL;

		zond_tree_store_insert(
				ZOND_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(ztv))),
				iter_anchor, child, &iter_new);

		if (type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK) {
			icon_name = g_strdup_printf("%d", node_id); //head_nr wird hier gespeichert
			node_id = link * -1;
		}
		else {
			gint rc = 0;

			rc = zond_dbase_get_node(
					ztv_priv->zond->dbase_zond->zond_dbase_work, node_id, NULL,
					NULL, NULL, NULL, &icon_name, &node_text, NULL, error);
			if (rc)
				ERROR_Z
		}

		zond_tree_store_set(&iter_new, icon_name, node_text, node_id);

		g_free(icon_name);
		g_free(node_text);
	}

	if (iter_inserted)
		*iter_inserted = iter_new;

	return 0;
}

static gint zond_treeview_insert_links_foreach(ZondTreeview *ztv,
		GtkTreeIter *iter, GError **error) {
	gchar *icon_name = NULL;
	gint node_id = 0;
	gint head_nr = 0;

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	if (iter) {
		gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(ztv)), iter, 0,
				&icon_name, 2, &node_id, -1);

		head_nr = atoi(icon_name);
		g_free(icon_name);
	}

	if (node_id >= 0) {
		GtkTreeIter iter_child = { 0 };

		if (gtk_tree_model_iter_children(
				gtk_tree_view_get_model(GTK_TREE_VIEW(ztv)), &iter_child,
				iter)) {
			gint rc = 0;

			rc = zond_treeview_insert_links_foreach(ztv, &iter_child, error);
			if (rc)
				ERROR_Z
		}
	} else {
		GtkTreeIter iter_anchor = { 0 };
		gboolean child = FALSE;
		GtkTreeIter *iter_target = NULL;
		gint root = 0;
		gint rc = 0;

		node_id *= -1;

		rc = zond_treeview_get_root(ztv, node_id, &root, error);
		if (rc)
			ERROR_Z

				//iter_anchor basteln
		iter_anchor = *iter; //Abfrage, ob iter != NULL überflüssig, da dann node_id niemals negativ ist
		if (((GNode*) (iter->user_data))->prev == NULL) {
			iter_anchor.user_data = ((GNode*) (iter->user_data))->parent;
			child = TRUE;
		} else {
			iter_anchor.user_data = ((GNode*) (iter->user_data))->prev;
			child = FALSE;
		}
		zond_tree_store_remove(iter);

		//iter_target ermitteln
		iter_target = zond_treeview_abfragen_iter(
				ZOND_TREEVIEW(ztv_priv->zond->treeview[root]), node_id);
		if (!iter_target) {
			if (error)
				*error = g_error_new( ZOND_ERROR, 0, "%s\nKein Iter ermittelt",
						__func__);

			return -1;
		}

		zond_tree_store_insert_link(iter_target, head_nr,
				zond_tree_store_get_tree_store(&iter_anchor),
				((GNode*) (iter_anchor.user_data)
						== zond_tree_store_get_root_node(
								zond_tree_store_get_tree_store(&iter_anchor))) ?
						NULL : &iter_anchor, child, iter); //*iter existiert, ist aber bis hierhin nutzlos

		gtk_tree_iter_free(iter_target);
	}

	if (iter && gtk_tree_model_iter_next(
			gtk_tree_view_get_model(GTK_TREE_VIEW(ztv)), iter)) {
		gint rc = 0;

		rc = zond_treeview_insert_links_foreach(ztv, iter, error);
		if (rc)
			ERROR_Z
	}

	return 0;
}

gint zond_treeview_load_baum(ZondTreeview *ztv, GError **error) {
	gint first_child = 0;
	gint rc = 0;
	gint root = 0;

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	zond_tree_store_clear(
			ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) )));

	root = zond_tree_store_get_root(
			ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) )));
	rc = zond_dbase_get_first_child(ztv_priv->zond->dbase_zond->zond_dbase_work,
			root, &first_child, error);
	if (rc)
		ERROR_Z
	else if (first_child == 0)
		return 0; //Baum leer

	rc = zond_treeview_walk_tree(ztv, TRUE, first_child, NULL, TRUE, NULL, root,
			NULL, zond_treeview_load_node, error);
	if (rc)
		ERROR_Z

	rc = zond_treeview_insert_links_foreach(ztv, NULL, error);
	if (rc)
		ERROR_Z

	return 0;
}

static gboolean zond_treeview_foreach_path(GtkTreeModel *model,
		GtkTreePath *path, GtkTreeIter *iter, gpointer user_data) {
	GtkTreePath **new_path = (GtkTreePath**) user_data;
	gint node_id = GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(model), "node_id" ));

	gint node_id_tree = 0;
	gtk_tree_model_get(model, iter, 2, &node_id_tree, -1);

	if (node_id == node_id_tree) {
		*new_path = gtk_tree_path_copy(path);
		return TRUE;
	} else
		return FALSE;
}

GtkTreePath*
zond_treeview_get_path(SondTreeview *treeview, gint node_id) {
	GtkTreePath *path = NULL;
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));

	g_object_set_data(G_OBJECT(model), "node_id", GINT_TO_POINTER(node_id));
	gtk_tree_model_foreach(model,
			(GtkTreeModelForeachFunc) zond_treeview_foreach_path, &path);

	return path;
}

static gint zond_treeview_get_filepart_and_section(ZondTreeview *ztv, GtkTreeIter *iter,
		gchar **file_part, gchar** section, GError **error) {
	gint node_id = 0;
	gint type = 0;
	gint link = 0;
	gchar* file_part_local = NULL;
	gchar* section_local = NULL;
	gint rc = 0;

	ZondTreeviewPrivate *ztv_priv = zond_treeview_get_instance_private(ztv);

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(ztv)), iter, 2,
			&node_id, -1);

	rc = zond_dbase_get_node(ztv_priv->zond->dbase_zond->zond_dbase_work, node_id,
			&type, &link, &file_part_local, &section_local, NULL, NULL, NULL, error);
	if (rc)
		ERROR_Z

	if (type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY) {
		gint rc = 0;

		node_id = link;
		rc = zond_dbase_get_node(ztv_priv->zond->dbase_zond->zond_dbase_work,
				node_id, NULL, NULL, &file_part_local, &section_local, NULL, NULL, NULL,
				error);
		if (rc)
			ERROR_Z
	}

	if (file_part)
		*file_part = file_part_local;
	else
		g_free(file_part_local);

	if (section)
		*section = section_local;
	else
		g_free(section_local);

	return node_id;
}

static gint zond_treeview_get_selected_fileparts_foreach(ZondTreeview *ztv,
		GtkTreeIter *iter, gpointer data, GError **error) {
	gint node_id = 0;
	gchar *file_part = NULL;
	SondFilePart *sfp = NULL;

	GHashTable *ht_fileparts = (GHashTable*) data;

	node_id = zond_treeview_get_filepart_and_section(ztv, iter, &file_part, NULL, error);
	if (node_id == -1)
		ERROR_Z

	sfp = sond_file_part_from_filepart(file_part, error);
	if (!sfp) {
		g_free(file_part);
		ERROR_Z
	}

	g_hash_table_add(ht_fileparts, sfp);

	return 0;
}

GHashTable* zond_treeview_get_selected_fileparts(ZondTreeview *ztv,
		GError **error) {
	GHashTable *ht_fileparts = NULL;
	gint rc = 0;

	ht_fileparts = g_hash_table_new_full(NULL, NULL, g_object_unref, NULL);

	rc = sond_treeview_selection_foreach(SOND_TREEVIEW(ztv),
			(gint(*)(SondTreeview*, GtkTreeIter*, gpointer, GError**))
			zond_treeview_get_selected_fileparts_foreach, ht_fileparts, error);

	if (rc) {
		g_hash_table_destroy(ht_fileparts);
		ERROR_Z_VAL(NULL)
	}

	return ht_fileparts;
}
