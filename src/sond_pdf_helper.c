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
#include "sond_misc.h"
#include "sond_log_and_error.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <glib.h>

/* PDF-Text-Filter */
typedef struct resources_stack {
	struct resources_stack *next;
	pdf_obj *res;
} resources_stack;

typedef struct
{
	pdf_processor super;
	fz_output *out;
	int ahxencode;
	int extgstate;
	int newlines;
	int balance;
	pdf_obj *res;
	pdf_obj *last_res;
	resources_stack *rstack;
	int sep;
} pdf_output_processor;

typedef struct {
	pdf_output_processor super;
	void (*pdf_drop_buffer_processor)(fz_context *ctx, pdf_processor *proc);

	void (*pdf_buffer_processor_op_q)(fz_context *ctx, pdf_processor *proc);
	void (*pdf_buffer_processor_op_Q)(fz_context *ctx, pdf_processor *proc);

	void (*pdf_buffer_processor_op_Tr)(fz_context *ctx, pdf_processor *proc,
			gint);

	void (*pdf_buffer_processor_op_TJ)(fz_context*, pdf_processor*, pdf_obj*);
	void (*pdf_buffer_processor_op_Tj)(fz_context*, pdf_processor*, gchar*,
			size_t);
	void (*pdf_buffer_processor_op_squote)(fz_context*, pdf_processor*, gchar*,
			size_t);
	void (*pdf_buffer_processor_op_dquote)(fz_context*, pdf_processor*, float,
			float, gchar*, size_t);

	GArray *arr_Tr;
	gint flags;
} pdf_text_filter_processor;

static void pdf_drop_text_filter_processor(fz_context *ctx, pdf_processor *proc) {
	pdf_text_filter_processor *p = (pdf_text_filter_processor*) proc;

	g_array_unref(p->arr_Tr);

	p->pdf_drop_buffer_processor(ctx, proc);

//	fz_free( ctx, proc );

	return;
}

static void pdf_text_filter_op_q(fz_context *ctx, pdf_processor *proc) {
	gint Tr = 0;

	pdf_text_filter_processor *p = (pdf_text_filter_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	g_array_append_val(p->arr_Tr, Tr);

	//chain-up
	p->pdf_buffer_processor_op_q(ctx, proc);

	return;
}

static void pdf_text_filter_op_Q(fz_context *ctx, pdf_processor *proc) {
	pdf_text_filter_processor *p = (pdf_text_filter_processor*) proc;

	if (p->arr_Tr->len) //wenn mehr Q als q, dann braucht man auch nicht weiterleiten...
	{
		g_array_remove_index(p->arr_Tr, p->arr_Tr->len - 1);

		p->pdf_buffer_processor_op_Q(ctx, proc);
	}

	return;
}

static void pdf_text_filter_op_Tr(fz_context *ctx, pdf_processor *proc,
		gint render) {
	pdf_text_filter_processor *p = (pdf_text_filter_processor*) proc;

	(((gint*) (void*) (p->arr_Tr)->data)[(p->arr_Tr->len - 1)]) = render;

	p->pdf_buffer_processor_op_Tr(ctx, proc, render);

	return;
}

static void pdf_text_filter_op_TJ(fz_context *ctx, pdf_processor *proc,
		pdf_obj *array) {
	gint Tr = 0;

	pdf_text_filter_processor *p = (pdf_text_filter_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if ((p->flags & 1) && Tr != 3)
		return;
	if ((p->flags & 2) && Tr == 3)
		return;

	p->pdf_buffer_processor_op_TJ(ctx, proc, array);

	return;
}

static void pdf_text_filter_op_Tj(fz_context *ctx, pdf_processor *proc,
		gchar *str, size_t len) {
	gint Tr = 0;

	pdf_text_filter_processor *p = (pdf_text_filter_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (p->flags & 1 && Tr != 3)
		return;
	if (p->flags & 2 && Tr == 3)
		return;

	p->pdf_buffer_processor_op_Tj(ctx, proc, str, len);

	return;
}

static void pdf_text_filter_op_squote(fz_context *ctx, pdf_processor *proc,
		gchar *str, size_t len) {
	gint Tr = 0;

	pdf_text_filter_processor *p = (pdf_text_filter_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (p->flags & 1 && Tr != 3)
		return;
	if (p->flags & 2 && Tr == 3)
		return;

	p->pdf_buffer_processor_op_squote(ctx, proc, str, len);

	return;
}

static void pdf_text_filter_op_dquote(fz_context *ctx, pdf_processor *proc,
		float aw, float ac, gchar *str, size_t len) {
	gint Tr = 0;

	pdf_text_filter_processor *p = (pdf_text_filter_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (p->flags & 1 && Tr != 3)
		return;
	if (p->flags & 2 && Tr == 3)
		return;

	p->pdf_buffer_processor_op_dquote(ctx, proc, aw, ac, str, len);

	return;
}

static pdf_processor*
pdf_new_text_filter_processor(fz_context *ctx, fz_buffer **buf, gint flags,
		gchar **errmsg) {
	*buf = NULL;
	gint zero = 0; //wg g_array_append_val
	pdf_text_filter_processor *proc = NULL;
	pdf_output_processor *proc_output = NULL;

	fz_try( ctx )
		*buf = fz_new_buffer(ctx, 1024);
	fz_catch(ctx)
		ERROR_MUPDF_R("fz_new_buffer", NULL)

	fz_try(ctx)
		proc_output = (pdf_output_processor*) pdf_new_buffer_processor(ctx,
				*buf, 0, 0);
	fz_catch(ctx) {
		fz_drop_buffer(ctx, *buf);
		ERROR_MUPDF_R("pdf_new_bufferprocessor", NULL)
	}

	proc = Memento_label(fz_calloc(ctx, 1, sizeof(pdf_text_filter_processor)),
			"pdf_processor");

	//output-processor in super-Struktur kopieren
	proc->super = *proc_output;

	//Hülle kann freigegeben werden
	fz_free(ctx, proc_output);

	proc->arr_Tr = g_array_new( FALSE, FALSE, sizeof(gint));
	g_array_append_val(proc->arr_Tr, zero);

	proc->flags = flags;

	//Funktionen "umleiten"
	proc->pdf_drop_buffer_processor = proc->super.super.drop_processor;
	proc->super.super.drop_processor = pdf_drop_text_filter_processor;

	/* special graphics state */
	proc->pdf_buffer_processor_op_q = proc->super.super.op_q;
	proc->super.super.op_q = pdf_text_filter_op_q;

	proc->pdf_buffer_processor_op_Q = proc->super.super.op_Q;
	proc->super.super.op_Q = pdf_text_filter_op_Q;

	proc->pdf_buffer_processor_op_Tr = proc->super.super.op_Tr;
	proc->super.super.op_Tr = pdf_text_filter_op_Tr;

	proc->pdf_buffer_processor_op_TJ = proc->super.super.op_TJ;
	proc->super.super.op_TJ = pdf_text_filter_op_TJ;
	proc->pdf_buffer_processor_op_Tj = proc->super.super.op_Tj;
	proc->super.super.op_Tj = pdf_text_filter_op_Tj;
	proc->pdf_buffer_processor_op_squote = proc->super.super.op_squote;
	proc->super.super.op_squote = pdf_text_filter_op_squote;
	proc->pdf_buffer_processor_op_dquote = proc->super.super.op_dquote;
	proc->super.super.op_dquote = pdf_text_filter_op_dquote;

	return (pdf_processor*) proc;
}

fz_buffer*
pdf_text_filter_page(fz_context *ctx, pdf_obj *obj, gint flags, gchar **errmsg) {
	pdf_obj *contents = NULL;
	pdf_obj *res = NULL;
	pdf_processor *proc = NULL;
	fz_buffer *buf = NULL;
	pdf_document *doc = NULL;

	fz_try( ctx )
		contents = pdf_dict_get(ctx, obj, PDF_NAME(Contents));
	fz_catch(ctx)
		ERROR_MUPDF_R("pdf_dict_get (Contents)", NULL)
	if (!contents)
		ERROR_S_MESSAGE_VAL("Kein Contents-Dict", NULL)

	fz_try(ctx)
		res = pdf_dict_get_inheritable(ctx, obj, PDF_NAME(Resources));
	fz_catch(ctx)
		ERROR_MUPDF_R("pdf_dict_get_inheritable (Ressources)", NULL)
	if (!res)
		ERROR_S_MESSAGE_VAL("Kein Ressources-Dict", NULL)

	proc = pdf_new_text_filter_processor(ctx, &buf, flags, errmsg);
	if (!proc)
		ERROR_S_VAL(NULL)

	doc = pdf_pin_document(ctx, obj);
	fz_try(ctx)
		pdf_process_contents(ctx, proc, doc, res, contents, NULL, NULL);
	fz_always(ctx) {
		pdf_drop_document(ctx, doc);
		pdf_close_processor(ctx, proc);
		pdf_drop_processor(ctx, proc);
	}
	fz_catch(ctx) {
		fz_drop_buffer(ctx, buf);
		ERROR_S_VAL(NULL)
	}

	return buf;
}

gint pdf_copy_page(fz_context *ctx, pdf_document *doc_src, gint page_from,
		gint page_to, pdf_document *doc_dest, gint page, gchar **errmsg) {
	pdf_graft_map *graft_map = NULL;

	graft_map = pdf_new_graft_map(ctx, doc_dest); //keine exception

	for (gint u = page_from; u <= page_to; u++) {
		fz_try(ctx)
			pdf_graft_mapped_page(ctx, graft_map, page++, doc_src, u);
		fz_catch( ctx )
		{
			pdf_drop_graft_map(ctx, graft_map);
			ERROR_MUPDF( "pdf_graft_mapped_page" )
		}
	}

	pdf_drop_graft_map(ctx, graft_map);

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
		ERROR_Z

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
					ERROR_Z

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
					ERROR_Z
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
		ERROR_Z

	if (!dict)
		return 0; //nicht einmal EmbeddedFiles-Dict gefunden

	rc = pdf_walk_names_dict(ctx, dict, NULL, callback_walk, data, error);
	if (rc)
		ERROR_Z

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
		pdf_dict_put_drop(ctx, filespec, PDF_NAME(F),
					 pdf_new_string(ctx, filename, strlen(filename)));
		pdf_dict_put(ctx, filespec, PDF_NAME(EF), ef);
		pdf_dict_put(ctx, filespec, PDF_NAME(Params), params);

		if (mime_type)
		{
			pdf_dict_put_drop(ctx, filespec, PDF_NAME(Subtype),
						 pdf_new_string(ctx, mime_type, strlen(mime_type)));
		}

		/* ---------- Key für Namen ---------- */
		key = pdf_new_string(ctx, filename, strlen(filename));
	}
	fz_always(ctx) {
		pdf_drop_obj(ctx, file_stream);
		pdf_drop_obj(ctx, params);
		pdf_drop_obj(ctx, ef);
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
		ERROR_Z

	return 0;
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
		proc_text = new_text_analyzer_processor(ctx, proc_run, 3, error);
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

gint pdf_get_sond_font(fz_context* ctx, pdf_document* doc, pdf_obj** font_ref,
		GError** error) {
	gint num_pages = 0;
	gint num = 0;

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

gint pdf_page_has_hidden_text(fz_context* ctx, pdf_page* page,
		gboolean* hidden, GError** error) {
	pdf_processor* proc = NULL;

	proc = new_text_analyzer_processor(ctx, NULL, 3, error);
	if (!proc)
		ERROR_Z

	fz_try(ctx)
		pdf_process_contents(ctx, proc, page->doc, pdf_page_resources(ctx, page),
				pdf_page_contents(ctx, page), NULL, NULL);
	fz_catch(ctx) {
		pdf_close_processor(ctx, proc);
		pdf_drop_processor(ctx, proc);

		ERROR_PDF
	}

	*hidden = ((pdf_text_analyzer_processor*) proc)->has_hidden_text;
	pdf_close_processor(ctx, proc);
	pdf_drop_processor(ctx, proc);

	return 0;
}

