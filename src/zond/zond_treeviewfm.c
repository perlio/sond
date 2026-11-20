#include "zond_treeviewfm.h"

#include <glib/gstdio.h>
#include <sys/stat.h>

#include "../misc.h"
#include "../sond_fileparts.h"
#include "../sond_index.h"
#include "../sond_pdf_helper.h"
#include "../sond_process_file.h"
#include "../sond_log_and_error.h"
#include "../sond_file_helper.h"
#include "../sond_mime.h"
#include "zond_indexsuche.h"
#include "zond_eingang.h"
#include "zond_pdf_document.h"

#include "../znd/zond_dbase.h"
#include "zond_treeview.h"
#include "zond_tree_store.h"

#include "10init/app_window.h"
#include "10init/headerbar.h"
#include "../znd/dbase_zond.h"
#include "../znd/sond_projekt_pflege.h"
#include "20allgemein/project.h"
#include "40viewer/viewer.h"
#include "40viewer/document.h"
#include "../znd/anbindung.h"

typedef struct {
	Projekt *zond;

	/* Pflege der Projektdaten (Verweise in store/work, Index, Anhang-Adressen, Struktur): GTK-frei,
	 * gemeinsam mit dem Client, s. src/znd/sond_projekt_pflege.h */
	SondProjektPflege *pflege;
} ZondTreeviewFMPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondTreeviewFM, zond_treeviewfm, SOND_TYPE_TREEVIEWFM)

//Index-Kontext für Schreibzugriffe in der Transaktion; NULL ohne Index
static SondIndexCtx* index_tx(ZondTreeviewFMPrivate *priv) {
	return sond_projekt_pflege_index_tx(priv->pflege);
}

//Transaktion verwerfen (Fehlerweg von before_delete/before_move)
static void tx_abort(ZondTreeviewFMPrivate *priv) {
	sond_projekt_pflege_tx_abort(priv->pflege);
}

//Fehler der Index-DB (idx_err wird freigegeben) in *error übernehmen
static void set_idx_error(GError **error, gchar const *func,
		gchar const *what, GError **idx_err) {
	if (error) *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
			"%s: %s: %s", func, what, *idx_err ? (*idx_err)->message : "?");
	g_clear_error(idx_err);

	return;
}

//Löschen gesperrt: Eintrag steht noch in der Speicher-Datenbank
static void show_still_in_store(ZondTreeviewFMPrivate *priv) {
	display_message(priv->zond->win.app_window,
			"Die zu löschende Datei bzw. der Abschnitt ist "
			"noch in der Speicher-Datenbank vorhanden.\n"
			"Bitte zuerst das Projekt speichern, "
			"bevor Sie die Datei bzw. den Abschnitt löschen.", NULL);

	return;
}

//Verzeichnis: sfp//Pfad ("" bei der Wurzel); sonst der Filepart der Datei (ohne Section)
static gchar* get_path_from_stvfm_item(SondTVFMItem* stvfm_item) {
	gchar* path = NULL;

	if (sond_tvfm_item_get_item_type(stvfm_item) == SOND_TVFM_ITEM_TYPE_DIR) {
		path = sond_tvfm_item_get_filepart(stvfm_item);

		return path ? path : g_strdup("");
	}

	if (sond_tvfm_item_get_sond_file_part(stvfm_item))
		path = sond_file_part_get_filepart(
				sond_tvfm_item_get_sond_file_part(stvfm_item));

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
				priv->zond->proj.dbase_zond->zond_dbase_work, filepart,
				section, TRUE, error);
		if (rc == -1)
			return -1;
		else if (rc == 1)
			return 1; //Treffer
	}

	return 0;
}

static gboolean get_gmessage_index(SondTVFMItem* stvfm_item, gint* index) {
	if (!sond_tvfm_item_get_sond_file_part(stvfm_item))
		return FALSE;

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

//Index-DB-Gegenstück zu dbase_zond_update_gmessage_index(), s. sond_projekt_pflege.h
static gint gmessage_index_renumber(ZondTreeviewFMPrivate *priv,
		gchar const *prefix, gint index, gboolean into, GError **error) {
	return sond_projekt_pflege_gmessage_index_renumber(priv->pflege, prefix, index, into, error);
}

/* PDF bzw. Mail, deren Inhalt das Element ist (Anhang, Mimepart, Multipart-
 * Verzeichnis einer Mail); NULL sonst (auch PageTree/Message, Sections) */
static SondFilePart* structure_container_of_item(SondTVFMItem *stvfm_item) {
	SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);
	SondFilePart *parent = NULL;

	if (!sfp || sond_tvfm_item_is_content_root_marker(stvfm_item))
		return NULL;

	if (sond_tvfm_item_get_path_or_section(stvfm_item)) //Multipart in Mail
		return SOND_IS_FILE_PART_GMESSAGE(sfp) ? sfp : NULL;

	parent = sond_file_part_get_parent(sfp);

	return (SOND_IS_FILE_PART_PDF(parent) || SOND_IS_FILE_PART_GMESSAGE(parent)) ?
			parent : NULL;
}

/* Ziel eines Einfügens/Verschiebens: das Eltern-Element selbst, wenn es
 * eine PDF oder Mail (bzw. ein Verzeichnis darin) ist */
static SondFilePart* structure_container_of_target(SondTVFMItem *parent_item) {
	SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(parent_item);

	return (SOND_IS_FILE_PART_PDF(sfp) || SOND_IS_FILE_PART_GMESSAGE(sfp)) ?
			sfp : NULL;
}

static void structure_pending_add(ZondTreeviewFMPrivate *priv,
		SondFilePart *container) {
	sond_projekt_pflege_structure_add(priv->pflege, container);
}

static void structure_pending_clear(ZondTreeviewFMPrivate *priv) {
	sond_projekt_pflege_structure_clear(priv->pflege);
}

//Nach erfolgreicher Aktion: Struktur der vorgemerkten PDFs/Mails neu in die Index-DB einlesen
static void structure_refresh(ZondTreeviewFMPrivate *priv) {
	sond_projekt_pflege_structure_refresh(priv->pflege);
}

static void emb_sibling_pending_clear(ZondTreeviewFMPrivate *priv) {
	sond_projekt_pflege_emb_sibling_clear(priv->pflege);
}

/* Eingebettete Datei einer PDF (nicht PageTree, keine Section)? Ohne
 * Dateizugriff. */
static gboolean is_pdf_emb(SondTVFMItem *stvfm_item) {
	SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);

	if (!sfp || sond_tvfm_item_get_path_or_section(stvfm_item) ||
			sond_tvfm_item_is_content_root_marker(stvfm_item))
		return FALSE;

	return SOND_IS_FILE_PART_PDF(sond_file_part_get_parent(sfp));
}

/* Ist stvfm_item eine eingebettete Datei einer PDF, ändert ihr Löschen bzw. Herausverschieben
 * (filename_new NULL) oder Umbenennen ggf. die Adressen der übrigen Anhänge (s.
 * sond_projekt_pflege_emb_sibling_changes()); nichts zu tun: nichts vorgemerkt. */
static gint emb_sibling_changes(ZondTreeviewFMPrivate *priv,
		SondTVFMItem *stvfm_item, gchar const *filename_new, GError **error) {
	emb_sibling_pending_clear(priv);

	if (!is_pdf_emb(stvfm_item))
		return 0;

	return sond_projekt_pflege_emb_sibling_changes(priv->pflege,
			sond_tvfm_item_get_sond_file_part(stvfm_item), filename_new, error);
}

//Vorgemerkte Anhang-Adressen in store/work und Index umschreiben - innerhalb der offenen Transaktionen
static gint emb_sibling_apply(ZondTreeviewFMPrivate *priv, GError **error) {
	return sond_projekt_pflege_emb_sibling_apply(priv->pflege, error);
}

//Nach erfolgreichem Commit: geöffnete Fileparts der Anhänge auf ihre neue Adresse setzen
static void emb_sibling_adjust_opened(ZondTreeviewFMPrivate *priv) {
	sond_projekt_pflege_emb_sibling_adjust_opened(priv->pflege);
}

static gint zond_treeviewfm_before_delete(ZondTreeviewFM* ztvfm,
		SondTVFMItem *stvfm_item, GError **error, gpointer *ctx,
		gpointer user_data) {
	gint rc = 0;
	g_autofree gchar* path = NULL;
	gint index_from = 0;
	gchar const* section = NULL;
	SondTVFMItemType type;
	gboolean from_gmessage = FALSE;
	gboolean dual = FALSE;
	g_autofree gchar* prefix = NULL;

	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(ztvfm);

	path = get_path_from_stvfm_item(stvfm_item);
	type = sond_tvfm_item_get_item_type(stvfm_item);
	if (type == SOND_TVFM_ITEM_TYPE_LEAF || type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION)
		section = sond_tvfm_item_get_path_or_section(stvfm_item);

	/* Sperre genau dann, wenn der zu löschende Knoten selbst (LEAF /
	 * LEAF_SECTION) oder - bei einem DIR - irgendeiner seiner Abkömmlinge
	 * im Bestandsverzeichnis angebunden ist. Das ist exakt der Fall, in
	 * dem ON DELETE CASCADE (über parent_ID abwärts) einen Anker (type=2,
	 * link zeigt auf den gelöschten Knoten) mit in den Abgrund reißen
	 * würde - eine tote Referenz. Ein VORFAHRE von ID ist davon nie
	 * betroffen (CASCADE läuft nie aufwärts) und braucht keine Sperre.
	 *
	 * DIR: Existenzprüfung mit Präfix - jede Anbindung, deren file_part
	 * mit path + Trenner beginnt. Trenner ist "//", wenn path ein bloßer
	 * Container-Root ist (sond_file_part gesetzt, path_or_section NULL -
	 * der Container selbst wird als Verzeichnis dargestellt), sonst "/"
	 * (echtes Dateisystemverzeichnis oder Unterpfad innerhalb eines
	 * Containers).
	 *
	 * LEAF / LEAF_SECTION: exakter Abgleich file_part == path, bei
	 * LEAF_SECTION zusätzlich Bereichsvergleich (umfaßt path_or_section
	 * die in BAUM_INHALT gefundene section, oder ist sie gleich?). Beides
	 * gegen beide Datenbanken. */
	{
		DBaseZondRef ref = DBASE_ZOND_REF_NONE;
		gboolean is_container_root = type == SOND_TVFM_ITEM_TYPE_DIR &&
				sond_tvfm_item_get_sond_file_part(stvfm_item) &&
				!sond_tvfm_item_get_path_or_section(stvfm_item);

		rc = sond_projekt_pflege_check_delete(priv->pflege, path,
				type == SOND_TVFM_ITEM_TYPE_DIR, is_container_root, section, &ref, error);
		if (rc == -1)
			return -1;

		//angebunden: in work, oder nur noch im store (in work schon entfernt, nicht gespeichert)
		if (ref == DBASE_ZOND_REF_PRIMARY)
			return 1;
		if (ref == DBASE_ZOND_REF_ONLY_STORE) {
			show_still_in_store(priv);

			return 1;
		}
	}

	/* Vorgezogen: muss vor der Wahl single-/dual-DB feststehen */
	from_gmessage = get_gmessage_index(stvfm_item, &index_from);
	if (from_gmessage)
		prefix = g_strndup(path, strlen(path) - strlen(strrchr(path, '/') + 1));

	/* Anhang einer PDF: ggf. Adressen der übrigen Anhänge nachführen
	 * (emb_sibling_apply() unten) - schreibt wie bei GMessage in beide
	 * Datenbanken */
	dual = from_gmessage || is_pdf_emb(stvfm_item) || !section;

	/* Kontext für "after": Bit 0 = dual_write, Bit 1 = changed vor der Transaktion */
	*ctx = GINT_TO_POINTER(
			(dual ? 1 : 0) |
			(priv->zond->proj.dbase_zond->changed ? 2 : 0));

	//Transaktion (mit Index-DB, wenn eine Datei gelöscht wird), s. sond_projekt_pflege_tx_begin()
	if (sond_projekt_pflege_tx_begin(priv->pflege, dual, !section, error))
		return -1;

	if (index_tx(priv)) {
		GError *idx_err = NULL;

		if (!sond_index_ctx_clear_file(index_tx(priv), path, &idx_err)) {
			tx_abort(priv);
			set_idx_error(error, __func__, "sond_index_ctx_clear_file", &idx_err);
			return -1;
		}

		/* Vorsichtsmaßnahme: einen eventuell bestehenden eigenen
		 * coverage-Eintrag für genau diesen Pfad (oder darunter) entfernen.
		 * Nicht wegen des gelöschten Pfads selbst (der existiert ja gleich
		 * nicht mehr), sondern damit nicht irgendwann später ein neuer,
		 * völlig anderer Pfad gleichen Namens fälschlich als "schon
		 * abgedeckt" gilt. Ein eventuell abdeckender VORFAHRE wird bewusst
		 * NICHT angetastet - reines Löschen von Inhalt kann dessen Aussage
		 * nicht verletzen, s. Kommentar bei sond_index_ctx_coverage_clear(). */
		if (!sond_index_ctx_coverage_clear(index_tx(priv), path, &idx_err)) {
			tx_abort(priv);
			set_idx_error(error, __func__, "sond_index_ctx_coverage_clear",
					&idx_err);
			return -1;
		}
	}

	//wenn aus GMessage verschoben wurde - nachfolgende indizes anpassen
	if (from_gmessage) {
		gint rc = 0;

		rc = dbase_zond_update_gmessage_index(priv->zond->proj.dbase_zond,
				prefix, index_from, FALSE, error);
		if (!rc && !section) //Index-DB ebenso
			rc = gmessage_index_renumber(priv, prefix, index_from, FALSE, error);
		if (rc) {
			tx_abort(priv);
			return -1;
		}
	}

	//Anhang einer PDF: Adressen der übrigen Anhänge nachführen
	if (emb_sibling_changes(priv, stvfm_item, NULL, error) ||
			emb_sibling_apply(priv, error)) {
		emb_sibling_pending_clear(priv);
		tx_abort(priv);
		return -1;
	}

	/* Die (nicht mehr angebundene, s. Sperre oben) FILE_PART-Zeile zum Pfad
	 * in work und store mitlöschen, sonst bleibt sie als Müll stehen */
	if (!section && !from_gmessage &&
			dbase_zond_remove_filepart(priv->zond->proj.dbase_zond, path, error)) {
		emb_sibling_pending_clear(priv);
		tx_abort(priv);
		return -1;
	}

	/* Erst jetzt, unmittelbar vor dem garantierten Erfolg, für den Abschluss vormerken: nach der
	 * physischen Löschung könnte das Elternverzeichnis von path vollständig abgedeckt sein (die
	 * gelöschte Datei war ggf. der einzige "Lückenfüller"). Eine Löschung ist nie eine physische
	 * Verschiebung: Vormerkung einer früheren Verschiebung verwerfen, sonst würde ein gescheiterter
	 * Commit sie versehentlich zurückdrehen. */
	sond_projekt_pflege_delete_merken(priv->pflege,
			(priv->zond->proj.wctx && priv->zond->proj.wctx->index_ctx && !section) ? path : NULL);
	sond_projekt_pflege_move_merken(priv->pflege, NULL, NULL, FALSE);

	//Struktur der PDF/Mail nach dem Löschen neu einlesen
	structure_pending_clear(priv);
	structure_pending_add(priv, structure_container_of_item(stvfm_item));

	return 0;
}

/* Analogon zu zond_treeviewfm_before_move(), aber für echtes Kopieren
 * (nicht Ausschneiden/Verschieben): sond_tvfm_item_copy() wird nicht nur
 * beim Verschieben, sondern auch beim reinen Kopieren (Kopieren/Einfügen)
 * aufgerufen und emittiert dabei selbst kein Signal - deshalb sendet
 * process_stvfm_item_move_or_copy() in sond_treeviewfm.c in diesem Fall
 * "before-insert". Anders als bei before-move gibt es keinen alten Pfad,
 * der umbenannt werden müsste; es genügt, die Ziel-Abdeckung aufzulösen,
 * weil dort gleich neuer, noch ungeprüfter Inhalt entsteht. Best-effort:
 * ein Fehler hier soll das eigentliche Kopieren nicht verhindern.
 * Ohne diesen Hook ließe das Kopieren einer nicht indizierten Datei in
 * einen als komplett indiziert markierten Ordner den Ordner fälschlich
 * grün, weil dieser Pfad sonst gar nicht auf Coverage hörte. */
static gint zond_treeviewfm_before_insert(SondTreeviewFM* stvfm,
		SondTVFMItem* stvfm_item, SondTVFMItem* stvfm_item_parent,
		gchar const* base_new, gint index_to, GError **error,
		gpointer user_data) {
	g_autofree gchar* prefix_new = NULL;

	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	if (!ztvfm_priv->zond->proj.wctx || !ztvfm_priv->zond->proj.wctx->index_ctx)
		return 0;

	prefix_new = get_path_from_stvfm_item(stvfm_item_parent);

	if (*prefix_new != '\0') { //wenn nicht root-Verzeichnis
		if (!sond_tvfm_item_get_path_or_section(stvfm_item_parent))
			prefix_new = add_string(prefix_new, g_strdup("//"));
		else
			prefix_new = add_string(prefix_new, g_strdup("/"));
	}

	if (SOND_IS_FILE_PART_GMESSAGE(sond_tvfm_item_get_sond_file_part(stvfm_item_parent)))
		prefix_new = add_string(prefix_new, g_strdup("alpha"));
	else if (SOND_IS_FILE_PART_PDF(sond_tvfm_item_get_sond_file_part(stvfm_item_parent)))
		//Adresse in einer PDF: kodierter Dateiname
		prefix_new = add_string(prefix_new, pdf_emb_escape(base_new));
	else
		prefix_new = add_string(prefix_new, g_strdup(base_new));

	//Abdeckung auflösen, Struktur der Ziel-PDF/Mail zum Neueinlesen vormerken
	sond_projekt_pflege_insert(ztvfm_priv->pflege, prefix_new,
			ztvfm_priv->zond->proj.project_dir,
			structure_container_of_target(stvfm_item_parent));

	return 0;
}

/* Gegenstück zu before-insert (Kopieren): Struktur neu einlesen */
static void zond_treeviewfm_after_insert(SondTreeviewFM* stvfm,
		gboolean suc, gpointer user_data) {
	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	sond_projekt_pflege_insert_ende(ztvfm_priv->pflege, suc);
}

static gint zond_treeviewfm_before_move(SondTreeviewFM* stvfm,
		SondTVFMItem* stvfm_item, SondTVFMItem* stvfm_item_parent,
		gchar const* base_new, gint index_to, GError **error, gpointer *ctx,
		gpointer user_data) {
	gint rc = 0;
	g_autofree gchar* prefix_old = NULL;
	g_autofree gchar* prefix_new = NULL;
	gboolean from_gmessage = FALSE;
	gint index_from = 0;
	gboolean rename_in_gmessage = FALSE;

	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	prefix_old = get_path_from_stvfm_item(stvfm_item);
	prefix_new = get_path_from_stvfm_item(stvfm_item_parent);

	//Falls aus GMessage verschoben wird - welchen Index hatte Eintrag?
	from_gmessage = get_gmessage_index(stvfm_item, &index_from);

	/* Umbenennen (index_to -1, s. sond_treeviewfm_text_edited()) eines
	 * Mimeparts: ändert nur den Anzeigenamen, der Pfad (Mimepart-Index)
	 * bleibt. Keine Pfad- oder Index-Änderung in den Datenbanken (der Teil
	 * wird nicht wie beim Verschieben an Index 0 umnummeriert).
	 * Transaktionen und Kontext bleiben wie sonst, s.
	 * zond_treeviewfm_after(). */
	rename_in_gmessage = index_to == -1 && from_gmessage &&
			SOND_IS_FILE_PART_GMESSAGE(
					sond_tvfm_item_get_sond_file_part(stvfm_item_parent));

	if (rename_in_gmessage) {
		g_free(prefix_new);
		prefix_new = g_strdup(prefix_old);
	}
	else if (*prefix_new != '\0') { //wenn nicht root-Verzeichnis
		if (!sond_tvfm_item_get_path_or_section(stvfm_item_parent))
			prefix_new = add_string(prefix_new, g_strdup("//"));
		else
			prefix_new = add_string(prefix_new, g_strdup("/"));
	}

	if (rename_in_gmessage)
		; //Pfad bleibt
	else if (SOND_IS_FILE_PART_GMESSAGE(sond_tvfm_item_get_sond_file_part(stvfm_item_parent)))
		prefix_new = add_string(prefix_new, g_strdup("alpha")); //irgendwas alphanumerisches
	else if (SOND_IS_FILE_PART_PDF(sond_tvfm_item_get_sond_file_part(stvfm_item_parent)))
		//Adresse in einer PDF: kodierter Dateiname
		prefix_new = add_string(prefix_new, pdf_emb_escape(base_new));
	else
		prefix_new = add_string(prefix_new, g_strdup(base_new));

	//Kontext für "after" setzen: move ist immer dual (Bit 0 = 1), Bit 1 = changed vor der Transaktion
	*ctx = GINT_TO_POINTER(1 | (ztvfm_priv->zond->proj.dbase_zond->changed ? 2 : 0));

	{
		SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);
		SondFilePart *sfp_parent = sond_tvfm_item_get_sond_file_part(stvfm_item_parent);
		SondProjektMove move = {
			.prefix_old = prefix_old,
			.prefix_new = prefix_new,
			.project_dir = ztvfm_priv->zond->proj.project_dir,
			.index_to = index_to,
			.from_gmessage = from_gmessage,
			.index_from = index_from,
			.into_gmessage = SOND_IS_FILE_PART_GMESSAGE(sfp_parent),
			.rename_in_gmessage = rename_in_gmessage,
			.sfp_emb = is_pdf_emb(stvfm_item) ? sfp : NULL,
			.sfp_target = sfp_parent,
			.base_new = base_new
		};

		//Transaktion beginnen, Pfade, Mimepart-Nummern und Anhang-Adressen umschreiben
		rc = sond_projekt_pflege_move(ztvfm_priv->pflege, &move, error);
		if (rc)
			return -1;
	}

	/* Erst hier, unmittelbar vor dem garantierten Erfolg, für den Abschluss vormerken: alter/neuer
	 * absoluter Pfad, samt Kennzeichnung, ob es eine physische Dateisystem-Aktion war (dann - und nur
	 * dann - kann ein gescheiterter Commit per sond_rename() zurückgenommen werden). */
	{
		g_autofree gchar *path_old_abs = g_strconcat(
				ztvfm_priv->zond->proj.project_dir, "/", prefix_old, NULL);
		g_autofree gchar *path_new_abs = g_strconcat(
				ztvfm_priv->zond->proj.project_dir, "/", prefix_new, NULL);

		sond_projekt_pflege_move_merken(ztvfm_priv->pflege, path_old_abs, path_new_abs,
				!sond_tvfm_item_get_sond_file_part(stvfm_item) &&
				!sond_tvfm_item_get_sond_file_part(stvfm_item_parent));
	}

	/* Wird der Eintrag aus seinem Verzeichnis herausgenommen, kann dieses dadurch vollständig
	 * abgedeckt sein (die herausgenommene, nicht indizierte Datei war der einzige "Lückenfüller"):
	 * wie beim Löschen nach der physischen Aktion wieder zusammenfassen. Nicht beim Umbenennen. */
	sond_projekt_pflege_delete_merken(ztvfm_priv->pflege,
			(index_to != -1 && !rename_in_gmessage && ztvfm_priv->zond->proj.wctx &&
			ztvfm_priv->zond->proj.wctx->index_ctx) ? prefix_old : NULL);

	//Struktur von Quell- und Ziel-PDF/Mail danach neu einlesen
	structure_pending_clear(ztvfm_priv);
	structure_pending_add(ztvfm_priv, structure_container_of_item(stvfm_item));
	structure_pending_add(ztvfm_priv,
			structure_container_of_target(stvfm_item_parent));

	return 0;
}

//Abschluss von Löschen, Verschieben und Umbenennen: Commit bzw. Rollback macht die Pflege, hier nur die Anzeige
static void zond_treeviewfm_after(SondTreeviewFM* stvfm,
		gboolean suc, gpointer ctx, gpointer user_data) {
	GError* error_int = NULL;
	gint c = GPOINTER_TO_INT(ctx);
	gboolean dual_write = c & 1;
	gboolean changed_before = (c >> 1) & 1;
	SondProjektTxErgebnis ergebnis = SOND_PROJEKT_TX_OK;
	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	ergebnis = sond_projekt_pflege_tx_ende(priv->pflege, suc, dual_write,
			priv->zond->proj.project_dir, &error_int);

	if (ergebnis == SOND_PROJEKT_TX_FEHLER) {
		if (dual_write)
			display_message(priv->zond->win.app_window,
					"Speichern der Verschiebung/Umbenennung ist "
					"fehlgeschlagen und wurde rückgängig gemacht:\n\n",
					error_int ? error_int->message : "?", "\n\nBitte erneut versuchen.",
					NULL);
		else
			display_message(priv->zond->win.app_window,
					"Fehler beim Speichern:\n\n", error_int ? error_int->message : "?", NULL);
		g_clear_error(&error_int);
	} else if (ergebnis == SOND_PROJEKT_TX_KRITISCH) {
		/* Die Fehlerdatei ist geschrieben. Projekt in einer Schleife zu schließen versuchen (Chance,
		 * ein z.B. nur kurzzeitig nicht erreichbares Netzlaufwerk zwischenzeitlich zu beheben), dann
		 * in jedem Fall beenden. */
		display_message(priv->zond->win.app_window,
				"Kritischer Fehler beim Speichern einer "
				"Verschiebung/Umbenennung. Projekt und Datenbank "
				"können inkonsistent sein. Details wurden in eine "
				"Fehlerdatei im Projektordner geschrieben "
				"(ZOND_FEHLER_*.txt). Das Programm wird jetzt "
				"beendet.", NULL);
		g_clear_error(&error_int);

		{
			gint rc_close;

			do {
				GError *error_close = NULL;
				rc_close = project_close(priv->zond, &error_close);
				g_clear_error(&error_close);
			} while (rc_close);
		}

		exit(EXIT_FAILURE);
	}

	if (dual_write)
		project_reset_changed(priv->zond, changed_before);

	return;
}

static gint zond_treeviewfm_text_edited(SondTreeviewFM *stvfm,
		GtkTreeIter *iter, SondTVFMItem* stvfm_item, const gchar *new_text,
		GError **error) {
	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	if (sond_tvfm_item_get_item_type(stvfm_item) == SOND_TVFM_ITEM_TYPE_LEAF_SECTION) {
		gint ID_section = 0;
		g_autofree gchar* filepart = NULL;
		gint rc = 0;
		GtkTreeIter* iter_tree = NULL;

		filepart = sond_file_part_get_filepart(sond_tvfm_item_get_sond_file_part(stvfm_item));

		rc = zond_dbase_get_section(ztvfm_priv->zond->proj.dbase_zond->zond_dbase_work,
				filepart, sond_tvfm_item_get_path_or_section(stvfm_item), &ID_section, error);
		if (rc)
			return -1;

		if (ID_section == 0) {
			if (error) *error = g_error_new(ZOND_ERROR, 0,
					"%s\nAbschnitt nicht gefunden", __func__);

			return -1;
		}

		rc = zond_dbase_update_node_text(
				ztvfm_priv->zond->proj.dbase_zond->zond_dbase_work, ID_section,
				new_text, error);
		if (rc)
			return -1;

		//Text in treeview anpassen
		iter_tree = zond_tree_store_get_iter_by_node_id(
				ZOND_TREE_STORE(gtk_tree_view_get_model(
						GTK_TREE_VIEW(ztvfm_priv->zond->trees.treeview[BAUM_INHALT]))),
				ID_section);

		if (iter_tree) {
			zond_tree_store_set(iter_tree, NULL, new_text, 0);
			gtk_tree_iter_free(iter_tree);
		}

		/* Nur die Arbeitskopie wurde geändert (der Update-Hook hat das
		 * changed-Flag gesetzt) - es bleibt gesetzt, sonst ginge der Text
		 * beim Schließen ohne Rückfrage verloren. */
	}
	else { //chain-up, wenn nicht erledigt
		gint rc = 0;

		//Umbenennen schreibt in store und work; das changed-Flag stellt
		//zond_treeviewfm_after() wieder her
		rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->text_edited(stvfm,
				iter, stvfm_item, new_text, error);
		if (rc)
			return -1;
	}

	return 0;
}

static void zond_treeviewfm_results_row_activated(GtkTreeView *treeview,
		GtkTreePath *tree_path, GtkTreeViewColumn *col, gpointer data) {
	ZondTreeviewFM *ztvfm = (ZondTreeviewFM*) data;
	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ztvfm);

	if (!gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(ztvfm_priv->zond->win.fs_button)))
		gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(ztvfm_priv->zond->win.fs_button), TRUE);

	SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->results_row_activated(
			treeview, tree_path, col, data);

	return;
}

static gint zond_treeviewfm_open_stvfm_item(GtkTreeIter* iter, SondTVFMItem* stvfm_item,
		gboolean open_with, GError **error) {

	if (!open_with && SOND_IS_FILE_PART_PDF(sond_tvfm_item_get_sond_file_part(stvfm_item))) {
		PdfPos pdf_pos = { 0 };
		gchar const* section = NULL;
		gint rc = 0;
		SondFilePartPDF* sfp_pdf = NULL;
		DisplayedDocument* dd = NULL;
		Anbindung anbindung = { 0 };
		Anbindung anbindung_ges = { 0 };

		ZondTreeviewFM* ztvfm = ZOND_TREEVIEWFM(sond_tvfm_item_get_stvfm(stvfm_item));
		ZondTreeviewFMPrivate* ztvfm_priv =
				zond_treeviewfm_get_instance_private(ztvfm);

		sfp_pdf = SOND_FILE_PART_PDF(sond_tvfm_item_get_sond_file_part(stvfm_item));
		section = sond_tvfm_item_get_path_or_section(stvfm_item);
		if (section)
			anbindung_parse_file_section(section, &anbindung);

		if (ztvfm_priv->zond->win.state & GDK_CONTROL_MASK) {
			g_autoptr(SondTVFMItem) stvfm_item_parent = NULL;
			GtkTreeIter iter_parent = { 0 };
			GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(
					sond_tvfm_item_get_stvfm(stvfm_item)));

			//Eltern-Section nur für einen Punkt nötig (nächsthöherer Abschnitt)
			if (anbindung_is_pdf_punkt(anbindung)) {
				if (gtk_tree_model_iter_parent(model, &iter_parent, iter))
					stvfm_item_parent = sond_tvfm_item_from_model(model,
							&iter_parent);
				else
					LOG_WARN("Elter-Iter konnte nicht ermittelt werden"); //dann ganze PDF
			}

			anbindung_ges_nur_anbindung(anbindung,
					stvfm_item_parent ? sond_tvfm_item_get_path_or_section(
							stvfm_item_parent) : NULL, &anbindung_ges);
		}

		dd = document_new_displayed_document(sfp_pdf, &anbindung_ges, &anbindung,
				(ztvfm_priv->zond->win.state & GDK_MOD1_MASK), &pdf_pos, error);
		if (!dd)
			return -1;

		rc = zond_treeview_oeffnen_internal_viewer(ztvfm_priv->zond,
				dd, &pdf_pos, (ztvfm_priv->zond->win.state & GDK_SHIFT_MASK), error);
		if (rc)
			return -1;
	}
	else {
		gint rc = 0;

		rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->
				open_stvfm_item(iter, stvfm_item, open_with, error);
		if (rc)
			return -1;
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

	rc = zond_dbase_get_section(ztvfm_priv->zond->proj.dbase_zond->zond_dbase_work,
			filepart, section, &ID, error);
	g_free(filepart);
	if (rc)
		return -1;

	/* Ist ID selbst oder ein Abkömmling von ID im Bestandsverzeichnis
	 * angebunden, wurde die Löschung bereits in
	 * zond_treeviewfm_before_delete verhindert - diese Funktion hier wird
	 * für einen angebundenen Abschnitt also nie erreicht. */
	rc = zond_dbase_remove_node(ztvfm_priv->zond->proj.dbase_zond->zond_dbase_work, ID, error);
	if (rc)
		return -1;

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
		LOG_WARN("sfp darf nicht NULL sein");
		return 0;
	}

	if (!SOND_IS_FILE_PART_PDF(sfp))
		return 0;

	section = sond_tvfm_item_get_path_or_section(stvfm_item);
	filepart = sond_file_part_get_filepart(sfp);

	rc = zond_dbase_get_section(ztvfm_priv->zond->proj.dbase_zond->zond_dbase_work,
			filepart, section, &ID, error);
	if (rc)
		return -1;

	if (section && ID == 0) {
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s\nAbschnitt nicht gefunden", __func__);

		return -1;
	}

	rc = zond_dbase_get_first_child(ztvfm_priv->zond->proj.dbase_zond->zond_dbase_work, ID, &child, error);
	if (rc)
		return -1;

	if (!arr_children) {
		if (child) return 1;
		else return 0;
	}

	arr_children_int = g_ptr_array_new_with_free_func((GDestroyNotify) g_object_unref);
	while (child) {
		SondTVFMItem* stvfm_item_child = NULL;
		gchar* section_child = NULL;
		gint rc = 0;
		gint younger_sibling_id = 0;

		rc = zond_dbase_get_node(ztvfm_priv->zond->proj.dbase_zond->zond_dbase_work, child,
				NULL, NULL, NULL, &section_child, NULL, NULL, NULL, error);
		if (rc)
			return -1;

		stvfm_item_child =
				sond_tvfm_item_create(sond_tvfm_item_get_stvfm(stvfm_item),
						sfp, section_child);
		g_free(section_child);
		sond_tvfm_item_set_icon_name(stvfm_item_child,
				ztvfm_priv->zond->app.icon[ICON_ANBINDUNG].icon_name);
		g_ptr_array_add(arr_children_int, stvfm_item_child);

		rc = zond_dbase_get_younger_sibling(ztvfm_priv->zond->proj.dbase_zond->zond_dbase_work,
				child, &younger_sibling_id, error);
		if (rc)
			return -1;

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
		LOG_WARN("Konnte sections nicht laden: %s", error->message);
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

	rc = zond_dbase_get_section(ztvfm_priv->zond->proj.dbase_zond->zond_dbase_work,
			filepart, section, &ID, error);
	g_free(filepart);
	if (rc)
		return -1;

	if (ID == 0) {
		Anbindung anbindung = { 0 };

		anbindung_parse_file_section(section, &anbindung);
		*text = anbindung_to_human_readable(&anbindung);
	}
	else {
		rc = zond_dbase_get_node(ztvfm_priv->zond->proj.dbase_zond->zond_dbase_work,
				ID, NULL, NULL, NULL, NULL, NULL, text, NULL, error);
		if (rc)
			return -1;
	}

	return 0;
}

/* Reiner readdir-Scanner für "wirkliche" (nicht in einem Container liegende)
 * Dateisystem-Verzeichnisse - ersetzt für diesen Fall den Weg über
 * sond_tvfm_item_load_children()/sond_file_part_create() (Inhalts-Sniffing
 * der ersten 2 KB, SeaDrive-Hydrierung). Benutzt nur sond_dir_open()/
 * sond_dir_read_name() (Verzeichnis-Listing) und sond_stat() (Metadaten,
 * kein Inhalt) - beides zieht bei SeaDrive-Platzhaltern keine Hydrierung
 * nach sich. Der Dateityp wird ausschließlich über die Endung bestimmt
 * (mime_from_extension()) und über sond_file_part_create_leaf() verpackt
 * (ebenfalls ohne Dateizugriff) - für den Zweck hier (Soll/Ist-Abgleich
 * beim Durchsuchen/Löschen des Index gegen die DB) reicht das:
 * check_coverage_one() erkennt PDFs zusätzlich über die Endung, nicht nur
 * über den GObject-Typ (zond_indexsuche.c).
 *
 * In Container (ZIP/E-Mail/PDF mit Einbettungen) wird hier NICHT
 * hineingestiegen: ein frisch entdeckter Container-Top-Knoten hat
 * sond_file_part != NULL UND path_or_section == NULL, fällt unten in
 * zond_treeviewfm_item_get_fileparts() also stets in den ELSE-Zweig (ein
 * einziger, opaker Filepart, da "application/zip" & Co. ohnehin nicht
 * indizierbar sind). Inhalte innerhalb eines Containers werden nur über
 * eine explizite Auswahl darin erreicht (sond_file_part != NULL UND
 * path_or_section gesetzt) - dafür dient der echte Weg in
 * zond_treeviewfm_item_get_fileparts(). */
#define READDIR_MAX_DEPTH 64

static gint zond_treeviewfm_readdir_rek(SondTreeviewFM *stvfm,
		gchar const *rel_dir, GHashTable *ht, gboolean skip_fully_covered,
		guint depth, GError **error) {
	gchar const *root = sond_treeviewfm_get_root(stvfm);
	gchar *path_dir = NULL;
	SondDir *dir = NULL;
	gchar const *filename = NULL;

	//Schutz vor Verzeichnis-Schleifen (Junction/Symlink auf einen Vorfahren)
	if (depth > READDIR_MAX_DEPTH) {
		LOG_WARN("%s: Verzeichnistiefe > %d bei '%s' - übersprungen "
				"(Verzeichnisschleife?)", __func__, READDIR_MAX_DEPTH,
				rel_dir ? rel_dir : "");

		return 0;
	}

	/* Derselbe Verzeichnis-
	 * Kurzschluss wie in scan_coverage_gaps_fs() (zond_indexsuche.c) - ein
	 * bereits vollständig indizierter Ast wird gar nicht erst per readdir
	 * aufgeschlüsselt, erneutes Indizieren wäre per Definition von
	 * "Coverage" ein No-Op. rel_dir == NULL (Projektwurzel) wird nie
	 * geprüft - Coverage wird nie über die oberste Ebene hinaus
	 * zusammengefasst (sond_index_ctx_coverage_try_collapse()), ein
	 * Eintrag fürs ganze Projekt existiert also nie. Der Aufrufer trägt
	 * die Verantwortung, skip_fully_covered nicht zu setzen, wenn der
	 * OCR-Modus "erzwingen" ist (s. Doc-Kommentar im Header). */
	if (skip_fully_covered && rel_dir) {
		ZondTreeviewFMPrivate *priv =
				zond_treeviewfm_get_instance_private(ZOND_TREEVIEWFM(stvfm));

		if (priv->zond->proj.wctx && priv->zond->proj.wctx->index_ctx) {
			SondIndexStatus status = sond_index_ctx_get_dir_status(
					priv->zond->proj.wctx->index_ctx, rel_dir);

			if (status == SOND_INDEX_STATUS_FULL)
				return 0; /* ganzer Ast abgedeckt - nichts zu tun */
		}
	}

	path_dir = rel_dir ? g_strconcat(root, "/", rel_dir, NULL) : g_strdup(root);
	dir = sond_dir_open(path_dir, error);
	g_free(path_dir);
	if (!dir)
		return -1;

	while ((filename = sond_dir_read_name(dir)) != NULL) {
		gchar *rel_path_child = NULL;
		GStatBuf st = { 0 };
		GError *error_stat = NULL;

		rel_path_child = rel_dir ?
				g_strconcat(rel_dir, "/", filename, NULL) : g_strdup(filename);

		/* absoluter Pfad: ein projektrelativer hinge am Arbeitsverzeichnis
		 * des Prozesses */
		{
			gchar *abs_path_child = g_strconcat(root, "/", rel_path_child,
					NULL);
			gint rc_stat = sond_stat(abs_path_child, &st, &error_stat);

			g_free(abs_path_child);
			if (rc_stat) {
				LOG_WARN("%s: sond_stat('%s') gibt Fehler zurück: %s", __func__,
						rel_path_child,
						error_stat ? error_stat->message : "?");
				g_clear_error(&error_stat);
				g_free(rel_path_child);
				continue;
			}
		}

		if (S_ISDIR(st.st_mode)) {
			gint rc = zond_treeviewfm_readdir_rek(stvfm, rel_path_child, ht,
					skip_fully_covered, depth + 1, error);
			g_free(rel_path_child);
			if (rc) {
				sond_dir_close(dir);
				return -1;
			}
		} else if (sond_file_name_not_indexable(filename)) {
			//am Namen als nicht indizierbar erkennbar (Bild, Video, ...)
			g_free(rel_path_child);
		} else {
			gchar const *mime = mime_from_extension(filename);
			SondFilePart *sfp_leaf = sond_file_part_create_leaf(
					rel_path_child, NULL, mime);

			g_free(rel_path_child);
			g_hash_table_insert(ht, sfp_leaf, NULL);
		}
	}

	sond_dir_close(dir);

	return 0;
}

gint zond_treeviewfm_item_get_fileparts_readdir(SondTreeviewFM *stvfm,
		gchar const *rel_dir, GHashTable *ht, gboolean skip_fully_covered,
		GError **error) {
	return zond_treeviewfm_readdir_rek(stvfm, rel_dir, ht, skip_fully_covered,
			0, error);
}

/* zond_treeviewfm_get_fileparts() und Helfer:
 *
 * Die generische Basisklasse weiß bewusst nicht, was eine "Section" eines
 * SOND_TVFM_ITEM_TYPE_LEAF_SECTION-Knotens bedeutet (bei PDF ein
 * Seitenbereich, bei anderen Dateitypen ggf. etwas ganz anderes - z.B.
 * ein Zeitausschnitt bei Audio/Video). Nur zond selbst weiß: in BAUM_FS
 * ist ein LEAF_SECTION-Knoten immer eine Anbindung (angelegt in
 * ziele.c), deren path_or_section ein Seitenbereich ist. Deshalb baut
 * diese Funktion die Hashtable selbst, ausschließlich über die
 * öffentlichen SondTVFMItem-Zugriffsfunktionen (sond_treeviewfm.h). */
static gint zond_treeviewfm_item_get_fileparts(SondTVFMItem *stvfm_item,
		GHashTable *ht, gboolean reject_unterseitig,
		gboolean skip_fully_covered, GError **error) {
	SondTVFMItemType type = sond_tvfm_item_get_item_type(stvfm_item);
	gchar const *path_or_section = sond_tvfm_item_get_path_or_section(stvfm_item);
	SondFilePart *sond_file_part = sond_tvfm_item_get_sond_file_part(stvfm_item);

	//Wirkliches Dateisystem-Verzeichnis (auch verschachtelt - sond_file_part
	//bleibt dabei auf dem gesamten Ast NULL, s. sond_tvfm_item_load_fs_dir()):
	//reiner readdir-Scanner, s. dortigen Kommentar.
	if (type == SOND_TVFM_ITEM_TYPE_DIR && !sond_file_part) {
		return zond_treeviewfm_item_get_fileparts_readdir(
				sond_tvfm_item_get_stvfm(stvfm_item), path_or_section, ht,
				skip_fully_covered, error);
	}
	//Innerhalb eines Containers (ZIP/E-Mail/PDF mit Einbettungen), nur über
	//eine explizite Auswahl darin erreichbar - unverändert der bisherige,
	//echte Weg (der Nutzer hat diesen Container durch eigenes Navigieren
	//bereits geöffnet, s. Kommentar an zond_treeviewfm_item_get_fileparts_readdir).
	else if (type == SOND_TVFM_ITEM_TYPE_DIR && path_or_section) {
		g_autoptr(GPtrArray) arr_children = NULL;
		gint rc = 0;

		rc = sond_tvfm_item_load_children(stvfm_item, &arr_children, NULL, error);
		if (rc)
			return -1;

		for (guint i = 0; i < arr_children->len; i++) {
			SondTVFMItem *child = g_ptr_array_index(arr_children, i);

			rc = zond_treeviewfm_item_get_fileparts(child, ht,
					reject_unterseitig, skip_fully_covered, error);
			if (rc)
				return -1;
		}
	}
	else {
		/* eigene Ref pro Key nötig (ht: key-destroy-func g_object_unref,
		 * s. zond_treeviewfm_get_fileparts() unten) - sond_file_part
		 * gehört sonst dem stvfm_item. */
		SondPageRange *range = NULL;

		if (type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION && path_or_section) {
			/* Anbindung - path_or_section ist deren Seitenbereich-String
			 * (s. Anlage in ziele.c). Ohne "bis" (reiner Punkt) liefert
			 * anbindung_build_file_section() ein bis mit seite==0 UND
			 * index==0 - dieselbe Bedingung erkennt hier, dass nur eine
			 * einzelne Seite (nicht seite 0 als Bereichsende) gemeint ist. */
			Anbindung anbindung = { 0 };

			anbindung_parse_file_section(path_or_section, &anbindung);

			/* Index erstellen/löschen (Auswahl): unterseitige Anbindungen
			 * sind dafür nicht zulässig - s. anbindung_ist_unterseitig()
			 * (anbindung.h). */
			if (reject_unterseitig && anbindung_ist_unterseitig(anbindung)) {
				g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
						"Die Auswahl enthält eine unterseitige Anbindung "
						"(beginnt/endet nicht an einer Seitengrenze) - das "
						"ist für Indizierung/Index löschen nicht zulässig.");
				return -1;
			}

			range = sond_page_range_new(anbindung.von.seite,
					anbindung_bis_seite(anbindung));
		}
		else if (type == SOND_TVFM_ITEM_TYPE_LEAF && !path_or_section &&
				SOND_IS_FILE_PART_GMESSAGE(sond_file_part)) {
			/* Der "Message"-Knoten einer E-Mail: type==LEAF, path_or_section
			 * ist NULL (der "//message"-Marker wird beim Erzeugen des Items
			 * sofort gelöscht, s. sond_tvfm_item_create()), und sond_file_part
			 * ist derselbe SondFilePart wie der der gesamten eml - eindeutig
			 * unterscheidbar von "das ganze Dir/die ganze Datei" nur über
			 * diesen Item-Typ zum Zeitpunkt der Auswahl. Statt der
			 * ganzen Mail wird für diesen Eintrag nur der Header indiziert. */
			range = sond_page_range_new_gmessage_header();
		}
		else if (type == SOND_TVFM_ITEM_TYPE_LEAF &&
				SOND_IS_FILE_PART_PDF(sond_file_part) &&
				sond_tvfm_item_is_content_root_marker(stvfm_item)) {
			/* "PageTree"-Knoten einer PDF mit Einbettungen: nur die Seiten,
			 * die Einbettungen sind eigene Kind-Knoten. Die
			 * PDF selbst (DIR) bleibt "ganze Datei". */
			range = sond_page_range_new_pdf_pagetree();
		}

		/* Mehrere markierte Punkte derselben Datei (z.B. zwei Anbindungen):
		 * Vereinigung statt Ersetzen des bisherigen Eintrags */
		sond_page_range_merge(ht, g_object_ref(sond_file_part), range);
	}

	return 0;
}

typedef struct {
	GHashTable *ht;
	gboolean reject_unterseitig;
	gboolean skip_fully_covered;
} ZtvfmGetFilepartsData;

static gint zond_treeviewfm_get_fileparts_foreach(SondTreeview *stv,
		GtkTreeIter *iter, gpointer data, GError **error) {
	SondTVFMItem *stvfm_item = NULL;
	ZtvfmGetFilepartsData *gfd = (ZtvfmGetFilepartsData*) data;
	gint rc = 0;

	stvfm_item = sond_tvfm_item_from_view(GTK_TREE_VIEW(stv), iter);
	if (!stvfm_item) //Dummy-Zeile
		return 0;

	rc = zond_treeviewfm_item_get_fileparts(stvfm_item, gfd->ht,
			gfd->reject_unterseitig, gfd->skip_fully_covered, error);
	g_object_unref(stvfm_item);
	if (rc)
		return -1;

	return 0;
}

GHashTable* zond_treeviewfm_get_fileparts(ZondTreeviewFM *ztvfm,
		gboolean selected_only, gboolean reject_unterseitig,
		gboolean skip_fully_covered, GError **error) {
	GHashTable *ht = NULL;
	gint rc = 0;

	ht = g_hash_table_new_full(NULL, NULL, g_object_unref, sond_page_range_free);

	if (selected_only) {
		ZtvfmGetFilepartsData gfd = { ht, reject_unterseitig, skip_fully_covered };

		rc = sond_treeview_selection_foreach(SOND_TREEVIEW(ztvfm),
				zond_treeviewfm_get_fileparts_foreach, &gfd, error);
	} else {
		SondTVFMItem *stvfm_item =
				sond_tvfm_item_create(SOND_TREEVIEWFM(ztvfm), NULL, NULL);

		/* "Gesamtes Projekt": nie ablehnen, s. Doc-Kommentar (Header). */
		rc = zond_treeviewfm_item_get_fileparts(stvfm_item, ht, FALSE,
				skip_fully_covered, error);
		g_object_unref(stvfm_item);
	}

	if (rc) {
		g_hash_table_destroy(ht);
		return NULL;
	}

	return ht;
}

/* Vfunc für sond_treeviewfm.c (Indizierungsstatus-Overlay): ein
 * LEAF_SECTION-Knoten in BAUM_FS ist bei zond immer eine Anbindung
 * (angelegt in ziele.c), deren path_or_section ein Seitenbereich-String
 * ist - dieselbe Auswertung wie in zond_treeviewfm_item_get_fileparts().
 *
 * Liefert den fertigen SondIndexStatus (s. ausführlichen Doc-Kommentar an
 * der vfunc-Deklaration in sond_treeviewfm.h) - "Section = Seitenbereich" ist
 * eine zond/PDF-spezifische Interpretation, die hier (in der zond-Subklasse)
 * hingehört und nicht in die generische Basisklasse. Übernimmt dafür
 * auch die Mime-Type-Prüfung von sond_treeviewfm_get_index_status() (Leaf-
 * eigener, gesniffter Mime-Type bevorzugt, PDF/GMessage-Bypass) - dieselbe
 * Logik wie dort, hier auf die zugrundeliegende Datei der Section
 * angewandt. */
static SondIndexStatus zond_treeviewfm_get_section_index_status(
		SondTVFMItem *stvfm_item, SondIndexCtx *index_ctx) {
	gchar const *section = sond_tvfm_item_get_path_or_section(stvfm_item);
	SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);
	Anbindung anbindung = { 0 };
	gchar *coverage_path = NULL;
	SondIndexStatus status = SOND_INDEX_STATUS_NONE;
	gint von_seite = -1;
	gint bis_seite = -1;

	if (!section || !sfp)
		return SOND_INDEX_STATUS_NONE;

	coverage_path = sond_file_part_get_filepart(sfp);
	if (!coverage_path)
		return SOND_INDEX_STATUS_NONE;

	if (!sond_treeviewfm_file_part_indexable(sfp, coverage_path)) {
		g_free(coverage_path);
		return SOND_INDEX_STATUS_NONE;
	}

	anbindung_parse_file_section(section, &anbindung);

	von_seite = anbindung.von.seite;
	bis_seite = anbindung_bis_seite(anbindung);

	status = sond_index_ctx_get_file_status(index_ctx, coverage_path, von_seite,
			bis_seite);
	g_free(coverage_path);

	return status;
}

static void zond_treeviewfm_finalize(GObject *obj) {
	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(obj));

	sond_projekt_pflege_free(priv->pflege);
	priv->pflege = NULL;

	G_OBJECT_CLASS(zond_treeviewfm_parent_class)->finalize(obj);
}

//PDF ist im Viewer geöffnet
static gboolean zond_treeviewfm_file_part_busy(SondFilePart *sfp) {
	return SOND_IS_FILE_PART_PDF(sfp) &&
			zond_pdf_document_is_open(SOND_FILE_PART_PDF(sfp)) != NULL;
}

/* Ein Eintrag des Dateisystems wurde gelöscht: dessen eigene Eingangsangaben
 * (bei einem Ordner auch die darunter) gelten nicht mehr. */
static void zond_treeviewfm_fs_entry_deleted(SondTreeviewFM *stvfm,
		gchar const *rel_path, gboolean is_dir) {
	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	if (ztvfm_priv->zond)
		zond_eingang_geloescht(ztvfm_priv->zond, rel_path);
}

/* Ein Eintrag des Dateisystems wurde verschoben oder kopiert: seine
 * Eingangsangaben gehen mit bzw. werden mitkopiert. */
static gboolean zond_treeviewfm_fs_entry_moved(SondTreeviewFM *stvfm,
		gchar const *old_rel, gchar const *new_rel, gboolean is_dir,
		gboolean copied) {
	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	if (!ztvfm_priv->zond)
		return FALSE;

	return zond_eingang_verschoben(ztvfm_priv->zond, old_rel, new_rel, is_dir,
			copied);
}

/* Ein Eintrag des Dateisystems wurde umbenannt: seine Eingangsangaben (und die
 * darunter) gehen mit auf den neuen Pfad. */
static void zond_treeviewfm_fs_entry_renamed(SondTreeviewFM *stvfm,
		gchar const *old_rel, gchar const *new_rel, gboolean is_dir) {
	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	if (ztvfm_priv->zond)
		zond_eingang_umbenannt(ztvfm_priv->zond, old_rel, new_rel);
}

/* Wirksames Eingangsdatum eines Dateisystem-Eintrags aus der lokalen
 * Nachschlagetabelle des Projekts; -1, solange es keine gibt. */
static gint zond_treeviewfm_get_eingang(SondTreeviewFM *stvfm,
		gchar const *rel_path, gboolean is_dir) {
	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	if (!ztvfm_priv->zond || !ztvfm_priv->zond->proj.eingang_cache)
		return -1;

	return sond_eingang_cache_lookup(ztvfm_priv->zond->proj.eingang_cache, rel_path,
			is_dir);
}

static void zond_treeviewfm_class_init(ZondTreeviewFMClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = zond_treeviewfm_finalize;

	SOND_TREEVIEWFM_CLASS(klass)->text_from_section =
			zond_treeviewfm_get_text_from_section;
	SOND_TREEVIEWFM_CLASS(klass)->deter_background = zond_treeviewfm_deter_background;
	SOND_TREEVIEWFM_CLASS(klass)->text_edited = zond_treeviewfm_text_edited;
	SOND_TREEVIEWFM_CLASS(klass)->results_row_activated =
			zond_treeviewfm_results_row_activated;
	SOND_TREEVIEWFM_CLASS(klass)->open_stvfm_item = zond_treeviewfm_open_stvfm_item;
	SOND_TREEVIEWFM_CLASS(klass)->file_part_busy = zond_treeviewfm_file_part_busy;
	SOND_TREEVIEWFM_CLASS(klass)->load_sections = zond_treeviewfm_load_sections;
	SOND_TREEVIEWFM_CLASS(klass)->has_sections = zond_treeviewfm_has_sections;
	SOND_TREEVIEWFM_CLASS(klass)->delete_section = zond_treeviewfm_delete_section;
	SOND_TREEVIEWFM_CLASS(klass)->get_section_index_status =
			zond_treeviewfm_get_section_index_status;
	SOND_TREEVIEWFM_CLASS(klass)->get_eingang = zond_treeviewfm_get_eingang;
	SOND_TREEVIEWFM_CLASS(klass)->fs_entry_deleted =
			zond_treeviewfm_fs_entry_deleted;
	SOND_TREEVIEWFM_CLASS(klass)->fs_entry_renamed =
			zond_treeviewfm_fs_entry_renamed;
	SOND_TREEVIEWFM_CLASS(klass)->fs_entry_moved =
			zond_treeviewfm_fs_entry_moved;

	/* Zond-spezifische GMenu-Sections einmalig fuer diese Klasse aufbauen */
	SOND_TREEVIEW_CLASS(klass)->gmenu = g_menu_new();
	sond_treeviewfm_add_base_menu(SOND_TREEVIEW_CLASS(klass)->gmenu);

	// dann die ZTV-FM-Sections
	GMenu *gmenu = SOND_TREEVIEW_CLASS(klass)->gmenu;

	GMenu *sec_jump = g_menu_new();
	g_menu_append(sec_jump, "Zur Anbindung springen", "stv.jump");
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_jump));
	g_object_unref(sec_jump);

	/* "Index"-Untermenü analog zum Hauptmenü (headerbar.c), hier aber -
	 * wie bei "SeaDrive" im Kontextmenü - bewusst nur "Auswahl" je Aktion,
	 * kein "Gesamtes Projekt" (gleiche Menüstruktur bei Index und
	 * SeaDrive). */
	GMenu *sec_idx = g_menu_new();
	GMenu *sub_idx = g_menu_new();
	g_menu_append(sub_idx, "Erstellen",   "stv.index-erstellen-sel");
	g_menu_append(sub_idx, "Durchsuchen", "stv.indexsuche-sel");
	g_menu_append(sub_idx, "Löschen",     "stv.index-loeschen-sel");
	g_menu_append_submenu(sec_idx, "Index", G_MENU_MODEL(sub_idx));
	g_object_unref(sub_idx);
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_idx));
	g_object_unref(sec_idx);

	GMenu *sec_eingang = g_menu_new();
	GMenu *sub_eingang = g_menu_new();
	g_menu_append(sub_eingang, "Setzen ...", "stv.eingang-setzen");
	g_menu_append(sub_eingang, "Entfernen", "stv.eingang-entfernen");
	g_menu_append(sub_eingang, "Neu einlesen", "stv.eingang-neu");
	g_menu_append_submenu(sec_eingang, "Eingang", G_MENU_MODEL(sub_eingang));
	g_object_unref(sub_eingang);
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_eingang));
	g_object_unref(sec_eingang);

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

	if (!sond_treeview_get_cursor(zond->trees.treeview[BAUM_FS], &iter))
		return;

	stvfm_item = sond_tvfm_item_from_view(GTK_TREE_VIEW(zond->trees.treeview[BAUM_FS]), &iter);
	if (!stvfm_item) //Dummy-Zeile
		return;

	if (sond_tvfm_item_get_item_type(stvfm_item) == SOND_TVFM_ITEM_TYPE_DIR) {
		g_object_unref(stvfm_item);

		return;
	}

	filepart = sond_file_part_get_filepart(sond_tvfm_item_get_sond_file_part(stvfm_item));
	section = sond_tvfm_item_get_path_or_section(stvfm_item);

	rc = zond_dbase_get_section(zond->proj.dbase_zond->zond_dbase_work, filepart, section, &ID, &error);
	g_free(filepart);
	g_object_unref(stvfm_item); //section ist damit nicht mehr gültig
	if (rc) {
		display_message(zond->win.app_window, "Zur Anbindung springen nicht möglich\n\n",
				error->message, NULL);
		g_error_free(error);

		return;
	}

	if (ID)
		zond_treeview_jump_to_node_id(zond, ID);

	return;
}

static void zond_treeviewfm_action_jump(GSimpleAction *a, GVariant *p,
		gpointer d) {
	zond_treeviewfm_jump_activate(NULL, d);
}

static void zond_treeviewfm_action_indexsuche_auswahl(GSimpleAction *a,
		GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;

	//Rechtsklick im Dateiverzeichnis: es hat den Fokus (s. zond_baum_aktuell())
	zond_indexsuche_activate_fuer_baum(zond, zond_baum_aktuell(zond));
}

static void zond_treeviewfm_action_index_erstellen_auswahl(GSimpleAction *a,
		GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;

	/* Analogon zu zond_treeviewfm_action_indexsuche_auswahl() oberhalb,
	 * s. dortigen Kommentar. */
	zond_index_erstellen_activate_fuer_baum(zond, zond_baum_aktuell(zond));
}

static void zond_treeviewfm_action_index_loeschen_auswahl(GSimpleAction *a,
		GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;

	/* Analogon zu zond_treeviewfm_action_indexsuche_auswahl() oberhalb,
	 * s. dortigen Kommentar. */
	zond_index_loeschen_activate_fuer_baum(zond, zond_baum_aktuell(zond));
}

static void zond_treeviewfm_action_eingang_setzen(GSimpleAction *a,
		GVariant *p, gpointer d) {
	zond_eingang_setzen((Projekt*) d);
}

static void zond_treeviewfm_action_eingang_entfernen(GSimpleAction *a,
		GVariant *p, gpointer d) {
	zond_eingang_entfernen((Projekt*) d);
}

/* Zettel nachlesen, die seit dem Öffnen des Projekts dazugekommen sind
 * (z.B. ein Import auf einem anderen Rechner), und die Anzeige auffrischen */
static void zond_treeviewfm_action_eingang_neu(GSimpleAction *a, GVariant *p,
		gpointer d) {
	Projekt *zond = (Projekt*) d;
	GError *error = NULL;
	guint n_zettel = 0;
	gchar *text = NULL;

	if (!zond->proj.eingang_cache) {
		display_message(zond->win.app_window, "Eingänge neu einlesen\n\n",
				"Für dieses Projekt sind keine Eingangsangaben verfügbar.", NULL);

		return;
	}

	if (sond_eingang_cache_reload(zond->proj.eingang_cache, &n_zettel, &error)) {
		display_message(zond->win.app_window,
				"Eingänge neu einlesen fehlgeschlagen\n\n", error->message,
				NULL);
		g_error_free(error);

		return;
	}

	sond_treeviewfm_eingang_refresh(SOND_TREEVIEWFM(zond->trees.treeview[BAUM_FS]));

	text = g_strdup_printf("%u neue(r) Zettel eingelesen.", n_zettel);
	display_message(zond->win.app_window, "Eingänge neu einlesen\n\n", text, NULL);
	g_free(text);

	return;
}

static void zond_treeviewfm_init_contextmenu(ZondTreeviewFM *ztvfm,
		Projekt *zond) {
	/* Nur GActions registrieren — GMenu-Sections wurden bereits in
	 * zond_treeviewfm_class_init einmalig aufgebaut. */
	GSimpleActionGroup *ag = sond_treeview_get_action_group(
			SOND_TREEVIEW(ztvfm));

	GSimpleAction *act_jump = g_simple_action_new("jump", NULL);
	g_signal_connect(act_jump, "activate",
			G_CALLBACK(zond_treeviewfm_action_jump), zond);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_jump));
	g_object_unref(act_jump);

	GSimpleAction *act_idx_sel = g_simple_action_new("indexsuche-sel", NULL);
	g_signal_connect(act_idx_sel, "activate",
			G_CALLBACK(zond_treeviewfm_action_indexsuche_auswahl), zond);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_idx_sel));
	g_object_unref(act_idx_sel);

	GSimpleAction *act_idx_erst_sel = g_simple_action_new("index-erstellen-sel", NULL);
	g_signal_connect(act_idx_erst_sel, "activate",
			G_CALLBACK(zond_treeviewfm_action_index_erstellen_auswahl), zond);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_idx_erst_sel));
	g_object_unref(act_idx_erst_sel);

	GSimpleAction *act_idx_loesch_sel = g_simple_action_new("index-loeschen-sel", NULL);
	g_signal_connect(act_idx_loesch_sel, "activate",
			G_CALLBACK(zond_treeviewfm_action_index_loeschen_auswahl), zond);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_idx_loesch_sel));
	g_object_unref(act_idx_loesch_sel);

	GSimpleAction *act_eingang_setzen = g_simple_action_new("eingang-setzen", NULL);
	g_signal_connect(act_eingang_setzen, "activate",
			G_CALLBACK(zond_treeviewfm_action_eingang_setzen), zond);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_eingang_setzen));
	g_object_unref(act_eingang_setzen);

	GSimpleAction *act_eingang_entf = g_simple_action_new("eingang-entfernen", NULL);
	g_signal_connect(act_eingang_entf, "activate",
			G_CALLBACK(zond_treeviewfm_action_eingang_entfernen), zond);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_eingang_entf));
	g_object_unref(act_eingang_entf);

	GSimpleAction *act_eingang_neu = g_simple_action_new("eingang-neu", NULL);
	g_signal_connect(act_eingang_neu, "activate",
			G_CALLBACK(zond_treeviewfm_action_eingang_neu), zond);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_eingang_neu));
	g_object_unref(act_eingang_neu);
}

/* Getter fuer sond_treeviewfm_set_index_ctx_func(): liefert den aktuellen
 * SondIndexCtx* des Projekts (oder NULL, falls kein Projekt/wctx/index_ctx
 * vorhanden ist - dann zeichnet SondTreeviewFM einfach keine Overlay-Icons
 * fuer den Indizierungsstatus). */
static SondIndexCtx* zond_treeviewfm_get_index_ctx_cb(gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	return zond_project_index_ctx(zond);
}

//Woher die Pflege Datenbank und Index nimmt: das jeweils geöffnete Projekt
static DBaseZond* pflege_dbase(gpointer data) {
	return ((Projekt*) data)->proj.dbase_zond;
}

static SondIndexCtx* pflege_index_ctx(gpointer data) {
	Projekt *zond = data;

	return (zond->proj.wctx && zond->proj.wctx->index_ctx) ? zond->proj.wctx->index_ctx : NULL;
}

ZondTreeviewFM* zond_treeviewfm_new(Projekt* zond) {
	ZondTreeviewFM* ztvfm = NULL;
	ZondTreeviewFMPrivate* ztvfm_priv = NULL;

	ztvfm = g_object_new(ZOND_TYPE_TREEVIEWFM, NULL);
	ztvfm_priv = zond_treeviewfm_get_instance_private(ztvfm);

	ztvfm_priv->zond = zond;
	{
		SondProjektKontext kontext = { pflege_dbase, pflege_index_ctx, zond };

		ztvfm_priv->pflege = sond_projekt_pflege_new(&kontext);
	}

	sond_treeviewfm_set_index_ctx_func(SOND_TREEVIEWFM(ztvfm),
			zond_treeviewfm_get_index_ctx_cb, zond);

	zond_treeviewfm_init_contextmenu(ztvfm, zond);

	g_signal_connect(ztvfm, "before-delete",
			G_CALLBACK(zond_treeviewfm_before_delete), NULL);
	g_signal_connect(ztvfm, "before-move",
			G_CALLBACK(zond_treeviewfm_before_move), NULL);
	g_signal_connect(ztvfm, "before-insert",
			G_CALLBACK(zond_treeviewfm_before_insert), NULL);
	g_signal_connect(ztvfm, "after",
			G_CALLBACK(zond_treeviewfm_after), NULL);
	g_signal_connect(ztvfm, "after-insert",
			G_CALLBACK(zond_treeviewfm_after_insert), NULL);

	return ztvfm;
}

static gint zond_treeviewfm_find_section(ZondTreeviewFM *ztvfm,
		GtkTreeIter* iter, Anbindung anbindung, gboolean open,
		GtkTreeIter *iter_res, GError **error) {
	GtkTreeIter iter_child = { 0 };

	if (!sond_treeviewfm_first_loaded_child(SOND_TREEVIEWFM(ztvfm), iter, open,
			&iter_child))
		return 0;

	do {
		gchar const* section_child = NULL;
		SondTVFMItem* stvfm_item_child = NULL;
		Anbindung anbindung_child = { 0 };

		stvfm_item_child = sond_tvfm_item_from_view(GTK_TREE_VIEW(ztvfm), &iter_child);
		if (!stvfm_item_child) //Dummy-Zeile
			continue;
		section_child =
				sond_tvfm_item_get_path_or_section(stvfm_item_child);
		anbindung_parse_file_section(section_child, &anbindung_child);
		g_object_unref(stvfm_item_child);

		if (anbindung_1_eltern_von_2(anbindung_child, anbindung)) {
			gint rc = 0;

			rc = zond_treeviewfm_find_section(ztvfm, &iter_child, anbindung, open, iter_res, error);
			if (rc == -1)
				return -1;

			return rc;
		}
		else if (anbindung_1_gleich_2(anbindung, anbindung_child)) {
			if(iter_res)
				*iter_res = iter_child;

			return 1;
		}
	} while (gtk_tree_model_iter_next(
			gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)), &iter_child));

	return 0;
}

gint zond_treeviewfm_section_visible(ZondTreeviewFM *ztvfm,
		gchar const *file_part, gchar const *section, gboolean open,
		gboolean *visible, GtkTreeIter *iter, gboolean *children,
		gboolean *opened, GError **error) {
	gint rc = 0;
	GtkTreeIter iter_intern = { 0 };

	if (!open && !visible)
		return 0;

	rc = sond_treeviewfm_file_part_visible(SOND_TREEVIEWFM(ztvfm), NULL, file_part, open,
			&iter_intern, error);
	if (rc == -1)
		return -1;
	else if (rc == 0)
		return 0;

	if (section) {
		gint rc = 0;
		Anbindung anbindung = { 0 };
		GtkTreeIter iter_very_intern = { 0 };

		anbindung_parse_file_section(section, &anbindung);

		rc = zond_treeviewfm_find_section(ztvfm, &iter_intern, anbindung, open, &iter_very_intern, error);
		if (rc == -1)
			return -1;
		else if (rc == 0) {
			if (visible)
				*visible = FALSE;

			return 0;
		}
		iter_intern = iter_very_intern;
	}

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
		return -1;

	if (visible)
		sond_treeview_set_cursor(SOND_TREEVIEW(ztvfm), &iter);

	return 0;
}

/* ACHTUNG - Abhängigkeit von GTK3-INTERNA: Die folgenden drei Funktionen
 * (zond_treeviewfm_walk_tree(), _move_node(), zond_treeviewfm_kill_parent())
 * casten GtkTreeIter::user_data direkt auf GNode*, hängen Teilbäume per
 * g_node_unlink()/g_node_insert_after() zwischen zwei Positionen im Baum
 * um und feuern row_inserted/row_deleted/row_has_child_toggled von Hand,
 * statt eine öffentliche GtkTreeStore-Funktion aufzurufen. Grund: GTK3
 * bietet keine öffentliche API, mit der sich ein kompletter Teilbaum
 * (Knoten + alle Nachfahren) in einem GtkTreeStore an eine andere Stelle
 * umhängen ließe, ohne ihn komplett neu aufzubauen (rekursives
 * Entfernen+Neueinfügen jedes einzelnen Nachfahren, inkl. Verlust aller
 * daran hängenden GtkTreeRowReferences/Iteratoren) - gtk_tree_store_swap()
 * und _move_before()/_move_after() bewegen laut GTK3-Doku nur einen
 * einzelnen Knoten unter unverändertem Parent, gerade nicht das hier
 * gebrauchte "ganzen Teilbaum an neue Stelle" (auch unter neuem Parent).
 * Das funktioniert nur, weil GtkTreeStore intern tatsächlich mit GNode
 * arbeitet (s. gtktreestore.c in der GTK3-Quelle) - das ist aber ein
 * Implementierungsdetail, kein Teil der öffentlichen API/ABI, und könnte
 * sich mit einer künftigen GTK3-Version (oder gar innerhalb 3.x) ändern,
 * ohne dass der Compiler etwas davon merkt. Bei einem GTK3-Minor-Update
 * (oder erst recht bei einem Wechsel auf GTK4, das den Tree-Store-Unterbau
 * ohnehin grundlegend anders modelliert) muss dieser Abschnitt gezielt
 * gegen die dann aktuelle gtktreestore.c-Implementierung gegengeprüft
 * werden. */
#define G_NODE(node) ((GNode *)node)
static void zond_treeviewfm_walk_tree(GtkTreeModel *model, gint stamp,
		GNode *node) {
	GNode *child = NULL;
	GtkTreeIter iter = { 0 };
	GtkTreePath *path = NULL;

	iter.stamp = stamp;
	iter.user_data = node;
	path = gtk_tree_model_get_path(model, &iter);

	gtk_tree_model_row_inserted(model, path, &iter);

	if (node->parent->parent != NULL) {
		if (node->prev == NULL && node->next == NULL) {
			GtkTreeIter new_iter = { 0 };
			gtk_tree_path_up(path);
			new_iter.stamp = stamp;
			new_iter.user_data = node->parent;
			gtk_tree_model_row_has_child_toggled(model, path, &new_iter);
		}
	}
	gtk_tree_path_free(path);

	child = node->children;
	while (child) {
		zond_treeviewfm_walk_tree(model, stamp, child);

		child = child->next;
	}

	return;
}

static void zond_treeviewfm_move_node(GtkTreeModel *model, GtkTreeIter *iter_src,
		GtkTreeIter *anchor, gboolean child) {
	GNode *node_src = NULL;
	GNode *node_src_parent = NULL;
	GtkTreePath *path = NULL;

	node_src = iter_src->user_data;
	node_src_parent = node_src->parent;

	path = gtk_tree_model_get_path(model, iter_src);

	g_node_unlink(node_src);

	gtk_tree_model_row_deleted(model, path);

	if (node_src_parent->parent != NULL) {
		if (node_src_parent->children == NULL) {
			GtkTreeIter new_iter = { 0, };
			gtk_tree_path_up(path);
			new_iter.stamp = iter_src->stamp;
			new_iter.user_data = node_src_parent;
			gtk_tree_model_row_has_child_toggled(model, path, &new_iter);
		}
	}
	gtk_tree_path_free(path);

	if (child) {
		GNode *node_anchor = NULL;

		if (anchor)
			node_anchor = anchor->user_data;
		else {
			GtkTreeIter iter_first = { 0 };

			gtk_tree_model_get_iter_first(model, &iter_first);
			node_anchor = ((GNode*) (iter_first.user_data))->parent;
		}

		g_node_insert_after(node_anchor, NULL, node_src);
	} else {
		g_node_insert_after( G_NODE(anchor->user_data)->parent,
				G_NODE(anchor->user_data), node_src);
	}

	zond_treeviewfm_walk_tree(model, iter_src->stamp, node_src);

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
