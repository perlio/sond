/*
 sond (sond_tvfm_status.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_tvfm_status.h"

#include <string.h>

#include "sond_tvfm_item_private.h"
#include "../sond_mime.h"
#include "../seadrive/sond_seadrive_core.h"

#ifdef G_OS_WIN32
#include <windows.h>
#include "../sond_file_helper.h"
#endif

SondSeadriveBadge sond_tvfm_badge_seadrive_live(gchar const *full_path) {
#if defined(G_OS_WIN32)
	wchar_t *lp = prepare_long_path(full_path, NULL);
	DWORD attrs = INVALID_FILE_ATTRIBUTES;
	gboolean pinned = FALSE;
	gboolean offline = FALSE;

	if (!lp)
		return SOND_SEADRIVE_BADGE_NONE;

	attrs = GetFileAttributesW(lp);
	g_free(lp);
	if (attrs == INVALID_FILE_ATTRIBUTES)
		return SOND_SEADRIVE_BADGE_NONE;

	pinned = (attrs & 0x00080000) != 0;  //FILE_ATTRIBUTE_PINNED
	offline = (attrs & 0x00400000) != 0; //FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS

	//Prioritätsreihenfolge s. SondSeadriveBadge (sond_icon_util.h)
	if (offline && pinned)
		return SOND_SEADRIVE_BADGE_PENDING;
	if (offline)
		return SOND_SEADRIVE_BADGE_OFFLINE;
	if (pinned)
		return SOND_SEADRIVE_BADGE_PINNED;
#else
	(void) full_path;
#endif

	return SOND_SEADRIVE_BADGE_NONE;
}

gchar const* sond_tvfm_badge_seadrive_text(SondSeadriveBadge badge) {
	switch (badge) {
	case SOND_SEADRIVE_BADGE_OFFLINE:
		return "Nur online (nicht auf diesem Rechner)";
	case SOND_SEADRIVE_BADGE_PENDING:
		return "Immer offline (wird geladen)";
	case SOND_SEADRIVE_BADGE_PINNED:
		return "Immer offline (auf diesem Rechner vorhanden)";
	default:
		return NULL;
	}
}

gchar const* sond_tvfm_badge_dir_status_text(SondSeadriveDirStatus status) {
	switch (status) {
	case SOND_SEADRIVE_DIR_STATUS_FULL_OFFLINE:
		return "Alles nur online";
	case SOND_SEADRIVE_DIR_STATUS_FULL_HYDRATED_PINNED:
		return "Alles immer offline";
	case SOND_SEADRIVE_DIR_STATUS_MIXED:
		return "Gemischt";
	default:
		return NULL;
	}
}

/* Pfad der realen Datei bzw. des Dateisystem-Verzeichnisses, zu dem der Eintrag
 * gehört (relativ zur Wurzel); NULL, wenn der Eintrag keinen SeaDrive-Zustand trägt */
static gchar const* badge_rel_path(SondTVFMItemPrivate *item_priv) {
	SondFilePart *top = NULL;

	/* Dateien (LEAF) und Filesystem-Verzeichnisse erhalten Overlay-Icons.
	 *
	 * Der SeaDrive-Pin-/Hydrierungsstatus gehört zur realen Datei im
	 * Dateisystem, nicht zum einzelnen (virtuellen) Teil - alle Kinder EINER
	 * realen Datei (Mime-Parts, ZIP-Einträge, PDF-Seiten als eigene Zeilen usw.,
	 * mit sond_file_part_get_parent() != NULL) müssen also dasselbe Badge zeigen
	 * wie die Datei selbst. Deshalb wird immer zum obersten Vorfahren
	 * hochgelaufen und dessen Pfad verwendet. */
	if (item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF && item_priv->sond_file_part &&
			//PDF mit children - Pagetree
			!(SOND_IS_FILE_PART_PDF(item_priv->sond_file_part) &&
					sond_file_part_get_has_children(item_priv->sond_file_part))) {
		top = item_priv->sond_file_part;
		while (sond_file_part_get_parent(top))
			top = sond_file_part_get_parent(top);

		return sond_file_part_get_path(top);
	}

	if (item_priv->type == SOND_TVFM_ITEM_TYPE_DIR) {
		if (!item_priv->sond_file_part) //DIR im Filesystem
			return item_priv->path_or_section;

		/* Auch die (virtuellen) Verzeichnisse in einem Container (ZIP-Verzeichnis,
		 * Multipart) bekommen das Badge der realen Datei, also immer zum obersten
		 * Vorfahren hochlaufen. */
		top = item_priv->sond_file_part;
		while (sond_file_part_get_parent(top))
			top = sond_file_part_get_parent(top);

		return sond_file_part_get_path(top);
	}

	return NULL;
}

void sond_tvfm_item_get_badges(SondTVFMItem *item, SondTVFMOwner *owner,
		SondTVFMBadgeSources const *sources, gpointer sources_data,
		SondTVFMBadges *badges) {
	SondTVFMItemPrivate *item_priv = NULL;

	g_return_if_fail(badges != NULL);
	memset(badges, 0, sizeof(SondTVFMBadges));

	g_return_if_fail(SOND_IS_TVFM_ITEM(item));

	item_priv = sond_tvfm_item_get_priv(item);

	/* SeaDrive-Zustand */
	if (owner && sond_tvfm_owner_is_seadrive_path(owner)) {
		gchar const *root = sond_tvfm_owner_get_root(owner);
		gchar const *rel = badge_rel_path(item_priv);
		gchar *full_path = (rel && root) ? g_strconcat(root, "/", rel, NULL) : NULL;

		if (full_path) {
#if defined(G_OS_WIN32)
			if (item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {
				/* Datei-Badge: der Baum liest es aus seinen Tabellen (Watcher),
				 * ohne Dateisystemzugriff */
				if (sources && sources->file_badge)
					badges->seadrive = sources->file_badge(sources_data, full_path);
			} else if (item_priv->type == SOND_TVFM_ITEM_TYPE_DIR) {
				/* Ordner werden vom Watcher nicht in den Datei-Badges geführt; das
				 * eigene Attribut bleibt hier ein Live-Check (Ordner sind praktisch
				 * nie PINNED/offline, kein Performance-Thema). Aussagekräftig ist
				 * ohnehin der aggregierte Teilbaum-Status. */
				badges->seadrive = sond_tvfm_badge_seadrive_live(full_path);

				if (badges->seadrive == SOND_SEADRIVE_BADGE_NONE && sources &&
						sources->dir_status)
					badges->dir_status = sources->dir_status(sources_data,
							full_path);
			}
#endif
			g_free(full_path);
		}
	}

	/* Indizierungsstatus (unabhängig von SeaDrive, beide können zutreffen) */
	if (sources && sources->index_status)
		badges->index = sources->index_status(sources_data, item);

	/* Anhang und Schloss hängen am Dateiteil, nicht am Item-Typ */
	if (item_priv->sond_file_part) {
		badges->attachment = sond_file_part_get_is_attachment(item_priv->sond_file_part);
		badges->lock = sond_file_part_get_lock(item_priv->sond_file_part);
	}
}

/* --- Index-Status aus der Index-DB --- */

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
