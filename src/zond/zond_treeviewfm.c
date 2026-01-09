#include "zond_treeviewfm.h"

#include "../misc.h"
#include "../sond_fileparts.c"
#include "../sond_renderer.h"
//#include "../sond_treeviewfm.h"

#include "zond_dbase.h"
#include "zond_treeview.h"
#include "zond_tree_store.h"

#include "10init/app_window.h"
#include "20allgemein/project.h"
#include "20allgemein/oeffnen.h"
#include "99conv/general.h"

#include "global_types.h"

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32

typedef struct {
	Projekt *zond;
	gboolean changed_tmp;
} ZondTreeviewFMPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondTreeviewFM, zond_treeviewfm, SOND_TYPE_TREEVIEWFM)

static gchar* get_path_from_stvfm_item(SondTVFMItem* stvfm_item) {
	gchar* filepart = NULL;
	gchar* path = NULL;

	if (sond_tvfm_item_get_sond_file_part(stvfm_item))
		filepart = sond_file_part_get_filepart(
				sond_tvfm_item_get_sond_file_part(stvfm_item));

	if (sond_tvfm_item_get_item_type(stvfm_item) ==
			SOND_TVFM_ITEM_TYPE_DIR) {
		path = g_strconcat((filepart) ? filepart : "",
				(filepart && sond_tvfm_item_get_path_or_section(stvfm_item)) ? "//" : "",
				(sond_tvfm_item_get_path_or_section(stvfm_item)) ?
						sond_tvfm_item_get_path_or_section(stvfm_item) : "", NULL);
		g_free(filepart);
	}
	else
		path = filepart;

	return path;
}

static gint zond_treeviewfm_deter_background(SondTVFMItem *stvfm_item, GError **error) {
	//prüfen auf Volltreffer
	//nur, wenn kein Verzeichnis
	if (sond_tvfm_item_get_item_type(stvfm_item) != SOND_TVFM_ITEM_TYPE_DIR) {
		gint rc = 0;
		SondFilePart* sfp = NULL;
		g_autofree gchar* filepart = NULL;
		gchar const* section = NULL;

		ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(
				ZOND_TREEVIEWFM(sond_tvfm_item_get_stvfm(stvfm_item)));

		sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);
		filepart = sond_file_part_get_filepart(sfp);

		section = sond_tvfm_item_get_path_or_section(stvfm_item);

		//Funktion testet, ob mind ein Abschnitt in db, der section mindestens umfaßt
		rc = zond_dbase_test_path_section(
				priv->zond->dbase_zond->zond_dbase_work, filepart,
				section, TRUE, error);
		if (rc == -1)
			ERROR_Z
		else if (rc == 1)
			return 1; //Treffer
	}

	return 0;
}

static gboolean get_gmessage_index(SondTVFMItem* stvfm_item, gint* index) {
	if (sond_tvfm_item_get_path_or_section(stvfm_item)) {
		if (SOND_IS_FILE_PART_GMESSAGE(sond_tvfm_item_get_sond_file_part(stvfm_item))) {
			*index = strrchr(sond_tvfm_item_get_path_or_section(stvfm_item), '/') ?
					atoi(strrchr(sond_tvfm_item_get_path_or_section(stvfm_item), '/') + 1) :
					atoi(sond_tvfm_item_get_path_or_section(stvfm_item));
			return TRUE;
		}
	}
	else if (SOND_IS_FILE_PART_GMESSAGE(sond_file_part_get_parent(
			sond_tvfm_item_get_sond_file_part(stvfm_item)))) {
		gchar const* path_sfp_parent = NULL;

		path_sfp_parent = sond_file_part_get_path(
				sond_tvfm_item_get_sond_file_part(stvfm_item));

		*index = strrchr(path_sfp_parent, '/') ?
				atoi(strrchr(path_sfp_parent, '/') + 1) :
				atoi(path_sfp_parent);
		return TRUE;
	}

	return FALSE;
}

static gint zond_treeviewfm_before_delete(ZondTreeviewFM* ztvfm,
		SondTVFMItem *stvfm_item, GError **error) {
	gint rc = 0;
	g_autofree gchar* path = NULL;
	gint index_from = 0;
	gchar const* section = NULL;

	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(ztvfm);

	path = get_path_from_stvfm_item(stvfm_item);
	section = sond_tvfm_item_get_path_or_section(stvfm_item);

	rc = zond_dbase_test_path_section(priv->zond->dbase_zond->zond_dbase_work,
			path, section, FALSE, error);
	if (rc == -1)
		ERROR_Z
	else if (rc == 1)
		return 1;

	rc = zond_dbase_test_path_section(priv->zond->dbase_zond->zond_dbase_store,
			path, section, FALSE, error);
	if (rc == -1)
		ERROR_Z
	else if (rc == 1) {
		display_message(priv->zond->app_window,
				"Die zu löschende Datei bzw. der Abschnitt ist "
				"noch in der Speicher-Datenbank vorhanden.\n"
				"Bitte zuerst das Projekt speichern, "
				"bevor Sie die Datei bzw. den Abschnitt löschen.", NULL);

		return 1;
	}

	rc = dbase_zond_begin(priv->zond->dbase_zond, error);
	if (rc)
		ERROR_Z

	if (!section) { //wenn Datei gelöscht wird, kann auch Type-5-node gelöscht werden

	}

	//wenn aus GMessage verschoben wurde - nachfolgende indizes anpassen
	if (get_gmessage_index(stvfm_item, &index_from)) {
		gint rc = 0;
		gchar* prefix = NULL;

		prefix = g_strndup(path, strlen(path) - strlen(strrchr(path, '/') + 1));

		rc = dbase_zond_update_gmessage_index(priv->zond->dbase_zond,
				prefix, index_from, FALSE, error);
		g_free(prefix);
		if (rc) {
			dbase_zond_rollback(priv->zond->dbase_zond, error);
			ERROR_Z
		}
	}

	return 0;
}

static gint zond_treeviewfm_before_move(SondTreeviewFM* stvfm,
		SondTVFMItem* stvfm_item, SondTVFMItem* stvfm_item_parent,
		gchar const* base_new, gint index_to, GError **error) {
	gint rc = 0;
	g_autofree gchar* prefix_old = NULL;
	g_autofree gchar* prefix_new = NULL;
	gboolean from_gmessage = FALSE;
	gint index_from = 0;

	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	prefix_old = get_path_from_stvfm_item(stvfm_item);
	prefix_new = get_path_from_stvfm_item(stvfm_item_parent);

	//Falls aus GMessage verschoben wird - welchen Index hatte Eintrag?
	from_gmessage = get_gmessage_index(stvfm_item, &index_from);

	if (prefix_new) { //wenn nicht root-Verzeichnis
		if (!sond_tvfm_item_get_path_or_section(stvfm_item_parent))
			prefix_new = add_string(prefix_new, g_strdup("//"));
		else if (prefix_new) //wenn
			prefix_new = add_string(prefix_new, g_strdup("/"));
	}

	if (SOND_IS_FILE_PART_GMESSAGE(sond_tvfm_item_get_sond_file_part(stvfm_item_parent)))
		prefix_new = add_string(prefix_new, g_strdup("alpha")); //irgendwas alphanumerisches
	else
		prefix_new = add_string(prefix_new, g_strdup(base_new));

	//Änderungsstatus zwischenspeichern
	ztvfm_priv->changed_tmp = ztvfm_priv->zond->dbase_zond->changed;

	rc = dbase_zond_begin(ztvfm_priv->zond->dbase_zond, error);
	if (rc)
		ERROR_Z

	//alle Dateien, die mit filepart(stvfm_item) + path anfangen (einschließlich stvfm_item)
		//-> umbenennen
	rc = dbase_zond_update_path(ztvfm_priv->zond->dbase_zond, prefix_old, prefix_new, error);
	if (rc) {
		dbase_zond_rollback(ztvfm_priv->zond->dbase_zond, error);
		ERROR_Z
	}

	//wenn aus GMessage verschoben wurde - nachfolgende indizes anpassen (-1)
	if (from_gmessage) {
		gint rc = 0;
		gchar* prefix_gmessage = NULL;

		prefix_gmessage = g_strndup(prefix_old, strlen(prefix_old) -
				strlen(strrchr(prefix_old, '/') + 1));

		rc = dbase_zond_update_gmessage_index(ztvfm_priv->zond->dbase_zond,
				prefix_gmessage, index_from, FALSE, error);
		g_free(prefix_gmessage);
		if (rc) {
			dbase_zond_rollback(ztvfm_priv->zond->dbase_zond, error);
			ERROR_Z
		}
	}

	//wenn in GMESSAGE
	if (SOND_IS_FILE_PART_GMESSAGE(sond_tvfm_item_get_sond_file_part(stvfm_item_parent))) {
		gint rc = 0;
		gchar* prefix_gmessage = NULL;

		//"alpha" wieder wegnehmen
		prefix_gmessage = g_strndup(prefix_new, strlen(prefix_new) - strlen(strrchr(prefix_new, '/') + 1));

		//indizes ab index_to +1
		rc = dbase_zond_update_gmessage_index(ztvfm_priv->zond->dbase_zond,
				prefix_gmessage, index_to, TRUE, error);
		if (rc) {
			dbase_zond_rollback(ztvfm_priv->zond->dbase_zond, error);
			g_free(prefix_gmessage);

			ERROR_Z
		}

		//index_to als basename hinzufügen
		prefix_gmessage = add_string(prefix_gmessage, g_strdup_printf("%u", index_to));

		rc = dbase_zond_update_path(ztvfm_priv->zond->dbase_zond, prefix_new,
				prefix_gmessage, error);
		g_free(prefix_gmessage);
		if (rc) {
			dbase_zond_rollback(ztvfm_priv->zond->dbase_zond, error);
			ERROR_Z
		}
	}

	return 0;
}

static void zond_treeviewfm_after(SondTreeviewFM* stvfm,
		gboolean suc) {
	GError* error_int = NULL;
	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	if (suc) {
		gint rc = 0;

		rc = dbase_zond_commit(priv->zond->dbase_zond, &error_int);
		if (rc) {
			//ToDo: ausführliche Erklärung; verschobene Dateien loggen
			exit(EXIT_FAILURE);
		}
	}
	else
		dbase_zond_rollback(priv->zond->dbase_zond, &error_int);

	project_reset_changed(priv->zond, priv->changed_tmp);

	return;
}

static gint zond_treeviewfm_text_edited(SondTreeviewFM *stvfm,
		GtkTreeIter *iter, SondTVFMItem* stvfm_item, const gchar *new_text,
		GError **error) {
	gboolean changed = FALSE;

	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	if (ztvfm_priv->zond->dbase_zond->changed)
		changed = TRUE;

	if (sond_tvfm_item_get_item_type(stvfm_item) == SOND_TVFM_ITEM_TYPE_LEAF_SECTION) {
		gint ID_section = 0;
		g_autofree gchar* filepart = NULL;
		gint rc = 0;
		GtkTreeIter* iter = NULL;

		filepart = sond_file_part_get_filepart(sond_tvfm_item_get_sond_file_part(stvfm_item));

		rc = zond_dbase_get_section(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
				filepart, sond_tvfm_item_get_path_or_section(stvfm_item), &ID_section, error);
		if (rc)
			ERROR_Z

		if (ID_section == 0) {
			if (error) *error = g_error_new(ZOND_ERROR, 0,
					"%s\nAbschnitt nicht gefunden", __func__);

			return -1;
		}

		rc = zond_dbase_update_node_text(
				ztvfm_priv->zond->dbase_zond->zond_dbase_work, ID_section,
				new_text, error);
		if (rc)
			ERROR_Z

		//Text in treeview anpassen
		iter = zond_treeview_abfragen_iter(ZOND_TREEVIEW(ztvfm_priv->zond->treeview[BAUM_INHALT]),
				ID_section);
		if (!iter)
			g_warning("%s, Abschnitt %s: Knoten nicht in Baum INHALT gefunden",
					filepart, sond_tvfm_item_get_path_or_section(stvfm_item));
		else {
			zond_tree_store_set(iter, NULL, new_text, 0);
			gtk_tree_iter_free(iter);
		}
	}
	else { //chain-up, wenn nicht erledigt
		gint rc = 0;

		rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->text_edited(stvfm,
				iter, stvfm_item, new_text, error);
		if (rc)
			ERROR_Z
	}

	if (!changed)
		project_reset_changed(ztvfm_priv->zond, FALSE);

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

static gint zond_treeviewfm_open_stvfm_item(SondTVFMItem* stvfm_item,
		gboolean open_with, GError **error) {

	if (!open_with && SOND_IS_FILE_PART_PDF(sond_tvfm_item_get_sond_file_part(stvfm_item))) {
		PdfPos pos_pdf = { 0 };
		gchar const* section = NULL;
		gint rc = 0;
		SondFilePartPDF* sfp_pdf = NULL;

		ZondTreeviewFM* ztvfm = ZOND_TREEVIEWFM(sond_tvfm_item_get_stvfm(stvfm_item));
		ZondTreeviewFMPrivate* ztvfm_priv =
				zond_treeviewfm_get_instance_private(ztvfm);

		sfp_pdf = SOND_FILE_PART_PDF(sond_tvfm_item_get_sond_file_part(stvfm_item));
		section = sond_tvfm_item_get_path_or_section(stvfm_item);
		if (section) {
			Anbindung anbindung = { 0 };

			anbindung_parse_file_section(section,  &anbindung);
			pos_pdf.seite = anbindung.von.seite;
			pos_pdf.index = anbindung.von.index;
		}

		rc = zond_treeview_oeffnen_internal_viewer(ztvfm_priv->zond,
				sfp_pdf, NULL, &pos_pdf, error);
		if (rc)
			ERROR_Z
	}
	else { //chain-up, falls nicht bearbeitet
		gint rc = 0;

		rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->
				open_stvfm_item(stvfm_item, open_with, error);
		if (rc)
			ERROR_Z
	}

	return 0;
}

static gint zond_treeviewfm_delete_section(SondTVFMItem* stvfm_item, GError** error) {
	gint rc = 0;
	gchar* filepart = NULL;
	gchar const* section = NULL;
	gint ID = 0;

	ZondTreeviewFMPrivate* ztvfm_priv = NULL;

	ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(sond_tvfm_item_get_stvfm(stvfm_item)));

	section = sond_tvfm_item_get_path_or_section(stvfm_item);
	filepart = sond_file_part_get_filepart(
			sond_tvfm_item_get_sond_file_part(stvfm_item));

	rc = zond_dbase_get_section(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
			filepart, section, &ID, error);
	g_free(filepart);
	if (rc)
		ERROR_Z

	rc = zond_dbase_remove_node(ztvfm_priv->zond->dbase_zond->zond_dbase_work, ID, error);
	if (rc)
		ERROR_Z

	return 0;
}

static gint zond_treeviewfm_load_sections(SondTVFMItem* stvfm_item,
		GPtrArray** arr_children, GError** error) {
	g_autofree gchar* filepart = NULL;
	g_autoptr(GPtrArray) arr_children_int = NULL;
	SondFilePart* sfp = NULL;
	gchar const* section = NULL;
	gint ID = 0;
	gint child = 0;
	gint rc = 0;

	ZondTreeviewFMPrivate* ztvfm_priv =
			zond_treeviewfm_get_instance_private(
					ZOND_TREEVIEWFM(sond_tvfm_item_get_stvfm(stvfm_item)));

	sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);

	if (!sfp) {
		g_warning("sfp darf nicht NULL sein");
		return 0;
	}

	if (!SOND_IS_FILE_PART_PDF(sfp))
		return 0;

	section = sond_tvfm_item_get_path_or_section(stvfm_item);
	filepart = sond_file_part_get_filepart(sfp);

	rc = zond_dbase_get_section(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
			filepart, section, &ID, error);
	if (rc)
		ERROR_Z

	if (section && ID == 0) { //wenn section im Baum, dann muß sie auch in db sein
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s\nAbschnitt nicht gefunden", __func__);

		return -1;
	}

	rc = zond_dbase_get_first_child(ztvfm_priv->zond->dbase_zond->zond_dbase_work, ID, &child, error);
	if (rc)
		ERROR_Z

	if (!arr_children) {
		if (child) return 1; //hat sections
		else return 0;
	}

	arr_children_int = g_ptr_array_new_with_free_func((GDestroyNotify) g_object_unref);
	while (child) {
		SondTVFMItem* stvfm_item_child = NULL;
		gchar* section_child = NULL;
		gint rc = 0;
		gint younger_sibling_id = 0;
		gchar* icon_name = NULL;

		rc = zond_dbase_get_node(ztvfm_priv->zond->dbase_zond->zond_dbase_work, child,
				NULL, NULL, NULL, &section_child, &icon_name, NULL, NULL, error);
		if (rc)
			ERROR_Z

		stvfm_item_child =
				sond_tvfm_item_create(sond_tvfm_item_get_stvfm(stvfm_item),
						sfp, section_child);
		g_free(section_child);
		sond_tvfm_item_set_icon_name(stvfm_item_child, icon_name);
		g_ptr_array_add(arr_children_int, stvfm_item_child);

		rc = zond_dbase_get_younger_sibling(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
				child, &younger_sibling_id, error);
		if (rc)
			ERROR_Z

		child = younger_sibling_id;
	}

	*arr_children = g_ptr_array_ref(arr_children_int);

	return 0;
}

static gboolean zond_treeviewfm_has_sections(SondTVFMItem* stvfm_item) {
	GError* error = NULL;
	gint rc = FALSE;

	rc = zond_treeviewfm_load_sections(stvfm_item, NULL, &error);
	if (rc == -1) {
		g_warning("Konnte sections nicht laden: %s", error->message);
		g_error_free(error);

		return FALSE;
	}

	return (gboolean) rc;
}

static gint zond_treeviewfm_get_text_from_section(SondTVFMItem* stvfm_item,
		gchar** text, GError** error) {
	gchar* filepart = NULL;
	gchar const* section = NULL;
	gint ID = 0;
	gint rc = 0;

	ZondTreeviewFMPrivate* ztvfm_priv =
			zond_treeviewfm_get_instance_private(ZOND_TREEVIEWFM(sond_tvfm_item_get_stvfm(stvfm_item)));

	section = sond_tvfm_item_get_path_or_section(stvfm_item);
	filepart = sond_file_part_get_filepart(
			sond_tvfm_item_get_sond_file_part(stvfm_item));

	rc = zond_dbase_get_section(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
			filepart, section, &ID, error);
	g_free(filepart);
	if (rc)
		ERROR_Z

	if (ID == 0) {
		Anbindung anbindung = { 0 };

		anbindung_parse_file_section(section, &anbindung);
		*text = anbindung_to_human_readable(&anbindung);
	}
	else {
		rc = zond_dbase_get_node(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
				ID, NULL, NULL, NULL, NULL, NULL, text, NULL, error);
		if (rc)
			ERROR_Z
	}

	return 0;
}

static void zond_treeviewfm_class_init(ZondTreeviewFMClass *klass) {
	SOND_TREEVIEWFM_CLASS(klass)->text_from_section =
			zond_treeviewfm_get_text_from_section;
	SOND_TREEVIEWFM_CLASS(klass)->deter_background = zond_treeviewfm_deter_background;
	SOND_TREEVIEWFM_CLASS(klass)->text_edited = zond_treeviewfm_text_edited;
	SOND_TREEVIEWFM_CLASS(klass)->results_row_activated =
			zond_treeviewfm_results_row_activated;
	SOND_TREEVIEWFM_CLASS(klass)->open_stvfm_item = zond_treeviewfm_open_stvfm_item;
	SOND_TREEVIEWFM_CLASS(klass)->load_sections = zond_treeviewfm_load_sections;
	SOND_TREEVIEWFM_CLASS(klass)->has_sections = zond_treeviewfm_has_sections;
	SOND_TREEVIEWFM_CLASS(klass)->delete_section = zond_treeviewfm_delete_section;

	return;
}

static void zond_treeviewfm_init(ZondTreeviewFM *ztvfm) {

	return;
}

static void zond_treeviewfm_jump_activate(GtkMenuItem* item, gpointer data) {
	GtkTreeIter iter = { 0 };
	SondTVFMItem* stvfm_item = NULL;
	gchar* filepart = NULL;
	gchar const* section = NULL;
	gint rc = 0;
	gint ID = 0;
	GError* error = NULL;

	Projekt *zond = (Projekt*) data;

	if (!sond_treeview_get_cursor(zond->treeview[BAUM_FS], &iter))
		return;

	gtk_tree_model_get(gtk_tree_view_get_model(
			GTK_TREE_VIEW(zond->treeview[BAUM_FS])), &iter, 0, &stvfm_item, -1);
	g_object_unref(stvfm_item);

	if (sond_tvfm_item_get_item_type(stvfm_item) == SOND_TVFM_ITEM_TYPE_DIR)
		return;

	filepart = sond_file_part_get_filepart(sond_tvfm_item_get_sond_file_part(stvfm_item));
	section = sond_tvfm_item_get_path_or_section(stvfm_item);

	rc = zond_dbase_get_section(zond->dbase_zond->zond_dbase_work, filepart, section, &ID, &error);
	g_free(filepart);
	if (rc) {
		display_message(zond->app_window, "Zur Anbindung springen nicht möglich\n\n"
				"%s", error->message, NULL);
		g_error_free(error);

		return;
	}

	zond_treeview_jump_to_node_id(zond, ID);

	return;
}

ZondTreeviewFM* zond_treeviewfm_new(Projekt* zond) {
	ZondTreeviewFM* ztvfm = NULL;
	ZondTreeviewFMPrivate* ztvfm_priv = NULL;

	ztvfm = g_object_new(ZOND_TYPE_TREEVIEWFM, NULL);
	ztvfm_priv = zond_treeviewfm_get_instance_private(ztvfm);

	ztvfm_priv->zond = zond;

	g_signal_connect(ztvfm, "before-delete",
			G_CALLBACK(zond_treeviewfm_before_delete), NULL);
	g_signal_connect(ztvfm, "before-move",
			G_CALLBACK(zond_treeviewfm_before_move), NULL);
	g_signal_connect(ztvfm, "after",
			G_CALLBACK(zond_treeviewfm_after), NULL);

	//Ergänze contextmenu
	GtkWidget* contextmenu = sond_treeview_get_contextmenu(SOND_TREEVIEW(ztvfm));

	//Trennblatt
	GtkWidget *item_separator_0 = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(contextmenu), item_separator_0);

	//Zur Anbindung springen
	GtkWidget *item_jump = gtk_menu_item_new_with_label(
			"Zur Anbindung springen");
	gtk_menu_shell_append(GTK_MENU_SHELL(contextmenu), item_jump);

	gtk_widget_show_all(contextmenu);

	g_signal_connect(item_jump, "activate",
			G_CALLBACK(zond_treeviewfm_jump_activate),
			(gpointer) zond);

	return ztvfm;
}

static gint zond_treeviewfm_find_section(ZondTreeviewFM *ztvfm,
		GtkTreeIter* iter, Anbindung anbindung, gboolean open,
		GtkTreeIter *iter_res, GError **error) {
	GtkTreeIter iter_child = { 0 };
	SondTVFMItem* stvfm_item = NULL;

	if (!gtk_tree_model_iter_children(
			gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)),
			&iter_child, iter))
		return 0; //keine Kinder, also kann man hier aufhören

	gtk_tree_model_get(
			gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)),
			&iter_child, 0, &stvfm_item, -1);

	if (!stvfm_item) //dummy
	{
		if (!open) //wenn's nicht geöffnet werden soll, sind wir hier fertig
			return 0;

		sond_treeview_expand_row(SOND_TREEVIEW(ztvfm), iter);

		gtk_tree_model_iter_children(
				gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)), &iter_child, iter);
	}
	else
		g_object_unref(stvfm_item); //häßlich

	do {
		gchar const* section_child = NULL;
		SondTVFMItem* stvfm_item = NULL;
		Anbindung anbindung_child = { 0 };

		gtk_tree_model_get(
				gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)),
				&iter_child, 0, &stvfm_item, -1);
		section_child =
				sond_tvfm_item_get_path_or_section(stvfm_item);
		anbindung_parse_file_section(section_child, &anbindung_child);
		g_object_unref(stvfm_item);

		if (anbindung_1_eltern_von_2(anbindung_child, anbindung)) {
			gint rc = 0;

			rc = zond_treeviewfm_find_section(ztvfm, &iter_child, anbindung, open, iter_res, error);
			if (rc == -1)
				ERROR_Z

			return rc; //wenn hier nicht gefunden, dann auch nirgendwo anders
		}
		else if (anbindung_1_gleich_2(anbindung, anbindung_child)) {
			if(iter_res)
				*iter_res = iter_child;

			return 1;
		}
	} while (gtk_tree_model_iter_next(
			gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)), &iter_child));

	return 0; //nichts gefunden
}

gint zond_treeviewfm_section_visible(ZondTreeviewFM *ztvfm,
		gchar const *file_part, gchar const *section, gboolean open,
		gboolean *visible, GtkTreeIter *iter, gboolean *children,
		gboolean *opened, GError **error) {
	gint rc = 0;
	GtkTreeIter iter_intern = { 0 };

	if (!open && !visible)
		return 0; //ergibt einfach keinen Sinn

	rc = sond_treeviewfm_file_part_visible(SOND_TREEVIEWFM(ztvfm), NULL, file_part, open,
			&iter_intern, error);
	if (rc == -1)
		ERROR_Z
	else if (rc == 0)
		return 0;

	if (section) { //section muß gesucht werden
		gint rc = 0;
		Anbindung anbindung = { 0 };
		GtkTreeIter iter_very_intern = { 0 };

		anbindung_parse_file_section(section, &anbindung);

		rc = zond_treeviewfm_find_section(ztvfm, &iter_intern, anbindung, open, &iter_very_intern, error);
		if (rc == -1)
			ERROR_Z
		else if (rc == 0) {
			if (visible)
				*visible = FALSE;

			return 0; //nix gefunden
		}
		iter_intern = iter_very_intern;
	}

	//prüfen, ob Kinder, und wenn nur dummy
	if (children) {
		if (gtk_tree_model_iter_has_child(
				gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)), &iter_intern)) {
			*children = TRUE;

			if (opened)
				*opened = sond_treeview_row_expanded(SOND_TREEVIEW(ztvfm),
						&iter_intern);
		}
		else
			*children = FALSE;
	}

	if (visible)
		*visible = TRUE;

	if (iter)
		*iter = iter_intern;

	return 1;
}

gint zond_treeviewfm_set_cursor_on_section(ZondTreeviewFM *ztvfm,
		gchar const *file_part, gchar const *section, GError **error) {
	gint rc = 0;
	GtkTreeIter iter = { 0 };
	gboolean visible = FALSE;

	rc = zond_treeviewfm_section_visible(ztvfm, file_part, section,
	TRUE, &visible, &iter, NULL, NULL, error);
	if (rc == -1)
		ERROR_Z

	if (visible)
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

static void zond_treeviewfm_move_node(GtkTreeModel *model, GtkTreeIter *iter_src,
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
