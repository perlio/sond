#include "pdf.h"

#include <glib/gstdio.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "test.h"

#include "../../misc.h"
#include "../../sond_fileparts.h"

#include "../zond_pdf_document.h"
#include "../global_types.h"

/** Nicht thread-safe  **/
gint pdf_document_get_dest(fz_context *ctx, pdf_document *doc, gint page_doc,
		gpointer *ret, gboolean first_occ, gchar **errmsg) {
	pdf_obj *obj_dest_tree = NULL;
	pdf_obj *obj_key = NULL;
	pdf_obj *obj_val = NULL;
	pdf_obj *obj_val_resolved = NULL;
	pdf_obj *obj_page = NULL;
	gint num = 0;
	const gchar *dest_found = NULL;

	fz_try( ctx )
		obj_dest_tree = pdf_load_name_tree(ctx, doc, PDF_NAME(Dests));
	fz_catch(ctx)
		ERROR_MUPDF("pdf_load_name_tree")

	for (gint i = 0; i < pdf_dict_len(ctx, obj_dest_tree); i++) {
		fz_try( ctx )
			obj_key = pdf_dict_get_key(ctx, obj_dest_tree, i);
		fz_catch(ctx) ERROR_MUPDF( "pdf_dict_get_key" )

		fz_try( ctx ) obj_val = pdf_dict_get_val( ctx, obj_dest_tree, i );
		fz_catch( ctx ) ERROR_MUPDF( "pdf_dict_get_val" )

		fz_try( ctx ) obj_val_resolved = pdf_resolve_indirect( ctx, obj_val );
		fz_catch( ctx )
		{
			pdf_drop_obj( ctx, obj_dest_tree );

			ERROR_MUPDF( "pdf_resolve_indirect" )
		}

		//Altmodische PDF verweisen im NameTree auf ein Dict mit dem Schlüssel /D
		if ( pdf_is_array( ctx, obj_val_resolved ) ) obj_page =
				pdf_array_get( ctx, obj_val_resolved, 0 );
		else if ( pdf_is_dict( ctx, obj_val_resolved ) )
		{
			pdf_obj* obj_array = NULL;

			obj_array = pdf_dict_get( ctx, obj_val_resolved, PDF_NAME(D) );
			obj_page = pdf_array_get( ctx, obj_array, 0 );
		}
		else //Name-Tree-Val ist weder array noch dict - es widerspricht der obersten Direktive
		{
			pdf_drop_obj( ctx, obj_dest_tree );
			ERROR_S_MESSAGE( "NamedTree für NamedDests irregulär" )
		}

		fz_try( ctx ) num = pdf_lookup_page_number( ctx, doc, obj_page );
		fz_catch( ctx )
		{
			pdf_drop_obj( ctx, obj_dest_tree );

			ERROR_MUPDF( "pdf_lookup_page_number" )
		}

		if ( num == page_doc )
		{
			dest_found = pdf_to_name( ctx, obj_key );
			if ( g_str_has_prefix( dest_found, "ZND-" ) )
			{
				if ( g_uuid_string_is_valid( dest_found + 4 ) )
				{
					if ( first_occ )
					{
						*ret = (gpointer) g_strdup( dest_found );
						break;
					}
					else g_ptr_array_add( (GPtrArray*) *ret, (gpointer) g_strdup( dest_found ) );
				}
			}
		}
	}

	pdf_drop_obj(ctx, obj_dest_tree);

	return 0;
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

fz_buffer* pdf_doc_to_buf(fz_context* ctx, pdf_document* doc, GError** error) {
	fz_output* out = NULL;
	fz_buffer* buf = NULL;
	pdf_write_options in_opts =
//#ifdef __WIN32
			{ 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, ~0, "", "", 0, 0, 0, 0, 0 };
//#elif defined(__linux__)
 //           { 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, ~0, "", "" };
//#endif // __win32
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

gint pdf_save(fz_context *ctx, pdf_document *pdf_doc,
		SondFilePartPDF* sfp_pdf, GError **error) {
	gint rc = 0;
	fz_buffer* buf = NULL;

	buf = pdf_doc_to_buf(ctx, pdf_doc, error);
	if (!buf)
		ERROR_Z

	rc = sond_file_part_replace(SOND_FILE_PART(sfp_pdf), ctx, buf, error);
	fz_drop_buffer(ctx, buf);
	if (rc)
		ERROR_Z

	return 0;
}

gint pdf_clean(fz_context *ctx, SondFilePartPDF* sfp_pdf, GError **error) {
	pdf_document *doc = NULL;
	gint rc = 0;
	gint *pages = NULL;
	gint count = 0;
	gint ret = 0;
	gchar* path_tmp = NULL;

	//prüfen, ob in Viewer geöffnet
	if (zond_pdf_document_is_open(sfp_pdf)) {
		if (error)
			*error = g_error_new(ZOND_ERROR, 0, "Datei '%s' ist geöffnet",
					sond_file_part_get_filepart(SOND_FILE_PART(sfp_pdf)));

		return -1;
	}

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, FALSE, TRUE, TRUE, error);
	if (!doc) {
		if (g_error_matches(*error, g_quark_from_static_string("sond"), 1)) { //auth failed
			g_clear_error(error);

			return 1; //nächstes Dokument
		}
		else
			ERROR_Z
	}

//	path_tmp = g_strdup(fz_stream_filename(ctx, doc->file));
	count = pdf_count_pages(ctx, doc);
	pages = g_malloc(sizeof(gint) * count);
	for (gint i = 0; i < count; i++)
		pages[i] = i;

	fz_try( ctx )
#ifdef __WIN32__
		pdf_rearrange_pages(ctx, doc, count, pages, PDF_CLEAN_STRUCTURE_KEEP);
#elif defined(__linux__)
		pdf_rearrange_pages(ctx, doc, count, pages);
#endif // __win32__
	fz_always(ctx)
		g_free(pages);
	fz_catch(ctx) {
		gint ret = 0;

		pdf_drop_document(ctx, doc);
		g_object_unref(sfp_pdf);
		ret = g_remove(path_tmp);
		if (ret)
			g_warning("%s\nArbeitskopie '%s' konnte nicht gelöscht werden\n"
					"%s", __func__, path_tmp, strerror(errno));
		if (error)
			*error = g_error_new( ZOND_ERROR, 0, "%s\npdf_rearrange_pages\n%s",
					__func__, fz_caught_message(ctx));

		g_free(path_tmp);

		return -1;
	}

	rc = pdf_save(ctx, doc, sfp_pdf, error);
	pdf_drop_document(ctx, doc);
	g_object_unref(sfp_pdf);
	ret = g_remove(path_tmp);
	if (ret)
		g_warning("%s\nArbeitskopie '%s' konnte nicht gelöscht werden\n"
				"%s", __func__, path_tmp, strerror(errno));
	g_free(path_tmp);
	if (rc)
		ERROR_Z

	return 0;
}

gchar*
pdf_get_string_from_line(fz_context *ctx, fz_stext_line *line, gchar **errmsg) {
	gchar *line_string = NULL;
	fz_buffer *buf = NULL;

	fz_try( ctx )
		buf = fz_new_buffer(ctx, 128);
	fz_catch(ctx)
		ERROR_MUPDF_R("fz_new_buffer", NULL)

	//string aus Zeile bilden
	for (fz_stext_char *stext_char = line->first_char; stext_char; stext_char =
			stext_char->next) {
		fz_try( ctx )
			fz_append_rune(ctx, buf, stext_char->c);
		fz_catch(ctx)
			ERROR_MUPDF_R( "fz_append_rune", NULL )
	}

	line_string = g_strdup(fz_string_from_buffer(ctx, buf));

	fz_drop_buffer(ctx, buf);

	return line_string;
}

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

	if (p->flags & 1 && Tr != 3)
		return;
	if (p->flags & 2 && Tr == 3)
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

pdf_processor*
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

gint pdf_annot_delete(fz_context* ctx, pdf_annot* annot, GError** error) {
	fz_try(ctx)
		pdf_delete_annot(ctx, pdf_annot_page(ctx, annot), annot);
	fz_catch(ctx) {
		if (error) *error = g_error_new( g_quark_from_static_string("mupdf"),
				fz_caught(ctx), "%s\n%s", __func__, fz_caught_message(ctx));

		return -1;
	}

	return 0;
}

static fz_rect pdf_annot_rotate_rect(gint rotate, fz_rect rect) {
	if (rotate == 90) {
		rect.x0 -= 20;
		rect.x1 -= 20;
	} else if (rotate == 180) {
		rect.x0 -= 20;
		rect.x1 -= 20;
		rect.y0 -= 20;
		rect.y1 -= 20;
	} else if (rotate == 270) {
		rect.y0 -= 20;
		rect.y1 -= 20;
	}

	return rect;
}

gint pdf_annot_change(fz_context* ctx, pdf_annot* pdf_annot, gint rotate,
		Annot annot, GError** error) {
	if (annot.type == PDF_ANNOT_HIGHLIGHT || annot.type == PDF_ANNOT_UNDERLINE) {
		for (gint i = 0; i < annot.annot_text_markup.arr_quads->len; i++) {
			fz_try( ctx )
				pdf_add_annot_quad_point(ctx, pdf_annot,
						g_array_index(annot.annot_text_markup.arr_quads, fz_quad, i));
			fz_catch (ctx) {
				if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
						"%s\n%s", __func__, fz_caught_message(ctx));

				return -1;
			}
		}
	} else if (annot.type == PDF_ANNOT_TEXT) {
		fz_try(ctx) {
			if (annot.annot_text.content)
				pdf_set_annot_contents(ctx, pdf_annot, annot.annot_text.content);
			pdf_set_annot_rect(ctx, pdf_annot, pdf_annot_rotate_rect(rotate, annot.annot_text.rect));
		}
		fz_catch(ctx) {
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
					"%s\n%s", __func__, fz_caught_message(ctx));

			return -1;
		}
	}

	pdf_update_annot(ctx, pdf_annot);

	return 0;
}

pdf_annot* pdf_annot_create(fz_context* ctx, pdf_page* pdf_page, gint rotate,
		Annot annot, GError** error) {
	pdf_annot* pdf_annot = NULL;
	gint rc = 0;

	fz_try(ctx) pdf_annot = pdf_create_annot(ctx, pdf_page, annot.type);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
				"%s\n%s", __func__, fz_caught_message(ctx));

		return NULL;
	}
	pdf_drop_annot(ctx, pdf_annot); //geht schon jetzt; page behält ref!

	if (annot.type == PDF_ANNOT_UNDERLINE) {
		const gfloat color[3] = { 0.1, .85, 0 };
		fz_try(ctx) pdf_set_annot_color(ctx, pdf_annot, 3, color);
		fz_catch( ctx )
		{
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
					"%s\n%s", __func__, fz_caught_message(ctx));

			return NULL;
		}
	} else if (annot.type == PDF_ANNOT_TEXT) {
		fz_try(ctx) pdf_set_annot_icon_name( ctx, pdf_annot, "Comment" );
		fz_catch( ctx )
		{
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
					"%s\n%s", __func__, fz_caught_message(ctx));

			return NULL;
		}
	}

	rc = pdf_annot_change(ctx, pdf_annot, rotate, annot, error);
	if (rc) ERROR_Z_VAL(NULL)

	return pdf_annot;
}

gboolean pdf_annot_get_annot(fz_context *ctx, pdf_annot *pdf_annot, Annot* annot, GError **error) {

	assert(annot != NULL);
	assert(pdf_annot != NULL);
	assert(ctx != NULL);

	fz_try(ctx) annot->type = pdf_annot_type(ctx, pdf_annot);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
				fz_caught(ctx), "%s\n%s", __func__,
				fz_caught_message(ctx));

		return FALSE;
	}

	//Text-Markup-annots
	if (annot->type == PDF_ANNOT_HIGHLIGHT
			|| annot->type == PDF_ANNOT_UNDERLINE
			|| annot->type == PDF_ANNOT_STRIKE_OUT
			|| annot->type == PDF_ANNOT_SQUIGGLY) {
		gint n_quad = 0;

		fz_try(ctx) n_quad = pdf_annot_quad_point_count(ctx, pdf_annot);
		fz_catch(ctx) {
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
						fz_caught(ctx), "%s\n%s", __func__,
						fz_caught_message(ctx));

			return FALSE;
		}

		annot->annot_text_markup.arr_quads =
				g_array_new(FALSE, FALSE, sizeof( fz_quad ));

		for ( gint i = 0; i < n_quad; i++ )
		{
			fz_quad quad = pdf_annot_quad_point(ctx, pdf_annot, i);
			g_array_append_val(annot->annot_text_markup.arr_quads, quad);
		}

	}
	else if (annot->type == PDF_ANNOT_TEXT)
	{
		annot->annot_text.rect = pdf_bound_annot(ctx, pdf_annot);
		annot->annot_text.open = pdf_annot_is_open(ctx, pdf_annot);
		annot->annot_text.content = g_strdup(pdf_annot_contents(ctx, pdf_annot));
	}

	return TRUE;
}

pdf_annot* pdf_annot_lookup_obj(fz_context *ctx, pdf_page* pdf_page, pdf_obj *obj) {
	pdf_annot *pdf_annot = NULL;

	pdf_annot = pdf_first_annot(ctx, pdf_page); //kein Fehler

	while (pdf_annot) {
		if (pdf_annot_obj(ctx, pdf_annot) == obj) break;

		pdf_annot = pdf_next_annot(ctx, pdf_annot);
	}

	return pdf_annot;
}

gint pdf_page_rotate(fz_context *ctx, pdf_obj *page_obj, gint rotate,
		GError** error) {
	pdf_obj *rotate_obj = NULL;

	fz_try(ctx)
		rotate_obj = pdf_dict_get_inheritable(ctx, page_obj, PDF_NAME(Rotate));
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
				fz_caught(ctx), "%s\n%s", __func__, fz_caught_message(ctx));

		return -1;
	}

	if (!rotate_obj) {
		rotate_obj = pdf_new_int(ctx, (int64_t) rotate);
		fz_try(ctx)
			pdf_dict_put_drop(ctx, page_obj, PDF_NAME(Rotate), rotate_obj);
		fz_catch(ctx) {
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));
			pdf_drop_obj(ctx, rotate_obj);

			return -1;
		}
	} else
		pdf_set_int(ctx, rotate_obj, (int64_t) rotate);

	return 0;
}

gint pdf_get_names_tree_dict(fz_context* ctx, pdf_document* doc,
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
	{
		if (error)
			*error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));

		return -1;
	}

	if (dict) *dict = dict_res;

	return 0;
}

gint
pdf_walk_names_dict(fz_context* ctx, pdf_obj *node, pdf_cycle_list *cycle_up,
		gint (*callback_walk) (fz_context*, pdf_obj*, pdf_obj*,
				gpointer, GError**), gpointer data, GError** error)
{
	pdf_cycle_list cycle;
	pdf_obj *kids = NULL;
	pdf_obj *names = NULL;
	gboolean is_cycle = FALSE;

	fz_try(ctx)
	{
		kids = pdf_dict_get(ctx, node, PDF_NAME(Kids));
		names = pdf_dict_get(ctx, node, PDF_NAME(Names));
		is_cycle = pdf_cycle(ctx, &cycle, cycle_up, node);
	}
	fz_catch(ctx)
	{
		if (error)
			*error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));

		return -1;
	}

	if (kids && !is_cycle)
	{
		int len = 0;

		fz_try(ctx)
			pdf_array_len(ctx, kids);
		fz_catch(ctx)
		{
			if (error)
				*error = g_error_new(g_quark_from_static_string("mupdf"),
						fz_caught(ctx), "%s\n%s", __func__,
						fz_caught_message(ctx));

			return -1;
		}

		for (gint i = 0; i < len; i++) {
			gint rc = 0;

			rc = pdf_walk_names_dict(ctx, pdf_array_get(ctx, kids, i),
					&cycle, callback_walk, data, error);
			if (rc == -1)
				ERROR_Z
			else if (rc == 1)
				return 1;
		}
	}

	if (names)
	{
		int len = 0;

		fz_try(ctx)
			len = pdf_array_len(ctx, names);
		fz_catch(ctx)
		{
			if (error)
				*error = g_error_new(g_quark_from_static_string("mupdf"),
						fz_caught(ctx), "%s\n%s", __func__,
						fz_caught_message(ctx));

			return -1;
		}

		for (gint i = 0; i + 1 < len; i++)
		{
			gint rc = 0;
			pdf_obj* key = NULL;
			pdf_obj* val = NULL;

			fz_try(ctx) {
				key = pdf_array_get(ctx, names, i);
				val = pdf_array_get(ctx, names, i + 1);
			}
			fz_catch(ctx) {
				if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
						fz_caught(ctx), "%s\n%s", __func__,
						fz_caught_message(ctx));

				return -1;
			}


			rc = callback_walk(ctx, key, val, data, error);
			if (rc == -1)
				ERROR_Z
			else if (rc == 1)
				return 1;
		}
	}

	return 0;
}
/*
// Vergleicht zwei PDF-Strings
int compare_pdf_strings(fz_context *ctx, pdf_obj *a, const char *b)
{
    const char *astr = pdf_to_str_buf(ctx, a);
    return strcmp(astr, b);
}

// Fügt (key, value) sortiert in ein /Names-Array ein
void insert_sorted_into_names_array(fz_context *ctx, pdf_document *doc, pdf_obj *array, const char *key, pdf_obj *value)
{
    int n = pdf_array_len(ctx, array);
    pdf_obj *new_key = pdf_new_string(ctx, doc, key, strlen(key));

    // Suche die richtige Stelle zum Einfügen
    for (int i = 0; i < n; i += 2) {
        pdf_obj *entry_key = pdf_array_get(ctx, array, i);
        if (compare_pdf_strings(ctx, entry_key, key) > 0) {
            pdf_array_insert(ctx, array, i, value);
            pdf_array_insert(ctx, array, i, new_key);
            return;
        }
    }

    // Wenn wir hier sind: hinten anhängen
    pdf_array_push(ctx, array, new_key);
    pdf_array_push(ctx, array, value);
}

// Aktualisiert /Limits eines Leaf-Nodes nach Hinzufügen eines Keys
static void update_limits(fz_context *ctx, pdf_document *doc, pdf_obj *leaf, const char *key)
{
    pdf_obj *limits = pdf_dict_gets(ctx, leaf, "Limits");

    if (!limits || !pdf_is_array(ctx, limits) || pdf_array_len(ctx, limits) != 2) {
        limits = pdf_new_array(ctx, doc, 2);
        pdf_array_push(ctx, limits, pdf_new_string(ctx, doc, key, strlen(key)));
        pdf_array_push(ctx, limits, pdf_new_string(ctx, doc, key, strlen(key)));
        pdf_dict_puts(ctx, leaf, "Limits", limits);
        return;
    }

    pdf_obj *min = pdf_array_get(ctx, limits, 0);
    pdf_obj *max = pdf_array_get(ctx, limits, 1);

    if (compare_pdf_strings(ctx, min, key) > 0)
        pdf_array_put(ctx, limits, 0, pdf_new_string(ctx, doc, key, strlen(key)));

    if (compare_pdf_strings(ctx, max, key) < 0)
        pdf_array_put(ctx, limits, 1, pdf_new_string(ctx, doc, key, strlen(key)));
}

// Fügt in einen bestehenden Leaf-Node ein
void insert_into_leaf_node(fz_context *ctx, pdf_document *doc, pdf_obj *leaf, const char *key, pdf_obj *value)
{
    pdf_obj *names = pdf_dict_gets(ctx, leaf, "Names");
    if (!names || !pdf_is_array(ctx, names)) {
        names = pdf_new_array(ctx, doc, 2);
        pdf_dict_puts(ctx, leaf, "Names", names);
    }

    insert_sorted_into_names_array(ctx, doc, names, key, value);
    update_limits(ctx, doc, leaf, key);
}

// Erstellt neuen Leaf-Node mit diesem Key/Value
static pdf_obj* create_leaf_node(fz_context *ctx, pdf_document *doc, const char *key, pdf_obj *value)
{
    pdf_obj *leaf = pdf_new_dict(ctx, doc, 2);
    pdf_obj *names = pdf_new_array(ctx, doc, 2);
    pdf_array_push(ctx, names, pdf_new_string(ctx, doc, key, strlen(key)));
    pdf_array_push(ctx, names, value);
    pdf_dict_puts(ctx, leaf, "Names", names);

    pdf_obj *limits = pdf_new_array(ctx, doc, 2);
    pdf_array_push(ctx, limits, pdf_new_string(ctx, doc, key, strlen(key)));
    pdf_array_push(ctx, limits, pdf_new_string(ctx, doc, key, strlen(key)));
    pdf_dict_puts(ctx, leaf, "Limits", limits);

    return leaf;
}

// Hauptfunktion: trägt (key, value) korrekt in den Nametree ein
gint pdf_insert_into_nametree(fz_context *ctx, pdf_document *doc,
		pdf_obj *nametree, const char *key, pdf_obj *value, GError **error) {
    pdf_obj *names = NULL;

    fz_try(ctx)
    	names = pdf_dict_gets(ctx, nametree, "Names");
    fz_catch(ctx)
    	ERROR_PDF

    if (names) {
    	gboolean is_array = FALSE;

    	fz_try(ctx)
    		is_array = pdf_is_array(ctx, names);
    	fz_catch(ctx)
    		ERROR_PDF

    	if (is_array) {
    		gint rc = 0;

    		rc = insert_sorted_into_names_array(ctx, doc, names, key, value, error);
    		if (rc)
    			ERROR_Z

			rc = update_limits(ctx, doc, nametree, key, error);
    		if (rc)
				ERROR_Z

			return 0;
    	}
    	else {
    		if (error) *error_ = g_error_new(g_quark_from_static_string("sond"),
					0, "%s\ntree malformed", __func__);

    		return -1;
    	}
    }

    // Kein /Names → vielleicht /Kids
    pdf_obj *kids = pdf_dict_gets(ctx, nametree, "Kids");
    if (!kids || !pdf_is_array(ctx, kids)) {
        // Kein /Kids → neuen Leaf-Node direkt einfügen
        pdf_obj *leaf = create_leaf_node(ctx, doc, key, value);
        kids = pdf_new_array(ctx, doc, 1);
        pdf_array_push(ctx, kids, leaf);
        pdf_dict_puts(ctx, nametree, "Kids", kids);
        return;
    }

    // Suche passenden Kid
    int n = pdf_array_len(ctx, kids);
    for (int i = 0; i < n; ++i) {
        pdf_obj *kid = pdf_array_get(ctx, kids, i);
        pdf_obj *limits = pdf_dict_gets(ctx, kid, "Limits");

        if (!limits || !pdf_is_array(ctx, limits) || pdf_array_len(ctx, limits) != 2) {
            insert_into_leaf_node(ctx, doc, kid, key, value);
            return;
        }

        const char *min = pdf_to_str_buf(ctx, pdf_array_get(ctx, limits, 0));
        const char *max = pdf_to_str_buf(ctx, pdf_array_get(ctx, limits, 1));

        if (strcmp(key, min) >= 0 && strcmp(key, max) <= 0) {
            insert_into_leaf_node(ctx, doc, kid, key, value);
            return;
        }
    }

    // Kein passender Kid → neuen Leaf anlegen
    pdf_obj *new_leaf = create_leaf_node(ctx, doc, key, value);
    pdf_array_push(ctx, kids, new_leaf);
}
*/
