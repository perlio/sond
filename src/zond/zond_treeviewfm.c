#include "zond_treeviewfm.h"

#include "../misc.h"
#include "../sond_fileparts.c"

#include "sond_treeviewfm.h"

#include "zond_dbase.h"
#include "zond_treeview.h"

#include "10init/app_window.h"
#include "20allgemein/project.h"
#include "20allgemein/oeffnen.h"
#include "99conv/general.h"

#include "global_types.h"

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32

typedef enum {
	PROP_PROJEKT = 1, N_PROPERTIES
} ZondTreeviewFMProperty;

typedef struct {
	Projekt *zond;
} ZondTreeviewFMPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondTreeviewFM, zond_treeviewfm,
		SOND_TYPE_TREEVIEWFM)

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void zond_treeviewfm_set_property(GObject *object, guint property_id,
		const GValue *value, GParamSpec *pspec) {
	ZondTreeviewFM *self = ZOND_TREEVIEWFM(object);
	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(self);

	switch ((ZondTreeviewFMProperty) property_id) {
	case PROP_PROJEKT:
		priv->zond = g_value_get_pointer(value);
		break;

	default:
		/* We don't have any other property... */
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void zond_treeviewfm_get_property(GObject *object, guint property_id,
		GValue *value, GParamSpec *pspec) {
	ZondTreeviewFM *self = ZOND_TREEVIEWFM(object);
	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(self);

	switch ((ZondTreeviewFMProperty) property_id) {
	case PROP_PROJEKT:
		g_value_set_pointer(value, priv->zond);
		break;

	default:
		/* We don't have any other property... */
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static gint zond_treeviewfm_dbase_begin(SondTreeviewFM *stvfm, GError **error) {
	gint rc = 0;

	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));
	ZondDBase *dbase_work = sond_treeviewfm_get_dbase(stvfm);
	ZondDBase *dbase_store = priv->zond->dbase_zond->zond_dbase_store;

	rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->dbase_begin(
			SOND_TREEVIEWFM(stvfm), error);
	if (rc)
		ERROR_Z

	rc = zond_dbase_begin(dbase_store, error);
	if (rc)
		ERROR_ROLLBACK_Z(dbase_work);

	return 0;
}

static gint zond_treeviewfm_dbase_test(SondTreeviewFM *stvfm,
		const gchar *rel_path_source, GError **error) {
	gint rc = 0;

	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));
	ZondDBase *dbase_work = sond_treeviewfm_get_dbase(stvfm);

	rc = zond_dbase_test_path(dbase_work, rel_path_source, error);
	if (rc == -1)
		ERROR_Z
	else if (rc == 1)
		return 1;

	return 0;
}

static gint zond_treeviewfm_dbase_update_path(SondTreeviewFM *stvfm,
		const gchar *rel_path_source, const gchar *rel_path_dest,
		GError **error) {
	gint rc = 0;

	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	ZondDBase *dbase_work = sond_treeviewfm_get_dbase(stvfm);
	ZondDBase *dbase_store = priv->zond->dbase_zond->zond_dbase_store;

	rc = zond_dbase_update_path(dbase_store, rel_path_source, rel_path_dest,
			error);
	if (rc)
		ERROR_ROLLBACK_Z(dbase_store)

	rc = zond_dbase_update_path(dbase_work, rel_path_source, rel_path_dest,
			error);
	if (rc)
		ERROR_ROLLBACK_BOTH(dbase_work,
				priv->zond->dbase_zond->zond_dbase_store)

	return 0;
}

static gint zond_treeviewfm_dbase_end(SondTreeviewFM *stvfm, gboolean suc,
		GError **error) {
	gint rc = 0;

	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	ZondDBase *dbase_work = sond_treeviewfm_get_dbase(stvfm);
	ZondDBase *dbase_store = priv->zond->dbase_zond->zond_dbase_store;

	rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->dbase_end(stvfm,
			suc, error);
	if (rc)
		ERROR_ROLLBACK_BOTH(dbase_work, dbase_store)

	if (suc) {
		gint rc = 0;

		rc = zond_dbase_commit(priv->zond->dbase_zond->zond_dbase_store, error);
		if (rc)
			ERROR_ROLLBACK_BOTH(dbase_work, dbase_store)
	} else {
		gint rc = 0;

		rc = zond_dbase_rollback(dbase_store, error);
		if (rc)
			ERROR_Z
	}

	return 0;
}

static gint zond_treeviewfm_text_edited(SondTreeviewFM *stvfm,
		GtkTreeIter *iter, GObject *object, const gchar *new_text,
		GError **error) {
	gboolean changed = FALSE;
/*
	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	if (ztvfm_priv->zond->dbase_zond->changed)
		changed = TRUE;

	if (ZOND_IS_PDF_ABSCHNITT(object)) {
		gint rc = 0;
		gint ID_pdf_abschnitt = 0;

		ID_pdf_abschnitt = zond_pdf_abschnitt_get_ID(
				ZOND_PDF_ABSCHNITT(object));
		rc = zond_dbase_update_node_text(
				ztvfm_priv->zond->dbase_zond->zond_dbase_work, ID_pdf_abschnitt,
				new_text, error);
		if (rc)
			ERROR_Z

		//zond_treeview ändern
		zond_treeview_set_text_pdf_abschnitt(
				ZOND_TREEVIEW(ztvfm_priv->zond->treeview[BAUM_INHALT]),
				ID_pdf_abschnitt, new_text);
	}
	//chain-up, wenn nicht erledigt
	else
		SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->text_edited(stvfm,
				iter, object, new_text, error);

	if (!changed)
		project_reset_changed(ztvfm_priv->zond, FALSE);
*/
	return 0;
}

static void zond_treeviewfm_results_row_activated(GtkWidget *listbox,
		GtkWidget *row, gpointer data) {
	ZondTreeviewFM *ztvfm = (ZondTreeviewFM*) data;
	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ztvfm);

	//wenn FS nicht angezeigt: erst einschalten, damit man was sieht
	if (!gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(ztvfm_priv->zond->fs_button)))
		gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(ztvfm_priv->zond->fs_button), TRUE);

	//chain-up
	SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->results_row_activated(
			listbox, row, data);

	return;
}

gint zond_treeviewfm_insert_section(ZondTreeviewFM *ztvfm, gint node_id,
		GtkTreeIter *iter_anchor, gboolean child, GtkTreeIter *iter_inserted,
		GError **error) {
	gint rc = 0;
	gchar *file_part = NULL;
	gchar *section = NULL;
	Anbindung anbindung = { 0 };
	gchar *icon_name = NULL;
	gchar *node_text = NULL;
	GtkTreeIter iter_new = { 0 };
	gint first_grandchild = 0;

	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ztvfm);

	rc = zond_dbase_get_node(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
			node_id, NULL, NULL, &file_part, &section, &icon_name, &node_text,
			NULL, error);
	if (rc)
		ERROR_Z

	if (!section) {
		if (error)
			*error = g_error_new( ZOND_ERROR, 0,
					"%s\nKnoten enthält keine section", __func__);
		g_free(file_part);

		return -1;
	}

	anbindung_parse_file_section(section, &anbindung);
	g_free(section);
/*
	zpa = g_object_new( ZOND_TYPE_PDF_ABSCHNITT, NULL);

	zond_pdf_abschnitt_set(zpa, node_id, file_part, anbindung, icon_name,
			node_text);
	g_free(file_part);
	g_free(icon_name);
	g_free(node_text);

	//child in tree einfügen
	gtk_tree_store_insert_after(
			GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) )),
			&iter_new, (child) ? iter_anchor : NULL,
			(child) ? NULL : iter_anchor);
	gtk_tree_store_set(
			GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) )),
			&iter_new, 0, zpa, -1);

	g_object_unref(zpa);
*/
	//insert dummy
	zond_dbase_get_first_child(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
			node_id, &first_grandchild, error);
	if (rc)
		ERROR_Z

	if (first_grandchild) {
		GtkTreeIter iter_tmp = { 0 };

		gtk_tree_store_insert(
				GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) )),
				&iter_tmp, &iter_new, -1);
	}

	if (iter_inserted)
		*iter_inserted = iter_new;

	return 0;
}

static gint zond_treeviewfm_open_sfp(SondTreeviewFM* stvfm, SondFilePart *sfp, gboolean open_with,
		GError **error) {

	if (!open_with && SOND_IS_FILE_PART_PDF(sfp)) {
		PdfPos pos_pdf = { 0 };
		gchar const* section = NULL;
		gint rc = 0;
		ZondTreeviewFMPrivate* ztvfm_priv =
				zond_treeviewfm_get_instance_private(ZOND_TREEVIEWFM(stvfm));

		section = sond_file_part_get_path(sfp);
		if (section) {
			Anbindung anbindung = { 0 };

			anbindung_parse_file_section(section,  &anbindung);
			pos_pdf.seite = anbindung.von.seite;
			pos_pdf.index = anbindung.von.index;
		}

		rc = zond_treeview_oeffnen_internal_viewer(ztvfm_priv->zond,
				SOND_FILE_PART_PDF(sfp), NULL, &pos_pdf, error);
		if (rc)
			ERROR_Z
	}
	else { //chain-up, falls nicht bearbeitet
		gint rc = 0;

		rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->
				open_sfp(stvfm, sfp, open_with, error);
		if (rc)
			ERROR_Z
	}

	return 0;
}

static void zond_treeviewfm_class_init(ZondTreeviewFMClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->set_property = zond_treeviewfm_set_property;
	object_class->get_property = zond_treeviewfm_get_property;

	obj_properties[PROP_PROJEKT] = g_param_spec_pointer("Projekt", "Projekt",
			"Kontext-Struktur.", G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

	g_object_class_install_properties(object_class, N_PROPERTIES,
			obj_properties);

	SOND_TREEVIEWFM_CLASS(klass)->dbase_begin = zond_treeviewfm_dbase_begin;
	SOND_TREEVIEWFM_CLASS(klass)->dbase_test = zond_treeviewfm_dbase_test;
	SOND_TREEVIEWFM_CLASS(klass)->dbase_update_path =
			zond_treeviewfm_dbase_update_path;
	SOND_TREEVIEWFM_CLASS(klass)->dbase_end = zond_treeviewfm_dbase_end;
	SOND_TREEVIEWFM_CLASS(klass)->text_edited = zond_treeviewfm_text_edited;
	SOND_TREEVIEWFM_CLASS(klass)->results_row_activated =
			zond_treeviewfm_results_row_activated;
	SOND_TREEVIEWFM_CLASS(klass)->open_sfp = zond_treeviewfm_open_sfp;

	return;
}

static void zond_treeviewfm_init(ZondTreeviewFM *ztvfm) {
	return;
}

static gint zond_treeviewfm_file_part_visible(ZondTreeviewFM *ztvfm,
		gchar const *file_part, gboolean open, gboolean *visible,
		GtkTreeIter *iter, gboolean *children, gboolean *opened, GError **error) {
	gchar *rel_path = NULL;
	gint rc = 0;

	rel_path = get_rel_path_from_file_part(file_part);
	if (!rel_path) {
		if (error)
			*error = g_error_new( ZOND_ERROR, 0, "%s: file_part malformed",
					__func__);

		return -1;
	}

	rc = sond_treeviewfm_rel_path_visible(SOND_TREEVIEWFM(ztvfm), rel_path,
			open, visible, iter, error);
	g_free(rel_path);
	if (rc)
		ERROR_Z

	if (!(*visible))
		return 0;

	//ToDo: file_parts durchnudeln

	return 0;
}

gint zond_treeviewfm_section_visible(ZondTreeviewFM *ztvfm,
		gchar const *file_part, gchar const *section, gboolean open,
		gboolean *visible, GtkTreeIter *iter, gboolean *children,
		gboolean *opened, GError **error) {
	gint rc = 0;
	gboolean visible_intern = FALSE;
	GtkTreeIter iter_intern = { 0 };

	if (!open && !visible)
		return 0; //ergibt einfach keinen Sinn

	rc = zond_treeviewfm_file_part_visible(ztvfm, file_part, open,
			&visible_intern, &iter_intern, NULL, NULL, error);
	if (rc)
		ERROR_Z

	if (!visible_intern) {
		*visible = FALSE; //

		return 0;
	}
/*
	if (section) {
		Anbindung anbindung_prev = { { 0, 0 }, { 999999, 999999 } };
		Anbindung anbindung = { 0 };

		anbindung_parse_file_section(section, &anbindung);

		do {
			if (anbindung_1_eltern_von_2(anbindung_prev, anbindung)) {
				GObject *object = NULL;
				GtkTreeIter iter_child = { 0 };

				if (!gtk_tree_model_iter_children(
						gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)),
						&iter_child, &iter_intern)) {
					if (error)
						*error = g_error_new( ZOND_ERROR, 0,
								"%s\nKnoten hat keinen Abkömmling", __func__);

					return -1;
				}

				gtk_tree_model_get(
						gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)),
						&iter_child, 0, &object, -1);

				if (!object) //dummy
				{
					if (!open) //wenn's nicht geöffnet werden soll, sind wir hier fertig
					{
						*visible = FALSE; //visible muß != NULL sein, sonst schon oben Ende

						return 0;
					} else
						sond_treeview_expand_row(SOND_TREEVIEW(ztvfm),
								&iter_intern);
				} else //ist object
				{
					if (!ZOND_IS_PDF_ABSCHNITT(object)) {
						if (error)
							*error = g_error_new( ZOND_ERROR, 0,
									"%s\nobject ist kein PDF-Abschnitt",
									__func__);
						g_object_unref(object);

						return -1;
					}

					zond_pdf_abschnitt_get(ZOND_PDF_ABSCHNITT(object), NULL,
							NULL, &anbindung_prev, NULL, NULL);
					g_object_unref(object);

					iter_intern = iter_child;
				}
			} else if (anbindung_1_vor_2(anbindung_prev, anbindung)) {
				GtkTreeIter iter_sibling = { 0 };
				GObject *object = NULL;

				iter_sibling = iter_intern;

				if (!gtk_tree_model_iter_next(
						gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)),
						&iter_sibling)) {
					if (error)
						*error = g_error_new( ZOND_ERROR, 0,
								"%s\nKein jüngeres Geschwister", __func__);

					return -1;
				}

				gtk_tree_model_get(
						gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)),
						&iter_sibling, 0, &object, -1);

				if (!object) {
					if (error)
						*error =
								g_error_new( ZOND_ERROR, 0,
										"%sd\nZu jüngerem Geschwister kein object gespeichert",
										__func__);

					return -1;
				} else if (!ZOND_IS_PDF_ABSCHNITT(object)) {
					if (error)
						*error = g_error_new( ZOND_ERROR, 0,
								"%sd\nobject ist kein ZPA", __func__);
					g_object_unref(object);

					return -1;
				}

				zond_pdf_abschnitt_get(ZOND_PDF_ABSCHNITT(object), NULL, NULL,
						&anbindung_prev, NULL, NULL);
				g_object_unref(object);

				iter_intern = iter_sibling;
			} else if (anbindung_1_gleich_2(anbindung_prev, anbindung))
				break;
			else {
				if (error)
					*error = g_error_new( ZOND_ERROR, 0,
							"%s\nsection-tree ist korrupt", __func__);

				return -1;
			}
		} while (1);
	}
*/
	if (visible)
		*visible = TRUE;

	//children und opened abfragen
	//prüfen, ob Kinder, und wenn nur dummy
	if (children) {
		if (gtk_tree_model_iter_has_child(
				gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)), &iter_intern)) {
			GtkTreePath *path = NULL;
			gboolean expanded = FALSE;

			*children = TRUE;

			if (opened) {
				path = gtk_tree_model_get_path(
						gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)),
						&iter_intern);
				expanded = gtk_tree_view_row_expanded(GTK_TREE_VIEW(ztvfm),
						path);
				gtk_tree_path_free(path);

				if (expanded)
					*opened = TRUE;
				else
					*opened = FALSE;
			}
		} else
			*children = FALSE;
	}

	if (iter)
		*iter = iter_intern;

	return 0;
}

gint zond_treeviewfm_set_cursor_on_section(ZondTreeviewFM *ztvfm,
		gchar const *file_part, gchar const *section, GError **error) {
	gint rc = 0;
	GtkTreeIter iter = { 0 };

	rc = zond_treeviewfm_section_visible(ztvfm, file_part, section,
	TRUE, NULL, &iter, NULL, NULL, error);
	if (rc)
		ERROR_Z

	sond_treeview_set_cursor(SOND_TREEVIEW(ztvfm), &iter);

	return 0;
}

#define G_NODE(node) ((GNode *)node)
static void zond_treeviewfm_walk_tree(GtkTreeModel *model, gint stamp,
		GNode *node, gint pos) {
	GNode *child = NULL;
	GtkTreeIter iter = { 0 };
	GtkTreePath *path = NULL;
	gint pos_child = 0;

	iter.stamp = stamp;
	iter.user_data = node;
	path = gtk_tree_model_get_path(model, &iter);

	gtk_tree_model_row_inserted(model, path, &iter);

	if (node->parent->parent != NULL) {
		/* child_toggled */
		if (node->prev == NULL && node->next == NULL) //keineGeschwister
		{
			GtkTreeIter new_iter = { 0 };
			gtk_tree_path_up(path);
			new_iter.stamp = stamp;
			new_iter.user_data = node->parent;
			gtk_tree_model_row_has_child_toggled(model, path, &new_iter);
		}
	}
	gtk_tree_path_free(path);

	//Kinder durchgehen
	child = node->children;
	while (child) {
		zond_treeviewfm_walk_tree(model, stamp, child, pos_child);

		child = child->next;
		pos_child++;
	}

	return;
}

void zond_treeviewfm_move_node(GtkTreeModel *model, GtkTreeIter *iter_src,
		GtkTreeIter *anchor, gboolean child) {
	GNode *node_src = NULL;
	GNode *node_src_parent = NULL;
	GtkTreePath *path = NULL;
	gint pos = 0;

	node_src = iter_src->user_data;
	node_src_parent = node_src->parent;

	path = gtk_tree_model_get_path(model, iter_src);

	//node ausklinken
	g_node_unlink(node_src);

	//und im treeview bekanntgeben
	gtk_tree_model_row_deleted(model, path);

	if (node_src_parent->parent != NULL) //nicht root?
	{
		/* child_toggled */
		if (node_src_parent->children == NULL) //keineGeschwister
		{
			GtkTreeIter new_iter = { 0, };
			gtk_tree_path_up(path);
			new_iter.stamp = iter_src->stamp;
			new_iter.user_data = node_src_parent;
			gtk_tree_model_row_has_child_toggled(model, path, &new_iter);
		}
	}
	gtk_tree_path_free(path);

	//jetzt Knoten wieder einfügen
	if (child) {
		GNode *node_anchor = NULL;

		if (anchor)
			node_anchor = anchor->user_data;
		else //anchor ist root-node
		{
			GtkTreeIter iter_first = { 0 };

			gtk_tree_model_get_iter_first(model, &iter_first);
			node_anchor = ((GNode*) (iter_first.user_data))->parent;
		}

		g_node_insert_after(node_anchor, NULL, node_src);
	} else {
		g_node_insert_after( G_NODE(anchor->user_data)->parent,
				G_NODE(anchor->user_data), node_src);
		pos = g_node_child_position( G_NODE(anchor->user_data)->parent,
				node_src);
	}

	//im treeview bekannt geben
	zond_treeviewfm_walk_tree(model, iter_src->stamp, node_src, pos);

	return;
}

void zond_treeviewfm_kill_parent(ZondTreeviewFM *ztvfm, GtkTreeIter *iter) {
	GtkTreeIter child = { 0 };
	GtkTreeIter anchor = { 0 };
	GtkTreeModel *model = NULL;

	if (!iter)
		return;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm));

	anchor = *iter;

	while (gtk_tree_model_iter_children(model, &child, iter)) {
		zond_treeviewfm_move_node(model, &child, &anchor, FALSE);

		anchor = child;
	}

	gtk_tree_store_remove(GTK_TREE_STORE(model), iter);

	return;
}

gint zond_treeviewfm_get_id_pda(ZondTreeviewFM *ztvfm, GtkTreeIter *iter,
		gint *ID, GError **error) {
	GObject *object = NULL;
/*
	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)), iter, 0,
			&object, -1);
	if (!object) {
		if (error)
			*error = g_error_new( ZOND_ERROR, 0, "%s\nKnoten ist dummy",
					__func__);

		return -1;
	} else if (!ZOND_IS_PDF_ABSCHNITT(object)) {
		if (error)
			*error = g_error_new( ZOND_ERROR, 0,
					"%s\nKnoten ist kein Pdf-Abschnitt", __func__);
		g_object_unref(object);

		return -1;
	}

	if (ID)
		*ID = zond_pdf_abschnitt_get_ID(ZOND_PDF_ABSCHNITT(object));
	g_object_unref(object);
*/
	return 0;
}
