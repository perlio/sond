/*
 sojus (sojus_adressen_histbox.c) - Akten, Beweisstücke, Unterlagen
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

#include "sojus_adressen_histbox.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <glib/gstdio.h>

#include "../misc.h"

typedef enum {
	PROP_PATH = 1, N_PROPERTIES
} ZondPdfDocumentProperty;

typedef struct {
	GMutex mutex_doc;
	fz_context *ctx;
	pdf_document *doc;
	gboolean dirty;
	gchar *path;
	GPtrArray *pages; //array von DocumentPage*
	gchar *errmsg;
} ZondPdfDocumentPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondPdfDocument, zond_pdf_document, G_TYPE_OBJECT)

static void zond_pdf_document_set_property(GObject *object, guint property_id,
		const GValue *value, GParamSpec *pspec) {
	ZondPdfDocument *self = ZOND_PDF_DOCUMENT(object);
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	switch ((ZondPdfDocumentProperty) property_id) {
	case PROP_PATH:
		priv->path = g_strdup(g_value_get_string(value));
		break;

	default:
		/* We don't have any other property... */
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void zond_pdf_document_get_property(GObject *object, guint property_id,
		GValue *value, GParamSpec *pspec) {
	ZondPdfDocument *self = ZOND_PDF_DOCUMENT(object);
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(self);

	switch ((ZondPdfDocumentProperty) property_id) {
	case PROP_PATH:
		g_value_set_string(value, priv->path);
		break;

	default:
		/* We don't have any other property... */
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void zond_pdf_document_finalize(GObject *self) {
	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			ZOND_PDF_DOCUMENT(self));

	g_ptr_array_unref(priv->pages);

	pdf_drop_document(priv->ctx, priv->doc);
	zond_pdf_document_close_context(priv->ctx); //drop_context reicht nicht aus!

	g_free(priv->path);
	g_free(priv->errmsg);
	g_mutex_clear(&priv->mutex_doc);

	ZondPdfDocumentClass *klass = ZOND_PDF_DOCUMENT_GET_CLASS(self);

	g_ptr_array_remove_fast(klass->arr_pdf_documents, self);

	G_OBJECT_CLASS(zond_pdf_document_parent_class)->finalize(self);

	return;
}

static void zond_pdf_document_constructed(GObject *self) {
	gint number_of_pages = 0;

	ZondPdfDocumentPrivate *priv = zond_pdf_document_get_instance_private(
			ZOND_PDF_DOCUMENT(self));

	fz_try( priv->ctx )
		priv->doc = pdf_open_document(priv->ctx, priv->path);
fz_catch	( priv->ctx ) {
		priv->errmsg = g_strconcat("Bei Aufruf pdf_open_document:\n",
				fz_caught_message(priv->ctx), NULL);
		priv->doc = NULL;

		return;
	}

	number_of_pages = pdf_count_pages(priv->ctx, priv->doc);
	if (number_of_pages == 0) {
		priv->errmsg = g_strdup("Dokument enthält keine Seiten");

		return;
	}

	g_ptr_array_set_size(priv->pages, number_of_pages);

	for (gint i = 0; i < priv->pages->len; i++) {
		gint rc = 0;
		gchar *errmsg = NULL;

		rc = zond_pdf_document_page_init(ZOND_PDF_DOCUMENT(self), i, &errmsg);
		if (rc == -1) {
			priv->errmsg = g_strconcat("bei Aufruf document_load_page:\n",
					errmsg, NULL);
			g_free(errmsg);

			return;
		}
	}

	return;
}

static void zond_pdf_document_class_init(ZondPdfDocumentClass *klass) {
	GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

	klass->arr_pdf_documents = g_ptr_array_new();

	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->constructed = zond_pdf_document_constructed;
	object_class->finalize = zond_pdf_document_finalize;

	object_class->set_property = zond_pdf_document_set_property;
	object_class->get_property = zond_pdf_document_get_property;

	obj_properties[PROP_PATH] = g_param_spec_string("path", "gchar*",
			"Pfad zur Datei.",
			NULL, G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

	g_object_class_install_properties(object_class, N_PROPERTIES,
			obj_properties);

	return;
}

ZondPdfDocument*
zond_pdf_document_open(const gchar *path, gchar **errmsg) {
	ZondPdfDocument *zond_pdf_document = NULL;
	ZondPdfDocumentPrivate *priv = NULL;

	ZondPdfDocumentClass *klass = g_type_class_peek(ZOND_TYPE_PDF_DOCUMENT);

	if (klass) {
		for (gint i = 0; i < klass->arr_pdf_documents->len; i++) {
			zond_pdf_document = g_ptr_array_index(klass->arr_pdf_documents, i);
			ZondPdfDocumentPrivate *priv =
					zond_pdf_document_get_instance_private(zond_pdf_document);

			if (!g_strcmp0(priv->path, path))
				return g_object_ref(zond_pdf_document);
		}
	}

	zond_pdf_document = g_object_new(ZOND_TYPE_PDF_DOCUMENT, "path", path,
			NULL);

	priv = zond_pdf_document_get_instance_private(zond_pdf_document);
	if (priv->errmsg) {
		if (errmsg)
			*errmsg = g_strconcat("Bei Aufruf g_object_new:\n\n", priv->errmsg,
					NULL);
		g_object_unref(zond_pdf_document);

		return NULL;
	}

	if (!klass)
		klass = ZOND_PDF_DOCUMENT_GET_CLASS(zond_pdf_document);
	g_ptr_array_add(klass->arr_pdf_documents, zond_pdf_document);

	return zond_pdf_document;
}

