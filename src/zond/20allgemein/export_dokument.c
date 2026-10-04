/*
 zond (export_dokument.c) - Akten, Beweisstücke, Unterlagen
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
#include "../../sond_fileparts.h"
#include "../../sond_text_extract.h"
#include "../../sond_pdf_helper.h"

#include "../zond_pdf_document.h"
#include "../99conv/general.h"

#include "export_dokument.h"

//Auflösung, mit der PDF-Seiten als Bild ausgegeben werden
#define EXPORT_RENDER_DPI 150.0
//Bilder ohne bekannte Auflösung gelten als Bildschirmgröße
#define EXPORT_BILD_DPI 96.0

typedef struct {
	SondFilePart *sfp;
	pdf_document *doc;      //NULL, wenn keine PDF oder nicht zu öffnen
	ZondPdfDocument *zpdfd; //PDF ist im Viewer offen: doc gehört diesem
	gchar *hinweis;         //NULL, wenn geöffnet
} Quelle;

struct _ExportDokumentCtx {
	Projekt *zond;
	GHashTable *ht_quellen;
};

void export_einheit_free(gpointer data) {
	ExportEinheit *u = (ExportEinheit*) data;

	if (!u)
		return;

	g_free(u->text);
	if (u->bild)
		g_bytes_unref(u->bild);
	g_free(u);
}

static ExportEinheit* einheit_text(ExportEinheitTyp typ, gchar *text) {
	ExportEinheit *u = g_new0(ExportEinheit, 1);

	u->typ = typ;
	u->text = text; //Besitz geht über

	return u;
}

static void quelle_free(gpointer data) {
	Quelle *q = (Quelle*) data;

	g_free(q->hinweis);
	g_free(q);
}

ExportDokumentCtx* export_dokument_ctx_new(Projekt *zond) {
	ExportDokumentCtx *dctx = g_new0(ExportDokumentCtx, 1);

	dctx->zond = zond;
	dctx->ht_quellen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
			quelle_free);

	return dctx;
}

void export_dokument_ctx_free(ExportDokumentCtx *dctx) {
	GHashTableIter it;
	gpointer value = NULL;

	if (!dctx)
		return;

	g_hash_table_iter_init(&it, dctx->ht_quellen);
	while (g_hash_table_iter_next(&it, NULL, &value)) {
		Quelle *q = (Quelle*) value;

		if (q->zpdfd)
			g_object_unref(q->zpdfd);
		else if (q->doc)
			pdf_drop_document(dctx->zond->ctx, q->doc);
		if (q->sfp)
			g_object_unref(q->sfp);
	}

	g_hash_table_destroy(dctx->ht_quellen);
	g_free(dctx);
}

static Quelle* quelle_holen(ExportDokumentCtx *dctx, const gchar *datei) {
	Quelle *q = g_hash_table_lookup(dctx->ht_quellen, datei);
	GError *error = NULL;

	if (q)
		return q;

	q = g_new0(Quelle, 1);
	g_hash_table_insert(dctx->ht_quellen, g_strdup(datei), q);

	q->sfp = sond_file_part_from_filepart(datei, &error);
	if (!q->sfp) {
		q->hinweis = g_strdup_printf("%s - konnte nicht geöffnet werden: %s",
				datei, error ? error->message : "?");
		g_clear_error(&error);

		return q;
	}

	if (!SOND_IS_FILE_PART_PDF(q->sfp))
		return q;

	//im Viewer offen: dessen Dokument nehmen, es kennt auch ungespeicherte Seiten
	{
		ZondPdfDocument *zpdfd = zond_pdf_document_is_open(
				SOND_FILE_PART_PDF(q->sfp));

		if (zpdfd) {
			q->zpdfd = g_object_ref(zpdfd);
			q->doc = zond_pdf_document_get_pdf_doc(zpdfd);

			return q;
		}
	}

	q->doc = sond_file_part_pdf_open_document(dctx->zond->ctx,
			SOND_FILE_PART_PDF(q->sfp), TRUE, &error);
	if (!q->doc) {
		q->hinweis = g_strdup_printf("%s - konnte nicht geöffnet werden: %s",
				datei, error ? error->message : "?");
		g_clear_error(&error);
	}

	return q;
}

void export_dokument_oeffnen(ExportDokumentCtx *dctx, const gchar *datei,
		pdf_document **doc, ZondPdfDocument **zpdfd, const gchar **hinweis) {
	Quelle *q = quelle_holen(dctx, datei);

	*doc = q->doc;
	*zpdfd = q->zpdfd;
	*hinweis = q->hinweis;
}

void export_dokument_sperren(ZondPdfDocument *zpdfd) {
	if (zpdfd)
		zond_pdf_document_mutex_lock(zpdfd);
}

void export_dokument_entsperren(ZondPdfDocument *zpdfd) {
	if (zpdfd)
		zond_pdf_document_mutex_unlock(zpdfd);
}

void export_dokument_bereich_clear(ExportPdfBereich *b) {
	g_free(b->auslassen);
	b->auslassen = NULL;
}

gint export_dokument_bereich(fz_context *ctx, pdf_document *src,
		ZondPdfDocument *zpdfd, const ExportEintrag *e, ExportPdfBereich *b,
		gchar **hinweis, GError **error) {
	gint n = 0;
	Anbindung a = e->anb;

	if (zpdfd)
		n = zond_pdf_document_get_number_of_pages(zpdfd);
	else {
		fz_try(ctx)
			n = pdf_count_pages(ctx, src);
		fz_catch(ctx)
			ERROR_PDF
	}

	b->y0 = -1;
	b->y1 = -1;
	b->auslassen = NULL;

	if (anbindung_is_empty(&a)) {
		b->von = 0;
		b->bis = n - 1;
	} else {
		//Punkt steht für seine ganze Seite
		if (anbindung_is_pdf_punkt(a)) {
			a.bis.seite = a.von.seite;
			a.von.index = 0;
			a.bis.index = EOP;
		}

		//auf die Seitenzählung des offenen Dokuments umrechnen, wie der Viewer
		if (zpdfd)
			anbindung_aktualisieren(zpdfd, &a);

		b->von = a.von.seite;
		b->bis = a.bis.seite;

		if (a.von.index > 0)
			b->y0 = a.von.index;
		if (a.bis.index < EOP)
			b->y1 = a.bis.index;
	}

	if (b->von < 0 || b->bis >= n || b->von > b->bis) {
		*hinweis = g_strdup_printf("%s - Seitenbereich (S. %d bis S. %d) liegt "
				"außerhalb der Datei (%d Seiten)", e->datei, b->von + 1,
				b->bis + 1, n);

		return 1;
	}

	//im Viewer gelöschte Seiten bleiben im Dokument, werden aber nicht gezeigt
	if (zpdfd) {
		for (gint i = b->von; i <= b->bis; i++) {
			PdfDocumentPage *pdfp = zond_pdf_document_get_pdf_document_page(
					zpdfd, i);

			if (pdfp && pdfp->deleted) {
				if (!b->auslassen)
					b->auslassen = g_new0(guint8, b->bis - b->von + 1);
				b->auslassen[i - b->von] = 1;
			}
		}
	}

	return 0;
}

/* Eine PDF-Seite als PNG, bei Zuschnitt nur der Ausschnitt. y0/y1 wie in
 * ExportPdfBereich (Seitenkoordinaten von oben). */
static ExportEinheit* export_seite_rendern(fz_context *ctx, pdf_document *doc,
		gint nr, gdouble y0, gdouble y1, GError **error) {
	pdf_page *page = NULL;
	fz_pixmap *pix = NULL;
	fz_device *dev = NULL;
	fz_buffer *png = NULL;
	ExportEinheit *u = NULL;
	fz_rect r = { 0 };
	gdouble s = EXPORT_RENDER_DPI / 72.0;

	fz_try(ctx) {
		fz_matrix ctm = fz_scale(s, s);
		fz_irect bbox = { 0 };
		unsigned char *data = NULL;
		size_t len = 0;

		page = pdf_load_page(ctx, doc, nr);
		r = fz_bound_page(ctx, &page->super);

		if (y0 >= 0)
			r.y0 = MAX(r.y0, (float) y0);
		if (y1 >= 0)
			r.y1 = MIN(r.y1, (float) y1);
		if (r.y1 <= r.y0)
			r = fz_bound_page(ctx, &page->super);

		bbox = fz_round_rect(fz_transform_rect(r, ctm));
		pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), bbox, NULL, 0);
		fz_clear_pixmap_with_value(ctx, pix, 0xff);

		dev = fz_new_draw_device(ctx, fz_identity, pix);
		fz_run_page(ctx, &page->super, dev, ctm, NULL);
		fz_close_device(ctx, dev);

		png = fz_new_buffer_from_pixmap_as_png(ctx, pix,
				fz_default_color_params);
		len = fz_buffer_storage(ctx, png, &data);

		u = g_new0(ExportEinheit, 1);
		u->typ = EXPORT_EINHEIT_BILD;
		u->bild = g_bytes_new(data, len);
		u->ext = "png";
		u->breite_cm = (r.x1 - r.x0) / 72.0 * 2.54;
		u->hoehe_cm = (r.y1 - r.y0) / 72.0 * 2.54;
	}
	fz_always(ctx) {
		fz_drop_buffer(ctx, png);
		fz_drop_device(ctx, dev);
		fz_drop_pixmap(ctx, pix);
		if (page)
			fz_drop_page(ctx, &page->super);
	}
	fz_catch(ctx) {
		g_set_error(error, g_quark_from_static_string("mupdf"), fz_caught(ctx),
				"%s\n%s", __func__, fz_caught_message(ctx));
		export_einheit_free(u);

		return NULL;
	}

	return u;
}

static void export_pdf_einheiten(ExportDokumentCtx *dctx, pdf_document *doc,
		ZondPdfDocument *zpdfd, const ExportEintrag *e, GPtrArray *einheiten) {
	ExportPdfBereich b = { 0 };
	gchar *hinweis = NULL;
	GError *error = NULL;
	gint rc = 0;
	//ein im Viewer offenes Dokument wird mit seinem eigenen Kontext gelesen
	fz_context *ctx = zpdfd ? zond_pdf_document_get_ctx(zpdfd) : dctx->zond->ctx;

	rc = export_dokument_bereich(dctx->zond->ctx, doc, zpdfd, e, &b, &hinweis,
			&error);
	if (rc == -1) {
		g_ptr_array_add(einheiten, einheit_text(EXPORT_EINHEIT_HINWEIS,
				g_strdup_printf("%s - %s", e->datei,
						error ? error->message : "?")));
		g_clear_error(&error);

		return;
	}
	if (rc == 1) {
		g_ptr_array_add(einheiten, einheit_text(EXPORT_EINHEIT_HINWEIS, hinweis));

		return;
	}

	export_dokument_sperren(zpdfd);

	for (gint i = b.von; i <= b.bis; i++) {
		ExportEinheit *u = NULL;

		if (b.auslassen && b.auslassen[i - b.von])
			continue;

		u = export_seite_rendern(ctx, doc, i, (i == b.von) ? b.y0 : -1.0,
				(i == b.bis) ? b.y1 : -1.0, &error);

		if (!u) {
			g_ptr_array_add(einheiten, einheit_text(EXPORT_EINHEIT_HINWEIS,
					g_strdup_printf("%s - Seite %d konnte nicht dargestellt "
							"werden: %s", e->datei, i + 1,
							error ? error->message : "?")));
			g_clear_error(&error);

			continue;
		}

		g_ptr_array_add(einheiten, u);
	}

	export_dokument_entsperren(zpdfd);
	export_dokument_bereich_clear(&b);
}

//Text aus den Segmenten zusammensetzen; segs wird freigegeben
static gchar* segmente_text(GPtrArray *segs) {
	GString *s = g_string_new(NULL);

	for (guint i = 0; i < segs->len; i++) {
		SondTextSegment *seg = g_ptr_array_index(segs, i);

		if (seg->text) {
			if (s->len)
				g_string_append_c(s, '\n');
			g_string_append(s, seg->text);
		}
	}
	g_ptr_array_unref(segs);

	return g_string_free(s, FALSE);
}

/* Bilddaten als Einheit. png und jpeg werden unverändert übernommen, alles
 * andere über GdkPixbuf nach png umgewandelt. NULL, wenn nicht lesbar. */
static ExportEinheit* export_bild_einheit(const guchar *data, gsize len,
		const gchar *mime) {
	GdkPixbufLoader *loader = NULL;
	GdkPixbuf *pixbuf = NULL;
	ExportEinheit *u = NULL;
	gboolean roh = FALSE;

	loader = gdk_pixbuf_loader_new();
	if (!gdk_pixbuf_loader_write(loader, data, len, NULL)) {
		gdk_pixbuf_loader_close(loader, NULL);
		g_object_unref(loader);

		return NULL;
	}
	gdk_pixbuf_loader_close(loader, NULL);

	pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
	if (!pixbuf) {
		g_object_unref(loader);

		return NULL;
	}

	u = g_new0(ExportEinheit, 1);
	u->typ = EXPORT_EINHEIT_BILD;
	u->breite_cm = gdk_pixbuf_get_width(pixbuf) / EXPORT_BILD_DPI * 2.54;
	u->hoehe_cm = gdk_pixbuf_get_height(pixbuf) / EXPORT_BILD_DPI * 2.54;

	if (mime && (!g_strcmp0(mime, "image/png"))) {
		u->ext = "png";
		roh = TRUE;
	} else if (mime && (!g_strcmp0(mime, "image/jpeg")
			|| !g_strcmp0(mime, "image/jpg"))) {
		u->ext = "jpg";
		roh = TRUE;
	}

	if (roh)
		u->bild = g_bytes_new(data, len);
	else {
		gchar *buf = NULL;
		gsize buf_len = 0;

		if (!gdk_pixbuf_save_to_buffer(pixbuf, &buf, &buf_len, "png", NULL,
				NULL)) {
			export_einheit_free(u);
			g_object_unref(loader);

			return NULL;
		}

		u->ext = "png";
		u->bild = g_bytes_new_take(buf, buf_len);
	}

	g_object_unref(loader);

	return u;
}

static void export_mail_einheiten(const guchar *data, gsize len,
		const ExportEintrag *e, GPtrArray *einheiten) {
	GPtrArray *segs = sond_text_extract_gmessage(data, len);
	GPtrArray *images = NULL;
	gchar *text = segmente_text(segs);

	if (*text)
		g_ptr_array_add(einheiten, einheit_text(EXPORT_EINHEIT_TEXT, text));
	else
		g_free(text);

	images = sond_text_extract_gmessage_images(data, len);
	for (guint i = 0; i < images->len; i++) {
		SondEmlImage *img = g_ptr_array_index(images, i);
		ExportEinheit *u = export_bild_einheit(img->image_data, img->image_len,
				img->mime_type);

		if (u)
			g_ptr_array_add(einheiten, u);
		else
			g_ptr_array_add(einheiten, einheit_text(EXPORT_EINHEIT_HINWEIS,
					g_strdup_printf("%s - Bildanhang %s nicht darstellbar",
							e->datei, img->filename ? img->filename : "")));
	}
	g_ptr_array_unref(images);
}

GPtrArray* export_dokument_einheiten(ExportDokumentCtx *dctx,
		const ExportEintrag *e, GError **error) {
	GPtrArray *einheiten = g_ptr_array_new_with_free_func(export_einheit_free);
	Quelle *q = NULL;
	GBytes *bytes = NULL;
	GError *error_bytes = NULL;
	const guchar *data = NULL;
	gsize len = 0;
	const gchar *mime = NULL;
	GPtrArray *segs = NULL;
	gchar *text = NULL;

	q = quelle_holen(dctx, e->datei);
	if (q->hinweis) {
		g_ptr_array_add(einheiten, einheit_text(EXPORT_EINHEIT_HINWEIS,
				g_strdup(q->hinweis)));

		return einheiten;
	}

	if (q->doc) {
		export_pdf_einheiten(dctx, q->doc, q->zpdfd, e, einheiten);

		return einheiten;
	}

	bytes = sond_file_part_get_bytes(q->sfp, &error_bytes);
	if (!bytes) {
		g_ptr_array_add(einheiten, einheit_text(EXPORT_EINHEIT_HINWEIS,
				g_strdup_printf("%s - konnte nicht gelesen werden: %s",
						e->datei, error_bytes ? error_bytes->message : "?")));
		g_clear_error(&error_bytes);

		return einheiten;
	}
	data = g_bytes_get_data(bytes, &len);

	if (SOND_IS_FILE_PART_GMESSAGE(q->sfp)) {
		export_mail_einheiten(data, len, e, einheiten);
		g_bytes_unref(bytes);

		return einheiten;
	}

	if (SOND_IS_FILE_PART_LEAF(q->sfp))
		mime = sond_file_part_leaf_get_mime_type(SOND_FILE_PART_LEAF(q->sfp));

	if (mime && g_str_has_prefix(mime, "image/")) {
		ExportEinheit *u = export_bild_einheit(data, len, mime);

		if (u)
			g_ptr_array_add(einheiten, u);
		else
			g_ptr_array_add(einheiten, einheit_text(EXPORT_EINHEIT_HINWEIS,
					g_strdup_printf("%s - Bild nicht darstellbar", e->datei)));
	} else if (mime && (!g_strcmp0(mime, "text/html")
			|| !g_strcmp0(mime, "application/vnd.oasis.opendocument.text")
			|| !g_strcmp0(mime, "application/vnd.openxmlformats-officedocument.wordprocessingml.document")
			|| g_str_has_prefix(mime, "text/"))) {
		if (!g_strcmp0(mime, "text/html"))
			segs = sond_text_extract_html(data, len);
		else if (!g_strcmp0(mime, "application/vnd.oasis.opendocument.text"))
			segs = sond_text_extract_odt(data, len, NULL);
		else if (g_str_has_prefix(mime, "text/"))
			segs = sond_text_extract_plain(data, len);
		else
			segs = sond_text_extract_docx(data, len, NULL);

		text = segmente_text(segs);
		if (*text)
			g_ptr_array_add(einheiten, einheit_text(EXPORT_EINHEIT_TEXT, text));
		else {
			g_free(text);
			g_ptr_array_add(einheiten, einheit_text(EXPORT_EINHEIT_HINWEIS,
					g_strdup_printf("%s - kein Text gefunden", e->datei)));
		}
	} else
		g_ptr_array_add(einheiten, einheit_text(EXPORT_EINHEIT_HINWEIS,
				g_strdup_printf("%s - Darstellung nicht möglich", e->datei)));

	g_bytes_unref(bytes);

	return einheiten;
}
