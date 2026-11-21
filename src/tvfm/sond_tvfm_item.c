/*
 sond (sond_tvfm_item.c) - Akten, Beweisstücke, Unterlagen
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

/*
 * SondTVFMItem - GObject-Derivat für EINEN Knoten im SondTreeviewFM-Baum;
 * ausgelagert aus sond_treeviewfm.c (reine Verschiebung, keine
 * Verhaltensänderung). Zugriff auf SondTreeviewFMPrivate über den
 * Freund-Accessor sond_treeviewfm_get_priv() (sond_treeviewfm_private.h),
 * analog sond_seadrive.c.
 *
 * sond_treeviewfm.c greift weiterhin direkt auf SondTVFMItemPrivate zu
 * (Rename/Kopieren/Einfügen/Löschen-Kaskaden über mehrere Items) - über
 * den Freund-Accessor sond_tvfm_item_get_priv(). Die vormals file-
 * statischen Funktionen sond_tvfm_item_get_basename(), _rename(), _copy(),
 * _move() und _delete() (früher delete_item()) werden
 * auch von dort aufgerufen und sind deshalb nicht mehr static, sondern in
 * sond_treeviewfm_private.h (nicht der öffentlichen sond_tvfm_item.h) als
 * modul-interne Freund-API deklariert.
 */

#include "sond_tvfm_item.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "../sond_log_and_error.h"
#include "../sond_fileparts.h"
#include "../sond_file_helper.h"
#include "../sond_mime.h"
#include "../sond_password.h"
#include "sond_tvfm_owner.h"
#include "sond_tvfm_item_private.h"

G_DEFINE_TYPE_WITH_PRIVATE(SondTVFMItem, sond_tvfm_item, G_TYPE_OBJECT)

/* Freund-Accessor für sond_seadrive.c/sond_treeviewfm.c - s. ausführl.
 * Kommentar in sond_treeviewfm_private.h. */
SondTVFMItemPrivate *sond_tvfm_item_get_priv(SondTVFMItem *item) {
	return sond_tvfm_item_get_instance_private(item);
}

static void sond_tvfm_item_finalize(GObject *self) {
	SondTVFMItemPrivate *sond_tvfm_item_priv =
			sond_tvfm_item_get_instance_private(SOND_TVFM_ITEM(self));

	g_free(sond_tvfm_item_priv->display_name);

	//sfp-Type oder root
	if (sond_tvfm_item_priv->sond_file_part)
		g_object_unref(sond_tvfm_item_priv->sond_file_part);

	g_free(sond_tvfm_item_priv->path_or_section);

	G_OBJECT_CLASS(sond_tvfm_item_parent_class)->finalize(self);

	return;
}

static void sond_tvfm_item_class_init(SondTVFMItemClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_tvfm_item_finalize;

	return;
}

static void sond_tvfm_item_init(SondTVFMItem *self) {

	return;
}

#if defined(GTK_MAJOR_VERSION) && GTK_MAJOR_VERSION < 4
SondTVFMItem* sond_tvfm_item_from_model(GtkTreeModel *model, GtkTreeIter *iter) {
	SondTVFMItem *stvfm_item = NULL;

	gtk_tree_model_get(model, iter, 0, &stvfm_item, -1);

	return stvfm_item;
}

SondTVFMItem* sond_tvfm_item_from_view(GtkTreeView *tree_view,
		GtkTreeIter *iter) {
	return sond_tvfm_item_from_model(gtk_tree_view_get_model(tree_view), iter);
}
#endif

gchar* sond_tvfm_item_get_filepart(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);
	g_autofree gchar *filepart_sfp = NULL;

	if (stvfm_item_priv->sond_file_part)
		filepart_sfp = sond_file_part_get_filepart(stvfm_item_priv->sond_file_part);

	if (filepart_sfp && stvfm_item_priv->path_or_section)
		return g_strconcat(filepart_sfp, "//", stvfm_item_priv->path_or_section,
				NULL);
	if (filepart_sfp)
		return g_steal_pointer(&filepart_sfp);
	if (stvfm_item_priv->path_or_section)
		return g_strdup(stvfm_item_priv->path_or_section);

	return NULL;
}

SondTVFMItemType sond_tvfm_item_get_item_type(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);
	return stvfm_item_priv->type;
}

gchar const* sond_tvfm_item_get_path_or_section(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->path_or_section;
}

gchar const* sond_tvfm_item_get_display_name(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->display_name;
}

gboolean sond_tvfm_item_is_content_root_marker(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->is_content_root_marker;
}

SondFilePart* sond_tvfm_item_get_sond_file_part(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->sond_file_part;
}

SondTreeviewFM* sond_tvfm_item_get_stvfm(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->stvfm; //GTK3: der Eigentümer ist der Dateibaum
}

gchar const* sond_tvfm_item_get_icon_name(SondTVFMItem* stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->icon_name;
}

void sond_tvfm_item_set_icon_name(SondTVFMItem* stvfm_item,
		gchar const* icon_name) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	stvfm_item_priv->icon_name = icon_name;

	return;
}

/* Liefert für die synthetischen Marker-Knoten (PDF-PageTree, GMESSAGE-
 * Message) den echten Basename der zugehörigen Datei - von
 * sond_file_part_get_path() DES SondFilePart selbst (PDF-/.eml-Datei),
 * nicht von path_or_section (das für diese Marker-Pfade "//" bzw.
 * "//message" nur Datenmüll basename-t). Nur für den Anbinden-Pfad
 * gebraucht (s. sond_tvfm_item_get_anbinden_label() unten) - der
 * display_name in BAUM_FS bleibt bewusst "PageTree"/"Message" (s.
 * sond_tvfm_item_create()). */
static gchar* sond_tvfm_item_basename_of_sfp_dup(SondFilePart *sond_file_part) {
	gchar const *path = NULL;
	gchar const *basename = NULL;

	if (!sond_file_part)
		return NULL;

	path = sond_file_part_get_path(sond_file_part);
	if (!path)
		return NULL;

	basename = strrchr(path, '/');
	basename = basename ? basename + 1 : path;

	return g_strdup(basename);
}

gchar const* sond_tvfm_item_get_basename(SondTVFMItem* stvfm_item) {
	gchar const* path = NULL;
	gchar const* basename = NULL;

	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	if (stvfm_item_priv->path_or_section)
		path = stvfm_item_priv->path_or_section;
	else if (stvfm_item_priv->sond_file_part)
		path = sond_file_part_get_path(stvfm_item_priv->sond_file_part);
	else
		return NULL;

	if (!path) //pfadloses sond_file_part (z.B. nach fehlerhafter Kopie)
		return NULL;

	basename = strrchr(path, '/');

	if (basename)
		basename++; //nach dem '/'
	else
		basename = path; //kein '/', also kompletter Pfad ist der Basename

	return basename;
}

/* Beschriftung speziell für den Anbinden-Pfad (s.
 * zond_treeview_leaf_anbinden(), zond_treeview.c - ruft dies statt
 * sond_tvfm_item_get_display_name() auf): für die synthetischen Marker-
 * Knoten (is_content_root_marker) der echte Dateiname, sonst unverändert
 * der normale display_name. Immer neu alloziert - Aufrufer muss immer
 * g_free()en, auch im Nicht-Marker-Fall, damit die Ownership-Regel
 * einheitlich bleibt. */
gchar* sond_tvfm_item_get_anbinden_label(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	if (stvfm_item_priv->is_content_root_marker) {
		gchar *real_basename = sond_tvfm_item_basename_of_sfp_dup(
				stvfm_item_priv->sond_file_part);

		if (real_basename)
			return real_basename;
		//Fallback, falls sond_file_part_get_path() wider Erwarten NULL
		//liefert: dann eben doch der Platzhalter, statt eines leeren
		//node_text.
	}

	return g_strdup(stvfm_item_priv->display_name);
}

static gint sond_tvfm_item_load_fs_dir(SondTVFMItem*, GPtrArray**, SondTVFMProgress*, GError**);
static gint sond_tvfm_item_load_zip_dir(SondTVFMItem*, GPtrArray**, SondTVFMProgress*, GError**);

static char const* mime_type_to_icon_name_manual(const char *mime_type)
{
    if (!mime_type)
        return "text-x-generic";

    // Exakte Matches
    static const struct {
        const char *mime;
        const char *icon;
    } mime_map[] = {
        // Dokumente
        {"application/pdf", "pdf"},
        {"application/vnd.oasis.opendocument.text", "x-office-document"},
        {"application/msword", "x-office-document"},
        {"application/vnd.openxmlformats-officedocument.wordprocessingml.document", "x-office-document"},

        // Tabellen
        {"application/vnd.oasis.opendocument.spreadsheet", "x-office-spreadsheet"},
        {"application/vnd.ms-excel", "x-office-spreadsheet"},
        {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "x-office-spreadsheet"},

        // Präsentationen
        {"application/vnd.oasis.opendocument.presentation", "x-office-presentation"},
        {"application/vnd.ms-powerpoint", "x-office-presentation"},

        // Bilder
        {"image/png", "image-x-generic"},
        {"image/jpeg", "image-x-generic"},
        {"image/gif", "image-x-generic"},
        {"image/svg+xml", "image-x-generic"},

        // Audio
        {"audio/mpeg", "audio-x-generic"},
        {"audio/ogg", "audio-x-generic"},
        {"audio/flac", "audio-x-generic"},

        // Video
        {"video/mp4", "video-x-generic"},
        {"video/x-matroska", "video-x-generic"},
        {"video/webm", "video-x-generic"},

        // Archive
        {"application/zip", "package-x-generic"},
        {"application/x-tar", "package-x-generic"},
        {"application/gzip", "package-x-generic"},
        {"application/x-7z-compressed", "package-x-generic"},
        {"application/x-rar", "package-x-generic"},

        // Text
        {"text/plain", "text-x-generic"},
        {"text/html", "text-html"},
        {"text/xml", "text-x-generic"}, //"text-xml" existiert nicht in jedem Icon-Theme (Warnung sond_icon_util_load_pixbuf) - "text-x-generic" ist der bereits als Fallback genutzte, garantiert vorhandene Name

        // Code
        {"text/x-c", "text-x-script"},
        {"text/x-python", "text-x-script"},
        {"text/x-java", "text-x-script"},
        {"application/javascript", "text-x-script"},

        {NULL, NULL}
    };

    // Exakte Suche
    for (int i = 0; mime_map[i].mime; i++) {
        if (strcmp(mime_type, mime_map[i].mime) == 0) {
            return mime_map[i].icon;
        }
    }

    // Prefix-basierte Suche
    if (g_str_has_prefix(mime_type, "text/")) {
        return "text-x-generic";
    }
    if (g_str_has_prefix(mime_type, "image/")) {
        return "image-x-generic";
    }
    if (g_str_has_prefix(mime_type, "audio/")) {
        return "audio-x-generic";
    }
    if (g_str_has_prefix(mime_type, "video/")) {
        return "video-x-generic";
    }
    if (g_str_has_prefix(mime_type, "application/")) {
        return "application-x-executable";
    }

    // Fallback
    return "text-x-generic";
}

SondTVFMItem* sond_tvfm_item_create(gpointer owner,
		SondFilePart *sond_file_part, gchar const* path_or_section) {
	SondTVFMItem *stvfm_item = NULL;
	SondTVFMItemPrivate *stvfm_item_priv = NULL;

	stvfm_item = g_object_new(SOND_TYPE_TVFM_ITEM, NULL);
	stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	g_return_val_if_fail(SOND_IS_TVFM_OWNER(owner), NULL);

	stvfm_item_priv->owner = SOND_TVFM_OWNER(owner);
	stvfm_item_priv->path_or_section = g_strdup(path_or_section);
	if (sond_file_part)
		stvfm_item_priv->sond_file_part = g_object_ref(sond_file_part);
	stvfm_item_priv->display_name =
			g_strdup(sond_tvfm_item_get_basename(stvfm_item));

	if (!sond_file_part) {
		gint rc = 0;
		GError* error = NULL;

		stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_DIR;

		//hat Verzeichnis Einträge?
		rc = sond_tvfm_item_load_fs_dir(stvfm_item, NULL, NULL, &error);
		if (rc == -1) {
			LOG_WARN("Fehler beim Öffnen des Verzeichnisses '%s':\n%s",
					(sond_tvfm_item_get_basename(stvfm_item)) ?
							sond_tvfm_item_get_basename(stvfm_item) :
							sond_tvfm_owner_get_root(stvfm_item_priv->owner),
							error->message);

			g_error_free(error);
		}
		else
			if (rc == 1) stvfm_item_priv->has_children = TRUE;

		stvfm_item_priv->icon_name = "folder";
	}
	else {
		if (SOND_IS_FILE_PART_PDF(sond_file_part)) {
			if (sond_file_part_get_has_children(sond_file_part) &&
					!path_or_section) { //Marker für PageTree nicht gesetzt
				stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_DIR;
				stvfm_item_priv->has_children = TRUE;
				stvfm_item_priv->icon_name = "pdf-folder";
			}
			else {
				stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_LEAF;
				stvfm_item_priv->icon_name = "pdf";
				if (!g_strcmp0(path_or_section, "//")) { //Marker gesetzt
					g_free(stvfm_item_priv->path_or_section); //Marker löschen
					stvfm_item_priv->path_or_section = NULL;
					g_free(stvfm_item_priv->display_name); //Display-Name ersetzen
					stvfm_item_priv->display_name = g_strdup("PageTree");
					//echter Dateiname nur beim Anbinden, s. get_anbinden_label()
					stvfm_item_priv->is_content_root_marker = TRUE;
				}
			}
		}
		else if (SOND_IS_FILE_PART_ZIP(sond_file_part)) {
			stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_DIR;

			/* Für path_or_section == NULL (das ZIP-File selbst, noch nicht
			 * hineinexpandiert) NICHT load_zip_dir() aufrufen - das
			 * erzwingt über sond_file_part_zip_list_dir() beim allerersten
			 * Zugriff auf dieses Archiv den kompletten dir_index-Aufbau
			 * (ALLE Einträge, alle Ebenen, s. sfp_zip_build_dir_index()),
			 * nur um zu prüfen ob überhaupt ein Eintrag existiert. Bei
			 * großen Archiven (mehrere Tausend Einträge) macht allein das
			 * bloße AUFLISTEN eines Verzeichnisses mit mehreren
			 * ZIP-Dateien darin (noch ohne sie zu öffnen) spürbar Zeit
			 * aus. Stattdessen das von sond_file_part_zip_test_for_files()
			 * (läuft schon in sond_file_part_create_from_mime_type() beim
			 * Erzeugen des SondFilePart) günstig gesetzte has_children-
			 * Flag wiederverwenden - analog zum PDF/GMessage-Zweig oben.
			 * Für bereits expandierte ZIP-Unterverzeichnisse
			 * (path_or_section != NULL) bleibt load_zip_dir() unverändert
			 * - dort ist dir_index durchs Expandieren ohnehin schon
			 * gecacht, also billig. */
			if (!path_or_section)
				stvfm_item_priv->has_children =
						sond_file_part_get_has_children(sond_file_part);
			else
				/* Rückgabewert ist -1 (Fehler), 0 (keine Kinder) oder 1
				 * (Kinder): nur 1 zählt als "hat Kinder". -1 (z.B. defektes/
				 * pfadloses sond_file_part nach fehlerhafter Kopie aus einem
				 * Container) würde sonst ein Dummy-Kind einfügen, obwohl das
				 * Verzeichnis beim echten Aufklappen mit einer Fehlermeldung
				 * fehlschlägt. */
				stvfm_item_priv->has_children =
						(sond_tvfm_item_load_zip_dir(stvfm_item, NULL, NULL, NULL) == 1) ?
								TRUE : FALSE;

			stvfm_item_priv->icon_name = (path_or_section) ?
					"folder" : "package-x-generic";
		}
		else if (SOND_IS_FILE_PART_GMESSAGE(sond_file_part)) {
			if (sond_file_part_get_has_children(sond_file_part) &&
					!path_or_section) { //Marker für Message nicht gesetzt
				stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_DIR;
				stvfm_item_priv->has_children = TRUE;
				stvfm_item_priv->icon_name = "mail-read";
			}
			else {
				stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_LEAF;
				stvfm_item_priv->icon_name = "mail-read";
				if (!g_strcmp0(path_or_section, "//message")) { //Marker gesetzt
					g_free(stvfm_item_priv->path_or_section); //Marker löschen
					stvfm_item_priv->path_or_section = NULL;
					g_free(stvfm_item_priv->display_name); //Display-Name ersetzen
					stvfm_item_priv->display_name = g_strdup("Message");
					//echter Dateiname nur beim Anbinden, s. get_anbinden_label()
					stvfm_item_priv->is_content_root_marker = TRUE;
				}
				else if (path_or_section) { //Multipart-Verzeichnis
					stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_DIR;
					stvfm_item_priv->has_children =
							sond_file_part_get_has_children(sond_file_part);
					stvfm_item_priv->icon_name = "folder";
				}
			}
		}
		else if (SOND_IS_FILE_PART_LEAF(sond_file_part)) {
			stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_LEAF;
			stvfm_item_priv->icon_name = mime_type_to_icon_name_manual(
					sond_file_part_leaf_get_mime_type(
							SOND_FILE_PART_LEAF(sond_file_part)));
		}
	}

	//section?
	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF
			&& stvfm_item_priv->path_or_section) //Spezialfall "//" wurde oben schon weggefischt
		stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_LEAF_SECTION;

	//Spezialbehandlung für Sections
	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION ||
			stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {
		//Wenn type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION:
		//stvfm_item_priv->icon_name muß in load_children gesetzt werden;
		if (sond_tvfm_owner_supports_sections(stvfm_item_priv->owner))
			stvfm_item_priv->has_children =
					sond_tvfm_owner_has_sections(stvfm_item_priv->owner, stvfm_item);
	}

	return stvfm_item;
}

static gint sond_tvfm_item_load_fs_dir(SondTVFMItem* stvfm_item,
		GPtrArray** arr_children, SondTVFMProgress* progress, GError **error) {
	GPtrArray* loaded_children = NULL;
	gboolean dir_has_children = FALSE;
	gchar* path_dir = NULL;
    SondDir* dir = NULL;
    gchar const* filename = NULL;

	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);
	gchar const *root = sond_tvfm_owner_get_root(stvfm_item_priv->owner);

	path_dir = g_strconcat(root,
			"/", stvfm_item_priv->path_or_section, NULL);
	dir = sond_dir_open(path_dir, error);
	g_free(path_dir);
	if (!dir)
		return -1;

	if (arr_children)
		loaded_children = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

    while ((filename = sond_dir_read_name(dir)) != NULL) {
		SondTVFMItem* stvfm_item_child = NULL;
		gchar* rel_path_child = NULL;
		GStatBuf st = { 0 };

		//Pfade mit führendem Punkt gehören dem System (.sond, .sond_index.db,
		//Projektdatei) und bleiben im Dateibaum unsichtbar
		if (filename[0] == '.')
			continue;

		dir_has_children = TRUE;

		if (!arr_children) //Es gibt Eintrag - reicht
    		break;

		if (stvfm_item_priv->path_or_section)
			rel_path_child = g_strconcat(stvfm_item_priv->path_or_section, "/",
					filename, NULL);
		else rel_path_child = g_strdup(filename);

		/* absoluter Pfad: ein projektrelativer hinge am Arbeitsverzeichnis des
		 * Prozesses (und bekäme unter Windows keinen Long-Path-Schutz) */
		{
			gchar *abs_path_child = g_strconcat(root, "/",
					rel_path_child, NULL);
			GError *error_stat = NULL;
			gint rc_stat = sond_stat(abs_path_child, &st, &error_stat);

			g_free(abs_path_child);
			if (rc_stat) {
				LOG_WARN("g_stat(%s) gibt Fehler zurück: %s", rel_path_child,
						error_stat ? error_stat->message : "?");
				g_clear_error(&error_stat);
				g_free(rel_path_child);

				continue;
			}
		}

		if (S_ISDIR(st.st_mode)) {
			//Verzeichnis
			stvfm_item_child = sond_tvfm_item_create(stvfm_item_priv->stvfm,
					stvfm_item_priv->sond_file_part, rel_path_child);
		}
		else {
			SondFilePart* sfp_child = NULL;
			gchar* full_path = NULL;
			const gchar* mime_from_ext = NULL;

			full_path = g_strconcat(root, "/", rel_path_child, NULL);

			//Ansicht ohne Lesen der Dateien (nur Endung): schnell, kein Download
			if (!sond_tvfm_owner_read_file_contents(stvfm_item_priv->owner)) {
				mime_from_ext = mime_from_extension(filename);
				if (!mime_from_ext)
					mime_from_ext = "application/octet-stream";
			}

#ifdef _WIN32
			/* Bei SeaDrive: offline-Dateien nicht lesen - nur Extension-basierter MIME-Typ */
			if (!mime_from_ext &&
					sond_tvfm_owner_is_seadrive_path(stvfm_item_priv->owner)) {
				wchar_t *lp = prepare_long_path(full_path, NULL);
				if (lp) {
					DWORD attrs = GetFileAttributesW(lp);
					g_free(lp);
					if (attrs != INVALID_FILE_ATTRIBUTES &&
							(attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) {
						mime_from_ext = mime_from_extension(filename);
						//unbekannte Endung: trotzdem nicht lesen (löst Download aus)
						if (!mime_from_ext)
							mime_from_ext = "application/octet-stream";
					}
				}
			}
#endif

			if (mime_from_ext) {
				/* Datei offline - kein Öffnen. ZIP und E-Mail sind immer
				 * Container (Pfeil, geprüft wird erst beim Aufklappen nach dem
				 * Laden), alles andere ein Leaf nach der Endung. */
				sfp_child = sond_file_part_create_unread(
						rel_path_child, stvfm_item_priv->sond_file_part,
						mime_from_ext);
				if (!sfp_child)
					sfp_child = sond_file_part_create_leaf(
							rel_path_child, stvfm_item_priv->sond_file_part,
							mime_from_ext);
			} else
				/* Datei lokal verfügbar - normal lesen und sfp erzeugen */
				sfp_child = sond_file_part_create(stvfm_item_priv->sond_file_part,
						rel_path_child, error);
			g_free(full_path);
			if (!sfp_child) {
				g_free(rel_path_child);
				sond_dir_close(dir);
				if (loaded_children)
					g_ptr_array_unref(loaded_children);
				return -1;
			}

			stvfm_item_child = sond_tvfm_item_create(stvfm_item_priv->stvfm,
					sfp_child, NULL);
			g_object_unref(sfp_child);
		}

		g_free(rel_path_child);

		//Änderungszeit und Größe aus dem sond_stat() oben (kein zweiter Zugriff)
		if (stvfm_item_child) {
			SondTVFMItemPrivate *child_priv =
					sond_tvfm_item_get_instance_private(stvfm_item_child);

			child_priv->mtime = (gint64) st.st_mtime;
			child_priv->size = (gint64) st.st_size;
			child_priv->is_dir = S_ISDIR(st.st_mode) ? TRUE : FALSE;
			child_priv->stat_done = TRUE;
		}

		g_ptr_array_add(loaded_children, stvfm_item_child);
    }

    sond_dir_close(dir);

	if (arr_children) *arr_children = loaded_children;
	else if (dir_has_children) return 1;

	return 0;
}

static gint sond_tvfm_item_load_zip_dir(SondTVFMItem* stvfm_item,
		GPtrArray** arr_children, SondTVFMProgress* progress, GError** error) {
	GPtrArray* entries = NULL;

	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	/* path_or_section ist das Verzeichnispäfix (ohne '/'), NULL = Archiv-
	 * Wurzel. sond_file_part_zip_list_dir() cacht den Verzeichnis-Index
	 * auf dem SondFilePartZip selbst (s. dortigen Doc-Kommentar in
	 * sond_fileparts.h) - hier kein erneuter Vollscan mehr je Knoten. */
	entries = sond_file_part_zip_list_dir(
			SOND_FILE_PART_ZIP(stvfm_item_priv->sond_file_part),
			stvfm_item_priv->path_or_section, error);
	if (!entries)
		return -1; /* Fehler */

	if (!arr_children) {
		/* Nur prüfen ob Einträge vorhanden */
		gboolean has = (entries->len > 0);
		g_ptr_array_unref(entries);
		return has ? 1 : 0;
	}

	*arr_children = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	for (guint i = 0; i < entries->len; i++) {
		SondZipDirEntry* e = g_ptr_array_index(entries, i);
		SondTVFMItem* child = NULL;

		/* Bei großen Archiven (>30000 Einträge) läuft diese Schleife lange
		 * (jede Iteration ruft sond_file_part_create() auf, das per libmagic
		 * den MIME-Typ sniffed). Deshalb Fortschritt und Abbruch: progress
		 * ist rein lesend eingesetzt, Abbruch verwirft nur noch nicht
		 * geladene Kinder, keine Rollback-Problematik (im Unterschied zu
		 * SondProcessFileCtx). */
		if (progress && (i % 200) == 0) {
			if (progress->progress_func)
				progress->progress_func(progress->progress_func_data, NULL);
			if (progress->cancel && *progress->cancel)
				break;
		}

		if (e->is_dir) {
			/* Verzeichnis: path endet auf '/', ohne dieses als path_or_section */
			gchar* dir_path = g_strndup(e->path, strlen(e->path) - 1);
			child = sond_tvfm_item_create(stvfm_item_priv->stvfm,
					stvfm_item_priv->sond_file_part, dir_path);
			g_free(dir_path);
		} else {
			SondFilePart* sfp_child = NULL;
			GError* error_create = NULL;

			sfp_child = sond_file_part_create(stvfm_item_priv->sond_file_part,
					e->path, &error_create);

			if (!sfp_child) {
				LOG_WARN("SondFilePart konnte nicht erzeugt werden:\n%s",
						error_create ? error_create->message : "?");
				g_clear_error(&error_create);

				continue;
			}

			child = sond_tvfm_item_create(stvfm_item_priv->stvfm, sfp_child, NULL);
			g_object_unref(sfp_child);
		}

		g_ptr_array_add(*arr_children, child);
	}

	g_ptr_array_unref(entries);

	return 0;
}

static gint sond_tvfm_item_load_pdf_dir(SondTVFMItem* stvfm_item, GPtrArray** arr_children,
		SondTVFMProgress* progress, GError** error) {
	gint rc = 0;
	GPtrArray* arr_emb_files = NULL;
	GPtrArray* arr_display_names = NULL;
	SondTVFMItemPrivate* stvfm_item_priv = NULL;
	SondTVFMItem* stvfm_item_pdf_page_tree = NULL;

	stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	rc = sond_file_part_pdf_load_embedded_files(SOND_FILE_PART_PDF(stvfm_item_priv->sond_file_part),
			&arr_emb_files, &arr_display_names, error);
	if (rc)
		return -1;

	*arr_children = g_ptr_array_new_with_free_func((GDestroyNotify) g_object_unref);

	stvfm_item_pdf_page_tree =
			sond_tvfm_item_create(stvfm_item_priv->stvfm,
					stvfm_item_priv->sond_file_part, "//");
	g_ptr_array_add(*arr_children, stvfm_item_pdf_page_tree);

	for (guint i = 0; i < arr_emb_files->len; i++) {
		SondFilePart* sfp = NULL;
		SondTVFMItem* stvfm_item_child = NULL;

		sfp = g_ptr_array_index(arr_emb_files, i);

		stvfm_item_child =
				sond_tvfm_item_create(stvfm_item_priv->stvfm, sfp, NULL);

		/* Pfad des Kindes ist die Adresse (ggf. der Schlüssel im
		 * Namensbaum), angezeigt wird der Dateiname */
		{
			SondTVFMItemPrivate* child_priv =
					sond_tvfm_item_get_instance_private(stvfm_item_child);

			g_free(child_priv->display_name);
			child_priv->display_name =
					g_strdup(g_ptr_array_index(arr_display_names, i));
		}

		g_ptr_array_add(*arr_children, stvfm_item_child);
	}

	g_ptr_array_unref(arr_emb_files);
	g_ptr_array_unref(arr_display_names);

	return 0;
}

static gint sond_tvfm_item_load_gmessage_dir(SondTVFMItem* stvfm_item,
		GPtrArray** arr_children, SondTVFMProgress* progress, GError** error) {
	gint rc = 0;
	GPtrArray* arr_mimeparts = NULL;

	SondTVFMItemPrivate* stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	rc = sond_file_part_gmessage_load_path(
			SOND_FILE_PART_GMESSAGE(stvfm_item_priv->sond_file_part),
			stvfm_item_priv->path_or_section,
			&arr_mimeparts, error);
	if (rc)
		return -1;

	*arr_children = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	/* Erstes Kind: Message (Header + Body) - analog zu PageTree bei PDF */
	if (!stvfm_item_priv->path_or_section) { /* Nur auf oberster Ebene der .eml */
		SondTVFMItem* stvfm_item_message = NULL;

		stvfm_item_message = sond_tvfm_item_create(stvfm_item_priv->stvfm,
				stvfm_item_priv->sond_file_part, "//message");

		g_ptr_array_add(*arr_children, stvfm_item_message);
	}

	for (guint i = 0; i < arr_mimeparts->len; i++) {
		SondTVFMItem* stvfm_item_child = NULL;
		GMimeContentType* mime_type = NULL;
		gchar const* mime_string = NULL;
		GMimeObject* mime_child = NULL;
		gchar* path = NULL;
		gchar* base = NULL;
		gchar const* filename = NULL;
		SondTVFMItemPrivate* stvfm_item_child_priv = NULL;

		mime_child = g_ptr_array_index(arr_mimeparts, i);

		mime_type = g_mime_object_get_content_type(
				mime_child);
		mime_string = g_mime_content_type_get_mime_type(mime_type);

		base = g_strdup_printf("%u", i);
		path = g_strconcat(stvfm_item_priv->path_or_section ?
				stvfm_item_priv->path_or_section : "",
				stvfm_item_priv->path_or_section ? "/" : "",
				base, NULL);
		g_free(base);

		if (GMIME_IS_MULTIPART(mime_child))
			stvfm_item_child =
					sond_tvfm_item_create(stvfm_item_priv->stvfm,
							stvfm_item_priv->sond_file_part, path);
		else {
			SondFilePart* sfp_child = NULL;
			GMimeContentDisposition* disp = NULL;
			gboolean is_attachment = FALSE;

			/* Content-Disposition unabhängig vom konkreten GMime-Typ
			 * einheitlich einmal lesen, damit Attachment/Inline im Baum
			 * unterscheidbar sind (der Disposition-WERT "attachment"/
			 * "inline" steht nur hier, nicht schon in
			 * g_mime_part_get_filename()). */
			disp = g_mime_object_get_content_disposition(mime_child);
			if (disp) {
				gchar const* dval = g_mime_content_disposition_get_disposition(disp);

				is_attachment = dval &&
						!g_ascii_strcasecmp(dval, "attachment");
			}

			if (GMIME_IS_PART(mime_child))
				filename = g_mime_part_get_filename(GMIME_PART(mime_child));
			else if (disp) //GMimeMessagepart
				filename = g_mime_content_disposition_get_parameter(disp, "filename");

			sfp_child = sond_file_part_is_open(stvfm_item_priv->sond_file_part, path);
			//deklarierter Typ ohne Aussage: Inhalt untersuchen
			if (!sfp_child && !g_strcmp0(mime_string, "application/octet-stream")) {
				GError* error_sniff = NULL;

				sfp_child = sond_file_part_create(
						stvfm_item_priv->sond_file_part, path, &error_sniff);
				if (!sfp_child) {
					LOG_WARN("%s: Typ von '%s' nicht ermittelbar: %s", __func__,
							path, error_sniff ? error_sniff->message : "?");
					g_clear_error(&error_sniff);
				}
			}
			if (!sfp_child)
				sfp_child = sond_file_part_create_from_mime_type(path,
						stvfm_item_priv->sond_file_part, mime_string);
			sond_file_part_set_is_attachment(sfp_child, is_attachment);

			stvfm_item_child = sond_tvfm_item_create(stvfm_item_priv->stvfm,
						sfp_child, NULL);
			g_object_unref(sfp_child);
		}

		g_free(path);

		stvfm_item_child_priv =
				sond_tvfm_item_get_instance_private(stvfm_item_child);
		g_free(stvfm_item_child_priv->display_name);
		stvfm_item_child_priv->display_name = filename ?
				g_strdup(filename) : g_strdup(mime_string);

		g_ptr_array_add(*arr_children, stvfm_item_child);
	}

	g_ptr_array_unref(arr_mimeparts);

	return 0;
}

gint sond_tvfm_item_load_children(SondTVFMItem* stvfm_item,
		GPtrArray** arr_children, SondTVFMProgress* progress, GError** error) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	g_return_val_if_fail(arr_children, -1);

	*arr_children = NULL; //bei Fehler nie gesetzt

	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR) {
		gint rc = 0;

		//untergliedern: dir in FileSystem, zip-Archiv oder GMessage
		if (stvfm_item_priv->sond_file_part == NULL) //FileSystem
			rc = sond_tvfm_item_load_fs_dir(stvfm_item, arr_children, progress, error);
		else if(SOND_IS_FILE_PART_ZIP(stvfm_item_priv->sond_file_part))
			rc = sond_tvfm_item_load_zip_dir(stvfm_item, arr_children, progress, error);
		else if (SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part))
			rc = sond_tvfm_item_load_pdf_dir(stvfm_item, arr_children, progress, error);
		else if(SOND_IS_FILE_PART_GMESSAGE(stvfm_item_priv->sond_file_part))
			rc = sond_tvfm_item_load_gmessage_dir(stvfm_item, arr_children, progress, error);

		if (rc)
			return -1;
	}
	else if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF ||
			stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION) {
		if (sond_tvfm_owner_supports_load_sections(stvfm_item_priv->owner)) {
			gint rc = 0;

			rc = sond_tvfm_owner_load_sections(stvfm_item_priv->owner, stvfm_item,
					arr_children, error);
			if (rc)
				return -1;
		}
		else {
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
					"%s\nKeine Kinder vorhanden", __func__);

			return -1;
		}
	}

	//Aufrufer erwarten bei Erfolg immer ein Array (ggf. leer)
	if (!*arr_children)
		*arr_children = g_ptr_array_new_with_free_func(
				(GDestroyNotify) g_object_unref);

	return 0;
}

gint sond_tvfm_item_rename(SondTVFMItem* stvfm_item,
		SondTVFMItem* stvfm_item_parent, gchar const* base_new,
		GError** error) {
	g_autofree gchar* path_new = NULL;

	SondTVFMItemPrivate* stvfm_item_parent_priv =
			sond_tvfm_item_get_instance_private(stvfm_item_parent);
	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	path_new = g_strconcat((stvfm_item_parent_priv->path_or_section) ?
				stvfm_item_parent_priv->path_or_section : "",
				(stvfm_item_parent_priv->path_or_section) ? "/" : "",
						base_new, NULL);

	if (!stvfm_item_priv->path_or_section) {
		gint rc = 0;
		rc = sond_file_part_rename(stvfm_item_priv->sond_file_part,
				path_new, base_new, error);
		if (rc)
			return -1;
	}
	else { //richtiges Verzeichnis, nicht LEAF oder Root-Dir (=LEAF)
		//Normale Dateien
		if (!stvfm_item_priv->sond_file_part) {
			//->path_or_section immer != NULL, wenn nicht root-dir
			//absolute Pfade: relative hingen am Arbeitsverzeichnis des Prozesses
			gchar const *root = sond_tvfm_owner_get_root(stvfm_item_priv->owner);
			g_autofree gchar *abs_old = g_strconcat(root, "/",
					stvfm_item_priv->path_or_section, NULL);
			g_autofree gchar *abs_new = g_strconcat(root, "/", path_new, NULL);

			//sond_rename() überschreibt ein vorhandenes Ziel: hier nie
			if (sond_rename_target_taken(abs_old, abs_new)) {
				g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
						"\"%s\" existiert bereits", path_new);

				return -1;
			}

			if (!sond_rename(abs_old, abs_new, error))
				return -1;

			//path_new ist der volle neue Pfad (Eltern + Name)
			g_free(stvfm_item_priv->path_or_section);
			stvfm_item_priv->path_or_section = g_strdup(path_new);
		}
		else if (SOND_IS_FILE_PART_ZIP(stvfm_item_priv->sond_file_part)) {
			//nur innerhalb desselben Archivs
			if (stvfm_item_parent_priv->sond_file_part !=
					stvfm_item_priv->sond_file_part) {
				if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
						"%s\nVerzeichnis kann nur innerhalb desselben "
						"ZIP-Archivs umbenannt bzw. verschoben werden", __func__);

				return -1;
			}

			if (sond_file_part_zip_rename_dir(
					SOND_FILE_PART_ZIP(stvfm_item_priv->sond_file_part),
					stvfm_item_priv->path_or_section, path_new, error))
				return -1;

			g_free(stvfm_item_priv->path_or_section);
			stvfm_item_priv->path_or_section = g_strdup(path_new);
		}
		else if (SOND_IS_FILE_PART_GMESSAGE(stvfm_item_priv->sond_file_part)) {
			//ToDo: Multipart umbenennen
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
					"%s\nrename GMimeMultipart noch nicht implementiert", __func__);

			return -1;
		}
		//was anderes?
		else {
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
					"%s\nNicht implementiert", __func__);

			return -1;
		}
	}

	return 0;
}

gint sond_tvfm_item_delete(SondTVFMItem* stvfm_item, GError** error) {
	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR) {
		if (!stvfm_item_priv->sond_file_part) { //FileSystem - geht schon
			gboolean res = FALSE;
			gchar* path = NULL;
			gchar const *root = sond_tvfm_owner_get_root(stvfm_item_priv->owner);

			if (!stvfm_item_priv->path_or_section)
				return 0; //Root-Verzeichnis kann nicht gelöscht werden!

			path = g_strconcat(root, "/", stvfm_item_priv->path_or_section, NULL);

			res = sond_rmdir_r(path, error);
			g_free(path);
			if (!res)
				return -1;
		}
		else if (!stvfm_item_priv->path_or_section) { //sfp existiert - also root-Element
			gint rc = 0;

			rc = sond_file_part_delete(stvfm_item_priv->sond_file_part, error);
			if (rc) {
				return -1;
			}
		}
		else if (SOND_IS_FILE_PART_ZIP(stvfm_item_priv->sond_file_part)) {
			g_set_error(error, SOND_ERROR, 0, "%s\nnicht implementiert", __func__);

			return -1;
		}
		else if (SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part)) { //PDF-Datei ist Dir - ganz löschen
			g_set_error(error, SOND_ERROR, 0, "%s\nnicht implementiert", __func__);

			return -1;
		}
		else if (SOND_IS_FILE_PART_GMESSAGE(stvfm_item_priv->sond_file_part)) {
			g_set_error(error, SOND_ERROR, 0, "%s\nnicht implementiert", __func__);

			return -1;
		}
	}
	else if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {
		gint rc = 0;

		if (SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part) &&
				sond_file_part_get_has_children(stvfm_item_priv->sond_file_part)) {
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
					"Löschen des Pagetree aus PDF-Datei nicht unterstützt");

			return -1;
		}
		else if (SOND_IS_FILE_PART_GMESSAGE(stvfm_item_priv->sond_file_part)) {
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"EMail-Leaf-Eintrag kann nicht gelöscht werden");

			return -1;
		}

		rc = sond_file_part_delete(stvfm_item_priv->sond_file_part, error);
		if (rc)
			return -1;
	}
	else if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION) {
		if (sond_tvfm_owner_supports_delete_section(stvfm_item_priv->owner)) {
			gint rc = 0;

			rc = sond_tvfm_owner_delete_section(stvfm_item_priv->owner, stvfm_item, error);
			if (rc)
				return -1;
		}
		else
			return 0;
	}

	return 0;
}

/* Kopiert ein Verzeichnis aus einem Container (ZIP/PDF-Ordner/GMessage-
 * Multipart) rekursiv in das echte Dateisystem. path_dst_rel: Zielpfad
 * relativ zur Projektwurzel (ohne führendes '/') - wird hier angelegt.
 *
 * Eingebettete Dateien, die selbst wieder ein Container sind
 * (verschachtelte ZIP/PDF/E-Mail), werden als EINE Datei kopiert (ihre
 * rohen Bytes über sond_file_part_copy(), wie beim normalen Datei-Kopieren
 * aus einem Container ins Filesystem) - NICHT in ihre interne Struktur
 * (PageTree, Anhänge etc.) aufgelöst; das entspricht dem, was der Nutzer
 * beim Herauskopieren erwartet. Kriterium
 * dafür: ein Kind zählt nur dann als "echtes" Unterverzeichnis desselben
 * Archivs (und wird rekursiv weiter aufgeschlüsselt), wenn es denselben
 * SondFilePart wie der Quellknoten trägt (s. sond_tvfm_item_load_zip_dir():
 * Unterverzeichnisse im selben ZIP bekommen den identischen SondFilePart,
 * nur mit anderem path_or_section; eine eingebettete Datei bekommt dagegen
 * immer einen NEUEN, eigenen SondFilePart). */
static gint copy_container_dir_to_fs(SondTVFMItem* stvfm_item_src,
		gchar const* path_dst_rel, GError** error) {
	SondTVFMItemPrivate* stvfm_item_src_priv =
			sond_tvfm_item_get_instance_private(stvfm_item_src);
	gchar const *root = sond_tvfm_owner_get_root(stvfm_item_src_priv->owner);
	gchar* path_dst_real = NULL;
	GPtrArray* arr_children = NULL;
	GHashTable* used_names = NULL;
	gint rc = 0;

	path_dst_real = g_strconcat(root, "/", path_dst_rel, NULL);
	rc = sond_mkdir(path_dst_real, error) ? 0 : -1;
	g_free(path_dst_real);
	if (rc)
		return -1;

	rc = sond_tvfm_item_load_children(stvfm_item_src, &arr_children, NULL, error);
	if (rc)
		return -1;

	//bereits vergebene Namen in diesem Ordner (klein geschrieben)
	used_names = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	for (guint i = 0; i < arr_children->len; i++) {
		SondTVFMItem* child = g_ptr_array_index(arr_children, i);
		SondTVFMItemPrivate* child_priv =
				sond_tvfm_item_get_instance_private(child);
		gchar const* child_base = sond_tvfm_item_get_display_name(child);
		gchar* child_path_dst_rel = NULL;
		gchar* child_base_clean = NULL;
		gchar* child_name = NULL;
		gboolean is_real_subdir = FALSE;

		//wegen fehlenden Passworts übersprungener Eintrag (Vorab-Phase)
		if (child_priv->sond_file_part && !child_priv->path_or_section) {
			g_autofree gchar* filepart_skip =
					sond_file_part_get_filepart(child_priv->sond_file_part);

			if (sond_password_action_is_skipped(filepart_skip))
				continue;
		}

		/* Sonderfall PDF/"PageTree" bzw. GMessage/"Message": dieser
		 * synthetische Pseudo-Knoten trägt denselben SondFilePart wie der
		 * "Ordner" selbst (die PDF/E-Mail-Datei), ist aber vom Typ LEAF,
		 * nicht DIR (s. sond_tvfm_item_create()) - er steht für den
		 * Inhalt der Container-Datei SELBST, nicht für eine eigene
		 * Kind-Datei. Für ZIP kommt das nie vor (ZIP-Items sind immer
		 * DIR). Kopieren als "Datei" würde hier fälschlich noch einmal
		 * die gesamte PDF/E-Mail-Datei unter dem Namen 'PageTree'/
		 * 'Message' duplizieren - daher hier klar als (noch) nicht
		 * unterstützt abgebrochen, statt ein falsches Ergebnis zu
		 * erzeugen. */
		if (child_priv->sond_file_part == stvfm_item_src_priv->sond_file_part
				&& child_priv->type != SOND_TVFM_ITEM_TYPE_DIR) {
			g_set_error(error, SOND_ERROR, 0,
					"%s\nKopieren eines PDF-/E-Mail-'Ordners' (mit "
					"eigenem Inhalt PageTree/Message) in das Dateisystem "
					"noch nicht implementiert - bitte die Datei selbst "
					"kopieren", __func__);
			rc = -1;
			break;
		}

		is_real_subdir = (child_priv->type == SOND_TVFM_ITEM_TYPE_DIR)
				&& (child_priv->sond_file_part == stvfm_item_src_priv->sond_file_part);

		//Zielname: Sonderzeichen ersetzen, Kollisionen (auch nur durch
		//Groß-/Kleinschreibung) mit " (n)" auflösen
		child_base_clean = sond_sanitize_filename(child_base);
		child_name = g_strdup(child_base_clean);
		for (guint n = 1; ; n++) {
			gchar* key = g_utf8_strdown(child_name, -1);

			//Schlüssel gehört danach der Hashtabelle (auch bei FALSE freigegeben)
			if (g_hash_table_add(used_names, key))
				break;

			g_free(child_name);
			child_name = sond_filename_add_counter(child_base_clean, n,
					!is_real_subdir);
		}
		g_free(child_base_clean);

		child_path_dst_rel = g_strconcat(path_dst_rel, "/", child_name, NULL);
		g_free(child_name);

		if (is_real_subdir)
			rc = copy_container_dir_to_fs(child, child_path_dst_rel, error);
		else
			//Datei (auch: eingebetteter Container) - als Ganzes/1:1 kopieren
			rc = sond_file_part_copy(child_priv->sond_file_part, NULL,
					child_path_dst_rel, error);

		g_free(child_path_dst_rel);
		if (rc)
			break;
	}

	g_hash_table_unref(used_names);
	g_ptr_array_unref(arr_children);

	return rc;
}

static gint copy_dir_across_sfps(SondTVFMItem* stvfm_item,
		SondTVFMItem* stvfm_item_parent, gchar const* base,
		GError** error) {
	SondTVFMItemPrivate* stvfm_item_parent_priv =
			sond_tvfm_item_get_instance_private(stvfm_item_parent);
	gchar* path_dst_rel = NULL;
	gint rc = 0;

	if (stvfm_item_parent_priv->sond_file_part) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
			"%s\nKopieren eines Verzeichnisses aus einem Container in ein "
				"anderes Container-Ziel (nur Kopieren in das Dateisystem "
				"ist implementiert) noch nicht implementiert", __func__);

		return -1;
	}

	path_dst_rel = stvfm_item_parent_priv->path_or_section ?
			g_strconcat(stvfm_item_parent_priv->path_or_section, "/", base, NULL) :
			g_strdup(base);

	rc = copy_container_dir_to_fs(stvfm_item, path_dst_rel, error);
	g_free(path_dst_rel);

	return rc;
}

gint sond_tvfm_item_copy(SondTVFMItem* stvfm_item,
		SondTVFMItem* stvfm_item_parent, gchar const* base,
		gint index_to, GError** error) {
	gint rc = 0;
	gchar* base_real = NULL;

	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);
	SondTVFMItemPrivate* stvfm_item_parent_priv =
			sond_tvfm_item_get_instance_private(stvfm_item_parent);

	//Abschnitte (Anbindungen) sind keine Dateien oder Verzeichnisse
	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION) {
		g_set_error(error, SOND_ERROR, 0,
				"Abschnitte (Anbindungen) lassen sich nicht kopieren oder "
				"verschieben");

		return -1;
	}

	base_real = (stvfm_item_parent_priv->sond_file_part &&
			SOND_IS_FILE_PART_GMESSAGE(stvfm_item_parent_priv->sond_file_part)) ?
					g_strdup_printf("%u", index_to) :
					g_strdup(base);

	//sfp soll kopiert werden
	if (!stvfm_item_priv->path_or_section) {
		gchar* path = NULL;

		//Page-Tree einer PDF-Datei: geht (noch) nicht
		if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {
			if (SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part) &&
				sond_file_part_get_has_children(
						stvfm_item_priv->sond_file_part)) {
				if (error) *error = g_error_new(SOND_ERROR, 0,
						"%s\nKopieren des Page-Tree aus PDF-Dateien nicht implementiert",
						__func__);
				g_free(base_real);

				return -1;
			}
			else if (SOND_IS_FILE_PART_GMESSAGE(stvfm_item_priv->sond_file_part)) {
				g_set_error(error, SOND_ERROR, 0, "Kopieren des Email-Leaf-Eintrags nicht zulässig");
				g_free(base_real);

				return -1;
			}
		}

		//Wenn in ein dir kopiert wird, ist der Pfad des dir vom übergeordneten sfp
		//der Beginn des neuen Pfades des sfp
		path = stvfm_item_parent_priv->path_or_section ?
				g_strconcat(stvfm_item_parent_priv->path_or_section, "/", base_real, NULL) :
				g_strdup(base_real);
		g_free(base_real);

		rc = sond_file_part_copy(stvfm_item_priv->sond_file_part,
				stvfm_item_parent_priv->sond_file_part, path, error);
		g_free(path);
	}
	else { //dir soll kopiert werden
		//innerhalt Dateisystem - geht schon
		if (!stvfm_item_parent_priv->sond_file_part &&
				!stvfm_item_priv->sond_file_part) {
			gchar* path_dst = NULL;
			gchar* abs_src = NULL;
			gchar const* root = sond_tvfm_owner_get_root(stvfm_item_priv->owner);

			//absolute Pfade: das Arbeitsverzeichnis ist nicht die Projektwurzel
			path_dst = g_strconcat(root, "/",
							(stvfm_item_parent_priv->path_or_section) ?
							stvfm_item_parent_priv->path_or_section : "",
							(stvfm_item_parent_priv->path_or_section) ? "/" : "",
							base_real, NULL);
			g_free(base_real);
			abs_src = g_strconcat(root, "/", stvfm_item_priv->path_or_section, NULL);

			rc = sond_copy_r(abs_src, path_dst, FALSE, error);
			g_free(abs_src);
			g_free(path_dst);
		}
		else { //kopieren Verzeichnis zwischen zwei sfp-Welten
			rc = copy_dir_across_sfps(stvfm_item, stvfm_item_parent,
					base_real, error);
			g_free(base_real);
		}
	}
	if (rc)
		return -1;

	return 0;
}

static gint move_item(SondTVFMItem* stvfm_item_src,
		SondTVFMItem* stvfm_item_parent_dst,
		gchar const* base, gint index_to, GError** error) {
	gint rc = 0;

	rc = sond_tvfm_item_copy(stvfm_item_src,
			stvfm_item_parent_dst, base, index_to, error);
	if (rc)
		return -1;

	//Jetzt Quelle löschen
	rc = sond_tvfm_item_delete(stvfm_item_src, error);
	if (rc) {
		/* Nicht mehr nur loggen und Erfolg vortäuschen: Kopie liegt zwar
		 * schon am Ziel, aber die Quelle konnte nicht entfernt werden -
		 * das muß dem Nutzer gemeldet werden (sonst Datei/Verzeichnis
		 * unbemerkt doppelt vorhanden). Ursprüngliche Fehlermeldung von
		 * sond_tvfm_item_delete() in die neue GError-Meldung übernehmen. */
		g_autofree gchar *msg_delete = (error && *error) ?
				g_strdup((*error)->message) : NULL;
		g_clear_error(error);

		g_set_error(error, SOND_ERROR, 0,
				"Kopiert, aber am Ursprungsort konnte nicht gelöscht werden"
				"%s%s", msg_delete ? ":\n" : "", msg_delete ? msg_delete : "");

		return -1;
	}

	return 0;
}

gint sond_tvfm_item_move(SondTVFMItem* stvfm_item,
		SondTVFMItem* stvfm_item_parent, gchar const* base,
		gint index_to, GError** error) {
	gint rc = 0;
	gint res = 0;
	gboolean in_place = FALSE;
	gpointer ctx = NULL;

	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);
	SondTVFMItemPrivate* stvfm_item_parent_priv =
			sond_tvfm_item_get_instance_private(stvfm_item_parent);

	//vor den Signalen (diese öffnen Transaktionen): Abschnitte nicht verschiebbar
	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION) {
		g_set_error(error, SOND_ERROR, 0,
				"Abschnitte (Anbindungen) lassen sich nicht kopieren oder "
				"verschieben");

		return -1;
	}

	if (stvfm_item_priv->owner == stvfm_item_parent_priv->owner)
		res = sond_tvfm_owner_before_move(stvfm_item_priv->owner, stvfm_item,
				stvfm_item_parent, base, index_to, error, &ctx);
	else {
		res = sond_tvfm_owner_before_delete(stvfm_item_priv->owner, stvfm_item,
				error, &ctx);
		if (res == -1)
			return -1;
		else if (res == 1)
			return 1;

		res = sond_tvfm_owner_before_insert(stvfm_item_parent_priv->owner,
				stvfm_item, stvfm_item_parent, base, index_to, error);
	}

	if (res)
		return -1;

	//ZIP-Verzeichnis: trägt das sfp des Archivs selbst, nicht eines eigenen
	//Eintrags - Umbenennen, wenn das Ziel im selben Archiv liegt
	if (stvfm_item_priv->path_or_section && stvfm_item_priv->sond_file_part &&
			SOND_IS_FILE_PART_ZIP(stvfm_item_priv->sond_file_part))
		in_place = (stvfm_item_parent_priv->sond_file_part ==
				stvfm_item_priv->sond_file_part);
	//Verschieben innerhalb des gleichen sfp, das aber nicht GMessage ist
	else
		in_place = stvfm_item_parent_priv->sond_file_part ==
				sond_file_part_get_parent(stvfm_item_priv->sond_file_part) &&
				!SOND_IS_FILE_PART_GMESSAGE(stvfm_item_parent_priv->sond_file_part) &&
				//außer wenn PageTree
				!(stvfm_item_priv->sond_file_part &&
						SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part) &&
						sond_file_part_get_has_children(stvfm_item_priv->sond_file_part));

	if (in_place)
		rc = sond_tvfm_item_rename(stvfm_item, stvfm_item_parent, base, error);
	else
		rc = move_item(stvfm_item, stvfm_item_parent, base, index_to, error);

	sond_tvfm_owner_after(stvfm_item_parent_priv->owner, (rc == 0) ? TRUE : FALSE, ctx);
	if (rc)
		return -1;

	return 0;
}

/* ------------------------------------------------------------------------
 * Namen prüfen, geöffnete Dateien nachführen, Verzeichnis anlegen
 * (aus sond_treeviewfm.c herausgelöst: reine Dateilogik ohne Baum)
 * --------------------------------------------------------------------- */

gboolean sond_tvfm_item_is_valid_filename(const gchar *filename) {
	g_autofree gchar *sanitized = NULL;

	if (filename == NULL || *filename == '\0')
		return FALSE;

	//zulässig ist nur, was sond_sanitize_filename() unverändert lässt
	sanitized = sond_sanitize_filename(filename);

	return !g_strcmp0(sanitized, filename);
}

void sond_tvfm_item_adjust_sfps_in_dir(SondFilePart* sfp_dir, SondFilePart* sfp_dst,
		gchar const* path_old, gchar const* path_new) {
	GPtrArray* arr_opened_children = NULL;

	arr_opened_children = sond_file_part_get_arr_opened_files(sfp_dir); //NULL ist ok

	//welche liegen "unterhalb"?
	for (guint i = 0; arr_opened_children && i < arr_opened_children->len; i++) {
		SondFilePart* sfp_child = NULL;
		gchar const* path_child = NULL;

		sfp_child = g_ptr_array_index(arr_opened_children, i);
		path_child = sond_file_part_get_path(sfp_child);

		//Treffer nur unterhalb von path_old ("sub" nicht für "sub38/...")
		if (g_str_has_prefix(path_child, path_old) &&
				path_child[strlen(path_old)] == '/') {
			//ggf. neues Eltern-sfp
			if (sfp_dir != sfp_dst)
				sond_file_part_set_parent(sfp_child, sfp_dst);

			//Pfad von sfp_child anpassen
			gchar* path_child_new = NULL;

			//stvfm_item_priv->path_or_section ist der neue Pfad des Verzeichnisses
			//kann nicht NULL sein, ist mindestens toplevel_path!
			path_child_new = g_strconcat(path_new, "/",
					path_child + ((path_old) ? strlen(path_old) + 1 : 0), NULL);
			sond_file_part_set_path(sfp_child, path_child_new);
			g_free(path_child_new);
		}
	}

	return;
}

static gint insert_dir_in_fs(SondTVFMItemPrivate* stvfm_item_priv,
		gchar** path, GError** error) {
	guint max_tries = 100;

	for (guint i = 0; i <= max_tries; i++) {
		gboolean suc = FALSE;
		g_autofree gchar *trial_path = NULL;

		if (i == 0)
			trial_path = (stvfm_item_priv->path_or_section) ?
					g_strconcat(stvfm_item_priv->path_or_section,
							"/Neues Verzeichnis", NULL) : g_strdup("Neues Verzeichnis");
		else trial_path = (stvfm_item_priv->path_or_section) ?
				g_strdup_printf("%s/Neues Verzeichnis (%u)", stvfm_item_priv->path_or_section, i) :
				g_strdup_printf("Neues Verzeichnis (%u)", i);

		//absoluter Pfad: ein relativer hinge am Arbeitsverzeichnis des Prozesses
		{
			g_autofree gchar *abs_path = g_strconcat(
					sond_tvfm_owner_get_root(stvfm_item_priv->owner), "/",
					trial_path, NULL);

			suc = sond_mkdir(abs_path, error);
		}
		if (suc) {
			*path = g_strdup(trial_path);

			return 0;
		}

		if (!suc) {
			if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
				g_clear_error(error);
				continue;  // nächster Suffix versuchen
			}
			else
				return -1;
		}
	}

	g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
				"Kein eindeutiger Zielname nach %u Versuchen", max_tries);

	return -1;
}

static gint insert_dir_in_zip(SondTVFMItemPrivate* stvfm_item_priv, gboolean child,
		gchar** path, GError** error) {
	return sond_file_part_zip_mkdir(
			SOND_FILE_PART_ZIP(stvfm_item_priv->sond_file_part),
			stvfm_item_priv->path_or_section, "Neues Verzeichnis", path, error) ?
			-1 : 0;
}

/* Legt ein neues Verzeichnis im Verzeichnis parent an (Dateisystem oder ZIP). *path
 * erhält den Pfad des neuen Verzeichnisses (g_free()). 0 bei Erfolg, -1 bei Fehler. */
gint sond_tvfm_item_mkdir(SondTVFMItem *parent, gboolean child, gchar **path,
		GError **error) {
	SondTVFMItemPrivate *priv = sond_tvfm_item_get_instance_private(parent);

	if (!priv->sond_file_part)
		return insert_dir_in_fs(priv, path, error);
	else if (SOND_IS_FILE_PART_ZIP(priv->sond_file_part))
		return insert_dir_in_zip(priv, child, path, error);

	//PDF, E-Mail: hier lassen sich keine Verzeichnisse anlegen
	g_set_error(error, g_quark_from_static_string("sond"), 0,
			"%s\nIn dieser Datei kann kein Verzeichnis angelegt werden",
			__func__);

	return -1;
}

/* ------------------------------------------------------------------------
 * Dateisystem-Angaben eines Eintrags (aus sond_treeviewfm.c herausgelöst)
 * --------------------------------------------------------------------- */

gboolean sond_tvfm_item_priv_is_fs_entry(SondTVFMItemPrivate *item_priv) {
	if (!item_priv->sond_file_part) //Verzeichnis (oder Wurzel ohne Pfad)
		return item_priv->path_or_section != NULL;

	return !item_priv->path_or_section
			&& !sond_file_part_get_parent(item_priv->sond_file_part);
}

/* Änderungszeit, Größe und Verzeichnis-Kennzeichen des Eintrags. Meist schon
 * beim Einlesen des Verzeichnisses gemerkt; sonst (z.B. nach dem Einfügen)
 * einmal nachgeholt. FALSE: kein Dateisystem-Eintrag oder nicht lesbar. */
gboolean sond_tvfm_item_priv_fs_stat(SondTVFMItemPrivate *item_priv,
		gint64 *mtime, gint64 *size, gboolean *is_dir) {
	if (!sond_tvfm_item_priv_is_fs_entry(item_priv))
		return FALSE;

	if (!item_priv->stat_done) {
		gchar const *root = sond_tvfm_owner_get_root(item_priv->owner);
		gchar const *rel = item_priv->sond_file_part ?
				sond_file_part_get_path(item_priv->sond_file_part) :
				item_priv->path_or_section;
		gchar *full = NULL;
		GStatBuf st = { 0 };

		if (!root || !rel)
			return FALSE;

		full = g_strconcat(root, "/", rel, NULL);
		if (sond_stat(full, &st, NULL)) {
			g_free(full);

			return FALSE;
		}
		g_free(full);

		item_priv->mtime = (gint64) st.st_mtime;
		item_priv->size = (gint64) st.st_size;
		item_priv->is_dir = S_ISDIR(st.st_mode) ? TRUE : FALSE;
		item_priv->stat_done = TRUE;
	}

	if (mtime) *mtime = item_priv->mtime;
	if (size) *size = item_priv->size;
	if (is_dir) *is_dir = item_priv->is_dir;

	return TRUE;
}

gboolean sond_tvfm_item_fs_stat(SondTVFMItem *item, gint64 *mtime, gint64 *size,
		gboolean *is_dir) {
	return sond_tvfm_item_priv_fs_stat(sond_tvfm_item_get_instance_private(item),
			mtime, size, is_dir);
}

gboolean sond_tvfm_item_fs_path(SondTVFMItem *item, gchar **rel_path,
		gboolean *is_dir) {
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_instance_private(item);
	gboolean dir = FALSE;
	gchar const *rel = NULL;

	if (!sond_tvfm_item_priv_fs_stat(item_priv, NULL, NULL, &dir))
		return FALSE;

	rel = item_priv->sond_file_part ?
			sond_file_part_get_path(item_priv->sond_file_part) :
			item_priv->path_or_section;
	if (!rel)
		return FALSE;

	if (rel_path)
		*rel_path = g_strdup(rel);
	if (is_dir)
		*is_dir = dir;

	return TRUE;
}
