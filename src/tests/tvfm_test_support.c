/*
 sond (tvfm_test_support.c) - Akten, Beweisstücke, Unterlagen
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

#include "tvfm_test_support.h"

#include <glib/gstdio.h>
#include <gmime/gmime.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <stdarg.h>
#include <string.h>
#include <zip.h>

#include "../sond_pdf_helper.h"

/* ---------------------------------------------------------------------------------------- */
/* Eigentümer                                                                                */

typedef struct {
	GObjectClass parent_class;
} TvfmTestOwnerClass;

static gchar const* owner_get_root(SondTVFMOwner *o) {
	return ((TvfmTestOwner*) o)->root;
}

static gboolean owner_is_seadrive_path(SondTVFMOwner *o) {
	(void) o;

	return FALSE;
}

static gint owner_before_move(SondTVFMOwner *o, SondTVFMItem *item, SondTVFMItem *parent,
		gchar const *base, gint index_to, GError **error, gpointer *ctx) {
	(void) item; (void) parent; (void) base; (void) index_to; (void) error; (void) ctx;
	((TvfmTestOwner*) o)->n_before_move++;

	return 0;
}

static void owner_after(SondTVFMOwner *o, gboolean success, gpointer ctx) {
	(void) ctx;
	((TvfmTestOwner*) o)->n_after++;
	((TvfmTestOwner*) o)->last_ok = success;
}

static void owner_iface_init(SondTVFMOwnerInterface *iface) {
	iface->get_root = owner_get_root;
	iface->is_seadrive_path = owner_is_seadrive_path;
	iface->before_move = owner_before_move;
	iface->after = owner_after;
}

G_DEFINE_TYPE_WITH_CODE(TvfmTestOwner, tvfm_test_owner, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(SOND_TYPE_TVFM_OWNER, owner_iface_init))

static void owner_finalize(GObject *self) {
	g_free(((TvfmTestOwner*) self)->root);
	G_OBJECT_CLASS(tvfm_test_owner_parent_class)->finalize(self);
}

static void tvfm_test_owner_class_init(TvfmTestOwnerClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = owner_finalize;
}

static void tvfm_test_owner_init(TvfmTestOwner *self) {
	(void) self;
}

/* ---------------------------------------------------------------------------------------- */
/* Rahmen                                                                                    */

static void rm_rf(gchar const *path) {
	GDir *d = g_dir_open(path, 0, NULL);
	gchar const *name = NULL;

	if (d) {
		while ((name = g_dir_read_name(d))) {
			gchar *p = g_build_filename(path, name, NULL);

			if (g_file_test(p, G_FILE_TEST_IS_DIR))
				rm_rf(p);
			else
				g_remove(p);
			g_free(p);
		}
		g_dir_close(d);
	}
	g_rmdir(path);
}

Fix* fix_new(void) {
	static gboolean gmime_ok = FALSE;
	Fix *f = g_new0(Fix, 1);
	SondFilePartClass *klass = g_type_class_ref(SOND_TYPE_FILE_PART);
	GError *error = NULL;

	if (!gmime_ok) {
		g_mime_init();
		gmime_ok = TRUE;
	}

	f->dir = g_dir_make_tmp("tvfm_XXXXXX", &error);
	g_assert_no_error(error);
	f->owner = g_object_new(tvfm_test_owner_get_type(), NULL);
	f->owner->root = g_strdup(f->dir);

	//der Dateibaum hält path_root synchron mit der Wurzel des Eigentümers
	g_free(klass->path_root);
	klass->path_root = g_strdup(f->dir);

	return f;
}

void fix_free(Fix *f) {
	SondFilePartClass *klass = g_type_class_ref(SOND_TYPE_FILE_PART);

	g_object_unref(f->owner);
	g_free(klass->path_root);
	klass->path_root = NULL;
	rm_rf(f->dir);
	g_free(f->dir);
	g_free(f);
}

/* ---------------------------------------------------------------------------------------- */
/* Dateien                                                                                   */

static gchar* voll(Fix *f, gchar const *rel) {
	return g_build_filename(f->dir, rel, NULL);
}

static void eltern_anlegen(gchar const *voller_pfad) {
	gchar *dir = g_path_get_dirname(voller_pfad);

	g_mkdir_with_parents(dir, 0755);
	g_free(dir);
}

void schreibe(Fix *f, gchar const *rel, gchar const *inhalt) {
	gchar *p = voll(f, rel);

	eltern_anlegen(p);
	g_assert_true(g_file_set_contents(p, inhalt, -1, NULL));
	g_free(p);
}

gchar* lese(Fix *f, gchar const *rel) {
	gchar *p = voll(f, rel);
	gchar *inhalt = NULL;

	if (!g_file_get_contents(p, &inhalt, NULL, NULL))
		inhalt = NULL;
	g_free(p);

	return inhalt;
}

gboolean existiert(Fix *f, gchar const *rel) {
	gchar *p = voll(f, rel);
	gboolean ok = g_file_test(p, G_FILE_TEST_EXISTS);

	g_free(p);

	return ok;
}

void schreibe_zip(Fix *f, gchar const *rel, ...) {
	gchar *p = voll(f, rel);
	int err = 0;
	zip_t *z = NULL;
	va_list args;
	gchar const *pfad = NULL;

	eltern_anlegen(p);
	z = zip_open(p, ZIP_CREATE | ZIP_TRUNCATE, &err);
	g_assert_nonnull(z);

	va_start(args, rel);
	while ((pfad = va_arg(args, gchar const*))) {
		gchar const *inhalt = va_arg(args, gchar const*);

		if (g_str_has_suffix(pfad, "/"))
			g_assert_cmpint(zip_dir_add(z, pfad, ZIP_FL_ENC_UTF_8), >=, 0);
		else {
			gsize len = inhalt ? strlen(inhalt) : 0;
			zip_source_t *src = zip_source_buffer(z, g_memdup2(inhalt ? inhalt : "", len + 1),
					len, 1);

			g_assert_nonnull(src);
			g_assert_cmpint(zip_file_add(z, pfad, src, ZIP_FL_ENC_UTF_8), >=, 0);
		}
	}
	va_end(args);

	g_assert_cmpint(zip_close(z), ==, 0);
	g_free(p);
}

void schreibe_eml(Fix *f, gchar const *rel, gchar const *betreff, gchar const *text,
		gchar const *anhang_name, gchar const *anhang_inhalt) {
	gchar *b64 = g_base64_encode((guchar const*) anhang_inhalt, strlen(anhang_inhalt));
	gchar *eml = g_strdup_printf(
			"From: absender@example.org\r\n"
			"To: empfaenger@example.org\r\n"
			"Subject: %s\r\n"
			"Date: Mon, 02 Nov 2026 10:00:00 +0100\r\n"
			"MIME-Version: 1.0\r\n"
			"Content-Type: multipart/mixed; boundary=\"GRENZE\"\r\n"
			"\r\n"
			"--GRENZE\r\n"
			"Content-Type: text/plain; charset=utf-8\r\n"
			"\r\n"
			"%s\r\n"
			"--GRENZE\r\n"
			"Content-Type: application/octet-stream\r\n"
			"Content-Transfer-Encoding: base64\r\n"
			"Content-Disposition: attachment; filename=\"%s\"\r\n"
			"\r\n"
			"%s\r\n"
			"--GRENZE--\r\n",
			betreff, text, anhang_name, b64);

	schreibe(f, rel, eml);
	g_free(eml);
	g_free(b64);
}

void schreibe_pdf(Fix *f, gchar const *rel, ...) {
	gchar *p = voll(f, rel);
	fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	pdf_document *doc = NULL;
	va_list args;
	gchar const *name = NULL;

	g_assert_nonnull(ctx);
	eltern_anlegen(p);

	fz_try(ctx) {
		pdf_obj *seite = NULL;
		pdf_obj *res = NULL;
		fz_buffer *leer = NULL;

		doc = pdf_create_document(ctx);
		res = pdf_new_dict(ctx, doc, 1);
		leer = fz_new_buffer(ctx, 1);
		seite = pdf_add_page(ctx, doc, fz_make_rect(0, 0, 200, 200), 0, res, leer);
		pdf_insert_page(ctx, doc, -1, seite);
		fz_drop_buffer(ctx, leer);
		pdf_drop_obj(ctx, res);
		pdf_drop_obj(ctx, seite);
	}
	fz_catch(ctx)
		g_error("PDF nicht anlegbar: %s", fz_caught_message(ctx));

	va_start(args, rel);
	while ((name = va_arg(args, gchar const*))) {
		gchar const *inhalt = va_arg(args, gchar const*);
		fz_buffer *buf = NULL;
		GError *error = NULL;

		fz_try(ctx)
			buf = fz_new_buffer_from_copied_data(ctx, (guchar const*) inhalt, strlen(inhalt));
		fz_catch(ctx)
			g_error("Puffer: %s", fz_caught_message(ctx));

		g_assert_cmpint(pdf_insert_emb_file(ctx, doc, buf, name, "text/plain", &error), ==, 0);
		g_assert_no_error(error);
		fz_drop_buffer(ctx, buf);
	}
	va_end(args);

	fz_try(ctx)
		pdf_save_document(ctx, doc, p, NULL);
	fz_catch(ctx)
		g_error("PDF nicht speicherbar: %s", fz_caught_message(ctx));

	pdf_drop_document(ctx, doc);
	fz_drop_context(ctx);
	g_free(p);
}

void schreibe_pdf_seiten(Fix *f, gchar const *rel, gint n) {
	gchar *p = voll(f, rel);
	fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	pdf_document *doc = NULL;

	g_assert_nonnull(ctx);
	eltern_anlegen(p);

	fz_try(ctx) {
		doc = pdf_create_document(ctx);
		for (gint i = 0; i < n; i++) {
			pdf_obj *res = pdf_new_dict(ctx, doc, 1);
			fz_buffer *leer = fz_new_buffer(ctx, 1);
			pdf_obj *seite = pdf_add_page(ctx, doc, fz_make_rect(0, 0, 200, 200), 0, res, leer);

			pdf_insert_page(ctx, doc, -1, seite);
			fz_drop_buffer(ctx, leer);
			pdf_drop_obj(ctx, res);
			pdf_drop_obj(ctx, seite);
		}
		pdf_save_document(ctx, doc, p, NULL);
	}
	fz_catch(ctx)
		g_error("PDF nicht anlegbar: %s", fz_caught_message(ctx));

	pdf_drop_document(ctx, doc);
	fz_drop_context(ctx);
	g_free(p);
}

/* ---------------------------------------------------------------------------------------- */
/* Items                                                                                     */

SondTVFMItem* wurzel(Fix *f) {
	return sond_tvfm_item_create(f->owner, NULL, NULL);
}

SondTVFMItem* verzeichnis(Fix *f, gchar const *rel) {
	return sond_tvfm_item_create(f->owner, NULL, rel);
}

SondTVFMItem* datei(Fix *f, gchar const *rel) {
	GError *error = NULL;
	SondFilePart *sfp = sond_file_part_create(NULL, rel, &error);
	SondTVFMItem *item = NULL;

	g_assert_no_error(error);
	g_assert_nonnull(sfp);
	item = sond_tvfm_item_create(f->owner, sfp, NULL);
	g_object_unref(sfp);

	return item;
}

SondTVFMItem* kind(GPtrArray *arr, gchar const *name) {
	for (guint i = 0; i < arr->len; i++) {
		SondTVFMItem *item = g_ptr_array_index(arr, i);

		if (!g_strcmp0(sond_tvfm_item_get_display_name(item), name))
			return item;
	}

	return NULL;
}
