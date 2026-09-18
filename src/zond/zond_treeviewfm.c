#include "zond_treeviewfm.h"

#include <glib/gstdio.h>
#include <sys/stat.h>

#include "../misc.h"
#include "../sond_fileparts.h"
#include "../sond_renderer.h"
#include "../sond_index.h"
#include "../sond_process_file.h"
#include "../sond_ocr.h"
#include "../sond_log_and_error.h"
#include "../sond_file_helper.h"
#include "../sond_mime.h"
#include "zond_indexsuche.h"

#include "zond_dbase.h"
#include "zond_treeview.h"
#include "zond_tree_store.h"

#include "10init/app_window.h"
#include "10init/headerbar.h"
#include "20allgemein/project.h"
#include "40viewer/viewer.h"
#include "40viewer/document.h"
#include "99conv/general.h"

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32

typedef struct {
	Projekt *zond;

	/* Übergabe zond_treeviewfm_before_delete() -> zond_treeviewfm_after():
	 * Pfad des gerade gelöschten Knotens, damit NACH der tatsächlichen
	 * physischen Löschung (also erst im "after"-Handler, wenn
	 * g_dir_open()/g_dir_read_name() den Knoten nicht mehr sehen) geprüft
	 * werden kann, ob das Elternverzeichnis dadurch jetzt vollständig
	 * abgedeckt ist (sond_index_ctx_coverage_try_collapse()). NULL, wenn
	 * für die aktuelle Löschung keine Coverage-Prüfung nötig/möglich ist.
	 * Wird in before_delete() ausschließlich unmittelbar vor dem
	 * erfolgreichen return gesetzt, damit bei einem Fehler-return (kein
	 * "after" wird dann emittiert) nichts hängen bleibt. */
	gchar *pending_delete_path;

	/* Übergabe zond_treeviewfm_before_move() -> zond_treeviewfm_after():
	 * alter/neuer absoluter Pfad der gerade vorgemerkten Verschiebung/
	 * Umbenennung, für den Fehlerbericht (s. write_commit_failure_report())
	 * IMMER gesetzt, unabhängig davon, ob es sich um eine physische
	 * Dateisystem-Aktion handelte. pending_move_is_physical ist nur dann
	 * TRUE, wenn weder der verschobene Knoten noch sein neuer Elternknoten
	 * einen sond_file_part haben - exakt die Bedingung, unter der die
	 * Basisklasse (sond_tvfm_item_rename()/sond_tvfm_item_copy() in
	 * sond_treeviewfm.c) real sond_rename() bzw.
	 * sond_copy_r()+sond_rmdir_r() aufruft; nur dann ist ein Revert per
	 * sond_rename() mit vertauschten Pfaden sinnvoll/zulässig (deckt
	 * sowohl den reinen Rename- als auch den Move(Kopieren+Löschen)-Fall
	 * ab, weil das Dateisystem danach gleich aussieht). FALSE z.B. bei
	 * reiner GMessage-Index-Renumerierung ohne physische Aktion. Wird -
	 * wie pending_delete_path - erst unmittelbar vor dem garantiert
	 * erfolgreichen return in before_move() gesetzt, damit bei einem
	 * Fehler-return (kein "after" wird dann emittiert) nichts hängen
	 * bleibt. */
	gchar *pending_move_path_old;
	gchar *pending_move_path_new;
	gboolean pending_move_is_physical;
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
			return -1;
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
		SondTVFMItem *stvfm_item, GError **error, gpointer *ctx,
		gpointer user_data) {
	gint rc = 0;
	g_autofree gchar* path = NULL;
	gint index_from = 0;
	gchar const* section = NULL;
	SondTVFMItemType type;
	gboolean from_gmessage = FALSE;
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
	if (type == SOND_TVFM_ITEM_TYPE_DIR) {
		gboolean is_container_root = sond_tvfm_item_get_sond_file_part(stvfm_item)
				&& !sond_tvfm_item_get_path_or_section(stvfm_item);
		g_autofree gchar *path_prefix = g_strconcat(path,
				is_container_root ? "//" : "/", NULL);

		rc = zond_dbase_test_path(priv->zond->dbase_zond->zond_dbase_work,
				path_prefix, error);
		if (rc == -1)
			return -1;
		else if (rc == 1)
			return 1;

		rc = zond_dbase_test_path(priv->zond->dbase_zond->zond_dbase_store,
				path_prefix, error);
		if (rc == -1)
			return -1;
		else if (rc == 1) {
			display_message(priv->zond->app_window,
					"Die zu löschende Datei bzw. der Abschnitt ist "
					"noch in der Speicher-Datenbank vorhanden.\n"
					"Bitte zuerst das Projekt speichern, "
					"bevor Sie die Datei bzw. den Abschnitt löschen.", NULL);

			return 1;
		}
	} else {
		rc = zond_dbase_test_path_section(priv->zond->dbase_zond->zond_dbase_work,
				path, section, FALSE, error);
		if (rc == -1)
			return -1;
		else if (rc == 1)
			return 1;

		rc = zond_dbase_test_path_section(priv->zond->dbase_zond->zond_dbase_store,
				path, section, FALSE, error);
		if (rc == -1)
			return -1;
		else if (rc == 1) {
			display_message(priv->zond->app_window,
					"Die zu löschende Datei bzw. der Abschnitt ist "
					"noch in der Speicher-Datenbank vorhanden.\n"
					"Bitte zuerst das Projekt speichern, "
					"bevor Sie die Datei bzw. den Abschnitt löschen.", NULL);

			return 1;
		}
	}

	/* Vorgezogen: muss vor der Wahl single-/dual-DB feststehen */
	from_gmessage = get_gmessage_index(stvfm_item, &index_from);
	if (from_gmessage)
		prefix = g_strndup(path, strlen(path) - strlen(strrchr(path, '/') + 1));

	/* Index-DB-Transaktion öffnen (vor zond-DB, damit both oder neither) */
	if (priv->zond->wctx && priv->zond->wctx->index_ctx && !section) {
		GError *idx_err = NULL;
		if (sqlite3_exec(priv->zond->wctx->index_ctx->db, "BEGIN;",
				NULL, NULL, NULL) != SQLITE_OK) {
			if (error) *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
					"%s: Index-DB BEGIN fehlgeschlagen: %s", __func__,
					sqlite3_errmsg(priv->zond->wctx->index_ctx->db));
			return -1;
		}
		if (!sond_index_ctx_clear_file(priv->zond->wctx->index_ctx, path, &idx_err)) {
			sqlite3_exec(priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
			if (error) *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
					"%s: sond_index_ctx_clear_file: %s", __func__,
					idx_err ? idx_err->message : "?");
			g_clear_error(&idx_err);
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
		if (!sond_index_ctx_coverage_clear(priv->zond->wctx->index_ctx,
				path, &idx_err)) {
			sqlite3_exec(priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
			if (error) *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
					"%s: sond_index_ctx_coverage_clear: %s", __func__,
					idx_err ? idx_err->message : "?");
			g_clear_error(&idx_err);
			return -1;
		}
	}

	/* Kontext für "after": Bit 0 = dual_write, Bit 1 = changed vor der Transaktion */
	*ctx = GINT_TO_POINTER(
			(from_gmessage ? 1 : 0) |
			(priv->zond->dbase_zond->changed ? 2 : 0));

	rc = from_gmessage ?
			dbase_zond_begin(priv->zond->dbase_zond, error) :
			zond_dbase_begin(priv->zond->dbase_zond->zond_dbase_work, error);
	if (rc) {
		if (priv->zond->wctx && priv->zond->wctx->index_ctx && !section)
			sqlite3_exec(priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
		return -1;
	}

	//wenn aus GMessage verschoben wurde - nachfolgende indizes anpassen
	if (from_gmessage) {
		gint rc = 0;

		rc = dbase_zond_update_gmessage_index(priv->zond->dbase_zond,
				prefix, index_from, FALSE, error);
		if (rc) {
			dbase_zond_rollback(priv->zond->dbase_zond, error);
			if (priv->zond->wctx && priv->zond->wctx->index_ctx && !section)
				sqlite3_exec(priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
			return -1;
		}
	}

	/* Erst jetzt, unmittelbar vor dem garantiert erfolgreichen return, für
	 * zond_treeviewfm_after() vormerken: nach der gleich folgenden
	 * physischen Löschung könnte das Elternverzeichnis von path vollständig
	 * abgedeckt sein (die gelöschte Datei war ja evtl. gerade der einzige
	 * "Lückenfüller") - s. Kommentar bei pending_delete_path. Bewusst erst
	 * hier (nach allen obigen Fehler-return-Pfaden, bei denen kein "after"
	 * emittiert wird) gesetzt, damit nichts hängen bleibt. */
	if (priv->zond->wctx && priv->zond->wctx->index_ctx && !section) {
		g_free(priv->pending_delete_path);
		priv->pending_delete_path = g_strdup(path);
	}

	/* Diese Löschung (nicht zond_treeviewfm_before_move()) hat die
	 * folgende "after"-Emission ausgelöst - ein hier evtl. dual_write=1
	 * (from_gmessage, s.o.) gesetzter Kontext betrifft eine rein
	 * datenbankinterne Index-Renumerierung, nie eine physische
	 * Dateisystem-Aktion. pending_move_path_old/_new/pending_move_is_physical
	 * daher hier zurücksetzen, damit zond_treeviewfm_after() im Fehlerfall
	 * nicht versehentlich mit veralteten Pfaden von einer früheren,
	 * unabhängigen Verschiebung revertiert (s. Kommentar bei
	 * pending_move_path_old/_new oben). */
	g_clear_pointer(&priv->pending_move_path_old, g_free);
	g_clear_pointer(&priv->pending_move_path_new, g_free);
	priv->pending_move_is_physical = FALSE;

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
 * (Bug-Fix 11.09.2026: Kopieren einer nicht indizierten Datei in einen
 * als komplett indiziert markierten Ordner ließ den Ordner fälschlich
 * grün, weil dieser Pfad bislang gar nicht auf Coverage hörte.) */
static gint zond_treeviewfm_before_insert(SondTreeviewFM* stvfm,
		SondTVFMItem* stvfm_item, SondTVFMItem* stvfm_item_parent,
		gchar const* base_new, gint index_to, GError **error,
		gpointer user_data) {
	g_autofree gchar* prefix_new = NULL;
	GError *idx_err = NULL;

	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	if (!ztvfm_priv->zond->wctx || !ztvfm_priv->zond->wctx->index_ctx)
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
	else
		prefix_new = add_string(prefix_new, g_strdup(base_new));

	if (!sond_index_ctx_coverage_invalidate(ztvfm_priv->zond->wctx->index_ctx,
			prefix_new, ztvfm_priv->zond->project_dir, &idx_err)) {
		LOG_WARN("%s: sond_index_ctx_coverage_invalidate('%s'): %s", __func__,
				prefix_new, idx_err ? idx_err->message : "?");
		g_clear_error(&idx_err);
	}

	return 0;
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

	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	prefix_old = get_path_from_stvfm_item(stvfm_item);
	prefix_new = get_path_from_stvfm_item(stvfm_item_parent);

	//Falls aus GMessage verschoben wird - welchen Index hatte Eintrag?
	from_gmessage = get_gmessage_index(stvfm_item, &index_from);

	if (*prefix_new != '\0') { //wenn nicht root-Verzeichnis
		if (!sond_tvfm_item_get_path_or_section(stvfm_item_parent))
			prefix_new = add_string(prefix_new, g_strdup("//"));
		else
			prefix_new = add_string(prefix_new, g_strdup("/"));
	}

	if (SOND_IS_FILE_PART_GMESSAGE(sond_tvfm_item_get_sond_file_part(stvfm_item_parent)))
		prefix_new = add_string(prefix_new, g_strdup("alpha")); //irgendwas alphanumerisches
	else
		prefix_new = add_string(prefix_new, g_strdup(base_new));

	//Kontext für "after" setzen: move ist immer dual (Bit 0 = 1), Bit 1 = changed vor der Transaktion
	*ctx = GINT_TO_POINTER(1 | (ztvfm_priv->zond->dbase_zond->changed ? 2 : 0));

	/* Index-DB-Transaktion öffnen und Pfad umbenennen */
	if (ztvfm_priv->zond->wctx && ztvfm_priv->zond->wctx->index_ctx) {
		GError *idx_err = NULL;
		if (sqlite3_exec(ztvfm_priv->zond->wctx->index_ctx->db, "BEGIN;",
				NULL, NULL, NULL) != SQLITE_OK) {
			if (error) *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
					"%s: Index-DB BEGIN fehlgeschlagen: %s", __func__,
					sqlite3_errmsg(ztvfm_priv->zond->wctx->index_ctx->db));
			return -1;
		}

		/* Vorsichtsmaßnahme: falls der ZIEL-Pfad schon (direkt oder über
		 * einen Vorfahren) als abgedeckt gilt, muss das VOR dem Umbenennen
		 * aufgelöst werden - hierher kommt gleich neuer Inhalt (die
		 * verschobene Datei/das Verzeichnis), der ggf. noch gar nicht
		 * geprüft ist und die Abdeckungs-Aussage sonst verletzen würde.
		 * Reihenfolge wichtig: muss VOR sond_index_ctx_rename_file()
		 * laufen, sonst würde der gerade erst umbenannte, korrekte eigene
		 * Eintrag der verschobenen Datei gleich wieder mitgelöscht. */
		if (!sond_index_ctx_coverage_invalidate(ztvfm_priv->zond->wctx->index_ctx,
				prefix_new, ztvfm_priv->zond->project_dir, &idx_err)) {
			sqlite3_exec(ztvfm_priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
			if (error) *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
					"%s: sond_index_ctx_coverage_invalidate: %s", __func__,
					idx_err ? idx_err->message : "?");
			g_clear_error(&idx_err);
			return -1;
		}

		if (!sond_index_ctx_rename_file(ztvfm_priv->zond->wctx->index_ctx,
				prefix_old, prefix_new, &idx_err)) {
			sqlite3_exec(ztvfm_priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
			if (error) *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
					"%s: sond_index_ctx_rename_file: %s", __func__,
					idx_err ? idx_err->message : "?");
			g_clear_error(&idx_err);
			return -1;
		}
	}

	rc = dbase_zond_begin(ztvfm_priv->zond->dbase_zond, error);
	if (rc) {
		if (ztvfm_priv->zond->wctx && ztvfm_priv->zond->wctx->index_ctx)
			sqlite3_exec(ztvfm_priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
		return -1;
	}

	//alle Dateien, die mit filepart(stvfm_item) + path anfangen (einschließlich stvfm_item)
		//-> umbenennen
	rc = dbase_zond_update_path(ztvfm_priv->zond->dbase_zond, prefix_old, prefix_new, error);
	if (rc) {
		dbase_zond_rollback(ztvfm_priv->zond->dbase_zond, error);
		if (ztvfm_priv->zond->wctx && ztvfm_priv->zond->wctx->index_ctx)
			sqlite3_exec(ztvfm_priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
		return -1;
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
			if (ztvfm_priv->zond->wctx && ztvfm_priv->zond->wctx->index_ctx)
				sqlite3_exec(ztvfm_priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
			return -1;
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
			if (ztvfm_priv->zond->wctx && ztvfm_priv->zond->wctx->index_ctx)
				sqlite3_exec(ztvfm_priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
			g_free(prefix_gmessage);

			return -1;
		}

		//index_to als basename hinzufügen
		prefix_gmessage = add_string(prefix_gmessage, g_strdup_printf("%u", index_to));

		rc = dbase_zond_update_path(ztvfm_priv->zond->dbase_zond, prefix_new,
				prefix_gmessage, error);
		g_free(prefix_gmessage);
		if (rc) {
			dbase_zond_rollback(ztvfm_priv->zond->dbase_zond, error);
			if (ztvfm_priv->zond->wctx && ztvfm_priv->zond->wctx->index_ctx)
				sqlite3_exec(ztvfm_priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
			return -1;
		}
	}

	/* Erst hier, unmittelbar vor dem garantiert erfolgreichen return (s.
	 * Kommentar bei pending_move_path_old/_new/pending_move_is_physical
	 * oben), für zond_treeviewfm_after() vormerken: alter/neuer absoluter
	 * Pfad dieser Verschiebung/Umbenennung, samt Kennzeichnung, ob es sich
	 * dabei um eine physische Dateisystem-Aktion handelt (dann - und nur
	 * dann - kann im Fehlerfall per sond_rename() revertiert werden). */
	g_free(ztvfm_priv->pending_move_path_old);
	g_free(ztvfm_priv->pending_move_path_new);
	ztvfm_priv->pending_move_path_old = g_strconcat(
			ztvfm_priv->zond->project_dir, "/", prefix_old, NULL);
	ztvfm_priv->pending_move_path_new = g_strconcat(
			ztvfm_priv->zond->project_dir, "/", prefix_new, NULL);
	ztvfm_priv->pending_move_is_physical =
			!sond_tvfm_item_get_sond_file_part(stvfm_item) &&
			!sond_tvfm_item_get_sond_file_part(stvfm_item_parent);

	return 0;
}

/* Schreibt eine für den Anwender lesbare Klartext-Fehlerdatei
 * (ZOND_FEHLER_<Zeitstempel>.txt) ins Projektverzeichnis, wenn eine
 * Verschiebung/Umbenennung in der Datenbank nicht gespeichert werden
 * konnte und (soweit physisch möglich) auch nicht per sond_rename()
 * rückgängig gemacht werden konnte - s. zond_treeviewfm_after(). Einziger
 * dauerhafter Anhaltspunkt für Anwender/Support, weil project_close()
 * gleich im Anschluss die lokale Arbeitskopie aufräumt. path_old/path_new
 * können NULL sein (keine physische Aktion betroffen), ebenso msg_revert
 * (kein Revert versucht). Best-effort: ein Fehler beim Schreiben des
 * Berichts selbst wird nur geloggt, verhindert aber nicht das Beenden. */
static void write_commit_failure_report(Projekt *zond, gchar const *path_old,
		gchar const *path_new, gchar const *msg_commit,
		gchar const *msg_revert) {
	g_autoptr(GDateTime) now = NULL;
	g_autofree gchar *timestamp = NULL;
	g_autofree gchar *filename = NULL;
	GError *error_report = NULL;
	FILE *fp = NULL;

	if (!zond->project_dir)
		return;

	now = g_date_time_new_now_local();
	timestamp = g_date_time_format(now, "%Y%m%d_%H%M%S");
	filename = g_strdup_printf("%s/ZOND_FEHLER_%s.txt", zond->project_dir,
			timestamp);

	fp = sond_fopen(filename, "w", &error_report);
	if (!fp) {
		LOG_WARN("%s: sond_fopen('%s'): %s", __func__, filename,
				error_report ? error_report->message : "?");
		g_clear_error(&error_report);
		return;
	}

	fprintf(fp, "ZOND - Fehlerbericht\n");
	fprintf(fp, "Zeitpunkt: %s\n\n", timestamp);
	fprintf(fp, "Eine Verschiebung/Umbenennung konnte nicht gespeichert "
			"werden. Das Programm wurde sicherheitshalber beendet, "
			"nachdem versucht wurde, das Projekt zu speichern und zu "
			"schließen.\n\n");

	if (path_old && path_new) {
		fprintf(fp, "Alter Pfad: %s\n", path_old);
		fprintf(fp, "Neuer Pfad: %s\n\n", path_new);
	} else
		fprintf(fp, "(Keine physische Dateisystem-Änderung betroffen.)\n\n");

	fprintf(fp, "Fehler beim Speichern:\n%s\n\n",
			msg_commit ? msg_commit : "unbekannt");

	if (msg_revert)
		fprintf(fp, "Fehler beim Rückgängigmachen der Verschiebung:\n%s\n",
				msg_revert);
	else if (path_old && path_new)
		fprintf(fp, "Ein Rückgängigmachen wurde nicht versucht.\n");

	fclose(fp);

	return;
}

static void zond_treeviewfm_after(SondTreeviewFM* stvfm,
		gboolean suc, gpointer ctx, gpointer user_data) {
	GError* error_int = NULL;
	gint c = GPOINTER_TO_INT(ctx);
	gboolean dual_write = c & 1;
	gboolean changed_before = (c >> 1) & 1;
	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(stvfm));

	if (suc) {
		gint rc = 0;

		if (dual_write) {
			rc = dbase_zond_commit(priv->zond->dbase_zond, &error_int);
			if (rc) {
				gboolean reverted = FALSE;
				GError *error_revert = NULL;

				if (priv->zond->wctx && priv->zond->wctx->index_ctx)
					sqlite3_exec(priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);

				/* Physische Verschiebung rückgängig machen, wenn möglich
				 * (s. Kommentar bei pending_move_is_physical) - vertauschte
				 * Pfade decken sowohl reinen Rename- als auch
				 * Move(Kopieren+Löschen)-Fall ab, weil das Dateisystem
				 * danach gleich aussieht. */
				if (priv->pending_move_is_physical)
					reverted = sond_rename(priv->pending_move_path_new,
							priv->pending_move_path_old, &error_revert);

				if (reverted) {
					display_message(priv->zond->app_window,
							"Speichern der Verschiebung/Umbenennung ist "
							"fehlgeschlagen und wurde rückgängig gemacht:\n\n",
							error_int->message, "\n\nBitte erneut versuchen.",
							NULL);
					g_clear_error(&error_int);
					g_clear_pointer(&priv->pending_move_path_old, g_free);
					g_clear_pointer(&priv->pending_move_path_new, g_free);
					priv->pending_move_is_physical = FALSE;
					g_clear_pointer(&priv->pending_delete_path, g_free);

					return;
				}

				/* Revert fehlgeschlagen oder keiner möglich (z.B. reine
				 * GMessage-Index-Renumerierung ohne physische Aktion):
				 * Fehlerbericht schreiben, Projekt in einer Schleife zu
				 * schließen versuchen (Chance, ein z.B. nur kurzzeitig
				 * nicht erreichbares Netzlaufwerk zwischenzeitlich zu
				 * beheben), dann in jedem Fall beenden. */
				write_commit_failure_report(priv->zond,
						priv->pending_move_is_physical ? priv->pending_move_path_old : NULL,
						priv->pending_move_is_physical ? priv->pending_move_path_new : NULL,
						error_int->message,
						error_revert ? error_revert->message : NULL);

				display_message(priv->zond->app_window,
						"Kritischer Fehler beim Speichern einer "
						"Verschiebung/Umbenennung. Projekt und Datenbank "
						"können inkonsistent sein. Details wurden in eine "
						"Fehlerdatei im Projektordner geschrieben "
						"(ZOND_FEHLER_*.txt). Das Programm wird jetzt "
						"beendet.", NULL);

				g_clear_error(&error_int);
				g_clear_error(&error_revert);

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
		} else {
			rc = zond_dbase_commit(priv->zond->dbase_zond->zond_dbase_work, &error_int);
			if (rc) {
				if (priv->zond->wctx && priv->zond->wctx->index_ctx)
					sqlite3_exec(priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
				display_message(priv->zond->app_window,
						"Fehler beim Speichern:\n\n", error_int->message, NULL);
				g_clear_error(&error_int);
			}
		}
		/* Löschung war erfolgreich: falls dabei ein Pfad zur
		 * Coverage-Nachprüfung vorgemerkt wurde (s. pending_delete_path),
		 * jetzt - und nur jetzt, weil der Knoten physisch weg ist und ein
		 * echtes Verzeichnis-Listing ihn nicht mehr sieht - prüfen, ob das
		 * Elternverzeichnis dadurch vollständig abgedeckt ist (die gerade
		 * gelöschte Datei war ja evtl. der einzige "Lückenfüller"). Bug-Fix
		 * 11.09.2026: sonst blieb ein Ordner nach Löschen der zuvor
		 * hineinkopierten, nicht indizierten Datei dauerhaft "orange"
		 * (gemischt), obwohl wieder alle verbliebenen Geschwister einzeln
		 * abgedeckt waren - es fehlte lediglich das erneute Zusammenfassen
		 * (Coalescing) zu einem Ordner-Eintrag. Vor dem COMMIT, damit der
		 * evtl. neu gesetzte coverage-Eintrag Teil derselben Transaktion
		 * ist wie das Löschen selbst. */
		if (priv->pending_delete_path && priv->zond->wctx &&
				priv->zond->wctx->index_ctx) {
			GError *collapse_error = NULL;

			if (!sond_index_ctx_coverage_try_collapse(
					priv->zond->wctx->index_ctx, priv->pending_delete_path,
					priv->zond->project_dir, &collapse_error)) {
				LOG_WARN("%s: sond_index_ctx_coverage_try_collapse('%s'): %s",
						__func__, priv->pending_delete_path,
						collapse_error ? collapse_error->message : "?");
				g_clear_error(&collapse_error);
			}
		}

		if (priv->zond->wctx && priv->zond->wctx->index_ctx)
			sqlite3_exec(priv->zond->wctx->index_ctx->db, "COMMIT;", NULL, NULL, NULL);
	}
	else {
		if (dual_write)
			dbase_zond_rollback(priv->zond->dbase_zond, &error_int);
		else
			zond_dbase_rollback(priv->zond->dbase_zond->zond_dbase_work, &error_int);

		if (priv->zond->wctx && priv->zond->wctx->index_ctx)
			sqlite3_exec(priv->zond->wctx->index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
	}

	if (dual_write)
		project_reset_changed(priv->zond, changed_before);

	g_clear_pointer(&priv->pending_delete_path, g_free);
	g_clear_pointer(&priv->pending_move_path_old, g_free);
	g_clear_pointer(&priv->pending_move_path_new, g_free);
	priv->pending_move_is_physical = FALSE;

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
			return -1;

		if (ID_section == 0) {
			if (error) *error = g_error_new(ZOND_ERROR, 0,
					"%s\nAbschnitt nicht gefunden", __func__);

			return -1;
		}

		rc = zond_dbase_update_node_text(
				ztvfm_priv->zond->dbase_zond->zond_dbase_work, ID_section,
				new_text, error);
		if (rc)
			return -1;

		//Text in treeview anpassen
		iter = zond_tree_store_get_iter_by_node_id(
				ZOND_TREE_STORE(gtk_tree_view_get_model(
						GTK_TREE_VIEW(ztvfm_priv->zond->treeview[BAUM_INHALT]))),
				ID_section);

		if (iter) {
			zond_tree_store_set(iter, NULL, new_text, 0);
			gtk_tree_iter_free(iter);
		}
	}
	else { //chain-up, wenn nicht erledigt
		gint rc = 0;

		rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->text_edited(stvfm,
				iter, stvfm_item, new_text, error);
		if (rc)
			return -1;
	}

	if (!changed)
		project_reset_changed(ztvfm_priv->zond, FALSE);

	return 0;
}

static void zond_treeviewfm_results_row_activated(GtkTreeView *treeview,
		GtkTreePath *tree_path, GtkTreeViewColumn *col, gpointer data) {
	ZondTreeviewFM *ztvfm = (ZondTreeviewFM*) data;
	ZondTreeviewFMPrivate *ztvfm_priv = zond_treeviewfm_get_instance_private(
			ztvfm);

	if (!gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(ztvfm_priv->zond->fs_button)))
		gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(ztvfm_priv->zond->fs_button), TRUE);

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

		if (ztvfm_priv->zond->state & GDK_CONTROL_MASK) {
			if (!anbindung_is_pdf_punkt(anbindung))
				anbindung_ges = anbindung;
			else { //Eltern-iter holen
				GtkTreeIter iter_parent = { 0 };
				GtkTreeModel* model = NULL;
				SondTVFMItem* stvfm_item_parent = NULL;
				gchar const* section_parent = NULL;

				model = gtk_tree_view_get_model(GTK_TREE_VIEW(
						sond_tvfm_item_get_stvfm(stvfm_item)));
				if (!gtk_tree_model_iter_parent(model, &iter_parent, iter)) {
					LOG_WARN("Elter-Iter konnte nicht ermittelt werden");
					//dann halt weiter mit ganzem PDF...
				}
				gtk_tree_model_get(model, &iter_parent, 0, &stvfm_item_parent, -1);
				section_parent = sond_tvfm_item_get_path_or_section(stvfm_item_parent);
				if (section_parent)
					anbindung_parse_file_section(section, &anbindung_ges);
				g_object_unref(stvfm_item_parent);
			}
		}

		dd = document_new_displayed_document(sfp_pdf, &anbindung_ges, &anbindung,
				(ztvfm_priv->zond->state & GDK_MOD1_MASK), &pdf_pos, error);
		if (!dd)
			return -1;

		rc = zond_treeview_oeffnen_internal_viewer(ztvfm_priv->zond,
				dd, &pdf_pos, error);
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

	rc = zond_dbase_get_section(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
			filepart, section, &ID, error);
	g_free(filepart);
	if (rc)
		return -1;

	/* Ist ID selbst oder ein Abkömmling von ID im Bestandsverzeichnis
	 * angebunden, wurde die Löschung bereits in
	 * zond_treeviewfm_before_delete verhindert - diese Funktion hier wird
	 * für einen angebundenen Abschnitt also nie erreicht. */
	rc = zond_dbase_remove_node(ztvfm_priv->zond->dbase_zond->zond_dbase_work, ID, error);
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

	rc = zond_dbase_get_section(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
			filepart, section, &ID, error);
	if (rc)
		return -1;

	if (section && ID == 0) {
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s\nAbschnitt nicht gefunden", __func__);

		return -1;
	}

	rc = zond_dbase_get_first_child(ztvfm_priv->zond->dbase_zond->zond_dbase_work, ID, &child, error);
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

		rc = zond_dbase_get_node(ztvfm_priv->zond->dbase_zond->zond_dbase_work, child,
				NULL, NULL, NULL, &section_child, NULL, NULL, NULL, error);
		if (rc)
			return -1;

		stvfm_item_child =
				sond_tvfm_item_create(sond_tvfm_item_get_stvfm(stvfm_item),
						sfp, section_child);
		g_free(section_child);
		sond_tvfm_item_set_icon_name(stvfm_item_child,
				ztvfm_priv->zond->icon[ICON_ANBINDUNG].icon_name);
		g_ptr_array_add(arr_children_int, stvfm_item_child);

		rc = zond_dbase_get_younger_sibling(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
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

	rc = zond_dbase_get_section(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
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
		rc = zond_dbase_get_node(ztvfm_priv->zond->dbase_zond->zond_dbase_work,
				ID, NULL, NULL, NULL, NULL, NULL, text, NULL, error);
		if (rc)
			return -1;
	}

	return 0;
}

/* zond_treeviewfm_get_fileparts() und Helfer:
 *
 * Eigene, zond-spezifische Variante von sond_treeviewfm_get_fileparts()
 * (sond_treeviewfm.c). Die generische Basisklasse kennt nur "ganze Datei"
 * (NULL) als Wert, weil sie bewusst nicht weiß, was eine "Section" eines
 * SOND_TVFM_ITEM_TYPE_LEAF_SECTION-Knotens bedeutet (bei PDF ein
 * Seitenbereich, bei anderen Dateitypen ggf. etwas ganz anderes - z.B.
 * ein Zeitausschnitt bei Audio/Video). Nur zond selbst weiß: in BAUM_FS
 * ist ein LEAF_SECTION-Knoten immer eine Anbindung (angelegt in
 * ziele.c), deren path_or_section ein Seitenbereich ist. Deshalb baut
 * diese Funktion die Hashtable komplett selbst, ausschließlich über die
 * schon vorhandenen öffentlichen SondTVFMItem-Zugriffsfunktionen
 * (sond_treeviewfm.h) - ohne jede Änderung an der Basisklasse. */
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
 * hineingestiegen - das war beim bisherigen Weg für "Gesamtes Projekt"/
 * ordnerbasierte "Auswahl" ohnehin nie der Fall: ein frisch entdeckter
 * Container-Top-Knoten hat sond_file_part != NULL UND path_or_section ==
 * NULL, fällt unten in zond_treeviewfm_item_get_fileparts() also stets in
 * den ELSE-Zweig (ein einziger, opaker Filepart, da "application/zip" &
 * Co. ohnehin nicht indizierbar sind) - keine Verhaltensänderung. Inhalte
 * innerhalb eines Containers werden weiterhin nur über eine explizite
 * Auswahl darin erreicht (sond_file_part != NULL UND path_or_section
 * gesetzt) - dafür bleibt der bisherige, echte Weg unverändert, s.
 * zond_treeviewfm_item_get_fileparts(). ToDo.c, 12.-14.09.2026. */
gint zond_treeviewfm_item_get_fileparts_readdir(SondTreeviewFM *stvfm,
		gchar const *rel_dir, GHashTable *ht, gboolean skip_fully_covered,
		GError **error) {
	gchar const *root = sond_treeviewfm_get_root(stvfm);
	gchar *path_dir = NULL;
	SondDir *dir = NULL;
	gchar const *filename = NULL;

	/* Nutzer-Wunsch 16.09.2026 (Task #100): derselbe Verzeichnis-
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

		if (priv->zond->wctx && priv->zond->wctx->index_ctx) {
			SondIndexStatus status = sond_index_ctx_get_dir_status(
					priv->zond->wctx->index_ctx, rel_dir);

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

		if (sond_stat(rel_path_child, &st, &error_stat)) {
			LOG_WARN("%s: sond_stat('%s') gibt Fehler zurück: %s", __func__,
					rel_path_child,
					error_stat ? error_stat->message : "?");
			g_clear_error(&error_stat);
			g_free(rel_path_child);
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			gint rc = zond_treeviewfm_item_get_fileparts_readdir(stvfm,
					rel_path_child, ht, skip_fully_covered, error);
			g_free(rel_path_child);
			if (rc) {
				sond_dir_close(dir);
				return -1;
			}
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
		GPtrArray *arr_children = NULL;
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
			 * (99conv/general.h) und ToDo.c (11.09.2026,
			 * Nutzerentscheidung). */
			if (reject_unterseitig && anbindung_ist_unterseitig(anbindung)) {
				g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
						"Die Auswahl enthält eine unterseitige Anbindung "
						"(beginnt/endet nicht an einer Seitengrenze) - das "
						"ist für Indizierung/Index löschen nicht zulässig.");
				return -1;
			}

			range = sond_page_range_new(anbindung.von.seite,
					(anbindung.bis.seite == 0 && anbindung.bis.index == 0) ?
							anbindung.von.seite : anbindung.bis.seite);
		}
		else if (type == SOND_TVFM_ITEM_TYPE_LEAF && !path_or_section &&
				SOND_IS_FILE_PART_GMESSAGE(sond_file_part)) {
			/* Der "Message"-Knoten einer E-Mail: type==LEAF, path_or_section
			 * ist NULL (der "//message"-Marker wird beim Erzeugen des Items
			 * sofort gelöscht, s. sond_tvfm_item_create()), und sond_file_part
			 * ist derselbe SondFilePart wie der der gesamten eml - eindeutig
			 * unterscheidbar von "das ganze Dir/die ganze Datei" nur über
			 * diesen Item-Typ zum Zeitpunkt der Auswahl (s. ToDo.c,
			 * 17.09.2026, E-Mail-Coverage-Redesign, Schritt 2/6). Statt der
			 * ganzen Mail wird für diesen Eintrag nur der Header indiziert. */
			range = sond_page_range_new_gmessage_header();
		}

		g_hash_table_insert(ht, g_object_ref(sond_file_part), range);
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

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stv)),
			iter, 0, &stvfm_item, -1);
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
 * Nutzer-Einwand 16.09.2026: liefert jetzt den fertigen SondIndexStatus
 * statt nur des Seitenbereichs (s. ausführlichen Doc-Kommentar an der
 * vfunc-Deklaration in sond_treeviewfm.h) - "Section = Seitenbereich" ist
 * eine zond/PDF-spezifische Interpretation, die hier (in der zond-Subklasse)
 * hingehört und nicht in die generische Basisklasse gehört. Übernimmt dafür
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

	if (!SOND_IS_FILE_PART_PDF(sfp) && !SOND_IS_FILE_PART_GMESSAGE(sfp)) {
		gchar const *mime_type = SOND_IS_FILE_PART_LEAF(sfp) ?
				sond_file_part_leaf_get_mime_type(SOND_FILE_PART_LEAF(sfp)) :
				mime_from_extension(coverage_path);

		if (!sond_index_mime_type_supported(mime_type)) {
			g_free(coverage_path);
			return SOND_INDEX_STATUS_NONE;
		}
	}

	anbindung_parse_file_section(section, &anbindung);

	von_seite = anbindung.von.seite;
	bis_seite = (anbindung.bis.seite == 0 && anbindung.bis.index == 0) ?
			anbindung.von.seite : anbindung.bis.seite;

	status = sond_index_ctx_get_file_status(index_ctx, coverage_path, von_seite,
			bis_seite);
	g_free(coverage_path);

	return status;
}

static void zond_treeviewfm_finalize(GObject *obj) {
	ZondTreeviewFMPrivate *priv = zond_treeviewfm_get_instance_private(
			ZOND_TREEVIEWFM(obj));

	g_clear_pointer(&priv->pending_delete_path, g_free);
	g_clear_pointer(&priv->pending_move_path_old, g_free);
	g_clear_pointer(&priv->pending_move_path_new, g_free);

	G_OBJECT_CLASS(zond_treeviewfm_parent_class)->finalize(obj);
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
	SOND_TREEVIEWFM_CLASS(klass)->load_sections = zond_treeviewfm_load_sections;
	SOND_TREEVIEWFM_CLASS(klass)->has_sections = zond_treeviewfm_has_sections;
	SOND_TREEVIEWFM_CLASS(klass)->delete_section = zond_treeviewfm_delete_section;
	SOND_TREEVIEWFM_CLASS(klass)->get_section_index_status =
			zond_treeviewfm_get_section_index_status;

	/* Zond-spezifische GMenu-Sections einmalig fuer diese Klasse aufbauen */
	SOND_TREEVIEW_CLASS(klass)->gmenu = g_menu_new();
	sond_treeviewfm_add_base_menu(SOND_TREEVIEW_CLASS(klass)->gmenu);

	// dann die ZTV-FM-Sections
	GMenu *gmenu = SOND_TREEVIEW_CLASS(klass)->gmenu;

	GMenu *sec_jump = g_menu_new();
	g_menu_append(sec_jump, "Zur Anbindung springen", "stv.jump");
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_jump));
	g_object_unref(sec_jump);

	/* "Index"-Untermen\u00fc analog zum Hauptmen\u00fc (headerbar.c), hier aber -
	 * wie bei "SeaDrive" im Kontextmen\u00fc - bewusst nur "Auswahl" je Aktion,
	 * kein "Gesamtes Projekt" (Nutzerwunsch 11.09.2026: Parit\u00e4t der
	 * Men\u00fcstruktur zwischen Index und SeaDrive). */
	GMenu *sec_idx = g_menu_new();
	GMenu *sub_idx = g_menu_new();
	g_menu_append(sub_idx, "Erstellen",   "stv.index-erstellen-sel");
	g_menu_append(sub_idx, "Durchsuchen", "stv.indexsuche-sel");
	g_menu_append(sub_idx, "Löschen",     "stv.index-loeschen-sel");
	g_menu_append_submenu(sec_idx, "Index", G_MENU_MODEL(sub_idx));
	g_object_unref(sub_idx);
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_idx));
	g_object_unref(sec_idx);

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

static void zond_treeviewfm_action_jump(GSimpleAction *a, GVariant *p,
		gpointer d) {
	zond_treeviewfm_jump_activate(NULL, d);
}

static void zond_treeviewfm_action_indexsuche_auswahl(GSimpleAction *a,
		GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;

	/* baum_active statt Scan: bei Rechtsklick im Dateiverzeichnis synchron
	 * per focus-in gesetzt (s. cb_treeview_focus_in, app_window.c), also
	 * hier zuverlässig BAUM_FS - anders als beim globalen Fenstermenü
	 * (s. zond_indexsuche_activate_fuer_baum() in zond_indexsuche.c). */
	zond_indexsuche_activate_fuer_baum(zond, zond->baum_active);
}

static void zond_treeviewfm_action_index_erstellen_auswahl(GSimpleAction *a,
		GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;

	/* Analogon zu zond_treeviewfm_action_indexsuche_auswahl() oberhalb,
	 * s. dortigen Kommentar. */
	zond_index_erstellen_activate_fuer_baum(zond, zond->baum_active);
}

static void zond_treeviewfm_action_index_loeschen_auswahl(GSimpleAction *a,
		GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;

	/* Analogon zu zond_treeviewfm_action_indexsuche_auswahl() oberhalb,
	 * s. dortigen Kommentar. */
	zond_index_loeschen_activate_fuer_baum(zond, zond->baum_active);
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
}

/* Getter fuer sond_treeviewfm_set_index_ctx_func(): liefert den aktuellen
 * SondIndexCtx* des Projekts (oder NULL, falls kein Projekt/wctx/index_ctx
 * vorhanden ist - dann zeichnet SondTreeviewFM einfach keine Overlay-Icons
 * fuer den Indizierungsstatus). */
static SondIndexCtx* zond_treeviewfm_get_index_ctx_cb(gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	if (!zond || !zond->wctx) return NULL;

	return zond->wctx->index_ctx;
}

ZondTreeviewFM* zond_treeviewfm_new(Projekt* zond) {
	ZondTreeviewFM* ztvfm = NULL;
	ZondTreeviewFMPrivate* ztvfm_priv = NULL;

	ztvfm = g_object_new(ZOND_TYPE_TREEVIEWFM, NULL);
	ztvfm_priv = zond_treeviewfm_get_instance_private(ztvfm);

	ztvfm_priv->zond = zond;

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
		return 0;

	gtk_tree_model_get(
			gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)),
			&iter_child, 0, &stvfm_item, -1);

	if (!stvfm_item) //dummy
	{
		if (!open)
			return 0;

		sond_treeview_expand_row(SOND_TREEVIEW(ztvfm), iter);

		gtk_tree_model_iter_children(
				gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)), &iter_child, iter);
	}
	else
		g_object_unref(stvfm_item);

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
		pos = g_node_child_position( G_NODE(anchor->user_data)->parent,
				node_src);
	}

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
