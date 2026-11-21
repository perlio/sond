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

#include "../tvfm/sond_tvfm_index_sync.h"

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

static gboolean write_check_adapter(gpointer data, GError **error) {
	SondProjektTreeviewFMPrivate *priv = data;

	return priv->hooks.permit ? priv->hooks.permit(priv->hooks.data, error) : TRUE;
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
		else if (ergebnis == SOND_PROJEKT_TX_KRITISCH)
			melden(priv, "Kritischer Fehler beim Speichern",
					"Projektdatei und Dateien können inkonsistent sein. Details stehen in "
					"einer Fehlerdatei im Projektordner (ZOND_FEHLER_*.txt). Bitte den "
					"Dateibaum nicht weiter ändern.");
		g_clear_error(&error);
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

static void sond_projekt_treeviewfm_class_init(SondProjektTreeviewFMClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_projekt_treeviewfm_finalize;
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
}
