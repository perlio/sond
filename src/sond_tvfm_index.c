/*
 sond (sond_tvfm_index.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_tvfm_index.h"

#include <glib.h>

#include "sond_fileparts.h"
#include "sond_mime.h"
#include "sond_tvfm_item_private.h"

gboolean sond_tvfm_file_part_indexable(SondFilePart *sfp,
		gchar const *filepart) {
	gchar const *mime_type = NULL;

	//PDF und E-Mail sind unabhängig von der Endung immer unterstützt
	if (SOND_IS_FILE_PART_PDF(sfp) || SOND_IS_FILE_PART_GMESSAGE(sfp))
		return TRUE;

	mime_type = SOND_IS_FILE_PART_LEAF(sfp) ?
			sond_file_part_leaf_get_mime_type(SOND_FILE_PART_LEAF(sfp)) :
			mime_from_extension(filepart);

	return sond_index_mime_type_supported(mime_type);
}

/* Ermittelt den filepart-Pfad (Datei/Verzeichnis, "//"-Konvention bei
 * eingebetteten Inhalten) für die Indizierungsstatus-Abfrage - unabhängig
 * vom OS-Pfad "rel", den der SeaDrive-Zweig weiter unten für den
 * Dateisystem-Zugriff braucht. NULL, wenn für dieses Item kein sinnvoller
 * Pfad existiert (z.B. eine reine Anbindung/Section ohne eigene Datei). */
static gchar* coverage_path_of(SondTVFMItemPrivate *priv,
		gboolean *out_is_dir) {
	*out_is_dir = FALSE;

	if (priv->type == SOND_TVFM_ITEM_TYPE_LEAF && priv->sond_file_part)
		return sond_file_part_get_filepart(priv->sond_file_part);

	if (priv->type == SOND_TVFM_ITEM_TYPE_DIR) {
		*out_is_dir = TRUE;
		if (!priv->sond_file_part) //DIR im Filesystem
			return g_strdup(priv->path_or_section);
		else if (priv->path_or_section &&
				SOND_IS_FILE_PART_ZIP(priv->sond_file_part)) { //Ordner im ZIP
			g_autofree gchar *zip_fp =
					sond_file_part_get_filepart(priv->sond_file_part);

			return g_strconcat(zip_fp, "//", priv->path_or_section, NULL);
		}
		else //Container (z.B. Zip/GMessage) als "Verzeichnis" dargestellt
			return sond_file_part_get_filepart(priv->sond_file_part);
	}

	return NULL;
}

/* Indizierungsstatus für das Overlay-Icon, oder SOND_INDEX_STATUS_NONE
 * (kein Overlay - s. Absprache: nur teilweise/vollständig werden markiert,
 * sonst zu viel visuelles Rauschen in einem frischen, noch nicht
 * indizierten Projekt). Liefert außerdem NONE, wenn der Punkt gar nicht
 * indizierbar ist (z.B. .db/.znd) - dieselbe Prüfung wie beim
 * Abdeckungs-Check der Indexsuche (check_coverage_one()). */
SondIndexStatus sond_tvfm_item_index_status(SondTVFMItem *stvfm_item,
		SondIndexCtx *index_ctx) {
	SondTVFMItemPrivate *priv = sond_tvfm_item_get_priv(stvfm_item);
	gchar *coverage_path = NULL;
	gboolean is_dir = FALSE;
	SondIndexStatus status = SOND_INDEX_STATUS_NONE;

	if (!index_ctx || priv->type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION)
		return SOND_INDEX_STATUS_NONE;

	coverage_path = coverage_path_of(priv, &is_dir);
	if (!coverage_path)
		return SOND_INDEX_STATUS_NONE;

	if (is_dir)
		status = sond_index_ctx_get_dir_status(index_ctx, coverage_path);
	else {
		/* Nicht indizierbare Dateitypen (.db, .znd, Bilder, ...) gar nicht
		 * erst prüfen - sonst zeigt jede solche Datei dauerhaft "nicht
		 * indiziert" an, obwohl sie nie indiziert werden wird.
		 *
		 * Der MIME-Typ wird bevorzugt vom SondFilePart selbst geholt statt
		 * ihn aus der Endung von coverage_path zu raten. Bei einem
		 * SondFilePartLeaf ist das der beim Erzeugen per echtem
		 * Content-Sniffing ermittelte und gespeicherte Typ (s.
		 * sond_file_part_create()). Das betrifft v.a. eingebettete
		 * Container-Einträge ohne aussagekräftige Endung - z.B. einzelne
		 * MIME-Parts einer E-Mail (eine HTML-Alternative, ein
		 * Inline-Bild): deren "Pfad" ist ein interner, von der
		 * MIME-Bibliothek vergebener Name ohne (oder mit irreführender)
		 * Endung - der echte Typ ("text/html" etc.) steht aber auf dem
		 * SondFilePartLeaf. PDF und GMessage (E-Mail) als LEAF (kein
		 * Multipart/keine Einbettungen) sind unabhängig von der Endung
		 * immer unterstützt. */
		/* Message-Knoten einer E-Mail: eindeutig erkennbar wie in
		 * zond_treeviewfm_item_get_fileparts() - LEAF ohne path_or_section,
		 * dessen sond_file_part derselbe wie der der ganzen eml ist (kein
		 * eigener Mimepart-Kind-sfp). coverage_path ist dafür bewusst der
		 * BARE Dateiname ("mail.eml", s. coverage_path_of()) -
		 * der Header wird aber gezielt unter "mail.eml//header" abgedeckt,
		 * nicht unter "mail.eml" selbst. Badge zeigt FULL, wenn ENTWEDER
		 * der Header gezielt indiziert ist ODER die ganze Mail als ein
		 * Block/per Collapse unter "mail.eml" selbst abgedeckt ist - beides
		 * bedeutet "Header ist durchsucht". Sonst der jeweils bessere
		 * Teilstatus (NONE < PARTIAL < FULL), damit ein begonnener, aber
		 * noch nicht abgeschlossener Zustand nicht fälschlich als "gar
		 * nichts" erscheint. */
		if (priv->type == SOND_TVFM_ITEM_TYPE_LEAF && !priv->path_or_section &&
				SOND_IS_FILE_PART_GMESSAGE(priv->sond_file_part)) {
			gchar *header_path = g_strconcat(coverage_path, "//header", NULL);
			SondIndexStatus header_status =
					sond_index_ctx_get_file_status(index_ctx, header_path, -1, -1);
			SondIndexStatus whole_status =
					sond_index_ctx_get_file_status(index_ctx, coverage_path, -1, -1);

			g_free(header_path);
			status = MAX(header_status, whole_status);
		}
		else {
			if (!sond_tvfm_file_part_indexable(priv->sond_file_part,
					coverage_path)) {
				g_free(coverage_path);
				return SOND_INDEX_STATUS_NONE;
			}
			status = sond_index_ctx_get_file_status(index_ctx, coverage_path, -1, -1);
		}
	}

	g_free(coverage_path);

	return status;
}

