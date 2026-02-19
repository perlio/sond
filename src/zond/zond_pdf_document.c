/*
 zond (zond_pdf_document.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2021  pelo america

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

#include "zond_pdf_document.h"

#include <glib/gstdio.h>
#include <glib-object.h>

#include "../misc.h"
#include "../sond_fileparts.h"
#include "../sond_log_and_error.h"
#include "../sond_pdf_helper.h"
#include "../sond_file_helper.h"

#include "99conv/general.h"


typedef struct {
	GMutex mutex_doc;
	fz_context *ctx;
	pdf_document *doc;
	SondFilePartPDF* sfp_pdf;
	gboolean read_only;
	gchar *working_copy;
	GPtrArray *pages; //array von PdfDocumentPage*
	GPtrArray* arr_zpdf_parts; //array von zpdf-parts
	GArray *arr_journal;
	gint ocr_num;
} ZondPdfDocumentPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondPdfDocument, zond_pdf_document, G_TYPE_OBJECT)

gint pdf_document_page_annot_get_index(PdfDocumentPageAnnot* pdpa) {
	GPtrArray* arr_annots = NULL;
	guint index = 0;

	arr_annots = pdpa->pdf_document_page->arr_annots;

	g_ptr_array_find(arr_annots, pdpa, &index);

	return (gint) index;
}

pdf_annot* pdf_document_page_annot_get_pdf_annot(PdfDocumentPageAnnot* pdpa) {
	pdf_annot* pdf_annot = NULL;
	fz_context* ctx = NULL;
	gint index = 0;

	index = pdf_document_page_annot_get_index(pdpa);

	ctx = zond_pdf_document_get_ctx(pdpa->pdf_document_page->document);

	pdf_annot = pdf_first_annot(ctx, pdpa->pdf_document_page->page);

	for (gint i = 0; i < index; i++)
		if (pdf_annot)
			pdf_annot = pdf_next_annot(ctx, pdf_annot);

	return pdf_annot;
}

pdf_obj* pdf_document_page_get_page_obj(PdfDocumentPage* pdf_document_page, GError** error) {
	pdf_obj* obj = NULL;

	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			pdf_document_page->document);

	if (pdf_document_page->page)
		return pdf_document_page->page->obj;

	fz_try(priv->ctx)
		obj = pdf_lookup_page_obj(priv->ctx, priv->doc, pdf_document_page->page_akt);
	fz_catch(priv->ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
				fz_caught(priv->ctx), "%s\n%s", __func__, fz_caught_message(priv->ctx));

		return NULL;
	}

	return obj;
}

static void zond_pdf_document_close_context(fz_context *ctx) {
	GMutex *mutex = (GMutex*) ctx->locks.user;

	fz_drop_context(ctx);

	for (gint i = 0; i < FZ_LOCK_MAX; i++)
		g_mutex_clear(&mutex[i]);

	g_free(mutex);

	return;
}

static void zond_pdf_document_finalize(GObject *self) {
	gchar *path = NULL;

	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			ZOND_PDF_DOCUMENT(self));

	g_array_unref(priv->arr_journal);

	g_ptr_array_unref(priv->pages);
	g_ptr_array_unref(priv->arr_zpdf_parts);
	g_object_unref(priv->sfp_pdf);

	if (priv->doc) {
		path = g_strdup(fz_stream_filename(priv->ctx, priv->doc->file));
		pdf_drop_document(priv->ctx, priv->doc);

		if (!priv->read_only) {
			GError* error_rem = NULL;

			if (!sond_remove(path, &error_rem)) {
				LOG_WARN("Arbeitskopie %s konnte nicht gelöscht werden: %s",
						path, error_rem->message);
				g_error_free(error_rem);
			}
		}

		g_free(path);
	}

	zond_pdf_document_close_context(priv->ctx); //drop_context reicht nicht aus!

	g_mutex_clear(&priv->mutex_doc);

	ZondPdfDocumentClass *klass = ZOND_PDF_DOCUMENT_GET_CLASS(self);

	g_ptr_array_remove_fast(klass->arr_pdf_documents, self);

	G_OBJECT_CLASS(zond_pdf_document_parent_class)->finalize(self);

	return;
}

void zond_pdf_document_page_free(PdfDocumentPage *pdf_document_page) {
	if (!pdf_document_page)
		return;

	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			pdf_document_page->document);

	pdf_drop_page(priv->ctx, pdf_document_page->page); //greift gar nicht auf doc zu
	fz_drop_stext_page(priv->ctx, pdf_document_page->stext_page);
	fz_drop_display_list(priv->ctx, pdf_document_page->display_list);
	if (pdf_document_page->arr_annots)
		g_ptr_array_unref(pdf_document_page->arr_annots);

	g_free(pdf_document_page);

	return;
}

Annot annot_deep_copy(Annot annot) {
	Annot copy = { 0 };

	copy = annot;

	if (annot.type == PDF_ANNOT_HIGHLIGHT
			|| annot.type == PDF_ANNOT_UNDERLINE
			|| annot.type == PDF_ANNOT_STRIKE_OUT
			|| annot.type == PDF_ANNOT_SQUIGGLY)
		copy.annot_text_markup.arr_quads = g_array_ref(annot.annot_text_markup.arr_quads);
	else if (annot.type == PDF_ANNOT_TEXT)
		copy.annot_text.content = g_strdup(annot.annot_text.content);

	return copy;
}

void annot_free(Annot* annot) {
	if (!annot)
		return;

	if (annot->type == PDF_ANNOT_HIGHLIGHT
			|| annot->type == PDF_ANNOT_UNDERLINE
			|| annot->type == PDF_ANNOT_STRIKE_OUT
			|| annot->type == PDF_ANNOT_SQUIGGLY)
		g_array_unref(annot->annot_text_markup.arr_quads);
	else if (annot->type == PDF_ANNOT_TEXT)
		g_free(annot->annot_text.content);

	return;
}

static void pdf_document_page_annot_free(gpointer data) {
	PdfDocumentPageAnnot *pdf_document_page_annot = (PdfDocumentPageAnnot*) data;

	annot_free(&(pdf_document_page_annot->annot));

	g_free(pdf_document_page_annot);

	return;
}

static gboolean get_annot(fz_context* ctx, pdf_annot* pdf_annot,
		Annot* annot, GError** error) {
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

static gint zond_pdf_document_page_annot_load(PdfDocumentPage *pdf_document_page,
		pdf_annot *pdf_annot, GError **error) {
	PdfDocumentPageAnnot *pdf_document_page_annot = NULL;
	gboolean ret = FALSE;

	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			pdf_document_page->document);

	pdf_document_page_annot = g_malloc0(sizeof(PdfDocumentPageAnnot));

	pdf_document_page_annot->pdf_document_page = pdf_document_page;

	ret = get_annot(priv->ctx,
			pdf_annot, &pdf_document_page_annot->annot, error);
	if (!ret) {
		g_free(pdf_document_page_annot);
		ERROR_Z
	}

	g_ptr_array_add(pdf_document_page->arr_annots, pdf_document_page_annot);

	return 0;
}

gint zond_pdf_document_page_load_annots(PdfDocumentPage *pdf_document_page, GError** error) {
	pdf_annot *annot = NULL;

	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			pdf_document_page->document);

	annot = pdf_first_annot(priv->ctx, pdf_document_page->page);

	if (!annot)
		return 0;

	do{
		gint rc = 0;

		rc = zond_pdf_document_page_annot_load(pdf_document_page, annot, error);
		if (rc) ERROR_Z
	} while ((annot = pdf_next_annot(priv->ctx, annot)));

	return 0;
}

gint zond_pdf_document_load_page(PdfDocumentPage *pdf_document_page,
		fz_context* ctx, gchar **errmsg) {
	GError *error = NULL;
	gint rc = 0;

	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			pdf_document_page->document);

	if (pdf_document_page->page)
		pdf_drop_page(ctx, pdf_document_page->page);

	fz_try(ctx)
		pdf_document_page->page = pdf_load_page(ctx, priv->doc,
				pdf_document_page->page_akt);
	fz_catch(ctx) {
		if (errmsg) *errmsg = g_strdup_printf("%s\n%s", __func__, fz_caught_message(ctx));

		return -1;
	}

	pdf_document_page->arr_annots = g_ptr_array_new_with_free_func(
			pdf_document_page_annot_free);

	rc = zond_pdf_document_page_load_annots(pdf_document_page, &error);
	if (rc) {
		if (errmsg) *errmsg = g_strdup_printf("%s\n%s", __func__, error->message);
		g_error_free(error);

		return -1;
	}

	return 0;
}

static gint zond_pdf_document_init_page(ZondPdfDocument *self,
		PdfDocumentPage *pdf_document_page, gint index, GError **error) {
	fz_context *ctx = NULL;
	pdf_obj *rotate_obj = NULL;
	fz_rect mediabox = { 0 };
	fz_matrix page_ctm = { 0 };

	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			ZOND_PDF_DOCUMENT(self));

	pdf_document_page->document = self; //keine ref!
	pdf_document_page->page_akt = index;

	ctx = priv->ctx;

	fz_try(ctx) {
		pdf_obj* obj = NULL;

		zond_pdf_document_mutex_lock(self);
		obj = pdf_lookup_page_obj(ctx, priv->doc, index);

		pdf_page_obj_transform(ctx, obj, &mediabox,
				&page_ctm);
		pdf_document_page->rect = fz_transform_rect(mediabox, page_ctm);

		rotate_obj = pdf_dict_get(ctx, obj, PDF_NAME(Rotate));
		if (rotate_obj)
			pdf_document_page->rotate = pdf_to_int(ctx, rotate_obj);
		//else: 0
	}
	fz_always(ctx)
		zond_pdf_document_mutex_unlock(self);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
				fz_caught(ctx), "%s\n%s", __func__, fz_caught_message(ctx));

		return -1;
	}

	return 0;
}

static gint zond_pdf_document_init_pages(ZondPdfDocument *self, gint von,
		gint bis, GError **error) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	if (bis == -1)
		bis = priv->pages->len - 1;

	if (von < 0 || von > bis || bis > priv->pages->len - 1) {
		if (error)
			*error = g_error_new(ZOND_ERROR, 0,
					"%s\nSeitengrenzen nicht eingehalten", __func__);
		return -1;
	}

	for (gint i = von; i <= bis; i++) {
		gint rc = 0;
		PdfDocumentPage *pdf_document_page = NULL;

		//wenn schon initialisiert -> weiter
		if (g_ptr_array_index(priv->pages, i))
			continue;

		pdf_document_page = g_malloc0(sizeof(PdfDocumentPage));
		((priv->pages)->pdata)[i] = pdf_document_page;

		rc = zond_pdf_document_init_page(self, pdf_document_page, i, error);
		if (rc) {
			g_free(pdf_document_page);
			((priv->pages)->pdata)[i] = NULL;
			ERROR_Z
		}
	}

	return 0;
}

static void zond_pdf_document_class_init(ZondPdfDocumentClass *klass) {
	klass->arr_pdf_documents = g_ptr_array_new();

	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = zond_pdf_document_finalize;

	return;
}

static void mupdf_unlock(void *user, gint lock) {
	GMutex *mutex = (GMutex*) user;

	g_mutex_unlock(&(mutex[lock]));

	return;
}

static void mupdf_lock(void *user, gint lock) {
	GMutex *mutex = (GMutex*) user;

	g_mutex_lock(&(mutex[lock]));

	return;
}

static void*
mupdf_malloc(void *user, size_t size) {
	return g_malloc(size);
}

static void*
mupdf_realloc(void *user, void *old, size_t size) {
	return g_realloc(old, size);
}

static void mupdf_free(void *user, void *ptr) {
	g_free(ptr);

	return;
}

/** Wenn NULL, dann Fehler und *errmsg gesetzt **/
static fz_context*
zond_pdf_document_init_context(void) {
	GMutex *mutex = NULL;
	fz_context *ctx = NULL;
	fz_locks_context locks_context = { 0, };
	fz_alloc_context alloc_context = { 0, };

	//mutex für document
	mutex = g_malloc0(sizeof(GMutex) * FZ_LOCK_MAX);
	for (gint i = 0; i < FZ_LOCK_MAX; i++)
		g_mutex_init(&(mutex[i]));

	locks_context.user = mutex;
	locks_context.lock = mupdf_lock;
	locks_context.unlock = mupdf_unlock;

	alloc_context.user = NULL;
	alloc_context.malloc = mupdf_malloc;
	alloc_context.realloc = mupdf_realloc;
	alloc_context.free = mupdf_free;

	/* Create a context to hold the exception stack and various caches. */
	ctx = fz_new_context(&alloc_context, &locks_context, FZ_STORE_UNLIMITED);
	if (!ctx) {
		for (gint i = 0; i < FZ_LOCK_MAX; i++)
			g_mutex_clear(&mutex[i]);
		g_free(mutex);
	}

	return ctx;
}

void zond_pdf_document_free_journal_entry(JournalEntry *entry) {
	if (entry->type >= JOURNAL_TYPE_ANNOT_CREATED &&
			entry->type <= JOURNAL_TYPE_ANNOT_CHANGED) {
		annot_free(&(entry->annot_changed.annot_before));
		annot_free(&(entry->annot_changed.annot_after));
	} else if (entry->type == JOURNAL_TYPE_OCR) {
		ZondPdfDocumentPrivate *priv =
				zond_pdf_document_get_instance_private(entry->pdf_document_page->document);

		fz_drop_buffer(priv->ctx, entry->ocr.buf_old);
		fz_drop_buffer(priv->ctx, entry->ocr.buf_new);
	}
	else if (entry->type == JOURNAL_TYPE_PAGES_INSERTED)
		zpdfd_part_drop(entry->pages_inserted.zpdfd_part);

	return;
}

static void zond_pdf_document_init(ZondPdfDocument *self) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	g_mutex_init(&priv->mutex_doc);

	priv->pages = g_ptr_array_new_with_free_func(
			(GDestroyNotify) zond_pdf_document_page_free);
	priv->arr_zpdf_parts = g_ptr_array_new();
	priv->arr_journal = g_array_new( FALSE, FALSE, sizeof(JournalEntry));
	g_array_set_clear_func(priv->arr_journal,
			(GDestroyNotify) zond_pdf_document_free_journal_entry);

	return;
}

ZondPdfDocument* zond_pdf_document_is_open(SondFilePartPDF* sfp_pdf) {
	ZondPdfDocumentClass *klass = g_type_class_peek(
			zond_pdf_document_get_type());

	if (!klass)
		return NULL;

	for (gint i = 0; i < klass->arr_pdf_documents->len; i++) {
		ZondPdfDocumentPrivate *priv = NULL;
		ZondPdfDocument *zond_pdf_document = NULL;

		zond_pdf_document = g_ptr_array_index(klass->arr_pdf_documents, i);
		priv = zond_pdf_document_get_instance_private(zond_pdf_document);

		if (priv->sfp_pdf == sfp_pdf)
			return zond_pdf_document;
	}

	return NULL;
}

ZondPdfDocument*
zond_pdf_document_open(SondFilePartPDF* sfp_pdf, gint von, gint bis,
		GError **error) {
	gint rc = 0;
	ZondPdfDocument *zond_pdf_document = NULL;
	ZondPdfDocumentPrivate *priv = NULL;
	ZondPdfDocumentClass *klass = NULL;
	gint number_of_pages = 0;
	gchar* filename = NULL;

	zond_pdf_document = zond_pdf_document_is_open(sfp_pdf);
	if (zond_pdf_document) {
		gint rc = 0;

		rc = zond_pdf_document_init_pages(zond_pdf_document, von, bis, error);
		if (rc)
			ERROR_Z_VAL(NULL)

		return g_object_ref(zond_pdf_document);
	}

	zond_pdf_document = g_object_new(ZOND_TYPE_PDF_DOCUMENT, NULL);
	priv = zond_pdf_document_get_instance_private(zond_pdf_document);

	priv->sfp_pdf = g_object_ref(sfp_pdf);

	priv->ctx = zond_pdf_document_init_context();
	if (!priv->ctx) {
		if (error)
			*error = g_error_new(ZOND_ERROR, 0,
					"%s\nfz_context konnte nicht erzeugt werden", __func__);
		g_object_unref(zond_pdf_document);

		return NULL;
	}

	filename = sond_file_part_write_to_tmp_file(priv->ctx,
			SOND_FILE_PART(sfp_pdf), error);
	if (!filename) {
		g_object_unref(zond_pdf_document);

		ERROR_Z_VAL(NULL)
	}

	fz_try(priv->ctx)
		priv->doc = pdf_open_document(priv->ctx, filename);
	fz_catch(priv->ctx) {
		GError* error_rem = NULL;

		if (!sond_remove(filename, &error_rem)) {
			LOG_WARN("Datei '%s' konnte nicht gelöscht werden: %s",
					filename, error_rem->message);
			g_error_free(error_rem);
		}
		g_free(filename);
		g_object_unref(zond_pdf_document);

		ERROR_Z_VAL(NULL)
	}

	number_of_pages = pdf_count_pages(priv->ctx, priv->doc);
	if (number_of_pages == 0) {
		if (error)
			*error = g_error_new(ZOND_ERROR, 0, "%s\nDokument enthält keine Seiten",
					__func__);
		g_object_unref(zond_pdf_document);

		return NULL;
	}

	g_ptr_array_set_size(priv->pages, number_of_pages);

	rc = zond_pdf_document_init_pages(zond_pdf_document, von, bis, error);
	if (rc) {
		g_object_unref(zond_pdf_document);
		ERROR_Z_VAL(NULL)
	}

	klass = ZOND_PDF_DOCUMENT_GET_CLASS(zond_pdf_document);
	g_ptr_array_add(klass->arr_pdf_documents, zond_pdf_document);

	return zond_pdf_document;
}
/*
gint zond_pdf_document_save(ZondPdfDocument *self, GError **error) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	gint rc = 0;

	if (priv->read_only) {
		if (error)
			*error = g_error_new(ZOND_ERROR, 0,
					"%s\nDokument wurde schreibgeschützt geöffnet", __func__);

		return -1;
	}

	priv->ocr_num = 0;

	rc = sond_file_part_pdf_save_and_close(priv->ctx, priv->doc, priv->sfp_pdf, error);
	if (rc) ERROR_Z

	g_array_remove_range(priv->arr_journal, 0, priv->arr_journal->len);

	return 0;
}
*/
void zond_pdf_document_close(ZondPdfDocument *zond_pdf_document) {
	g_object_unref(zond_pdf_document);

	return;
}

pdf_document*
zond_pdf_document_get_pdf_doc(ZondPdfDocument *self) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	return priv->doc;
}

GPtrArray*
zond_pdf_document_get_arr_pages(ZondPdfDocument const *self) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			(ZondPdfDocument*) self);

	return priv->pages;
}

GArray*
zond_pdf_document_get_arr_journal(ZondPdfDocument const *self) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			(ZondPdfDocument*) self);

	return priv->arr_journal;
}

PdfDocumentPage*
zond_pdf_document_get_pdf_document_page(ZondPdfDocument *self, gint page_doc) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	return g_ptr_array_index(priv->pages, page_doc);
}

gint zond_pdf_document_get_number_of_pages(ZondPdfDocument *self) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	return priv->pages->len;
}

fz_context*
zond_pdf_document_get_ctx(ZondPdfDocument *self) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	return priv->ctx;
}

SondFilePartPDF*
zond_pdf_document_get_sfp_pdf(ZondPdfDocument *self) {
	if (!ZOND_IS_PDF_DOCUMENT(self))
		return NULL;
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	return priv->sfp_pdf;
}

void zond_pdf_document_mutex_lock(const ZondPdfDocument *self) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			(ZondPdfDocument*) self);

	g_mutex_lock(&priv->mutex_doc);

	return;
}

void zond_pdf_document_mutex_unlock(const ZondPdfDocument *self) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			(ZondPdfDocument*) self);

	g_mutex_unlock(&priv->mutex_doc);

	return;
}

//wird nur aufgerufen, wenn alle threadpools aus sind!
gint zond_pdf_document_insert_pages(ZondPdfDocument *zond_pdf_document,
		gint pos, ZPDFDPart* zpdfd_part, pdf_document *pdf_doc, GError **error) {
	gint rc = 0;
	gint count = 0;
	gchar* errmsg = NULL;

	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			zond_pdf_document);

	zond_pdf_document_mutex_lock(zond_pdf_document);
	count = pdf_count_pages(priv->ctx, pdf_doc);
	if (count == 0) {
		zond_pdf_document_mutex_unlock(zond_pdf_document);

		return 0;
	}

	//einfügen in doc
	rc = pdf_copy_page(priv->ctx, pdf_doc, 0, count - 1,
			priv->doc, pos, &errmsg);
	zond_pdf_document_mutex_unlock(zond_pdf_document);
	if (rc) {
		if (error) *error = g_error_new(ZOND_ERROR, 0, "%s\n%s", __func__, errmsg);
		g_free(errmsg);

		return -1;
	}

	//eingefügte Seiten als pdf_document_page erzeugen und initiieren
	for (gint i = pos; i < pos + count; i++) {
		gint rc = 0;
		PdfDocumentPage *pdf_document_page = NULL;

		pdf_document_page = g_malloc0(sizeof(PdfDocumentPage));

		g_ptr_array_insert(priv->pages, i, pdf_document_page);

		rc = zond_pdf_document_init_page(zond_pdf_document, pdf_document_page,
				i, error);
		if (rc == -1)
			ERROR_Z

		pdf_document_page->inserted = zpdfd_part;
	}

	//Index der nachfolgenden Seiten anpassen
	for (gint i = pos + count; i < priv->pages->len; i++) {
		PdfDocumentPage* pdfp_loop = NULL;

		pdfp_loop = g_ptr_array_index(priv->pages, i);
		pdfp_loop->page_akt += count;
	}

	return 0;
}

gint zond_pdf_document_get_ocr_num(ZondPdfDocument *self) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	return priv->ocr_num;
}

void zond_pdf_document_set_ocr_num(ZondPdfDocument *self, gint ocr_num) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	priv->ocr_num = ocr_num;

	return;
}

/**
 * ZPDF_PART ist die Struktur, die Zugriff auf zond_pdf_document hat
 */
void zpdfd_part_drop(ZPDFDPart* zpdfd_part) {
	if (zpdfd_part->ref > 1)
		zpdfd_part->ref--;
	else {
		ZondPdfDocumentPrivate* zpdfd_priv =
				zond_pdf_document_get_instance_private(zpdfd_part->zond_pdf_document);

		if (!g_ptr_array_remove_fast(zpdfd_priv->arr_zpdf_parts, zpdfd_part))
			LOG_WARN("zpdfd_part nicht in Array vorhanden");
		g_object_unref(zpdfd_part->zond_pdf_document);

		g_free(zpdfd_part);
	}

	return;
}

ZPDFDPart* zpdfd_part_ref(ZPDFDPart* zpdfd_part) {
	zpdfd_part->ref++;

	return zpdfd_part;
}

void zpdfd_part_get_anbindung(ZPDFDPart* zpdfd_part, Anbindung* anbindung) {
	anbindung->von.seite = zpdfd_part->first_page->page_akt;
	anbindung->von.index = anbindung->von.index;
	anbindung->bis.seite = zpdfd_part->last_page->page_akt;
	anbindung->bis.index = anbindung->bis.index;

	return;
}

ZPDFDPart* zpdfd_part_peek(SondFilePartPDF* sfp_pdf, Anbindung* anbindung,
		GError** error) {
	ZondPdfDocument* zpdfd = NULL;
	ZondPdfDocumentPrivate* zpdfd_priv = NULL;
	ZPDFDPart* zpdfd_part = NULL;

	zpdfd = zond_pdf_document_open(sfp_pdf, (anbindung) ? anbindung->von.seite : 0,
			(anbindung) ? anbindung->bis.seite : -1, error);
	if (!zpdfd)
		ERROR_Z_VAL(NULL)

	zpdfd_priv = zond_pdf_document_get_instance_private(zpdfd);

	//in Array von Parts suchen, ob schon vorhanden
	for (guint i = 0; i < zpdfd_priv->arr_zpdf_parts->len; i++) {
		ZPDFDPart* zpdfd_part_loop = NULL;

		zpdfd_part_loop = g_ptr_array_index(zpdfd_priv->arr_zpdf_parts, i);

		if (!anbindung) {
			if (!zpdfd_part_loop->has_anbindung)
				return zpdfd_part_ref(zpdfd_part_loop);
		}
		else {
			Anbindung anbindung_part = { 0 };

			zpdfd_part_get_anbindung(zpdfd_part_loop, &anbindung_part);

			if (anbindung_1_gleich_2(*anbindung, anbindung_part))
				return zpdfd_part_ref(zpdfd_part_loop);
		}
	}

	//nicht gefunden, dann neu
	zpdfd_part = g_malloc0(sizeof(ZPDFDPart));

	zpdfd_part->zond_pdf_document = zpdfd;

	zpdfd_part->first_page = g_ptr_array_index(zpdfd_priv->pages,
			(anbindung) ? anbindung->von.seite : 0);
	zpdfd_part->first_index = (anbindung) ? anbindung->von.index : 0;
	zpdfd_part->last_page = g_ptr_array_index(zpdfd_priv->pages,
			(anbindung) ? anbindung->bis.seite : zpdfd_priv->pages->len - 1);
	zpdfd_part->last_index = (anbindung) ? anbindung->bis.index : EOP;

	zpdfd_part->has_anbindung = (anbindung) ? TRUE : FALSE;

	zpdfd_part->ref = 1;

	g_ptr_array_add(zpdfd_priv->arr_zpdf_parts, zpdfd_part);

	return zpdfd_part;
}

