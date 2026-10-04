/*
 sond (sond_pdf_helper.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  peloamerica

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

#include "sond_pdf_helper.h"
#include "sond_mime.h"
#include "sond_log_and_error.h"
#include "sond_file_helper.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <glib.h>
#include <gio/gio.h>

typedef struct {
	pdf_processor super;
	gint flags;
	GArray* arr_Tr;
	gboolean has_visible_text;
	gboolean has_hidden_text;
} pdf_text_analyzer_processor;

static void text_analyzer_op_q(fz_context *ctx, pdf_processor *proc) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	g_array_append_val(p->arr_Tr, Tr);

	//chain-up
	if (proc && proc->chain && proc->chain->op_q)
		proc->chain->op_q(ctx, proc->chain);

	return;
}

static void text_analyzer_op_Q(fz_context *ctx, pdf_processor *proc) {
	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	if (p->arr_Tr->len) //wenn mehr Q als q, dann braucht man auch nicht weiterleiten...
		g_array_remove_index(p->arr_Tr, p->arr_Tr->len - 1);

	if (proc && proc->chain && proc->chain->op_Q)
		proc->chain->op_Q(ctx, proc->chain);

	return;
}

static void text_analyzer_op_Tr(fz_context *ctx, pdf_processor *proc,
		gint render) {
	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	(((gint*) (void*) (p->arr_Tr)->data)[(p->arr_Tr->len - 1)]) = render;

	if (proc && proc->chain && proc->chain->op_Tr)
		proc->chain->op_Tr(ctx, proc->chain, render);

	return;
}

static void text_analyzer_op_TJ(fz_context *ctx, pdf_processor *proc,
		pdf_obj *array) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (Tr == 3)
		p->has_hidden_text = TRUE;
	else
		p->has_visible_text = TRUE;

	if ((p->flags & 1) && Tr != 3)
		return;
	else if ((p->flags & 2) && Tr == 3)
		return;

	if (proc && proc->chain && proc->chain->op_TJ)
		proc->chain->op_TJ(ctx, proc->chain, array);

	return;
}

static void text_analyzer_op_Tj(fz_context *ctx, pdf_processor *proc,
		gchar *str, size_t len) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (Tr == 3)
		p->has_hidden_text = TRUE;
	else
		p->has_visible_text = TRUE;

	if ((p->flags & 1) && Tr != 3)
		return;
	else if ((p->flags & 2) && Tr == 3)
		return;

	if (proc && proc->chain && proc->chain->op_Tj)
		proc->chain->op_Tj(ctx, proc->chain, str, len);

	return;
}

static void text_analyzer_op_squote(fz_context *ctx, pdf_processor *proc,
		gchar *str, size_t len) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (Tr == 3)
		p->has_hidden_text = TRUE;
	else
		p->has_visible_text = TRUE;

	if ((p->flags & 1) && Tr != 3)
		return;
	else if ((p->flags & 2) && Tr == 3)
		return;

	if (proc && proc->chain && proc->chain->op_squote)
		proc->chain->op_squote(ctx, proc->chain, str, len);

	return;
}

static void text_analyzer_op_dquote(fz_context *ctx, pdf_processor *proc,
		float aw, float ac, gchar *str, size_t len) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (Tr == 3)
		p->has_hidden_text = TRUE;
	else
		p->has_visible_text = TRUE;

	if ((p->flags & 1) && Tr != 3)
		return;
	else if ((p->flags & 2) && Tr == 3)
		return;

	if (proc && proc->chain && proc->chain->op_dquote)
		proc->chain->op_dquote(ctx, proc->chain, aw, ac, str, len);

	return;
}

static void drop_text_analyzer_processor(fz_context* ctx, pdf_processor* proc) {
	g_array_unref(((pdf_text_analyzer_processor*) proc)->arr_Tr);

//	if (proc && proc->chain && proc->chain->drop_processor)
//		proc->chain->drop_processor(ctx, proc->chain);

	return;
}

static void reset_text_analyzer_processor(fz_context* ctx, pdf_processor* proc) {
	gint zero = 0;

	((pdf_text_analyzer_processor*) proc)->has_hidden_text = FALSE;
	((pdf_text_analyzer_processor*) proc)->has_visible_text = FALSE;
	g_array_remove_range(((pdf_text_analyzer_processor*) proc)->arr_Tr, 0,
			((pdf_text_analyzer_processor*) proc)->arr_Tr->len);
	g_array_append_val(((pdf_text_analyzer_processor*) proc)->arr_Tr, zero);

	if (proc && proc->chain)
		pdf_reset_processor(ctx, proc->chain);

	return;
}

pdf_processor*
pdf_new_text_analyzer_processor(fz_context *ctx, pdf_processor* chain, gint flags, GError** error) {
	gint zero = 0;
	pdf_text_analyzer_processor *proc = NULL;

	proc = pdf_new_processor(ctx, sizeof(pdf_text_analyzer_processor));

	//Funktionen "umleiten"
	proc->super.drop_processor = drop_text_analyzer_processor;
	proc->super.reset_processor = reset_text_analyzer_processor;

	proc->arr_Tr = g_array_new( FALSE, FALSE, sizeof(gint));
	g_array_append_val(proc->arr_Tr, zero);

	proc->flags = flags;

	proc->super.op_q = text_analyzer_op_q;
	proc->super.op_Q = text_analyzer_op_Q;
	proc->super.op_Tr = text_analyzer_op_Tr;
	proc->super.op_TJ = text_analyzer_op_TJ;
	proc->super.op_Tj = text_analyzer_op_Tj;
	proc->super.op_squote = text_analyzer_op_squote;
	proc->super.op_dquote = text_analyzer_op_dquote;

	proc->super.chain = chain;

	if (chain) {
		proc->super.requirements = proc->super.chain->requirements;
		proc->super.chain->rstack = proc->super.rstack;
	}

	return (pdf_processor*) proc;
}

fz_buffer*
pdf_text_filter_page(fz_context *ctx, pdf_page* page, gint flags, GError **error) {
	pdf_processor *proc = NULL;
	pdf_processor* proc_buf = NULL;
	fz_buffer *buf = NULL;

	fz_try(ctx) {
		buf = fz_new_buffer(ctx, 4096);
	}
	fz_catch(ctx)
		ERROR_PDF_VAL(NULL)

	fz_try(ctx)
		proc_buf = pdf_new_buffer_processor(ctx, buf, 0, 0);
	fz_catch(ctx) {
		fz_drop_buffer(ctx, buf);
		ERROR_PDF_VAL(NULL)
	}

	proc = pdf_new_text_analyzer_processor(ctx, proc_buf, flags, error);
	if (!proc) {
		pdf_drop_processor(ctx, proc_buf);
		fz_drop_buffer(ctx, buf);

		return NULL;
	}

	fz_try(ctx)
		pdf_process_contents(ctx, proc, page->doc, pdf_page_resources(ctx, page),
				pdf_page_contents(ctx, page), NULL, NULL);
	fz_always(ctx) {
		pdf_close_processor(ctx, proc);
		pdf_drop_processor(ctx, proc);
		pdf_drop_processor(ctx, proc_buf);
	}
	fz_catch(ctx) {
		fz_drop_buffer(ctx, buf);
		ERROR_PDF_VAL(NULL)
	}

	return buf;
}

gint pdf_copy_page(fz_context *ctx, pdf_document *doc_src, gint page_from,
		gint page_to, pdf_document *doc_dest, gint page, GError **error) {
	pdf_graft_map *graft_map = NULL;

	graft_map = pdf_new_graft_map(ctx, doc_dest); //keine exception

	for (gint u = page_from; u <= page_to; u++) {
		fz_try(ctx)
			pdf_graft_mapped_page(ctx, graft_map, page++, doc_src, u);
		fz_catch( ctx )
		{
			pdf_drop_graft_map(ctx, graft_map);
			ERROR_PDF
		}
	}

	pdf_drop_graft_map(ctx, graft_map);

	return 0;
}

//Annotation, die beim Seitenkopieren nicht mitgenommen wird
static gboolean pdf_annot_nicht_kopieren(fz_context *ctx, pdf_obj *annot) {
	pdf_obj *subtype = NULL;
	gint flags = 0;

	if (!pdf_is_dict(ctx, annot))
		return TRUE;

	subtype = pdf_dict_get(ctx, annot, PDF_NAME(Subtype));
	if (pdf_name_eq(ctx, subtype, PDF_NAME(Link))
			|| pdf_name_eq(ctx, subtype, PDF_NAME(Widget))
			|| pdf_name_eq(ctx, subtype, PDF_NAME(Popup)))
		return TRUE;

	//Hidden (2) und NoView (32)
	flags = pdf_dict_get_int(ctx, annot, PDF_NAME(F));

	return (flags & (2 | 32)) != 0;
}

//Annotation in doc_dest neu anlegen, ohne Verweise auf die Quellseite
static pdf_obj* pdf_annot_kopieren(fz_context *ctx, pdf_graft_map *map,
		pdf_document *doc_dest, pdf_obj *annot) {
	pdf_obj *copy = NULL;
	pdf_obj *ref = NULL;

	fz_var(copy);

	fz_try(ctx) {
		gint n = pdf_dict_len(ctx, annot);

		copy = pdf_new_dict(ctx, doc_dest, n);

		for (gint i = 0; i < n; i++) {
			pdf_obj *key = pdf_dict_get_key(ctx, annot, i);
			pdf_obj *val = NULL;

			if (pdf_name_eq(ctx, key, PDF_NAME(P))
					|| pdf_name_eq(ctx, key, PDF_NAME(Parent))
					|| pdf_name_eq(ctx, key, PDF_NAME(Popup))
					|| pdf_name_eq(ctx, key, PDF_NAME(IRT)))
				continue;

			val = pdf_graft_mapped_object(ctx, map,
					pdf_dict_get_val(ctx, annot, i));
			fz_try(ctx)
				pdf_dict_put(ctx, copy, key, val);
			fz_always(ctx)
				pdf_drop_obj(ctx, val);
			fz_catch(ctx)
				fz_rethrow(ctx);
		}

		ref = pdf_add_object(ctx, doc_dest, copy);
	}
	fz_always(ctx)
		pdf_drop_obj(ctx, copy);
	fz_catch(ctx)
		fz_rethrow(ctx);

	return ref;
}

gint pdf_graft_page_mit_annots(fz_context *ctx, pdf_graft_map *map,
		pdf_document *doc_dest, pdf_document *doc_src, gint page_src,
		GError **error) {
	pdf_obj *annots_dest = NULL;

	fz_var(annots_dest);

	fz_try(ctx) {
		pdf_obj *obj_src = NULL;
		pdf_obj *annots_src = NULL;
		gint n = 0;

		pdf_graft_mapped_page(ctx, map, -1, doc_src, page_src);

		obj_src = pdf_lookup_page_obj(ctx, doc_src, page_src);
		annots_src = pdf_dict_get(ctx, obj_src, PDF_NAME(Annots));
		n = pdf_array_len(ctx, annots_src);

		for (gint i = 0; i < n; i++) {
			pdf_obj *annot = pdf_array_get(ctx, annots_src, i);
			pdf_obj *ref = NULL;

			if (pdf_annot_nicht_kopieren(ctx, annot))
				continue;

			ref = pdf_annot_kopieren(ctx, map, doc_dest, annot);
			fz_try(ctx) {
				if (!annots_dest)
					annots_dest = pdf_new_array(ctx, doc_dest, n);
				pdf_array_push(ctx, annots_dest, ref);
			}
			fz_always(ctx)
				pdf_drop_obj(ctx, ref);
			fz_catch(ctx)
				fz_rethrow(ctx);
		}

		if (annots_dest) {
			//die eben angehängte Seite
			pdf_obj *obj_dest = pdf_lookup_page_obj(ctx, doc_dest,
					pdf_count_pages(ctx, doc_dest) - 1);

			pdf_dict_put(ctx, obj_dest, PDF_NAME(Annots), annots_dest);
		}
	}
	fz_always(ctx)
		pdf_drop_obj(ctx, annots_dest);
	fz_catch(ctx)
		ERROR_PDF

	return 0;
}

static gint pdf_page_get_rotate(fz_context *ctx, pdf_obj *page_obj, GError** error) {
	pdf_obj *rotate_obj = NULL;
	gint rotate = 0;

	fz_try(ctx) //existierenden rotate-Wert ermitteln
		rotate_obj = pdf_dict_get_inheritable(ctx, page_obj, PDF_NAME(Rotate));
	fz_catch(ctx)
		ERROR_PDF

	if (rotate_obj) //sonst halt 0
		rotate = pdf_to_int(ctx, rotate_obj);

	return rotate;
}

gint pdf_page_rotate(fz_context *ctx, pdf_obj *page_obj, gint winkel,
		GError** error) {
	pdf_obj *rotate_obj = NULL;
	gint rotate = 0;

	rotate = pdf_page_get_rotate(ctx, page_obj, error);
	if (rotate == -1)
		return -1;

	rotate = rotate + winkel;
	if (rotate < 0)
		rotate += 360;
	else if (rotate > 360)
		rotate -= 360;
	else if (rotate == 360)
		rotate = 0;

	//prüfen, ob page-Knoten einen /Rotate-Eintrag hat, nicht nur geerbt
	if (!(rotate_obj = pdf_dict_get(ctx, page_obj, PDF_NAME(Rotate)))) {
		pdf_obj* rotate_page = NULL;

		//dann erzeugen und einfügen
		rotate_page = pdf_new_int(ctx, (int64_t) rotate);
		fz_try(ctx)
			pdf_dict_put(ctx, page_obj, PDF_NAME(Rotate), rotate_page);
		fz_always(ctx)
			pdf_drop_obj(ctx, rotate_obj);
		fz_catch(ctx)
			ERROR_PDF
	}
	else
		pdf_set_int(ctx, rotate_obj, (int64_t) rotate);

	return 0;
}

fz_buffer* pdf_doc_to_buf(fz_context* ctx, pdf_document* doc, GError** error) {
	fz_output* out = NULL;
	fz_buffer* buf = NULL;
	pdf_write_options in_opts =
			{ 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, ~0, "", "", 0, 0, 0, 0, 0 };

	//	if (pdf_count_pages(ctx, pdf_doc) < BIG_PDF && !pdf_doc->crypt)
	in_opts.do_garbage = 4;

	fz_try(ctx) {
		buf = fz_new_buffer(ctx, 4096);
	}
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
				"%s\n%s", __func__, fz_caught_message(ctx));

		return NULL;
	}

	fz_try(ctx)
		out = fz_new_output_with_buffer(ctx, buf);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
				"%s\n%s", __func__, fz_caught_message(ctx));
		fz_drop_buffer(ctx, buf);

		return NULL;
	}

	/* Schlüssel eingebetteter Dateien an ihre Dateinamen angleichen (ToDo.c
	 * #193) - ein Fehler verhindert das Schreiben nicht */
	{
		GError* error_norm = NULL;

		if (pdf_emb_normalize_keys(ctx, doc, &error_norm)) {
			LOG_WARN("%s: %s", __func__,
					error_norm ? error_norm->message : "?");
			g_clear_error(&error_norm);
		}
	}

	//do_appereance wird in pdf_write_document ignoriert. deshalb muß es hier gemacht werden
	if (doc->resynth_required) {
		gint i = 0;
		gint n = 0;

		n = pdf_count_pages(ctx, doc);
		for (i = 0; i < n; ++i)
		{
			pdf_page *page = pdf_load_page(ctx, doc, i);
			fz_try(ctx)
				pdf_update_page(ctx, page);
			fz_always(ctx)
				fz_drop_page(ctx, &page->super);
			fz_catch(ctx)
				fz_warn(ctx, "could not create annotation appearances");

			if (!doc->resynth_required) break;
		}
	}

	//immer noch? weil keine annot im gesamten Dokement
	if (doc->resynth_required)
		doc->resynth_required = 0; //dann mit Gewalt

	fz_try(ctx)
		pdf_write_document(ctx, doc, out, &in_opts);
	fz_always(ctx) {
		fz_close_output(ctx, out);
		fz_drop_output(ctx, out);
	}
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
				"%s\npdf_write_document: %s", __func__, fz_caught_message(ctx));
		fz_drop_buffer(ctx, buf);

		return NULL;
	}

	return buf;
}

gchar* pdf_emb_escape(gchar const* name) {
	GString* s = g_string_new(NULL);

	for (gchar const* p = name; p && *p; p++) {
		if (*p == '%')
			g_string_append(s, "%25");
		else if (*p == '/')
			g_string_append(s, "%2F");
		else if (*p == '\n') //Adresslisten sind zeilenweise gespeichert
			g_string_append(s, "%0A");
		else if (*p == '\r')
			g_string_append(s, "%0D");
		else
			g_string_append_c(s, *p);
	}

	return g_string_free(s, FALSE);
}

pdf_obj* pdf_get_EF_F(fz_context* ctx, pdf_obj* val, gchar const** path, GError** error) {
	gchar const* path_tmp = NULL;
	pdf_obj* EF_F = NULL;
	pdf_obj* F = NULL;
	pdf_obj* UF = NULL;
	pdf_obj* EF = NULL;

	fz_try(ctx) {
		EF = pdf_dict_get(ctx, val, PDF_NAME(EF));
		EF_F = pdf_dict_get(ctx, EF, PDF_NAME(F));
		F = pdf_dict_get(ctx, val, PDF_NAME(F));
		UF = pdf_dict_get(ctx, val, PDF_NAME(UF));

		if (pdf_is_string(ctx, UF))
			path_tmp = pdf_to_text_string(ctx, UF);
		else if (pdf_is_string(ctx, F))
			path_tmp = pdf_to_text_string(ctx, F);
	}
	fz_catch(ctx) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));

		return NULL;
	}

	if (path)
		*path = path_tmp;

	return EF_F;
}

static gint pdf_get_names_tree_dict(fz_context* ctx, pdf_document* doc,
		pdf_obj* name_dict, pdf_obj** dict, GError **error)
{
	pdf_obj* dict_res = NULL;

	fz_try(ctx)
	{
		pdf_obj *root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
		pdf_obj *names = pdf_dict_get(ctx, root, PDF_NAME(Names));
		dict_res = pdf_dict_get(ctx, names, name_dict);
	}
	fz_catch(ctx)
		ERROR_PDF

	if (dict)
		*dict = dict_res;

	return 0;
}

static gint
pdf_walk_names_dict(fz_context* ctx, pdf_obj *node, pdf_cycle_list *cycle_up,
		gint (*callback_walk) (fz_context*, pdf_obj*, pdf_obj*, pdf_obj*,
				gpointer, GError**), gpointer data, GError** error) {
	pdf_cycle_list cycle;
	pdf_obj *kids = NULL;
	pdf_obj *names = NULL;
	gboolean is_cycle = FALSE;
	gint i = 0;

	fz_try(ctx)
	{
		kids = pdf_dict_get(ctx, node, PDF_NAME(Kids));
		names = pdf_dict_get(ctx, node, PDF_NAME(Names));
		is_cycle = pdf_cycle(ctx, &cycle, cycle_up, node);
	}
	fz_catch(ctx)
		ERROR_PDF

	if (kids && !is_cycle) {
		pdf_obj* ind = NULL;

		do {
			ind = pdf_array_get(ctx, kids, i);
			if (ind) {
				gint rc = 0;

				rc = pdf_walk_names_dict(ctx, ind, &cycle, callback_walk, data, error);
				if (rc == -1)
					return -1;

				i++;
			}

		} while (ind);
	}
	else if (names) {
		pdf_obj* key = NULL;
		pdf_obj* val = NULL;

		do {
			fz_try(ctx) {
				key = pdf_array_get(ctx, names, i);
				val = pdf_array_get(ctx, names, i + 1);
			}
			fz_catch(ctx)
				ERROR_PDF

			if (key && val) {
				gint rc = 0;

				rc = callback_walk(ctx, names, key, val, data, error);
				if (rc == -1)
					return -1;
				else if (rc == 1) //Abbruch
					return 0;

				i += 2;
			}
		} while (key && val);
	}

	if (i == 0 && (kids || names)) {//kein einziger Eintrag in kids- oder names-array
		fz_try(ctx)
			pdf_dict_del(ctx, node, PDF_NAME(Names)); //saubermachen
		fz_catch(ctx)
			ERROR_PDF
	}

	return 0;
}

gint pdf_walk_embedded_files(fz_context* ctx, pdf_document* doc,
		gint (*callback_walk) (fz_context*, pdf_obj*, pdf_obj*, pdf_obj*,
				gpointer, GError**), gpointer data, GError** error) {
	gint rc = 0;
	pdf_obj* dict = NULL;

	rc = pdf_get_names_tree_dict(ctx, doc, PDF_NAME(EmbeddedFiles), &dict, error);
	if (rc)
		return -1;

	if (!dict)
		return 0; //nicht einmal EmbeddedFiles-Dict gefunden

	rc = pdf_walk_names_dict(ctx, dict, NULL, callback_walk, data, error);
	if (rc)
		return -1;

	return 0;
}

static int pdf_compare_strings(fz_context *ctx, pdf_obj *a, pdf_obj *b)
{
	size_t la, lb;
	const char *sa = pdf_to_str_buf(ctx, a);
	const char *sb = pdf_to_str_buf(ctx, b);
	la = pdf_to_str_len(ctx, a);
	lb = pdf_to_str_len(ctx, b);

	int cmp = strncmp(sa, sb, la < lb ? la : lb);
	if (cmp != 0) return cmp;
	return (la < lb) ? -1 : (la > lb) ? 1 : 0;
}

static gint pdf_insert_into_name_tree(fz_context *ctx, pdf_document *doc,
		pdf_obj *node, pdf_obj *key, pdf_obj *value, gboolean is_root, GError** error) {
	pdf_obj *arr = NULL;       /* Für neues Blatt-Names-Array */
	pdf_obj *limits = NULL;    /* Für Limits-Array */

	fz_var(arr);
	fz_var(limits);

	fz_try(ctx)
	{
		/* Bestehende Objekte aus dem Node */
		pdf_obj *kids = pdf_dict_get(ctx, node, PDF_NAME(Kids));
		pdf_obj *names = pdf_dict_get(ctx, node, PDF_NAME(Names));

		/* ---------------- Blatt-Knoten ---------------- */
		if (names)
		{
			int n = pdf_array_len(ctx, names);
			int pos = 0;

			/* Sortierte Position für Key finden */
			while (pos < n)
			{
				pdf_obj *existing_key = pdf_array_get(ctx, names, pos);
				if (pdf_compare_strings(ctx, key, existing_key) < 0)
					break;
				pos += 2;
			}

			pdf_array_insert(ctx, names, key, pos);
			pdf_array_insert(ctx, names, value, pos + 1);

			/* Limits nur setzen, wenn nicht Root */
			if (!is_root)
			{
				pdf_obj *first_key = pdf_array_get(ctx, names, 0);
				pdf_obj *last_key  = pdf_array_get(ctx, names, pdf_array_len(ctx, names) - 2);

				limits = pdf_new_array(ctx, doc, 2);
				pdf_array_push(ctx, limits, first_key);
				pdf_array_push(ctx, limits, last_key);

				pdf_dict_put(ctx, node, PDF_NAME(Limits), limits);
			}

			return 0;
		}

		/* ---------------- Intermediate Node ---------------- */
		if (kids)
		{
			int n = pdf_array_len(ctx, kids);
			const char *skey = NULL;
			skey = pdf_to_str_buf(ctx, key);

			int inserted = 0;
			for (int i = 0; i < n; i++)
			{
				pdf_obj *kid = pdf_array_get(ctx, kids, i);
				pdf_obj *kid_limits = pdf_dict_get(ctx, kid, PDF_NAME(Limits));

				if (!kid_limits || pdf_array_len(ctx, kid_limits) < 2)
				{
					pdf_insert_into_name_tree(ctx, doc, kid, key, value, FALSE, error);
					inserted = 1;
					break;
				}

				const char *low  = pdf_to_str_buf(ctx, pdf_array_get(ctx, kid_limits, 0));
				const char *high = pdf_to_str_buf(ctx, pdf_array_get(ctx, kid_limits, 1));

				if (strcmp(skey, low) >= 0 && strcmp(skey, high) <= 0)
				{
					pdf_insert_into_name_tree(ctx, doc, kid, key, value, FALSE, error);
					inserted = 1;
					break;
				}
			}

			if (!inserted)
				pdf_insert_into_name_tree(ctx, doc,
						pdf_array_get(ctx, kids, n - 1), key, value, FALSE, error);

			/* Limits für Intermediate Node setzen, außer Root */
			if (!is_root)
			{
				pdf_obj *first_child = pdf_array_get(ctx, kids, 0);
				pdf_obj *last_child  = pdf_array_get(ctx, kids, n - 1);

				pdf_obj *first_key = pdf_array_get(ctx,
						pdf_dict_get(ctx, first_child, PDF_NAME(Limits)), 0);
				pdf_obj *last_key  = pdf_array_get(ctx,
						pdf_dict_get(ctx, last_child, PDF_NAME(Limits)), 1);

				limits = pdf_new_array(ctx, doc, 2);
				pdf_array_push(ctx, limits, first_key);
				pdf_array_push(ctx, limits, last_key);

				pdf_dict_put(ctx, node, PDF_NAME(Limits), limits);
			}

			return 0;
		}

		/* ---------------- Leerer Node: Neues Blatt ---------------- */
		arr = pdf_new_array(ctx, doc, 2);
		pdf_array_push(ctx, arr, key);
		pdf_array_push(ctx, arr, value);
		pdf_dict_put(ctx, node, PDF_NAME(Names), arr);

		/* Limits nur setzen, wenn nicht Root */
		if (!is_root)
		{
			limits = pdf_new_array(ctx, doc, 2);
			pdf_array_push(ctx, limits, key);  /* Kein Drop */
			pdf_array_push(ctx, limits, key);  /* Kein Drop */
			pdf_dict_put(ctx, node, PDF_NAME(Limits), limits);
		}
	}
	fz_always(ctx)
	{
		pdf_drop_obj(ctx, arr);
		pdf_drop_obj(ctx, limits);
	}
	fz_catch(ctx)
		ERROR_PDF

	return 0;
}

typedef struct {
	gchar const* key;
	gboolean found;
} KeySearch;

static gint pdf_emb_key_search(fz_context* ctx, pdf_obj* names, pdf_obj* key,
		pdf_obj* val, gpointer data, GError** error) {
	KeySearch* ks = (KeySearch*) data;
	gchar const* text = NULL;

	fz_try(ctx)
		text = pdf_to_text_string(ctx, key);
	fz_catch(ctx)
		text = NULL;

	if (!g_strcmp0(text, ks->key)) {
		ks->found = TRUE;
		return 1;
	}

	return 0;
}

/* Schlüssel im Namensbaum müssen eindeutig sein (PDF-Norm). Der Dateiname
 * genügt dafür nicht: nach einer Umbenennung (ändert nur /F und /UF) kann
 * ein alter Schlüssel noch belegt sein. Dann " (n)" anhängen. Rückgabe
 * neu alloziert, NULL bei Fehler. */
static gchar* pdf_emb_unique_key(fz_context* ctx, pdf_obj* emb,
		gchar const* filename, GError** error) {
	gchar* candidate = g_strdup(filename);

	for (guint i = 1; ; i++) {
		KeySearch ks = { candidate, FALSE };

		if (pdf_walk_names_dict(ctx, emb, NULL, pdf_emb_key_search, &ks, error)) {
			g_free(candidate);
			return NULL;
		}
		if (!ks.found)
			return candidate;

		g_free(candidate);
		candidate = g_strdup_printf("%s (%u)", filename, i);
	}
}

gint pdf_insert_emb_file(fz_context* ctx, pdf_document* doc,
		fz_buffer* buf, gchar const* filename,
		gchar const* mime_type, GError** error) {
	pdf_obj* catalog = NULL;
	pdf_obj* names = NULL;
	pdf_obj* emb = NULL;
	pdf_obj* file_stream = NULL;
	pdf_obj* params = NULL;
	pdf_obj* ef = NULL;
	pdf_obj* filespec = NULL;
	pdf_obj* key = NULL;
	gchar* key_str = NULL;
	gint rc = 0;

	fz_try(ctx)
		catalog = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
	fz_catch(ctx)
		ERROR_PDF

	if (!catalog) {
		if (error) *error = g_error_new(g_quark_from_static_string("sond"),
				0, "%s\nCatalog nicht gefunden", __func__);
		pdf_drop_document(ctx, doc);

		return -1;
	}

	fz_try(ctx)
		names = pdf_dict_get(ctx, catalog, PDF_NAME(Names));
	fz_catch(ctx)
		ERROR_PDF

	if (!names)
	{
		fz_var(names);
		fz_try(ctx) {
			names = pdf_new_dict(ctx, doc, 1);
			pdf_dict_put(ctx, catalog, PDF_NAME(Names), names);
		}
		fz_always(ctx)
			pdf_drop_obj(ctx, names);
		fz_catch(ctx) {
			pdf_drop_document(ctx, doc);

			ERROR_PDF
		}
	}

	fz_try(ctx)
		emb = pdf_dict_get(ctx, names, PDF_NAME(EmbeddedFiles));
	fz_catch(ctx)
		ERROR_PDF

	if (!emb) {
		pdf_obj* names_array = NULL;

		fz_var(names_array);
		fz_var(emb);
		fz_try(ctx) {
			emb = pdf_new_dict(ctx, doc, 2);
			pdf_dict_put(ctx, names, PDF_NAME(EmbeddedFiles), emb);

			names_array = pdf_new_array(ctx, doc, 0);
			pdf_dict_put(ctx, emb, PDF_NAME(Names), names_array);
		}
		fz_always(ctx) {
			pdf_drop_obj(ctx, emb);
			pdf_drop_obj(ctx, names_array);
		}
		fz_catch(ctx)
			ERROR_PDF
	}

	key_str = pdf_emb_unique_key(ctx, emb, filename, error);
	if (!key_str)
		return -1;

    /* ---------- Datei-Stream ---------- */
	fz_var(file_stream);
	fz_var(params);
	fz_var(ef);
	fz_var(filespec);
	fz_var(key);

	fz_try(ctx) {
		file_stream = pdf_add_stream(ctx, doc, buf, NULL, 0);

		/* ---------- Params ---------- */
		params = pdf_new_dict(ctx, doc, 2);
		pdf_dict_put_drop(ctx, params, PDF_NAME(Size), pdf_new_int(ctx, buf->len));

		/* ---------- EF ---------- */
		ef = pdf_new_dict(ctx, doc, 1);
		pdf_dict_put(ctx, ef, PDF_NAME(F), file_stream);

		/* ---------- FileSpec ---------- */
		filespec = pdf_new_dict(ctx, doc, 5);
		pdf_dict_put_drop(ctx, filespec, PDF_NAME(Type), pdf_new_name(ctx, "Filespec"));
		/* Dateiname als Textstring in /F und /UF (PDF-Norm), Schlüssel =
		 * Dateiname (s. pdf_emb_normalize_keys()) */
		pdf_dict_put_text_string(ctx, filespec, PDF_NAME(F), filename);
		pdf_dict_put_text_string(ctx, filespec, PDF_NAME(UF), filename);
		pdf_dict_put(ctx, filespec, PDF_NAME(EF), ef);
		pdf_dict_put(ctx, filespec, PDF_NAME(Params), params);

		if (mime_type)
		{
			pdf_dict_put_drop(ctx, filespec, PDF_NAME(Subtype),
						 pdf_new_string(ctx, mime_type, strlen(mime_type)));
		}

		/* ---------- Key für Namen ---------- */
		key = pdf_new_text_string(ctx, key_str);
	}
	fz_always(ctx) {
		pdf_drop_obj(ctx, file_stream);
		pdf_drop_obj(ctx, params);
		pdf_drop_obj(ctx, ef);
		g_free(key_str);
	}
	fz_catch(ctx) {
		pdf_drop_obj(ctx, filespec);
		pdf_drop_obj(ctx, key);

		ERROR_PDF
	}

	rc = pdf_insert_into_name_tree(ctx, doc, emb, key, filespec, TRUE, error);
	pdf_drop_obj(ctx, filespec);
	pdf_drop_obj(ctx, key);
	if (rc)
		return -1;

	return 0;
}

typedef struct {
	pdf_obj* key;      //eigene Ref
	pdf_obj* val;      //eigene Ref
	gchar* key_text;   //UTF-8
	gchar* filename;   //UTF-8 (/UF, sonst /F), NULL wenn keiner
	pdf_obj* key_new;  //eigene Ref, NULL = Schlüssel bleibt
} EmbEntry;

static void emb_entry_clear(fz_context* ctx, EmbEntry* e) {
	pdf_drop_obj(ctx, e->key);
	pdf_drop_obj(ctx, e->val);
	pdf_drop_obj(ctx, e->key_new);
	g_free(e->key_text);
	g_free(e->filename);
}

static gint emb_collect(fz_context* ctx, pdf_obj* names, pdf_obj* key,
		pdf_obj* val, gpointer data, GError** error) {
	GArray* entries = (GArray*) data;
	EmbEntry e = { 0 };
	gchar const* key_text = NULL;
	gchar const* filename = NULL;

	fz_try(ctx) {
		key_text = pdf_to_text_string(ctx, key);
		if (pdf_is_string(ctx, pdf_dict_get(ctx, val, PDF_NAME(UF))))
			filename = pdf_to_text_string(ctx, pdf_dict_get(ctx, val, PDF_NAME(UF)));
		else if (pdf_is_string(ctx, pdf_dict_get(ctx, val, PDF_NAME(F))))
			filename = pdf_to_text_string(ctx, pdf_dict_get(ctx, val, PDF_NAME(F)));
	}
	fz_catch(ctx)
		ERROR_PDF

	e.key = pdf_keep_obj(ctx, key);
	e.val = pdf_keep_obj(ctx, val);
	e.key_text = g_strdup(key_text);
	e.filename = (filename && *filename) ? g_strdup(filename) : NULL;
	g_array_append_val(entries, e);

	return 0;
}

/* Alle Einträge des EmbeddedFiles-Namensbaums; *entries bleibt NULL, wenn
 * es keinen Namensbaum gibt */
static gint emb_collect_all(fz_context* ctx, pdf_document* doc,
		pdf_obj** emb_out, GArray** entries, GError** error) {
	pdf_obj* emb = NULL;
	gint rc = 0;

	*entries = NULL;

	rc = pdf_get_names_tree_dict(ctx, doc, PDF_NAME(EmbeddedFiles), &emb, error);
	if (rc)
		return -1;
	if (!emb)
		return 0;

	*entries = g_array_new(FALSE, TRUE, sizeof(EmbEntry));
	rc = pdf_walk_names_dict(ctx, emb, NULL, emb_collect, *entries, error);
	if (rc) {
		for (guint i = 0; i < (*entries)->len; i++)
			emb_entry_clear(ctx, &g_array_index(*entries, EmbEntry, i));
		g_array_unref(*entries);
		*entries = NULL;

		return -1;
	}

	if (emb_out)
		*emb_out = emb;

	return 0;
}

static void emb_entries_free(fz_context* ctx, GArray* entries) {
	if (!entries)
		return;

	for (guint i = 0; i < entries->len; i++)
		emb_entry_clear(ctx, &g_array_index(entries, EmbEntry, i));
	g_array_unref(entries);
}

/* Gemeinsame Regel für Adresse und Angleichung: der Dateiname von Eintrag i
 * dient als Adresse (und Schlüssel), wenn kein anderer Eintrag denselben
 * Dateinamen hat und keiner ihn als Schlüssel trägt. Sonst bleibt der
 * Schlüssel maßgeblich - so ändert die Angleichung die Adresse nie. */
static gboolean emb_filename_is_address(GArray* entries, guint i) {
	EmbEntry* e = &g_array_index(entries, EmbEntry, i);

	if (!e->filename)
		return FALSE;

	for (guint j = 0; j < entries->len; j++) {
		EmbEntry* o = &g_array_index(entries, EmbEntry, j);

		if (j == i)
			continue;
		if (!g_strcmp0(o->filename, e->filename) ||
				!g_strcmp0(o->key_text, e->filename))
			return FALSE;
	}

	return TRUE;
}

/* Adresse von Eintrag i (neu alloziert, NULL ohne Schlüssel und Namen) */
static gchar* emb_address(GArray* entries, guint i) {
	EmbEntry* e = &g_array_index(entries, EmbEntry, i);
	gchar const* text = NULL;

	if (emb_filename_is_address(entries, i))
		text = e->filename;
	else if (e->key_text && *e->key_text)
		text = e->key_text;
	else
		text = e->filename;

	return text ? pdf_emb_escape(text) : NULL;
}

GHashTable* pdf_emb_addresses_new(fz_context* ctx, pdf_document* doc,
		GError** error) {
	GArray* entries = NULL;
	GHashTable* addresses = NULL;

	if (emb_collect_all(ctx, doc, NULL, &entries, error))
		return NULL;

	addresses = g_hash_table_new_full(NULL, NULL, NULL, g_free);
	if (!entries)
		return addresses;

	for (guint i = 0; i < entries->len; i++) {
		gchar* address = emb_address(entries, i);

		if (address)
			g_hash_table_insert(addresses,
					g_array_index(entries, EmbEntry, i).val, address);
	}

	emb_entries_free(ctx, entries);

	return addresses;
}

gint pdf_emb_address_changes(fz_context* ctx, pdf_document* doc,
		gchar const* address_target, gchar const* filename_new,
		GPtrArray** addresses_old, GPtrArray** addresses_new, GError** error) {
	GArray* entries = NULL;
	GArray* sim = NULL;
	GPtrArray* before = NULL;
	gint target = -1;

	*addresses_old = g_ptr_array_new_with_free_func(g_free);
	*addresses_new = g_ptr_array_new_with_free_func(g_free);

	if (emb_collect_all(ctx, doc, NULL, &entries, error)) {
		g_clear_pointer(addresses_old, g_ptr_array_unref);
		g_clear_pointer(addresses_new, g_ptr_array_unref);
		return -1;
	}
	if (!entries)
		return 0;

	before = g_ptr_array_new_with_free_func(g_free);
	for (guint i = 0; i < entries->len; i++) {
		gchar* address = emb_address(entries, i);

		if (target < 0 && !g_strcmp0(address, address_target))
			target = (gint) i;
		g_ptr_array_add(before, address);
	}
	//gespeicherte Adresse aus der Zeit vor #193: Ersatzsuche über den Namen
	for (guint i = 0; target < 0 && i < entries->len; i++)
		if (!g_strcmp0(g_array_index(entries, EmbEntry, i).filename,
				address_target))
			target = (gint) i;

	if (target < 0) //Ziel unbekannt - keine Änderungen ableitbar
		goto out;

	/* Zustand danach simulieren: Ziel entfernt bzw. mit neuem Namen (Schlüssel
	 * = Dateiname, s. pdf_emb_normalize_keys()) - nur key_text/filename
	 * werden für die Regel gebraucht */
	sim = g_array_new(FALSE, TRUE, sizeof(EmbEntry));
	for (guint i = 0; i < entries->len; i++) {
		EmbEntry* e = &g_array_index(entries, EmbEntry, i);
		EmbEntry s = { 0 };

		if ((gint) i == target && !filename_new)
			continue;

		s.key_text = g_strdup((gint) i == target ? filename_new : e->key_text);
		s.filename = g_strdup((gint) i == target ? filename_new : e->filename);
		g_array_append_val(sim, s);
	}

	for (guint i = 0, j = 0; i < entries->len; i++) {
		gchar* address_after = NULL;

		if ((gint) i == target && !filename_new)
			continue; //entfernt, kein Gegenstück in sim

		address_after = emb_address(sim, j++);
		if ((gint) i != target && address_after &&
				g_strcmp0(g_ptr_array_index(before, i), address_after)) {
			g_ptr_array_add(*addresses_old,
					g_strdup(g_ptr_array_index(before, i)));
			g_ptr_array_add(*addresses_new, address_after);
		}
		else
			g_free(address_after);
	}

out:
	g_ptr_array_unref(before);
	emb_entries_free(ctx, sim);
	emb_entries_free(ctx, entries);

	return 0;
}

static gint emb_entry_cmp(gconstpointer a, gconstpointer b, gpointer data) {
	EmbEntry const* ea = a;
	EmbEntry const* eb = b;

	return pdf_compare_strings((fz_context*) data,
			ea->key_new ? ea->key_new : ea->key,
			eb->key_new ? eb->key_new : eb->key);
}

gint pdf_emb_normalize_keys(fz_context* ctx, pdf_document* doc, GError** error) {
	pdf_obj* emb = NULL;
	GArray* entries = NULL;
	gboolean changed = FALSE;
	gint rc = 0;

	rc = emb_collect_all(ctx, doc, &emb, &entries, error);
	if (rc)
		return -1;
	if (!entries)
		return 0;

	/* Schlüssel auf den Dateinamen setzen, wo der Dateiname die Adresse ist
	 * (emb_filename_is_address()) - bei doppelten Dateinamen bleiben die
	 * Schlüssel (eindeutige Adressen) */
	for (guint i = 0; i < entries->len; i++) {
		EmbEntry* e = &g_array_index(entries, EmbEntry, i);

		if (!g_strcmp0(e->key_text, e->filename) ||
				!emb_filename_is_address(entries, i))
			continue;

		fz_try(ctx)
			e->key_new = pdf_new_text_string(ctx, e->filename);
		fz_catch(ctx) {
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__, fz_caught_message(ctx));
			rc = -1;
			goto out;
		}
		changed = TRUE;
	}

	if (!changed)
		goto out;

	/* Namensbaum flach und sortiert neu aufbauen; alte Zwischenknoten
	 * entfallen beim Schreiben (garbage collection) */
	g_array_sort_with_data(entries, emb_entry_cmp, ctx);

	fz_try(ctx) {
		pdf_obj* arr = pdf_new_array(ctx, doc, 2 * entries->len);

		for (guint i = 0; i < entries->len; i++) {
			EmbEntry* e = &g_array_index(entries, EmbEntry, i);

			pdf_array_push(ctx, arr, e->key_new ? e->key_new : e->key);
			pdf_array_push(ctx, arr, e->val);
		}
		pdf_dict_del(ctx, emb, PDF_NAME(Kids));
		pdf_dict_del(ctx, emb, PDF_NAME(Limits));
		pdf_dict_put_drop(ctx, emb, PDF_NAME(Names), arr);
	}
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
				fz_caught(ctx), "%s\n%s", __func__, fz_caught_message(ctx));
		rc = -1;
	}

out:
	emb_entries_free(ctx, entries);

	return rc;
}

static gint pdf_run_pixmap(fz_context* ctx, pdf_page* page,
		fz_device* dev, GError** error) {
	pdf_processor* proc_run = NULL;
	pdf_processor* proc_text = NULL;

	fz_try(ctx)
		proc_run = pdf_new_run_processor(ctx, page->doc, dev, fz_identity, -1,
			"View", NULL, NULL, NULL, NULL, NULL);
	fz_catch(ctx)
		ERROR_PDF

		// text-analyzer-Processor erstellen (dieser filtert den Text)
	fz_try(ctx) //flag == 3: aller Text weg, nur Bilder ocr-en
		proc_text = pdf_new_text_analyzer_processor(ctx, proc_run, 3, error);
	fz_catch(ctx) {
		pdf_close_processor(ctx, proc_run);
		pdf_drop_processor(ctx, proc_run);

		ERROR_PDF
	}

    // Content durch Filter-Kette schicken
	fz_try(ctx)
		pdf_process_contents(ctx, proc_text, page->doc,
				pdf_page_resources(ctx, page), pdf_page_contents(ctx, page),
				NULL, NULL);
	fz_always(ctx) {
		pdf_close_processor(ctx, proc_text);
		pdf_drop_processor(ctx, proc_text);
		pdf_drop_processor(ctx, proc_run);
	}
	fz_catch(ctx)
		ERROR_PDF

	return 0;
}

fz_pixmap* pdf_render_pixmap(fz_context *ctx, pdf_page* page,
		float scale, GError** error) {
	gint rc = 0;
	fz_device *draw_device = NULL;
	fz_pixmap *pixmap = NULL;
	fz_rect rect = { 0 };
	fz_matrix ctm = { 0 };

	pdf_page_transform(ctx, page, &rect, &ctm);
	ctm = fz_pre_scale(ctm, scale, scale);
	rect = fz_transform_rect(rect, ctm);

	//per draw-device to pixmap
	fz_try( ctx )
		pixmap = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx),
				fz_irect_from_rect(rect), NULL, 0);
	fz_catch(ctx)
		ERROR_PDF_VAL(NULL)

	fz_try( ctx)
		fz_clear_pixmap_with_value(ctx, pixmap, 255);
	fz_catch(ctx) {
		fz_drop_pixmap(ctx, pixmap);

		ERROR_PDF_VAL(NULL)
	}

	fz_try(ctx)
		draw_device = fz_new_draw_device(ctx, ctm, pixmap);
	fz_catch(ctx) {
		fz_drop_pixmap(ctx, pixmap);

		ERROR_PDF_VAL(NULL)
	}

	rc = pdf_run_pixmap(ctx, page, draw_device, error);
	fz_close_device(ctx, draw_device);
	fz_drop_device(ctx, draw_device);
	if (rc) {
		fz_drop_pixmap(ctx, pixmap);

		ERROR_PDF_VAL(NULL)
	}

	return pixmap;
}

gint pdf_set_content_stream(fz_context *ctx,
						   pdf_page *page,
						   fz_buffer* content,
						   GError** error) {
	//altes content-stream-object überschreiben
	fz_try(ctx) {
		pdf_obj* contents_new = pdf_add_object_drop(ctx, page->doc,
				pdf_new_dict(ctx, page->doc, 1));
		pdf_dict_put_drop(ctx, page->obj, PDF_NAME(Contents),
				contents_new);
	}
	fz_catch(ctx)
		ERROR_PDF

	fz_try( ctx ) {
		pdf_obj* contents = pdf_dict_get(ctx, page->obj, PDF_NAME(Contents));
		pdf_update_stream(ctx, page->doc, contents, content, 0);
	}
	fz_catch(ctx)
		ERROR_PDF

	return 0;
}

fz_buffer* pdf_get_content_stream_as_buffer(fz_context *ctx, pdf_obj *page_ref,
		GError **error) {
	pdf_obj *obj_contents = NULL;
	fz_stream *stream = NULL;
	fz_buffer *buf = NULL;

	fz_try( ctx ) {
		obj_contents = pdf_dict_get(ctx, page_ref, PDF_NAME(Contents));
		stream = pdf_open_contents_stream(ctx,
				pdf_get_bound_document(ctx, page_ref), obj_contents);
		buf = fz_read_all(ctx, stream, 1024);
	}
	fz_always( ctx )
		fz_drop_stream(ctx, stream);
	fz_catch ( ctx )
		ERROR_PDF_VAL(NULL)

	return buf;
}

gint pdf_get_sond_font(fz_context* ctx, pdf_document* doc, pdf_obj** font_ref,
		GError** error) {
	gint num_pages = 0;

	fz_try(ctx)
		num_pages = pdf_count_pages(ctx, doc);
	fz_catch(ctx)
		ERROR_PDF

	for (gint u = 0; u < num_pages; u++) {
		pdf_obj* page_ref = NULL;
		pdf_obj* resources = NULL;
		pdf_obj* font_dict = NULL;

		fz_try(ctx) {
			page_ref = pdf_lookup_page_obj(ctx, doc, u);
			resources = pdf_dict_get_inheritable(ctx, page_ref,
					PDF_NAME(Resources));
			font_dict = pdf_dict_get(ctx, resources, PDF_NAME(Font));
			*font_ref = pdf_dict_gets(ctx, font_dict, "FSond");
		}
		fz_catch(ctx)
			ERROR_PDF
	}

	return 0;
}

pdf_obj* pdf_put_sond_font(fz_context* ctx, pdf_document* doc, GError** error) {
	pdf_obj* font = NULL;
	pdf_obj* font_ref = NULL;

	fz_try(ctx)
		// Font neu anlegen als indirektes Objekt
		font = pdf_new_dict(ctx, doc, 4);
	fz_catch(ctx)
		ERROR_PDF_VAL(NULL)

	fz_try(ctx) {
		pdf_dict_put(ctx, font, PDF_NAME(Type), PDF_NAME(Font));
		pdf_dict_put(ctx, font, PDF_NAME(Subtype), PDF_NAME(Type1));
		pdf_dict_put_drop(ctx, font, PDF_NAME(BaseFont), pdf_new_name(ctx, "Helvetica"));
		pdf_dict_put(ctx, font, PDF_NAME(Encoding), PDF_NAME(WinAnsiEncoding));

		// Als indirektes Objekt hinzufügen
		font_ref = pdf_add_object(ctx, doc, font);
	}
	fz_always(ctx)
		pdf_drop_obj(ctx, font);
	fz_catch(ctx)
		ERROR_PDF_VAL(NULL)

	return font_ref;
}

gint pdf_page_has_text(fz_context* ctx, pdf_page* page,
		gboolean* has_text, gboolean* has_hidden_text, GError** error) {
	pdf_processor* proc = NULL;
	pdf_text_analyzer_processor* p = NULL;

	proc = pdf_new_text_analyzer_processor(ctx, NULL, 3, error);
	if (!proc)
		return -1;

	fz_try(ctx)
		pdf_process_contents(ctx, proc, page->doc, pdf_page_resources(ctx, page),
				pdf_page_contents(ctx, page), NULL, NULL);
	fz_catch(ctx) {
		pdf_close_processor(ctx, proc);
		pdf_drop_processor(ctx, proc);

		ERROR_PDF
	}

	p = (pdf_text_analyzer_processor*) proc;

	if (has_text)
		*has_text = p->has_visible_text || p->has_hidden_text;
	if (has_hidden_text)
		*has_hidden_text = p->has_hidden_text;

	pdf_close_processor(ctx, proc);
	pdf_drop_processor(ctx, proc);

	return 0;
}

pdf_annot* pdf_annot_lookup_index(fz_context* ctx, pdf_page* pdf_page, gint index) {
	pdf_annot *pdf_annot = NULL;

	pdf_annot = pdf_first_annot(ctx, pdf_page); //kein Fehler

	for (gint i = 0; i < index; i++)
		pdf_annot = pdf_next_annot(ctx, pdf_annot);

	return pdf_annot;
}

fz_stream*
sond_pdf_open_file(fz_context *ctx, const gchar *path, GError **error)
{
    fz_stream *stream = NULL;

#ifdef G_OS_WIN32
    /* Auf Windows: fz_open_file_w mit \\?\-Präfix für Long-Path-Support.
     * fz_open_file_w öffnet intern mit _wfopen und schließt FILE* beim Drop. */
    wchar_t *long_path = prepare_long_path(path, error);
    if (!long_path)
        return NULL;

    fz_try(ctx)
        stream = fz_open_file_w(ctx, long_path);
    fz_catch(ctx) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "%s\nfz_open_file_w: %s", __func__, fz_caught_message(ctx));
        g_free(long_path);
        return NULL;
    }
    g_free(long_path);
#else
    fz_try(ctx)
        stream = fz_open_file(ctx, path);
    fz_catch(ctx) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "%s\nfz_open_file: %s", __func__, fz_caught_message(ctx));
        return NULL;
    }
#endif

    return stream;
}

fz_stream*
sond_gbytes_to_fz_stream(fz_context* ctx, GBytes* bytes, GError** error)
{
    gsize len = 0;
    const guchar* data = g_bytes_get_data(bytes, &len);
    fz_buffer* buf = NULL;
    fz_stream* stream = NULL;

    fz_try(ctx)
        buf = fz_new_buffer_from_copied_data(ctx, data, len);
    fz_catch(ctx) {
        if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
                fz_caught(ctx), "%s\nfz_new_buffer_from_copied_data: %s",
                __func__, fz_caught_message(ctx));
        return NULL;
    }

    fz_try(ctx)
        stream = fz_open_buffer(ctx, buf);
    fz_always(ctx)
        fz_drop_buffer(ctx, buf); /* stream hält eigene Referenz */
    fz_catch(ctx) {
        if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
                fz_caught(ctx), "%s\nfz_open_buffer: %s",
                __func__, fz_caught_message(ctx));
        return NULL;
    }

    return stream;
}
