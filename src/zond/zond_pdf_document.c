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

#include "global_types.h"
#include "99conv/pdf.h"

#include "../misc.h"

typedef struct {
	GMutex mutex_doc;
	fz_context *ctx;
	pdf_document *doc;
	gchar *password;
	gint auth;
	gchar *file_part;
	gboolean read_only;
	gchar *working_copy;
	GPtrArray *pages; //array von PdfDocumentPage*
	GArray *arr_journal;
	GArray* arr_redo;
} ZondPdfDocumentPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondPdfDocument, zond_pdf_document, G_TYPE_OBJECT)

ZondAnnotObj* zond_annot_obj_new(pdf_obj* obj) {
	ZondAnnotObj* zond_annot_obj = NULL;

	zond_annot_obj = g_malloc0(sizeof(ZondAnnotObj));
	zond_annot_obj->ref = 1;
	zond_annot_obj->obj = obj;

	return zond_annot_obj;
}

ZondAnnotObj* zond_annot_obj_ref(ZondAnnotObj* zond_annot_obj) {
	if (zond_annot_obj) {
		zond_annot_obj->ref++;
		return zond_annot_obj;
	}

	return NULL;
}

void zond_drop_annot_obj(ZondAnnotObj* zond_annot_obj) {
	if (zond_annot_obj) {
		zond_annot_obj->ref--;
		if (zond_annot_obj->ref == 0) {
			g_free(zond_annot_obj);
		}
	}

	return;
}

pdf_obj* zond_annot_obj_get_obj(ZondAnnotObj* zond_annot_obj) {
	if (zond_annot_obj)
		return zond_annot_obj->obj;

	return NULL;
}

void zond_annot_obj_set_obj(ZondAnnotObj* zond_annot_obj, pdf_obj* obj) {
	if (zond_annot_obj)
		zond_annot_obj->obj = obj;

	return;
}

pdf_annot* pdf_document_page_annot_get_pdf_annot(PdfDocumentPageAnnot* pdpa) {
	pdf_annot* pdf_annot = NULL;
	fz_context* ctx = NULL;
	pdf_obj* obj = NULL;

	ctx = zond_pdf_document_get_ctx(pdpa->pdf_document_page->document);

	obj = zond_annot_obj_get_obj(pdpa->zond_annot_obj);

	pdf_annot = pdf_annot_lookup_obj(ctx, pdpa->pdf_document_page->page, obj);

	return pdf_annot;
}

gint pdf_document_page_get_index(PdfDocumentPage* pdf_document_page) {
	guint index = 0;
	GPtrArray* arr_pages = NULL;

	arr_pages = zond_pdf_document_get_arr_pages(pdf_document_page->document);
	if (!g_ptr_array_find(arr_pages, pdf_document_page, &index)) return -1;

	return (gint) index;
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
	g_array_unref(priv->arr_redo);

	g_ptr_array_unref(priv->pages);

	if (priv->doc) {
		path = g_strdup(fz_stream_filename(priv->ctx, priv->doc->file));
		pdf_drop_document(priv->ctx, priv->doc);

		if (!priv->read_only) {
			gint ret = 0;

			ret = remove(path);
			if (ret && errno == ENOENT) {
				gchar *error_text = NULL;

				error_text = g_strdup_printf("remove('%s'): %s", priv->file_part,
						strerror( errno));
				display_error( NULL,
						"Arbeitskopie konnte nicht gelöscht werden\n\n",
						error_text);
				g_free(error_text);
			}
		}

		g_free(path);
	}

	zond_pdf_document_close_context(priv->ctx); //drop_context reicht nicht aus!

	g_free(priv->file_part);
	g_free(priv->password);
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

	fz_drop_page(priv->ctx, &(pdf_document_page->page->super));
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

static void zond_pdf_document_page_annot_free(gpointer data) {
	PdfDocumentPageAnnot *pdf_document_page_annot = (PdfDocumentPageAnnot*) data;

	zond_drop_annot_obj(pdf_document_page_annot->zond_annot_obj);
	annot_free(&(pdf_document_page_annot->annot));

	g_free(pdf_document_page_annot);

	return;
}

static gint zond_pdf_document_page_annot_load(PdfDocumentPage *pdf_document_page,
		pdf_annot *pdf_annot, GError **error) {
	PdfDocumentPageAnnot *pdf_document_page_annot = NULL;
	gboolean ret = FALSE;

	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			pdf_document_page->document);

	pdf_document_page_annot = g_malloc0(sizeof(PdfDocumentPageAnnot));

	pdf_document_page_annot->pdf_document_page = pdf_document_page;
	pdf_document_page_annot->zond_annot_obj = zond_annot_obj_new(pdf_annot_obj(priv->ctx, pdf_annot));

	ret = pdf_annot_get_annot(priv->ctx,
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
		gchar **errmsg) {
	fz_context *ctx = NULL;
	GError *error = NULL;
	gint rc = 0;

	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			pdf_document_page->document);

	ctx = priv->ctx;

	fz_try( ctx )
		pdf_document_page->page = pdf_load_page(ctx, priv->doc,
				pdf_document_page_get_index(pdf_document_page));
	fz_catch(ctx)
		ERROR_MUPDF("pdf_load_page");

	pdf_document_page->arr_annots = g_ptr_array_new_with_free_func(
			zond_pdf_document_page_annot_free);

	rc = zond_pdf_document_page_load_annots(pdf_document_page, &error);
	if (rc) {
		if (errmsg) *errmsg = g_strdup_printf("%s\n%s", __func__, error->message);
		g_error_free(error);

		return -1;
	}

	return 0;
}

static gint zond_pdf_document_init_page(ZondPdfDocument *self,
		PdfDocumentPage *pdf_document_page, gint index, gchar **errmsg) {
	fz_context *ctx = NULL;
	pdf_obj *rotate_obj = NULL;
	fz_rect mediabox = { 0 };
	fz_matrix page_ctm = { 0 };

	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			ZOND_PDF_DOCUMENT(self));

	pdf_document_page->document = self; //keine ref!

	ctx = priv->ctx;

	fz_try( ctx ) {
		pdf_document_page->obj = pdf_lookup_page_obj(ctx, priv->doc, index);
		pdf_page_obj_transform(ctx, pdf_document_page->obj, &mediabox,
				&page_ctm);
		pdf_document_page->rect = fz_transform_rect(mediabox, page_ctm);

		rotate_obj = pdf_dict_get(ctx, pdf_document_page->obj,
				PDF_NAME(Rotate));
		if (rotate_obj)
			pdf_document_page->rotate = pdf_to_int(ctx, rotate_obj);
		//else: 0
	} fz_catch( ctx )
		ERROR_MUPDF("pdf_lookup_page_obj etc.");

	return 0;
}

static gint zond_pdf_document_init_pages(ZondPdfDocument *self, gint von,
		gint bis, gchar **errmsg) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	if (bis == -1)
		bis = priv->pages->len - 1;

	if (von < 0 || von > bis || bis > priv->pages->len - 1) {
		if (errmsg)
			*errmsg = g_strdup("Seitengrenzen nicht eingehalten");
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

		rc = zond_pdf_document_init_page(self, pdf_document_page, i, errmsg);
		if (rc) {
			g_free(pdf_document_page);
			((priv->pages)->pdata)[i] = NULL;
			ERROR_S
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
			entry->type >= JOURNAL_TYPE_ANNOT_CHANGED) {
		zond_drop_annot_obj(entry->annot_changed.zond_annot_obj);

		annot_free(&(entry->annot_changed.annot));
	} else if (entry->type == JOURNAL_TYPE_OCR) {
		ZondPdfDocumentPrivate *priv =
				zond_pdf_document_get_instance_private(entry->pdf_document_page->document);

		fz_drop_buffer(priv->ctx, entry->ocr.buf);
	}

	return;
}

static void zond_pdf_document_init(ZondPdfDocument *self) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	g_mutex_init(&priv->mutex_doc);

	priv->pages = g_ptr_array_new_with_free_func(
			(GDestroyNotify) zond_pdf_document_page_free);
	priv->arr_journal = g_array_new( FALSE, FALSE, sizeof(JournalEntry));
	g_array_set_clear_func(priv->arr_journal,
			(GDestroyNotify) zond_pdf_document_free_journal_entry);
	priv->arr_redo = g_array_new(FALSE, FALSE, sizeof(JournalEntry));
	g_array_set_clear_func(priv->arr_redo,
				(GDestroyNotify) zond_pdf_document_free_journal_entry);

	return;
}

const ZondPdfDocument*
zond_pdf_document_is_open(const gchar *file_part) {
	ZondPdfDocumentClass *klass = g_type_class_peek_static(
			zond_pdf_document_get_type());

	if (!klass)
		return NULL;

	for (gint i = 0; i < klass->arr_pdf_documents->len; i++) {
		ZondPdfDocument *zond_pdf_document = g_ptr_array_index(
				klass->arr_pdf_documents, i);
		ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
				zond_pdf_document);

		if (!g_strcmp0(priv->file_part, file_part))
			return zond_pdf_document;
	}

	return NULL;
}

ZondPdfDocument*
zond_pdf_document_open(const gchar *file_part, gint von, gint bis,
		gchar **errmsg) {
	gint rc = 0;
	ZondPdfDocument *zond_pdf_document = NULL;
	ZondPdfDocumentPrivate *priv = NULL;
	ZondPdfDocumentClass *klass = NULL;
	GError *error = NULL;
	gint number_of_pages = 0;

	zond_pdf_document = (ZondPdfDocument*) zond_pdf_document_is_open(file_part);
	if (zond_pdf_document) {
		gint rc = 0;

		rc = zond_pdf_document_init_pages(zond_pdf_document, von, bis, errmsg);
		if (rc)
			ERROR_S_VAL(NULL)

		return g_object_ref(zond_pdf_document);
	}

	zond_pdf_document = g_object_new( ZOND_TYPE_PDF_DOCUMENT, NULL);

	priv = zond_pdf_document_get_instance_private(zond_pdf_document);

	priv->ctx = zond_pdf_document_init_context();
	if (!priv->ctx) {
		if (errmsg)
			*errmsg = g_strdup_printf(
					"%s\nfz_context konnte nicht erzeugt werden", __func__);
		g_object_unref(zond_pdf_document);

		return NULL;
	}

	priv->file_part = g_strdup(file_part);

	rc = pdf_open_and_authen_document(priv->ctx, TRUE, FALSE, priv->file_part,
			&priv->password, &priv->doc, &priv->auth, &error);
	if (rc) {
		if (rc == -1) {
			if (errmsg)
				*errmsg = g_strdup_printf("%s\n%s", __func__, error->message);
			g_error_free(error);
		} else if (errmsg)
			*errmsg = g_strdup_printf("%s\nDokument verschlüsselt", __func__);

		g_object_unref(zond_pdf_document);

		return NULL;
	}

	number_of_pages = pdf_count_pages(priv->ctx, priv->doc);
	if (number_of_pages == 0) {
		if (errmsg)
			*errmsg = g_strdup_printf("%s\nDokument enthält keine Seiten",
					__func__);

		return NULL;
	}

	g_ptr_array_set_size(priv->pages, number_of_pages);

	rc = zond_pdf_document_init_pages(zond_pdf_document, von, bis, errmsg);
	if (rc) {
		g_object_unref(zond_pdf_document);
		ERROR_S_VAL(NULL)
	}

	klass = ZOND_PDF_DOCUMENT_GET_CLASS(zond_pdf_document);
	g_ptr_array_add(klass->arr_pdf_documents, zond_pdf_document);

	return zond_pdf_document;
}

gint zond_pdf_document_save(ZondPdfDocument *self, GError **error) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	gint rc = 0;

	if (priv->read_only) {
		if (error)
			*error = g_error_new(ZOND_ERROR, 0,
					"%s\nDokument wurde schreibgeschützt geöffnet", __func__);

		return -1;
	}

	rc = pdf_save(priv->ctx, priv->doc, priv->file_part, error);
	if (rc) ERROR_Z

	g_array_remove_range(priv->arr_journal, 0, priv->arr_journal->len);

	return 0;
}

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

const gchar*
zond_pdf_document_get_file_part(ZondPdfDocument *self) {
	if (!ZOND_IS_PDF_DOCUMENT(self))
		return NULL;
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	return priv->file_part;
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
		gint pos, pdf_document *pdf_doc, gchar **errmsg) {
	gint rc = 0;
	gint count = 0;

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
			priv->doc, pos, errmsg);
	zond_pdf_document_mutex_unlock(zond_pdf_document);
	if (rc)
		ERROR_S

	//eingefügte Seiten als pdf_document_page erzeugen und initiieren
	for (gint i = pos; i < pos + count; i++) {
		gint rc = 0;
		PdfDocumentPage *pdf_document_page = NULL;

		pdf_document_page = g_malloc0(sizeof(PdfDocumentPage));

		g_ptr_array_insert(priv->pages, i, pdf_document_page);

		rc = zond_pdf_document_init_page(zond_pdf_document, pdf_document_page,
				i, errmsg);
		if (rc == -1)
			ERROR_S
	}

	return 0;
}
