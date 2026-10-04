/*
 zond (export_selection.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2020  pelo america

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

#include <gtk/gtk.h>

#include "../../sond_log_and_error.h"

#include "../zond_dbase.h"
#include "../zond_tree_store.h"

#include "../10init/app_window.h"
#include "../99conv/general.h"

#include "project.h"
#include "export_selection.h"

typedef struct {
	Projekt *zond;
	Baum baum;
	const ExportOptionen *opt;
	GtkTreeModel *model;
	GPtrArray *eintraege;
} ExportCtx;

void export_eintrag_free(gpointer data) {
	ExportEintrag *e = (ExportEintrag*) data;

	if (!e)
		return;

	g_free(e->nummer);
	g_free(e->titel);
	g_free(e->pfad);
	g_free(e->notiz);
	g_free(e->datei);
	g_free(e->anbindung);
	g_free(e);
}

Baum export_selection_baum(Projekt *zond) {
	Baum baum = zond_baum_aktuell(zond);

	if (baum == BAUM_INHALT || baum == BAUM_AUSWERTUNG)
		return baum;

	return KEIN_BAUM;
}

gint export_selection_anzahl_markiert(Projekt *zond, Baum baum) {
	if (baum != BAUM_INHALT && baum != BAUM_AUSWERTUNG)
		return 0;

	return gtk_tree_selection_count_selected_rows(zond->selection[baum]);
}

/* Datei und Seitenbereich eines Knotens. Eine Copy trägt die Anbindung
 * ihres Originals. Knoten ohne file_part liefern NULL/NULL. */
static gint export_get_anbindung(ExportCtx *ctx, gint node_id,
		ExportEintrag *e, GError **error) {
	gint rc = 0;
	gint type = 0;
	gint link = 0;
	gchar *file_part = NULL;
	gchar *section = NULL;
	ZondDBase *dbase = ctx->zond->dbase_zond->zond_dbase_work;

	rc = zond_dbase_get_node(dbase, node_id, &type, &link, &file_part, &section,
			NULL, NULL, NULL, error);
	if (rc)
		return -1;

	if (type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY) {
		g_free(file_part);
		g_free(section);
		file_part = NULL;
		section = NULL;

		rc = zond_dbase_get_node(dbase, link, NULL, NULL, &file_part, &section,
				NULL, NULL, NULL, error);
		if (rc)
			return -1;
	}

	if (file_part && *file_part) {
		e->datei = file_part;

		if (section && *section) {
			anbindung_parse_file_section(section, &e->anb);
			if (!anbindung_is_empty(&e->anb))
				e->anbindung = anbindung_to_human_readable(&e->anb);
		}
	} else
		g_free(file_part);

	g_free(section);

	return 0;
}

//Titel der Vorfahren von iter, von außen nach innen, mit " > " getrennt
static gchar* export_get_pfad(ExportCtx *ctx, GtkTreeIter *iter) {
	GtkTreeIter it = *iter;
	GtkTreeIter parent = { 0 };
	GPtrArray *teile = g_ptr_array_new_with_free_func(g_free);
	gchar *pfad = NULL;

	while (gtk_tree_model_iter_parent(ctx->model, &parent, &it)) {
		gchar *text = NULL;

		gtk_tree_model_get(ctx->model, &parent, 1, &text, -1);
		g_ptr_array_insert(teile, 0, text ? text : g_strdup(""));
		it = parent;
	}

	if (teile->len) {
		g_ptr_array_add(teile, NULL);
		pfad = g_strjoinv(" > ", (gchar**) teile->pdata);
	}

	g_ptr_array_free(teile, TRUE);

	return pfad;
}

static gint export_visit(ExportCtx *ctx, GtkTreeIter *iter, gint ebene,
		const gchar *nummer_eltern, gint lfd, GError **error) {
	gint rc = 0;
	gint node_id = 0;
	gchar *titel = NULL;
	ExportEintrag *e = NULL;
	GtkTreeIter iter_child = { 0 };

	gtk_tree_model_get(ctx->model, iter, 1, &titel, 2, &node_id, -1);
	if (node_id < 0)
		node_id *= -1;

	e = g_new0(ExportEintrag, 1);
	e->node_id = node_id;
	e->ebene = ebene;
	e->titel = titel ? titel : g_strdup("");

	if (ctx->opt->nummern)
		e->nummer = nummer_eltern ?
				g_strdup_printf("%s.%d", nummer_eltern, lfd) :
				g_strdup_printf("%d", lfd);

	if (ctx->opt->pfad && ebene == 1)
		e->pfad = export_get_pfad(ctx, iter);

	if (ctx->opt->text) {
		rc = zond_dbase_get_text(ctx->zond->dbase_zond->zond_dbase_work,
				node_id, &e->notiz, error);
		if (rc) {
			export_eintrag_free(e);

			return -1;
		}
	}

	if (ctx->opt->anbindung || ctx->opt->dokumente) {
		rc = export_get_anbindung(ctx, node_id, e, error);
		if (rc) {
			export_eintrag_free(e);

			return -1;
		}
	}

	g_ptr_array_add(ctx->eintraege, e);

	if (ctx->opt->tiefe != EXPORT_TIEFE_ALLE && ebene > ctx->opt->tiefe)
		return 0;

	//Link-Kopf hat noch einen Platzhalter als Kind
	if (zond_tree_store_link_is_unloaded(iter))
		zond_tree_store_load_link(iter);

	if (!gtk_tree_model_iter_children(ctx->model, &iter_child, iter))
		return 0;

	gchar *nummer_kinder = e->nummer ? g_strdup(e->nummer) : NULL;
	gint lfd_kind = 1;

	do {
		rc = export_visit(ctx, &iter_child, ebene + 1, nummer_kinder,
				lfd_kind++, error);
		if (rc) {
			g_free(nummer_kinder);

			return -1;
		}
	} while (gtk_tree_model_iter_next(ctx->model, &iter_child));

	g_free(nummer_kinder);

	return 0;
}

GPtrArray* export_selection_build(Projekt *zond, Baum baum,
		const ExportOptionen *opt, GError **error) {
	ExportCtx ctx = { 0 };
	GList *selected = NULL;
	gint lfd = 1;

	if (baum != BAUM_INHALT && baum != BAUM_AUSWERTUNG) {
		g_set_error(error, SOND_ERROR, 0,
				"Kein Baum ausgewählt - bitte zuerst im Inhalts- oder "
				"Auswertungsbaum arbeiten");

		return NULL;
	}

	ctx.zond = zond;
	ctx.baum = baum;
	ctx.opt = opt;
	ctx.model = gtk_tree_view_get_model(GTK_TREE_VIEW(zond->treeview[baum]));
	ctx.eintraege = g_ptr_array_new_with_free_func(export_eintrag_free);

	if (opt->ganzer_baum) {
		GtkTreeIter iter = { 0 };

		if (gtk_tree_model_iter_children(ctx.model, &iter, NULL)) {
			do {
				if (export_visit(&ctx, &iter, 1, NULL, lfd++, error)) {
					g_ptr_array_unref(ctx.eintraege);

					return NULL;
				}
			} while (gtk_tree_model_iter_next(ctx.model, &iter));
		}
	} else {
		selected = gtk_tree_selection_get_selected_rows(zond->selection[baum],
				NULL);
		if (!selected) {
			g_set_error(error, SOND_ERROR, 0, "Keine Punkte markiert");
			g_ptr_array_unref(ctx.eintraege);

			return NULL;
		}

		selected = g_list_sort(selected, (GCompareFunc) gtk_tree_path_compare);

		for (GList *l = selected; l; l = l->next) {
			GtkTreeIter iter = { 0 };

			if (!gtk_tree_model_get_iter(ctx.model, &iter, l->data)) {
				g_set_error(error, SOND_ERROR, 0,
						"iter konnte nicht gesetzt werden");
				g_list_free_full(selected,
						(GDestroyNotify) gtk_tree_path_free);
				g_ptr_array_unref(ctx.eintraege);

				return NULL;
			}

			if (export_visit(&ctx, &iter, 1, NULL, lfd++, error)) {
				g_list_free_full(selected,
						(GDestroyNotify) gtk_tree_path_free);
				g_ptr_array_unref(ctx.eintraege);

				return NULL;
			}
		}

		g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);
	}

	return ctx.eintraege;
}
