/*
 sond (gtk4/sond_projekt_treeviewfm.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_projekt_treeviewfm.h"

#include "sond_treeviewfm_private.h"
#include "sond_eingang_dialog.h"
#include "../sond_eingang_pflege.h"
#include "../sond_log_and_error.h"
#include "../tvfm/sond_tvfm_index_sync.h"
#include "../znd/sond_projekt_index.h"

typedef struct {
	SondProjektHooks hooks;
	SondProjektPflege *pflege;
	gboolean insert_aktiv; //zwischen before-insert und after-insert ist ein write_begin offen
} SondProjektTreeviewFMPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondProjektTreeviewFM, sond_projekt_treeviewfm, SOND_TYPE_TREEVIEWFM)

/* Was nach einer erfolgreichen Dateioperation ohne Projektdatei im Index nachzuführen ist */
typedef struct {
	gchar *alt;       //projektrelativer Pfad vorher
	gchar *neu;       //nachher (NULL beim Löschen)
} IdxOp;

//Kontext zwischen before-* und after
typedef struct {
	gboolean pflege;  //über sond_projekt_pflege (Transaktion offen)
	gboolean dual;
	IdxOp *op;        //sonst: nur der Index
} OpCtx;

static void idxop_free(IdxOp *op) {
	if (!op)
		return;
	g_free(op->alt);
	g_free(op->neu);
	g_free(op);
}

static void opctx_free(OpCtx *oc) {
	if (!oc)
		return;
	idxop_free(oc->op);
	g_free(oc);
}

//Projektrelativer Zielpfad parent/base; FALSE, wenn das Ziel nicht im Dateisystem liegt
static gboolean ziel_pfad(SondTVFMItem *parent, const gchar *base, gchar **neu) {
	const gchar *eltern = NULL;

	if (!parent || sond_tvfm_item_get_sond_file_part(parent))
		return FALSE;

	eltern = sond_tvfm_item_get_path_or_section(parent);
	*neu = eltern ? g_strconcat(eltern, "/", base, NULL) : g_strdup(base);

	return TRUE;
}

static SondProjektTreeviewFMPrivate* priv_of(SondTreeviewFM *tree) {
	return sond_projekt_treeviewfm_get_instance_private(SOND_PROJEKT_TREEVIEWFM(tree));
}

/* Kontext der Pflege: Datenbank und Index kommen bei jedem Aufruf von der Anwendung */
static DBaseZond* kontext_dbase(gpointer data) {
	SondProjektTreeviewFMPrivate *priv = data;

	return priv->hooks.dbase ? priv->hooks.dbase(priv->hooks.data) : NULL;
}

static SondIndexCtx* kontext_index_ctx(gpointer data) {
	SondProjektTreeviewFMPrivate *priv = data;

	return priv->hooks.index_ctx ? priv->hooks.index_ctx(priv->hooks.data) : NULL;
}

static void melden(SondProjektTreeviewFMPrivate *priv, gchar const *title, gchar const *text) {
	if (priv->hooks.message)
		priv->hooks.message(priv->hooks.data, title, text);
	else
		g_warning("%s: %s", title, text);
}

/* ---- Abschnitte (Anbindungen) ----
 *
 * Gelesen wird aus der Datenbank, die die Anwendung liefert (Client: die lesend gehaltene
 * Projektdatei; zond: work). Fehlt sie, gibt es keine Abschnitte. */

static SondProjektTreeviewFMPrivate* priv_of_item(SondTVFMItem *item) {
	GObject *tree = G_OBJECT(sond_tvfm_item_get_stvfm(item));

	return SOND_IS_PROJEKT_TREEVIEWFM(tree) ?
			sond_projekt_treeviewfm_get_instance_private(SOND_PROJEKT_TREEVIEWFM(tree)) : NULL;
}

//Pflege des Baums, zu dem der Eintrag gehört, wenn eine Datenbank vorliegt, sonst NULL
static SondProjektPflege* pflege_of_item(SondTVFMItem *item) {
	SondProjektTreeviewFMPrivate *priv = priv_of_item(item);

	return priv && priv->hooks.dbase && priv->hooks.dbase(priv->hooks.data) ? priv->pflege : NULL;
}

static gint vfunc_load_sections(SondTVFMItem *item, GPtrArray **arr_children, GError **error) {
	g_autofree gchar *filepart = NULL;
	g_autoptr(GPtrArray) sections = NULL;
	SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(item);
	SondProjektPflege *pflege = pflege_of_item(item);
	gint rc = 0;

	//nur PDFs haben Abschnitte
	if (!pflege || !sfp || !SOND_IS_FILE_PART_PDF(sfp))
		return 0;

	filepart = sond_file_part_get_filepart(sfp);

	//ohne arr_children: nur die Frage, ob es Abschnitte gibt (1/0)
	if (!arr_children)
		return sond_projekt_pflege_sections(pflege, filepart,
				sond_tvfm_item_get_path_or_section(item), NULL, error);

	rc = sond_projekt_pflege_sections(pflege, filepart,
			sond_tvfm_item_get_path_or_section(item), &sections, error);
	if (rc)
		return -1;

	*arr_children = g_ptr_array_new_with_free_func((GDestroyNotify) g_object_unref);
	for (guint i = 0; i < sections->len; i++) {
		SondTVFMItem *child = sond_tvfm_item_create(sond_tvfm_item_get_stvfm(item), sfp,
				g_ptr_array_index(sections, i));

		sond_tvfm_item_set_icon_name(child, "anbindung");
		g_ptr_array_add(*arr_children, child);
	}

	return 0;
}

static gboolean vfunc_has_sections(SondTVFMItem *item) {
	GError *error = NULL;
	gint rc = vfunc_load_sections(item, NULL, &error);

	if (rc == -1) {
		LOG_WARN("Konnte Abschnitte nicht laden: %s", error ? error->message : "?");
		g_clear_error(&error);

		return FALSE;
	}

	return rc == 1;
}

static gint vfunc_text_from_section(SondTVFMItem *item, gchar **text, GError **error) {
	g_autofree gchar *filepart = sond_file_part_get_filepart(
			sond_tvfm_item_get_sond_file_part(item));
	SondProjektPflege *pflege = pflege_of_item(item);

	if (!pflege) {
		*text = g_strdup("");

		return 0;
	}

	return sond_projekt_pflege_section_text(pflege, filepart,
			sond_tvfm_item_get_path_or_section(item), text, error);
}

//Hervorhebung: ist die Datei bzw. der Abschnitt ganz in einer Anbindung enthalten?
static gint vfunc_deter_background(SondTVFMItem *item, GError **error) {
	SondProjektPflege *pflege = pflege_of_item(item);
	SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(item);
	g_autofree gchar *filepart = NULL;

	if (!pflege || !sfp || sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_DIR)
		return 0;

	filepart = sond_file_part_get_filepart(sfp);

	return sond_projekt_pflege_anbindung_volltreffer(pflege, filepart,
			sond_tvfm_item_get_path_or_section(item), error);
}

//Abschnitt löschen (Haken des Baums, nach before-delete): in der offenen Transaktion
static gint vfunc_delete_section(SondTVFMItem *item, GError **error) {
	g_autofree gchar *filepart = sond_file_part_get_filepart(
			sond_tvfm_item_get_sond_file_part(item));
	SondProjektPflege *pflege = pflege_of_item(item);

	if (!pflege) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Keine Projektdatei vorhanden");

		return -1;
	}

	return sond_projekt_pflege_section_delete(pflege, filepart,
			sond_tvfm_item_get_path_or_section(item), error);
}

#define ONLY_STORE_TEXT "Die zu löschende Datei bzw. der Abschnitt ist noch in der " \
		"Speicher-Datenbank vorhanden.\nBitte zuerst das Projekt speichern, bevor Sie die " \
		"Datei bzw. den Abschnitt löschen."

static gboolean write_check_adapter(gpointer data, GError **error) {
	SondProjektTreeviewFMPrivate *priv = data;

	return priv->hooks.permit ? priv->hooks.permit(priv->hooks.data, error) : TRUE;
}

/* Abschnitt (Anbindung einer Datei) löschen: nur ohne Anbindung im Inhaltsbaum; die Transaktion läuft
 * über die Projektdatei (Client) bzw. work (zond), Dateien und Index sind nicht berührt. */
static gint before_delete_section(SondProjektTreeviewFMPrivate *priv, SondTVFMItem *item,
		GError **error, gpointer *ctx) {
	SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(item);
	gchar const *section = sond_tvfm_item_get_path_or_section(item);
	g_autofree gchar *filepart = NULL;
	DBaseZondRef ref = DBASE_ZOND_REF_NONE;
	OpCtx *oc = NULL;
	gint modus = 0;

	if (!sfp || !section)
		return 0;

	filepart = sond_file_part_get_filepart(sfp);

	modus = priv->hooks.write_begin(priv->hooks.data, TRUE, error);
	if (modus < 0)
		return -1;

	if (modus == 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Es gibt keine Projektdatei, der Abschnitt kann nicht gelöscht werden.");
		priv->hooks.write_end(priv->hooks.data);

		return -1;
	}

	if (sond_projekt_pflege_check_delete(priv->pflege, filepart, FALSE, FALSE, section, &ref,
			error) || ref != DBASE_ZOND_REF_NONE) {
		if (ref == DBASE_ZOND_REF_ONLY_STORE && !(error && *error)) {
			melden(priv, "Löschen nicht möglich", ONLY_STORE_TEXT);
			priv->hooks.write_end(priv->hooks.data);

			return 1;
		}
		if (ref != DBASE_ZOND_REF_NONE && error && !*error)
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
					"Der Abschnitt ist im Projekt angebunden und kann nicht gelöscht werden. "
					"Bitte zuerst die Anbindung entfernen.");
		priv->hooks.write_end(priv->hooks.data);

		return -1;
	}

	oc = g_new0(OpCtx, 1);
	{
		SondProjektDelete del = { .path = filepart, .section = section };

		if (sond_projekt_pflege_delete(priv->pflege, &del, &oc->dual, error)) {
			priv->hooks.write_end(priv->hooks.data);
			opctx_free(oc);

			return -1;
		}
	}

	oc->pflege = TRUE;
	*ctx = oc;

	return 0;
}

static gint on_before_delete(SondTreeviewFM *tree, SondTVFMItem *item, gpointer error_p,
		gpointer ctx_p, gpointer user_data) {
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);
	GError **error = error_p;
	gpointer *ctx = ctx_p;
	gchar *rel = NULL;
	gboolean ist_ordner = FALSE;
	gint modus = 0;
	OpCtx *oc = NULL;

	(void) user_data;

	if (priv->hooks.write_begin &&
			sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_LEAF_SECTION)
		return before_delete_section(priv, item, error, ctx);

	if (!priv->hooks.write_begin || !sond_tvfm_item_fs_path(item, &rel, &ist_ordner))
		return 0; //Inhalt eines Containers: hier nicht unterstützt

	modus = priv->hooks.write_begin(priv->hooks.data, TRUE, error);
	if (modus < 0) {
		g_free(rel);

		return -1;
	}

	oc = g_new0(OpCtx, 1);

	if (modus == 0) {
		oc->op = g_new0(IdxOp, 1);
		oc->op->alt = rel;
		*ctx = oc;

		return 0;
	}

	{
		DBaseZondRef ref = DBASE_ZOND_REF_NONE;

		//Ein angebundener Eintrag (oder ein angebundener Abkömmling) darf nicht gelöscht werden
		if (sond_projekt_pflege_check_delete(priv->pflege, rel, ist_ordner, FALSE, NULL, &ref,
				error) || ref != DBASE_ZOND_REF_NONE) {
			if (ref == DBASE_ZOND_REF_ONLY_STORE && !(error && *error)) {
				//in work schon entfernt, aber noch nicht gespeichert: überspringen
				melden(priv, "Löschen nicht möglich", ONLY_STORE_TEXT);
				priv->hooks.write_end(priv->hooks.data);
				g_free(rel);
				opctx_free(oc);

				return 1;
			}
			if (ref != DBASE_ZOND_REF_NONE && error && !*error)
				g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
						"\"%s\" ist im Projekt angebunden und kann nicht gelöscht werden. "
						"Bitte zuerst die Anbindung entfernen.", rel);
			priv->hooks.write_end(priv->hooks.data);
			g_free(rel);
			opctx_free(oc);

			return -1;
		}
	}

	{
		SondProjektDelete del = { .path = rel };

		if (sond_projekt_pflege_delete(priv->pflege, &del, &oc->dual, error)) {
			priv->hooks.write_end(priv->hooks.data);
			g_free(rel);
			opctx_free(oc);

			return -1;
		}
	}

	g_free(rel);
	oc->pflege = TRUE;
	*ctx = oc;

	return 0;
}

static gint on_before_move(SondTreeviewFM *tree, SondTVFMItem *item, SondTVFMItem *parent,
		const gchar *base, gint index, gpointer error_p, gpointer ctx_p, gpointer user_data) {
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);
	GError **error = error_p;
	gpointer *ctx = ctx_p;
	gchar const *root = sond_treeviewfm_get_root(tree);
	gchar *alt = NULL;
	gchar *neu = NULL;
	gint modus = 0;
	OpCtx *oc = NULL;

	(void) user_data;

	if (!priv->hooks.write_begin || !root || !sond_tvfm_item_fs_path(item, &alt, NULL) ||
			!ziel_pfad(parent, base, &neu)) {
		g_free(alt);

		return 0; //nicht im Dateisystem: hier nicht unterstützt
	}

	modus = priv->hooks.write_begin(priv->hooks.data, TRUE, error);
	if (modus < 0) {
		g_free(alt);
		g_free(neu);

		return -1;
	}

	oc = g_new0(OpCtx, 1);

	if (modus == 0) {
		SondIndexCtx *idx = kontext_index_ctx(priv);

		//eine abdeckende Aussage am Ziel auflösen (außer beim Umbenennen an Ort und Stelle);
		//schadet nicht, wenn die Dateioperation danach scheitert
		if (index != -1 && idx) {
			GError *idx_error = NULL;

			if (!sond_tvfm_index_sync_prepare_insert(idx, neu, root, &idx_error))
				g_warning("Index: Abdeckung am Ziel nicht aufgelöst: %s",
						idx_error ? idx_error->message : "?");
			g_clear_error(&idx_error);
		}

		oc->op = g_new0(IdxOp, 1);
		oc->op->alt = alt;
		oc->op->neu = neu;
		*ctx = oc;

		return 0;
	}

	{
		SondProjektMove move = {
			.prefix_old = alt,
			.prefix_new = neu,
			.project_dir = root,
			.index_to = index,
			.base_new = base
		};

		//Transaktion beginnen, Pfade in Projektdatei und Index umschreiben
		if (sond_projekt_pflege_move(priv->pflege, &move, error)) {
			priv->hooks.write_end(priv->hooks.data);
			g_free(alt);
			g_free(neu);
			opctx_free(oc);

			return -1;
		}
	}

	{
		g_autofree gchar *alt_abs = g_strconcat(root, "/", alt, NULL);
		g_autofree gchar *neu_abs = g_strconcat(root, "/", neu, NULL);

		//physische Verschiebung: bei gescheitertem Commit wird sie zurückgenommen
		sond_projekt_pflege_move_merken(priv->pflege, alt_abs, neu_abs, TRUE);
	}

	//Wird der Eintrag aus seinem Verzeichnis genommen, kann dieses vollständig abgedeckt sein
	sond_projekt_pflege_delete_merken(priv->pflege,
			(index != -1 && kontext_index_ctx(priv)) ? alt : NULL);

	g_free(alt);
	g_free(neu);
	oc->pflege = TRUE;
	oc->dual = TRUE;
	*ctx = oc;

	return 0;
}

static gint on_before_insert(SondTreeviewFM *tree, SondTVFMItem *item, SondTVFMItem *parent,
		const gchar *base, gint index, gpointer error_p, gpointer user_data) {
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);
	GError **error = error_p;
	gchar const *root = sond_treeviewfm_get_root(tree);
	gchar *neu = NULL;

	(void) item;
	(void) index;
	(void) user_data;

	if (!priv->hooks.write_begin || !root || !ziel_pfad(parent, base, &neu))
		return 0;

	//am Ziel entsteht neuer Inhalt: eine abdeckende Aussage auflösen (nur Index)
	if (priv->hooks.write_begin(priv->hooks.data, FALSE, error) < 0) {
		g_free(neu);

		return -1;
	}

	priv->insert_aktiv = TRUE;
	sond_projekt_pflege_insert(priv->pflege, neu, root, NULL);
	g_free(neu);

	return 0;
}

static void on_after_insert(SondTreeviewFM *tree, gboolean erfolg, gpointer user_data) {
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);

	(void) user_data;

	sond_projekt_pflege_insert_ende(priv->pflege, erfolg);
	if (priv->insert_aktiv) {
		priv->insert_aktiv = FALSE;
		priv->hooks.write_end(priv->hooks.data);
	}
}

static void on_after(SondTreeviewFM *tree, gboolean erfolg, gpointer ctx, gpointer user_data) {
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);
	OpCtx *oc = ctx;
	GError *error = NULL;

	(void) user_data;

	if (!oc)
		return;

	if (oc->pflege) {
		//Commit bzw. Rollback; bei gescheitertem Commit ist die Dateioperation zurückgenommen
		SondProjektTxErgebnis ergebnis = sond_projekt_pflege_tx_ende(priv->pflege, erfolg,
				oc->dual, sond_treeviewfm_get_root(tree), &error);

		if (ergebnis == SOND_PROJEKT_TX_FEHLER)
			melden(priv, "Speichern fehlgeschlagen", error ? error->message :
					"Die Änderung wurde rückgängig gemacht. Bitte erneut versuchen.");
		else if (ergebnis == SOND_PROJEKT_TX_KRITISCH) {
			gchar const *text = "Projektdatei und Dateien können inkonsistent sein. Details stehen in "
					"einer Fehlerdatei im Projektordner (ZOND_FEHLER_*.txt). Bitte den "
					"Dateibaum nicht weiter ändern.";

			if (priv->hooks.critical)
				priv->hooks.critical(priv->hooks.data, text); //zond beendet sich hier
			else
				melden(priv, "Kritischer Fehler beim Speichern", text);
		}
		g_clear_error(&error);

		if (priv->hooks.tx_done)
			priv->hooks.tx_done(priv->hooks.data, oc->dual);
	} else if (oc->op && erfolg) {
		SondIndexCtx *idx = kontext_index_ctx(priv);

		if (idx) {
			gboolean ok = oc->op->neu ?
					sond_tvfm_index_sync_moved(idx, oc->op->alt, oc->op->neu, &error) :
					sond_tvfm_index_sync_deleted(idx, oc->op->alt, &error);

			if (!ok)
				g_warning("Index nicht nachgeführt (%s): %s", oc->op->alt,
						error ? error->message : "?");
			g_clear_error(&error);
		}
	}

	if (priv->hooks.write_end)
		priv->hooks.write_end(priv->hooks.data);
	opctx_free(oc);
}

/* Umbenennen: bei einem Abschnitt der Text der Anbindung (in einer eigenen Transaktion), sonst wie
 * im Dateibaum */
static gint vfunc_text_edited(SondTreeviewFM *tree, SondTVFMItem *item, SondTVFMItem *parent,
		gchar const *text_new, GError **error) {
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);
	g_autofree gchar *filepart = NULL;
	SondProjektTxErgebnis ergebnis = SOND_PROJEKT_TX_OK;
	gint modus = 0;

	if (!priv->hooks.write_begin || sond_tvfm_item_get_item_type(item) != SOND_TVFM_ITEM_TYPE_LEAF_SECTION)
		return SOND_TREEVIEWFM_CLASS(sond_projekt_treeviewfm_parent_class)->text_edited(tree, item,
				parent, text_new, error);

	filepart = sond_file_part_get_filepart(sond_tvfm_item_get_sond_file_part(item));

	modus = priv->hooks.write_begin(priv->hooks.data, TRUE, error);
	if (modus < 0)
		return -1;

	if (modus == 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Es gibt keine Projektdatei, der Abschnitt kann nicht geändert werden.");
		priv->hooks.write_end(priv->hooks.data);

		return -1;
	}

	if (sond_projekt_pflege_tx_begin(priv->pflege, FALSE, FALSE, error)) {
		priv->hooks.write_end(priv->hooks.data);

		return -1;
	}

	if (sond_projekt_pflege_section_set_text(priv->pflege, filepart,
			sond_tvfm_item_get_path_or_section(item), text_new, NULL, error)) {
		sond_projekt_pflege_tx_ende(priv->pflege, FALSE, FALSE, sond_treeviewfm_get_root(tree),
				NULL);
		priv->hooks.write_end(priv->hooks.data);

		return -1;
	}

	ergebnis = sond_projekt_pflege_tx_ende(priv->pflege, TRUE, FALSE,
			sond_treeviewfm_get_root(tree), error);
	priv->hooks.write_end(priv->hooks.data);

	return ergebnis == SOND_PROJEKT_TX_OK ? 0 : -1;
}

/* Abschnitt auflösen: der markierte Abschnitt wird entfernt, seine Unterabschnitte bleiben und
 * rücken an seine Stelle */
static void action_aufloesen(GSimpleAction *action, GVariant *parameter, gpointer data) {
	SondTreeviewFM *tree = SOND_TREEVIEWFM(data);
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);
	SondTreeview *base = SOND_TREEVIEW(tree);
	GPtrArray *rows = sond_treeview_get_selected_rows(base);
	GtkTreeListRow *row = rows->len == 1 ? g_ptr_array_index(rows, 0) : NULL;
	SondTVFMItem *item = row ? gtk_tree_list_row_get_item(row) : NULL;
	SondLazyChildren *lazy = NULL;
	g_autofree gchar *filepart = NULL;
	g_autoptr(GPtrArray) children = NULL;
	GError *error = NULL;
	gboolean angebunden = FALSE;
	gboolean ok = FALSE;
	gint modus = 0;

	(void) action;
	(void) parameter;

	if (!item || sond_tvfm_item_get_item_type(item) != SOND_TVFM_ITEM_TYPE_LEAF_SECTION ||
			!priv->hooks.write_begin) {
		melden(priv, "Abschnitt auflösen", "Bitte genau einen Abschnitt markieren.");
		goto out;
	}

	lazy = fm_lazy_from_model(sond_treeview_row_get_container(base, row));
	filepart = sond_file_part_get_filepart(sond_tvfm_item_get_sond_file_part(item));

	modus = priv->hooks.write_begin(priv->hooks.data, TRUE, &error);
	if (modus < 0)
		goto fehler;
	if (modus == 0) {
		g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Es gibt keine Projektdatei, der Abschnitt kann nicht geändert werden.");
		goto fehler_end;
	}

	if (sond_projekt_pflege_section_angebunden(priv->pflege, filepart,
			sond_tvfm_item_get_path_or_section(item), &angebunden, &error))
		goto fehler_end;
	if (angebunden) {
		g_set_error(&error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
				"Der Abschnitt ist im Projekt angebunden. Bitte zuerst die Anbindung entfernen.");
		goto fehler_end;
	}

	//die Unterabschnitte vorher lesen: sie bleiben als eigene Zeilen
	if (sond_tvfm_item_load_children(item, &children, NULL, &error))
		goto fehler_end;

	if (sond_projekt_pflege_tx_begin(priv->pflege, FALSE, FALSE, &error))
		goto fehler_end;
	if (sond_projekt_pflege_section_aufloesen(priv->pflege, filepart,
			sond_tvfm_item_get_path_or_section(item), &error)) {
		sond_projekt_pflege_tx_ende(priv->pflege, FALSE, FALSE, sond_treeviewfm_get_root(tree), NULL);
		goto fehler_end;
	}
	ok = sond_projekt_pflege_tx_ende(priv->pflege, TRUE, FALSE, sond_treeviewfm_get_root(tree),
			&error) == SOND_PROJEKT_TX_OK;
	priv->hooks.write_end(priv->hooks.data);

	if (!ok)
		goto fehler;

	//Zeile durch ihre Unterabschnitte ersetzen
	if (lazy) {
		gint index = fm_lazy_index_of(lazy, item);

		fm_lazy_remove(lazy, item);
		for (guint i = 0; i < children->len; i++)
			fm_lazy_insert_at(lazy, index >= 0 ? index + (gint) i : -1,
					g_ptr_array_index(children, i));
	}

	goto out;

fehler_end:
	priv->hooks.write_end(priv->hooks.data);
fehler:
	melden(priv, "Abschnitt auflösen nicht möglich", error ? error->message : "unbekannt");
	g_clear_error(&error);

out:
	g_clear_object(&item);
	g_ptr_array_unref(rows);
}

/* ---- Eingang (Stempel) ---- */

static SondEingangCache* eingang_cache_of(SondProjektTreeviewFMPrivate *priv) {
	return priv->hooks.eingang_cache ? priv->hooks.eingang_cache(priv->hooks.data) : NULL;
}

static GtkWindow* eingang_parent(SondTreeviewFM *tree) {
	GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(tree));

	return GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
}

//Wirksames Eingangsdatum (JJJJMMTT) für die Spalte; -1, solange es keine Angaben gibt
static gint vfunc_get_eingang(SondTreeviewFM *tree, gchar const *rel_path, gboolean is_dir) {
	SondEingangCache *cache = eingang_cache_of(priv_of(tree));

	return cache ? sond_eingang_cache_lookup(cache, rel_path, is_dir) : -1;
}

//Ein Eintrag des Dateisystems wurde gelöscht: seine Stempel (und die darunter) gelten nicht mehr
static void vfunc_fs_entry_deleted(SondTreeviewFM *tree, gchar const *rel_path, gboolean is_dir) {
	SondEingangCache *cache = eingang_cache_of(priv_of(tree));

	(void) is_dir;

	//still: ein Fehlschlag wird dort nur protokolliert
	if (cache)
		sond_eingang_pflege_geloescht(cache, sond_treeviewfm_get_root(tree), rel_path);
}

//Umbenannt: die Stempel (und die darunter) gehen auf den neuen Pfad mit
static void vfunc_fs_entry_renamed(SondTreeviewFM *tree, gchar const *old_rel, gchar const *new_rel,
		gboolean is_dir) {
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);
	SondEingangCache *cache = eingang_cache_of(priv);
	GError *error = NULL;

	(void) is_dir;

	if (!cache || !old_rel || !new_rel)
		return;

	if (sond_eingang_pflege_umbenannt(cache, sond_treeviewfm_get_root(tree), old_rel, new_rel,
			&error)) {
		g_autofree gchar *text = g_strdup_printf("%s\n\nBitte mit \"Eingang setzen\" neu erfassen.",
				error ? error->message : "unbekannter Fehler");

		melden(priv, "Eingangsangaben konnten nicht auf den neuen Namen übertragen werden", text);
		g_clear_error(&error);
	}
}

/* Verschoben oder kopiert: die Stempel gehen mit bzw. werden mitkopiert. TRUE, wenn sich
 * Eingangsangaben geändert haben (die Anzeige ist aufzufrischen). */
static gboolean vfunc_fs_entry_moved(SondTreeviewFM *tree, gchar const *old_rel, gchar const *new_rel,
		gboolean is_dir, gboolean copy) {
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);
	SondEingangCache *cache = eingang_cache_of(priv);
	SondEingangVerschoben ergebnis = SOND_EINGANG_VERSCHOBEN_KEIN_HINWEIS;
	GError *error = NULL;
	gboolean geaendert = FALSE;
	gchar const *grund = NULL;

	if (!cache || !old_rel)
		return FALSE;

	geaendert = sond_eingang_pflege_verschoben(cache, sond_treeviewfm_get_root(tree), old_rel,
			new_rel, is_dir, copy, &ergebnis, &error);
	grund = error ? error->message : "unbekannter Fehler";

	switch (ergebnis) {
	case SOND_EINGANG_VERSCHOBEN_KOPIE_IN_CONTAINER:
		melden(priv, "Eingang", "Die Kopie liegt in einem Container; dort gibt es keine "
				"Eingangsangaben. Das Original behält seinen Eingang.");
		break;
	case SOND_EINGANG_VERSCHOBEN_IN_CONTAINER:
		melden(priv, "Eingang", "Der Eintrag liegt jetzt in einem Container; dort gibt es keine "
				"Eingangsangaben.");
		break;
	case SOND_EINGANG_VERSCHOBEN_FEHLER_AUFHEBEN:
		melden(priv, "Eingangsangaben konnten nicht aufgehoben werden", grund);
		break;
	case SOND_EINGANG_VERSCHOBEN_FEHLER_UEBERTRAGEN: {
		g_autofree gchar *text = g_strdup_printf("%s\n\nBitte mit \"Eingang setzen\" neu erfassen.",
				grund);

		melden(priv, "Eingangsangaben konnten nicht auf den neuen Ort übertragen werden", text);
		break;
	}
	default:
		break;
	}
	g_clear_error(&error);

	return geaendert;
}

/* Auf der obersten Ebene sind Einträge hinzugekommen (Import, auch von einem anderen Rechner): die
 * Zettel nachlesen, die dabei in .sond/eingang entstanden sind, und die Spalte auffrischen */
static void on_root_entries_added(SondTreeviewFM *tree, gpointer data) {
	SondEingangCache *cache = eingang_cache_of(priv_of(tree));
	guint n_zettel = 0;

	(void) data;

	if (cache && !sond_eingang_cache_reload(cache, &n_zettel, NULL) && n_zettel > 0)
		sond_treeviewfm_eingang_refresh(tree);
}

typedef struct {
	SondTreeviewFM *tree;     //Referenz
	GPtrArray *pfade;         //gchar*, relativ zur Wurzel
	GArray *ordner;           //gboolean, parallel zu pfade
	guint uebersprungen;      //Einträge in Containern
} Auswahl;

static void auswahl_init(Auswahl *a, SondTreeviewFM *tree) {
	a->tree = g_object_ref(tree);
	a->pfade = g_ptr_array_new_with_free_func(g_free);
	a->ordner = g_array_new(FALSE, FALSE, sizeof(gboolean));
	a->uebersprungen = 0;
}

static void auswahl_clear(Auswahl *a) {
	g_clear_object(&a->tree);
	g_clear_pointer(&a->pfade, g_ptr_array_unref);
	g_clear_pointer(&a->ordner, g_array_unref);
}

//Markierte Einträge des Dateisystems sammeln; Einträge in Containern lassen sich nicht stempeln
static void auswahl_holen(Auswahl *a) {
	GPtrArray *rows = sond_treeview_get_selected_rows(SOND_TREEVIEW(a->tree));

	for (guint i = 0; i < rows->len; i++) {
		SondTVFMItem *item = gtk_tree_list_row_get_item(g_ptr_array_index(rows, i));
		gchar *rel = NULL;
		gboolean ist_ordner = FALSE;

		if (!item)
			continue;

		if (sond_tvfm_item_fs_path(item, &rel, &ist_ordner)) {
			g_ptr_array_add(a->pfade, rel);
			g_array_append_val(a->ordner, ist_ordner);
		} else
			a->uebersprungen++;
		g_object_unref(item);
	}
	g_ptr_array_unref(rows);
}

/* Zettel in die Akte schreiben und in die lokale Tabelle eintragen, danach die Spalte auffrischen */
static gboolean zettel_ablegen(SondTreeviewFM *tree, SondEingangZettel *zettel) {
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);
	GError *error = NULL;
	SondEingangAblegen ergebnis = sond_eingang_pflege_ablegen(eingang_cache_of(priv),
			sond_treeviewfm_get_root(tree), zettel, &error);

	if (ergebnis == SOND_EINGANG_ABLEGEN_NICHT_GESPEICHERT) {
		melden(priv, "Eingangsangabe konnte nicht gespeichert werden",
				error ? error->message : "unbekannter Fehler");
		g_clear_error(&error);

		return FALSE;
	}

	if (ergebnis == SOND_EINGANG_ABLEGEN_NICHT_UEBERNOMMEN) {
		g_autofree gchar *text = g_strdup_printf("%s\n\nMit \"Neu einlesen\" erneut versuchen.",
				error ? error->message : "unbekannter Fehler");

		melden(priv, "Eingangsangabe gespeichert, aber nicht übernommen", text);
		g_clear_error(&error);
	}

	sond_treeviewfm_eingang_refresh(tree);

	return ergebnis == SOND_EINGANG_ABLEGEN_OK;
}

//Voraussetzungen jeder Eingang-Aktion, die schreibt: es gibt einen Cache, die Akte darf geändert werden
static gboolean eingang_bereit(SondProjektTreeviewFMPrivate *priv, gboolean schreibt) {
	GError *error = NULL;

	if (!eingang_cache_of(priv)) {
		melden(priv, "Eingang", "Für dieses Projekt sind keine Eingangsangaben verfügbar.");

		return FALSE;
	}

	if (schreibt && priv->hooks.permit && !priv->hooks.permit(priv->hooks.data, &error)) {
		melden(priv, "Eingang", error ? error->message : "Die Akte darf nicht geändert werden.");
		g_clear_error(&error);

		return FALSE;
	}

	return TRUE;
}

static void eingang_setzen_fertig(gchar const *iso, gchar const *art, gpointer data) {
	Auswahl *a = data;
	SondEingangZettel *zettel = sond_eingang_zettel_new(g_get_user_name(), "manuell");

	for (guint i = 0; i < a->pfade->len; i++)
		sond_eingang_zettel_add(zettel, g_ptr_array_index(a->pfade, i),
				g_array_index(a->ordner, gboolean, i), iso, art, NULL, 0);

	zettel_ablegen(a->tree, zettel);
	sond_eingang_zettel_free(zettel);
	auswahl_clear(a);
	g_free(a);
}

static void action_eingang_setzen(GSimpleAction *action, GVariant *parameter, gpointer data) {
	SondTreeviewFM *tree = SOND_TREEVIEWFM(data);
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);
	Auswahl *a = NULL;
	g_autofree gchar *zusammenfassung = NULL;
	gint vorbelegung = 0;

	(void) action;
	(void) parameter;

	if (!eingang_bereit(priv, TRUE))
		return;

	a = g_new0(Auswahl, 1);
	auswahl_init(a, tree);
	auswahl_holen(a);

	if (a->pfade->len == 0) {
		melden(priv, "Eingang setzen", "Bitte Dateien oder Ordner des Dateiverzeichnisses markieren. "
				"Einträge in Containern (ZIP, E-Mail, PDF-Anhänge) lassen sich nicht stempeln.");
		auswahl_clear(a);
		g_free(a);

		return;
	}

	if (a->pfade->len == 1)
		zusammenfassung = g_strdup_printf("Eingang für %s", (gchar const*) g_ptr_array_index(a->pfade, 0));
	else
		zusammenfassung = g_strdup_printf("Eingang für %u markierte Einträge", a->pfade->len);
	if (a->uebersprungen > 0) {
		gchar *mit = g_strdup_printf("%s (%u Einträge in Containern werden übergangen)", zusammenfassung,
				a->uebersprungen);

		g_free(zusammenfassung);
		zusammenfassung = mit;
	}

	//Vorbelegung: das bisher wirksame Datum des ersten Eintrags
	vorbelegung = sond_eingang_cache_lookup(eingang_cache_of(priv), g_ptr_array_index(a->pfade, 0),
			g_array_index(a->ordner, gboolean, 0));

	sond_eingang_dialog_show(eingang_parent(tree), zusammenfassung, vorbelegung, eingang_setzen_fertig, a);
}

static void eingang_entfernen_antwort(GObject *source, GAsyncResult *result, gpointer data) {
	Auswahl *a = data;
	gint button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, NULL);

	if (button == 1) {
		SondEingangZettel *zettel = sond_eingang_zettel_new(g_get_user_name(), "manuell");

		for (guint i = 0; i < a->pfade->len; i++)
			sond_eingang_zettel_add_entfernen(zettel, g_ptr_array_index(a->pfade, i));

		zettel_ablegen(a->tree, zettel);
		sond_eingang_zettel_free(zettel);
	}

	auswahl_clear(a);
	g_free(a);
}

static void action_eingang_entfernen(GSimpleAction *action, GVariant *parameter, gpointer data) {
	SondTreeviewFM *tree = SOND_TREEVIEWFM(data);
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);
	SondEingangCache *cache = NULL;
	Auswahl alle = { 0 };
	Auswahl *eigene = NULL;
	GtkAlertDialog *dialog = NULL;
	gchar *text = NULL;
	static gchar const *knoepfe[] = { "Abbrechen", "Entfernen", NULL };

	(void) action;
	(void) parameter;

	if (!eingang_bereit(priv, TRUE))
		return;
	cache = eingang_cache_of(priv);

	auswahl_init(&alle, tree);
	auswahl_holen(&alle);

	//nur Einträge mit eigenem Stempel; die übrigen erben ohnehin
	eigene = g_new0(Auswahl, 1);
	auswahl_init(eigene, tree);
	for (guint i = 0; i < alle.pfade->len; i++)
		if (sond_eingang_cache_has_own(cache, g_ptr_array_index(alle.pfade, i))) {
			gboolean ordner = g_array_index(alle.ordner, gboolean, i);

			g_ptr_array_add(eigene->pfade, g_strdup(g_ptr_array_index(alle.pfade, i)));
			g_array_append_val(eigene->ordner, ordner);
		}
	auswahl_clear(&alle);

	if (eigene->pfade->len == 0) {
		melden(priv, "Eingang entfernen", "Für die markierten Einträge ist kein eigener Eingang gesetzt "
				"(sie erben ihn von einem übergeordneten Ordner oder haben keinen).");
		auswahl_clear(eigene);
		g_free(eigene);

		return;
	}

	text = g_strdup_printf("Eigenen Eingang von %u Eintrag/Einträgen entfernen? Sie erben danach den "
			"Eingang des übergeordneten Ordners.", eigene->pfade->len);
	dialog = gtk_alert_dialog_new("Eingang entfernen");
	gtk_alert_dialog_set_detail(dialog, text);
	gtk_alert_dialog_set_buttons(dialog, knoepfe);
	gtk_alert_dialog_set_cancel_button(dialog, 0);
	gtk_alert_dialog_set_default_button(dialog, 0);
	gtk_alert_dialog_set_modal(dialog, TRUE);
	gtk_alert_dialog_choose(dialog, eingang_parent(tree), NULL, eingang_entfernen_antwort, eigene);
	g_object_unref(dialog);
	g_free(text);
}

//Zettel nachlesen, die seit dem Öffnen dazugekommen sind (z.B. ein Import auf einem anderen Rechner)
static void action_eingang_neu(GSimpleAction *action, GVariant *parameter, gpointer data) {
	SondTreeviewFM *tree = SOND_TREEVIEWFM(data);
	SondProjektTreeviewFMPrivate *priv = priv_of(tree);
	GError *error = NULL;
	guint n_zettel = 0;
	g_autofree gchar *text = NULL;

	(void) action;
	(void) parameter;

	if (!eingang_bereit(priv, FALSE))
		return;

	if (sond_eingang_cache_reload(eingang_cache_of(priv), &n_zettel, &error)) {
		melden(priv, "Eingänge neu einlesen fehlgeschlagen", error ? error->message : "unbekannter Fehler");
		g_clear_error(&error);

		return;
	}

	sond_treeviewfm_eingang_refresh(tree);

	text = g_strdup_printf("%u neue(r) Zettel eingelesen.", n_zettel);
	melden(priv, "Eingänge neu einlesen", text);
}

static void vfunc_setup_menu(SondTreeviewFM *self) {
	static const GActionEntry entries[] = {
		{ "section-aufloesen", action_aufloesen, NULL, NULL, NULL },
		{ "eingang-setzen", action_eingang_setzen, NULL, NULL, NULL },
		{ "eingang-entfernen", action_eingang_entfernen, NULL, NULL, NULL },
		{ "eingang-neu", action_eingang_neu, NULL, NULL, NULL }
	};
	GMenu *menu = sond_treeview_get_menu(SOND_TREEVIEW(self));
	GMenu *section = g_menu_new();
	GMenu *sec_eingang = g_menu_new();
	GMenu *sub_eingang = g_menu_new();

	SOND_TREEVIEWFM_CLASS(sond_projekt_treeviewfm_parent_class)->setup_menu(self);

	g_action_map_add_action_entries(sond_treeview_get_action_map(SOND_TREEVIEW(self)), entries,
			G_N_ELEMENTS(entries), self);
	sond_treeviewfm_register_write_action(self, "section-aufloesen");
	sond_treeviewfm_register_write_action(self, "eingang-setzen");
	sond_treeviewfm_register_write_action(self, "eingang-entfernen");

	g_menu_append(section, "Abschnitt auflösen (Unterabschnitte behalten)", "tv.section-aufloesen");
	g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
	g_object_unref(section);

	g_menu_append(sub_eingang, "Setzen ...", "tv.eingang-setzen");
	g_menu_append(sub_eingang, "Entfernen", "tv.eingang-entfernen");
	g_menu_append(sub_eingang, "Neu einlesen", "tv.eingang-neu");
	g_menu_append_submenu(sec_eingang, "Eingang", G_MENU_MODEL(sub_eingang));
	g_menu_append_section(menu, NULL, G_MENU_MODEL(sec_eingang));
	g_object_unref(sub_eingang);
	g_object_unref(sec_eingang);
}

void sond_projekt_treeviewfm_set_hooks(SondProjektTreeviewFM *self, SondProjektHooks const *hooks) {
	SondProjektTreeviewFMPrivate *priv = NULL;

	g_return_if_fail(SOND_IS_PROJEKT_TREEVIEWFM(self));
	g_return_if_fail(hooks && hooks->write_begin && hooks->write_end && hooks->dbase);

	priv = sond_projekt_treeviewfm_get_instance_private(self);
	priv->hooks = *hooks;

	sond_treeviewfm_set_write_check(SOND_TREEVIEWFM(self), write_check_adapter, priv);
}

SondProjektPflege* sond_projekt_treeviewfm_get_pflege(SondProjektTreeviewFM *self) {
	SondProjektTreeviewFMPrivate *priv = NULL;

	g_return_val_if_fail(SOND_IS_PROJEKT_TREEVIEWFM(self), NULL);
	priv = sond_projekt_treeviewfm_get_instance_private(self);

	return priv->pflege;
}

GtkWidget* sond_projekt_treeviewfm_new(void) {
	return g_object_new(SOND_TYPE_PROJEKT_TREEVIEWFM, NULL);
}

static void sond_projekt_treeviewfm_finalize(GObject *object) {
	SondProjektTreeviewFMPrivate *priv = sond_projekt_treeviewfm_get_instance_private(
			SOND_PROJEKT_TREEVIEWFM(object));

	sond_projekt_pflege_free(priv->pflege);

	G_OBJECT_CLASS(sond_projekt_treeviewfm_parent_class)->finalize(object);
}

//Index-Status eines Abschnitts (Anbindung): der Seitenbereich in der PDF, wie in zond
static SondIndexStatus vfunc_section_index_status(SondTVFMItem *item, SondIndexCtx *index_ctx) {
	SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(item);
	g_autofree gchar *filepart = sfp ? sond_file_part_get_filepart(sfp) : NULL;

	return sond_projekt_index_status_abschnitt(index_ctx, filepart,
			sond_tvfm_item_get_path_or_section(item));
}

static void sond_projekt_treeviewfm_class_init(SondProjektTreeviewFMClass *klass) {
	SondTreeviewFMClass *tclass = SOND_TREEVIEWFM_CLASS(klass);

	G_OBJECT_CLASS(klass)->finalize = sond_projekt_treeviewfm_finalize;

	tclass->get_section_index_status = vfunc_section_index_status;
	tclass->has_sections = vfunc_has_sections;
	tclass->load_sections = vfunc_load_sections;
	tclass->text_from_section = vfunc_text_from_section;
	tclass->deter_background = vfunc_deter_background;
	tclass->delete_section = vfunc_delete_section;
	tclass->setup_menu = vfunc_setup_menu;
	tclass->get_eingang = vfunc_get_eingang;
	tclass->fs_entry_deleted = vfunc_fs_entry_deleted;
	tclass->fs_entry_renamed = vfunc_fs_entry_renamed;
	tclass->fs_entry_moved = vfunc_fs_entry_moved;
	tclass->text_edited = vfunc_text_edited;
}

static void sond_projekt_treeviewfm_init(SondProjektTreeviewFM *self) {
	SondProjektTreeviewFMPrivate *priv = sond_projekt_treeviewfm_get_instance_private(self);
	SondProjektKontext kontext = { kontext_dbase, kontext_index_ctx, priv };

	priv->pflege = sond_projekt_pflege_new(&kontext);

	g_signal_connect(self, "before-delete", G_CALLBACK(on_before_delete), NULL);
	g_signal_connect(self, "before-move", G_CALLBACK(on_before_move), NULL);
	g_signal_connect(self, "before-insert", G_CALLBACK(on_before_insert), NULL);
	g_signal_connect(self, "after", G_CALLBACK(on_after), NULL);
	g_signal_connect(self, "after-insert", G_CALLBACK(on_after_insert), NULL);
	g_signal_connect(self, "root-entries-added", G_CALLBACK(on_root_entries_added), NULL);
}
