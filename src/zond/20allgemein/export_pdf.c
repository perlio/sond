/*
 zond (export_pdf.c) - Akten, Beweisstücke, Unterlagen
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
#include "../../sond_pdf_helper.h"

#include "../99conv/general.h"

#include "export_pdf.h"
#include "export_pdf_seiten.h"
#include "export_dokument.h"

#define HTML_MAX_EBENE 6

static void html_text(GString *out, const gchar *s) {
	gboolean vorher_leer = FALSE;

	for (; s && *s; s++) {
		switch (*s) {
		case '&':
			g_string_append(out, "&amp;");
			break;
		case '<':
			g_string_append(out, "&lt;");
			break;
		case '>':
			g_string_append(out, "&gt;");
			break;
		case '\n':
			g_string_append(out, "<br/>");
			break;
		case '\r':
			break;
		case '\t':
			g_string_append(out, "&#160;&#160;&#160;&#160;");
			break;
		case ' ':
			//Leerzeichenfolgen und führende Leerzeichen bleiben erhalten
			g_string_append(out, vorher_leer || out->len == 0
					|| g_str_has_suffix(out->str, "<br/>") ? "&#160;" : " ");
			break;
		default:
			g_string_append_c(out, *s);
		}
		vorher_leer = (*s == ' ');
	}
}

static void html_absatz(GString *h, const gchar *klasse, const gchar *text) {
	if (klasse)
		g_string_append_printf(h, "<p class=\"%s\">", klasse);
	else
		g_string_append(h, "<p>");
	html_text(h, text);
	g_string_append(h, "</p>");
}

static void export_pdf_info_html(GString *h, const ExportEintrag *e,
		const ExportOptionen *opt) {
	GString *kopf = g_string_new(NULL);

	if (opt->nummern && e->nummer)
		g_string_append(kopf, e->nummer);
	if (opt->nodetext && e->titel && *e->titel) {
		if (kopf->len)
			g_string_append_c(kopf, ' ');
		g_string_append(kopf, e->titel);
	}

	if (kopf->len) {
		gint ebene = MIN(e->ebene, HTML_MAX_EBENE);

		g_string_append_printf(h, "<h%d>", ebene);
		html_text(h, kopf->str);
		g_string_append_printf(h, "</h%d>", ebene);
	}
	g_string_free(kopf, TRUE);

	if (opt->pfad && e->pfad) {
		gchar *zeile = g_strdup_printf("Pfad: %s", e->pfad);

		html_absatz(h, "pfad", zeile);
		g_free(zeile);
	}

	if (opt->anbindung && e->datei) {
		gchar *zeile = e->anbindung ?
				g_strdup_printf("Datei: %s, %s", e->datei, e->anbindung) :
				g_strdup_printf("Datei: %s", e->datei);

		html_absatz(h, "anb", zeile);
		g_free(zeile);
	}

	if (opt->text && e->notiz && *e->notiz)
		html_absatz(h, NULL, e->notiz);
}

//Pro Quell-PDF eine Graft-Map, damit gemeinsame Ressourcen nur einmal kopiert werden
static pdf_graft_map* map_holen(fz_context *ctx, GHashTable *ht_maps,
		pdf_document *dest, pdf_document *src) {
	pdf_graft_map *map = g_hash_table_lookup(ht_maps, src);

	if (!map) {
		map = pdf_new_graft_map(ctx, dest); //wirft keine Exception
		g_hash_table_insert(ht_maps, src, map);
	}

	return map;
}

/* Text und Bilder eines nicht-PDF-Dokuments im fortlaufenden Satz. Hinweise
 * stehen schon im Kopf des Knotens und werden hier übersprungen. Lässt sich
 * ein Bild nicht setzen, folgt ein Hinweis statt eines Abbruchs. */
static gint export_pdf_einheiten_setzen(ExportPdfSatz *satz,
		GPtrArray *einheiten, GError **error) {
	for (guint i = 0; i < einheiten->len; i++) {
		ExportEinheit *u = g_ptr_array_index(einheiten, i);

		if (u->typ == EXPORT_EINHEIT_TEXT) {
			GString *h = g_string_new(NULL);
			gint rc = 0;

			html_absatz(h, "dok", u->text);
			rc = export_pdf_satz_html(satz, h->str, error);
			g_string_free(h, TRUE);
			if (rc)
				return -1;
		} else if (u->typ == EXPORT_EINHEIT_BILD) {
			gsize len = 0;
			const guchar *data = g_bytes_get_data(u->bild, &len);
			GError *error_bild = NULL;

			if (export_pdf_satz_bild(satz, data, len, u->breite_cm,
					u->hoehe_cm, &error_bild)) {
				GString *h = g_string_new(NULL);
				gchar *text = g_strdup_printf("Bild konnte nicht eingefügt "
						"werden: %s", error_bild ? error_bild->message : "?");
				gint rc = 0;

				html_absatz(h, "hinweis", text);
				rc = export_pdf_satz_html(satz, h->str, error);
				g_string_free(h, TRUE);
				g_free(text);
				g_clear_error(&error_bild);
				if (rc)
					return -1;
			}
		}
	}

	return 0;
}

gint export_pdf_schreiben(Projekt *zond, GPtrArray *eintraege,
		const ExportOptionen *opt, const gchar *filename, GError **error) {
	fz_context *ctx = zond->ctx;
	pdf_document *dest = NULL;
	ExportDokumentCtx *dctx = NULL;
	ExportPdfSatz *satz = NULL;
	GHashTable *ht_maps = NULL;
	fz_buffer *buf = NULL;
	gint seiten = 0;
	gint rc = 0;

	fz_try(ctx)
		dest = pdf_create_document(ctx);
	fz_catch(ctx)
		ERROR_PDF

	dctx = export_dokument_ctx_new(zond);
	satz = export_pdf_satz_new(ctx, dest);
	ht_maps = g_hash_table_new(NULL, NULL);

	for (guint i = 0; i < eintraege->len; i++) {
		ExportEintrag *e = g_ptr_array_index(eintraege, i);
		GString *html = g_string_new(NULL);
		gchar *hinweis = NULL;
		pdf_document *src = NULL;
		ZondPdfDocument *zpdfd = NULL;
		const gchar *hinweis_quelle = NULL;
		ExportPdfBereich bereich = { 0 };
		gboolean mit_dokument = FALSE;
		GPtrArray *einheiten = NULL;

		export_pdf_info_html(html, e, opt);

		if (opt->dokumente && e->datei) {
			export_dokument_oeffnen(dctx, e->datei, &src, &zpdfd,
					&hinweis_quelle);

			if (src) {
				gint ret = export_dokument_bereich(ctx, src, zpdfd, e,
						&bereich, &hinweis, error);

				if (ret == -1) {
					g_string_free(html, TRUE);
					rc = -1;

					goto aufraeumen;
				}
				mit_dokument = (ret == 0);
			} else if (hinweis_quelle)
				hinweis = g_strdup(hinweis_quelle);
			else {
				//Text, Bild, E-Mail: Hinweise in den Kopf, Rest danach
				einheiten = export_dokument_einheiten(dctx, e, error);
				if (!einheiten) {
					g_string_free(html, TRUE);
					rc = -1;

					goto aufraeumen;
				}

				for (guint u = 0; u < einheiten->len; u++) {
					ExportEinheit *einheit = g_ptr_array_index(einheiten, u);

					if (einheit->typ == EXPORT_EINHEIT_HINWEIS)
						html_absatz(html, "hinweis", einheit->text);
				}
			}
		}

		if (hinweis) {
			html_absatz(html, "hinweis", hinweis);
			g_free(hinweis);
		}

		//Kopf, Text und Bilder laufen fortlaufend, ohne eigene Seite je Knoten
		if ((html->len && export_pdf_satz_html(satz, html->str, error))
				|| (einheiten && export_pdf_einheiten_setzen(satz, einheiten,
						error))) {
			g_string_free(html, TRUE);
			export_dokument_bereich_clear(&bereich);
			if (einheiten)
				g_ptr_array_unref(einheiten);
			rc = -1;

			goto aufraeumen;
		}
		g_string_free(html, TRUE);
		if (einheiten)
			g_ptr_array_unref(einheiten);

		if (mit_dokument) {
			gint ret = 0;

			//fremde Seiten beginnen auf einer neuen Seite
			if (export_pdf_satz_schliessen(satz, error)) {
				export_dokument_bereich_clear(&bereich);
				rc = -1;

				goto aufraeumen;
			}

			export_dokument_sperren(zpdfd);
			ret = export_pdf_seiten_kopieren(ctx, dest, src,
					map_holen(ctx, ht_maps, dest, src), &bereich, error);
			export_dokument_entsperren(zpdfd);
			export_dokument_bereich_clear(&bereich);

			if (ret) {
				rc = -1;

				goto aufraeumen;
			}
		}
	}

	if (export_pdf_satz_schliessen(satz, error)) {
		rc = -1;

		goto aufraeumen;
	}

	fz_try(ctx)
		seiten = pdf_count_pages(ctx, dest);
	fz_catch(ctx) {
		g_set_error(error, SOND_ERROR, 0, "%s\n%s", __func__,
				fz_caught_message(ctx));
		rc = -1;

		goto aufraeumen;
	}

	if (!seiten) {
		g_set_error(error, SOND_ERROR, 0, "Der Export enthält keine Seiten");
		rc = -1;

		goto aufraeumen;
	}

	buf = pdf_doc_to_buf(ctx, dest, error);
	if (!buf) {
		rc = -1;

		goto aufraeumen;
	}

	{
		unsigned char *data = NULL;
		size_t len = fz_buffer_storage(ctx, buf, &data);

		if (!g_file_set_contents(filename, (const gchar*) data, len, error))
			rc = -1;
	}

	aufraeumen:
	if (buf)
		fz_drop_buffer(ctx, buf);

	export_pdf_satz_free(satz);

	{
		GHashTableIter it;
		gpointer value = NULL;

		g_hash_table_iter_init(&it, ht_maps);
		while (g_hash_table_iter_next(&it, NULL, &value))
			pdf_drop_graft_map(ctx, (pdf_graft_map*) value);
	}
	g_hash_table_destroy(ht_maps);

	//Quellen erst nach den Graft-Maps, die auf sie verweisen
	export_dokument_ctx_free(dctx);

	pdf_drop_document(ctx, dest);

	return rc;
}
