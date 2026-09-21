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
 * _move(), _delete() (früher delete_item()) und _get_fileparts() werden
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

#include "misc.h"
#include "misc_stdlib.h"
#include "sond_log_and_error.h"
#include "sond_fileparts.h"
#include "sond_file_helper.h"
#include "sond_mime.h"
#include "sond_treeviewfm.h"
#include "sond_treeviewfm_private.h"

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

	klass->load_sections = NULL;

	return;
}

static void sond_tvfm_item_init(SondTVFMItem *self) {

	return;
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

SondFilePart* sond_tvfm_item_get_sond_file_part(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->sond_file_part;
}

SondTreeviewFM* sond_tvfm_item_get_stvfm(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->stvfm;
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
        return g_strdup("text-x-generic");

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
        {"text/xml", "text-xml"},

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

SondTVFMItem* sond_tvfm_item_create(SondTreeviewFM* stvfm,
		SondFilePart *sond_file_part, gchar const* path_or_section) {
	SondTVFMItem *stvfm_item = NULL;
	SondTVFMItemPrivate *stvfm_item_priv = NULL;

	stvfm_item = g_object_new(SOND_TYPE_TVFM_ITEM, NULL);
	stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	stvfm_item_priv->stvfm = stvfm;
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
							sond_treeviewfm_get_root(sond_tvfm_item_get_stvfm(stvfm_item)),
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

			/* Nutzer-Fund 16.09.2026: für path_or_section == NULL (das
			 * ZIP-File selbst, noch nicht hineinexpandiert) NICHT
			 * load_zip_dir() aufrufen - das erzwingt über
			 * sond_file_part_zip_list_dir() beim allerersten Zugriff auf
			 * dieses Archiv den kompletten dir_index-Aufbau (ALLE
			 * Einträge, alle Ebenen, s. sfp_zip_build_dir_index()), nur
			 * um zu prüfen ob überhaupt ein Eintrag existiert. Bei
			 * großen Archiven (mehrere Tausend Einträge) macht allein
			 * das bloße AUFLISTEN eines Verzeichnisses mit mehreren
			 * ZIP-Dateien darin (noch ohne sie zu öffnen) spürbar Zeit
			 * aus. Stattdessen die von sond_file_part_zip_test_for_files()
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
				/* Nutzer-Fund 16.09.2026: Rückgabewert ist -1 (Fehler), 0
				 * (keine Kinder) oder 1 (Kinder) - der frühere Vergleich
				 * "? TRUE : FALSE" wertete auch -1 (Fehler, z.B. defektes/
				 * pfadloses sond_file_part nach fehlerhafter Kopie aus
				 * einem Container) fälschlich als "hat Kinder", wodurch ein
				 * Dummy-Kind eingefügt wurde, obwohl das Verzeichnis beim
				 * echten Aufklappen dann mit einer Fehlermeldung
				 * fehlschlägt. Jetzt: nur 1 zählt als "hat Kinder". */
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
		if (SOND_TREEVIEWFM_GET_CLASS(stvfm)->has_sections)
			stvfm_item_priv->has_children =
					SOND_TREEVIEWFM_GET_CLASS(stvfm)->has_sections(stvfm_item);
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
	SondTreeviewFMPrivate* stvfm_priv =
			sond_treeviewfm_get_priv(stvfm_item_priv->stvfm);

	path_dir = g_strconcat(stvfm_priv->root,
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

		dir_has_children = TRUE;

		if (!arr_children) //Es gibt Eintrag - reicht
    		break;

		if (stvfm_item_priv->path_or_section)
			rel_path_child = g_strconcat(stvfm_item_priv->path_or_section, "/",
					filename, NULL);
		else rel_path_child = g_strdup(filename);

		if (sond_stat(rel_path_child, &st, error)) {
			LOG_WARN("g_stat(%s) gibt Fehler zurück: %s", rel_path_child, (*error)->message);
			g_clear_error(error);
			g_free(rel_path_child);

			continue;
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

			full_path = g_strconcat(stvfm_priv->root, "/", rel_path_child, NULL);

#ifdef _WIN32
			/* Bei SeaDrive: offline-Dateien nicht lesen - nur Extension-basierter MIME-Typ */
			if (stvfm_priv->is_seadrive_path) {
				wchar_t *lp = prepare_long_path(full_path, NULL);
				if (lp) {
					DWORD attrs = GetFileAttributesW(lp);
					g_free(lp);
					if (attrs != INVALID_FILE_ATTRIBUTES &&
							(attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS))
						mime_from_ext = mime_from_extension(filename);
				}
			}
#endif

			if (mime_from_ext) {
				/* Datei offline - SondFilePartLeaf erzwingen, kein Öffnen */
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
				return -1;
			}

			stvfm_item_child = sond_tvfm_item_create(stvfm_item_priv->stvfm,
					sfp_child, NULL);
			g_object_unref(sfp_child);
		}

		g_free(rel_path_child);

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

		/* Nutzer-Fund 16.09.2026: Bei großen Archiven (>30000 Einträge)
		 * lief diese Schleife bisher komplett unbeobachtbar und
		 * unabbrechbar durch (jede Iteration ruft sond_file_part_create()
		 * auf, das per libmagic den MIME-Typ sniffed - je nach Puffergröße
		 * spürbar Zeit pro Eintrag). progress ist rein lesend eingesetzt:
		 * Abbruch hier verwirft nur noch nicht geladene Kinder, keine
		 * Rollback-Problematik (im Unterschied zu SondProcessFileCtx). */
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

			sfp_child = sond_file_part_create(stvfm_item_priv->sond_file_part,
					e->path, error);

			if (!sfp_child) {
				LOG_WARN("SondFilePart konnte nicht erzeugt werden:\n%s",
						(*error)->message);
				g_clear_error(error);

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
	SondTVFMItemPrivate* stvfm_item_priv = NULL;
	SondTVFMItem* stvfm_item_pdf_page_tree = NULL;

	stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	rc = sond_file_part_pdf_load_embedded_files(SOND_FILE_PART_PDF(stvfm_item_priv->sond_file_part),
			&arr_emb_files, error);
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

		g_ptr_array_add(*arr_children, stvfm_item_child);
	}

	g_ptr_array_unref(arr_emb_files);

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

			/* Content-Disposition unabhängig vom konkreten GMime-Typ einmal
			 * einheitlich lesen (Nutzerwunsch 16.09.2026: Attachment/Inline
			 * im Baum unterscheidbar machen) - vorher wurde disp nur im
			 * GMimeMessagePart-Zweig (für den Dateinamen) geholt, im
			 * GMIME_IS_PART-Zweig lieferte das schon g_mime_part_get_filename()
			 * intern mit, der Disposition-WERT selbst ("attachment"/"inline")
			 * aber nirgends. */
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
		if (SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->load_sections) {
			gint rc = 0;

			rc = SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->load_sections(stvfm_item,
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

	return 0;
}

/**
 * Ändert stvfm_item_priv->path_or_section
 */
static void sond_tvfm_item_set_basename(SondTVFMItem* stvfm_item,
		gchar const* new_basename) {
	gchar const* path = NULL;
	gchar const* dir = NULL;
	gchar* path_new = NULL;

	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	if (!stvfm_item_priv->path_or_section)
		LOG_WARN("STVFMItem ist Leaf oder root-dir ('%s')",
				sond_file_part_get_path(stvfm_item_priv->sond_file_part));

	path = stvfm_item_priv->path_or_section;

	dir = strrchr(path, '/');

	if (!dir)
		path_new = g_strdup(new_basename);
	else
		path_new = g_strdup_printf("%.*s/%s", (int)(dir - path), path, new_basename);

	g_free(stvfm_item_priv->path_or_section);
	stvfm_item_priv->path_or_section = g_strdup(path_new);

	return;
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
			if (!sond_rename(stvfm_item_priv->path_or_section, path_new, error))
				return -1;

			sond_tvfm_item_set_basename(stvfm_item, path_new);
		}
		else if (SOND_IS_FILE_PART_ZIP(stvfm_item_priv->sond_file_part)) {
			//ToDo: zip-Verzeichnis-Namen ändern
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
					"%s\nrename zip-dir noch nicht implementiert", __func__);

			return -1;
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
			SondTreeviewFMPrivate* stvfm_priv =
					sond_treeviewfm_get_priv(stvfm_item_priv->stvfm);

			if (!stvfm_item_priv->path_or_section)
				return 0; //Root-Verzeichnis kann nicht gelöscht werden!

			path = g_strconcat(stvfm_priv->root, "/", stvfm_item_priv->path_or_section, NULL);

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
		if (SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->delete_section) {
			gint rc = 0;

			rc = SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->delete_section(stvfm_item, error);
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
 * beim Herauskopieren erwartet (Nutzer-Entscheidung, 16.09.2026). Kriterium
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
	SondTreeviewFMPrivate* stvfm_priv =
			sond_treeviewfm_get_priv(stvfm_item_src_priv->stvfm);
	gchar* path_dst_real = NULL;
	GPtrArray* arr_children = NULL;
	gint rc = 0;

	path_dst_real = g_strconcat(stvfm_priv->root, "/", path_dst_rel, NULL);
	rc = sond_mkdir(path_dst_real, error) ? 0 : -1;
	g_free(path_dst_real);
	if (rc)
		return -1;

	rc = sond_tvfm_item_load_children(stvfm_item_src, &arr_children, NULL, error);
	if (rc)
		return -1;

	for (guint i = 0; i < arr_children->len; i++) {
		SondTVFMItem* child = g_ptr_array_index(arr_children, i);
		SondTVFMItemPrivate* child_priv =
				sond_tvfm_item_get_instance_private(child);
		gchar const* child_base = sond_tvfm_item_get_display_name(child);
		gchar* child_path_dst_rel = NULL;
		gboolean is_real_subdir = FALSE;

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
		 * erzeugen (Nutzeranfrage betraf nur ZIP, 16.09.2026). */
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

		child_path_dst_rel = g_strconcat(path_dst_rel, "/", child_base, NULL);

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

			path_dst = g_strconcat((stvfm_item_parent_priv->path_or_section) ?
							stvfm_item_parent_priv->path_or_section : "",
							(stvfm_item_parent_priv->path_or_section) ? "/" : "",
							base_real, NULL);
			g_free(base_real);

			rc = sond_copy_r(stvfm_item_priv->path_or_section, path_dst, FALSE, error);
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
	gpointer ctx = NULL;

	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);
	SondTVFMItemPrivate* stvfm_item_parent_priv =
			sond_tvfm_item_get_instance_private(stvfm_item_parent);

	if (stvfm_item_priv->stvfm == stvfm_item_parent_priv->stvfm)
		g_signal_emit(stvfm_item_priv->stvfm,
				SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->signal_before_move, 0,
				stvfm_item, stvfm_item_parent, base, index_to, error, &ctx, &res);
	else {
		g_signal_emit(stvfm_item_priv->stvfm,
				SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->signal_before_delete, 0,
				stvfm_item, error, &ctx, &res);
		if (res == -1)
			return -1;
		else if (res == 1)
			return 1;

		g_signal_emit(stvfm_item_parent_priv->stvfm,
				SOND_TREEVIEWFM_GET_CLASS(stvfm_item_parent_priv->stvfm)->signal_before_insert, 0,
				stvfm_item, stvfm_item_parent, base, index_to, error, &res);
	}

	if (res)
		return -1;

	//Verschieben innerhalb des gleichen sfp, das aber nicht GMessage ist
	if (stvfm_item_parent_priv->sond_file_part ==
			sond_file_part_get_parent(stvfm_item_priv->sond_file_part) &&
			!SOND_IS_FILE_PART_GMESSAGE(stvfm_item_parent_priv->sond_file_part) &&
			//außer wenn PageTree
			!(stvfm_item_priv->sond_file_part &&
					SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part) &&
					sond_file_part_get_has_children(stvfm_item_priv->sond_file_part)))
		rc = sond_tvfm_item_rename(stvfm_item, stvfm_item_parent, base, error);
	else
		rc = move_item(stvfm_item, stvfm_item_parent, base, index_to, error);

	g_signal_emit(stvfm_item_parent_priv->stvfm,
			SOND_TREEVIEWFM_GET_CLASS(stvfm_item_parent_priv->stvfm)->signal_after, 0,
			(rc == 0) ? TRUE : FALSE, ctx);
	if (rc)
		return -1;

	return 0;
}

gint sond_tvfm_item_get_fileparts(SondTVFMItem *stvfm_item,
		GHashTable* ht, GError **error) {
	SondTVFMItemPrivate *stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	//"Wirkliches" dir und root-dir - ITEM_TYPE_DIR mit path_or_section != NULL
	//ist ja in echt filepart
	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR &&
			(stvfm_item_priv->path_or_section ||
					!stvfm_item_priv->sond_file_part)) {
		GPtrArray *arr_children = NULL;

		gint rc = 0;

		rc = sond_tvfm_item_load_children(stvfm_item, &arr_children, NULL, error);
		if (rc)
			return -1;

		for (guint i = 0; i < arr_children->len; i++) {
			SondTVFMItem *child = g_ptr_array_index(arr_children, i);

			rc = sond_tvfm_item_get_fileparts(child, ht, error);
			if (rc)
				return -1;
		}
	}
	else
		/* ht wurde mit g_object_unref als key-destroy-func angelegt
		 * (sond_treeviewfm_get_fileparts()) - erwartet also eine eigene
		 * Ref pro Key. sond_file_part gehört sonst dem stvfm_item (dessen
		 * eigene Ref), ohne g_object_ref() hier würde die Hashtable beim
		 * Zerstören eine fremde Ref freigeben und damit ein Objekt, das noch
		 * im Treeview angezeigt wird, vorzeitig finalisieren.
		 * Wert explizit NULL (nicht g_hash_table_add(), das würde value ==
		 * key setzen) - sond_process_fileparts() liest den Wert als
		 * SondPageRange* (NULL == ganze Datei); dieser Baum (BAUM_FS) kennt
		 * keine Anbindungen, hier ist immer die ganze Datei gemeint. */
		g_hash_table_insert(ht, g_object_ref(stvfm_item_priv->sond_file_part), NULL);

	return 0;
}
