/*
 zond (export_docx.c) - Akten, Beweisstücke, Unterlagen
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

#include "export_docx.h"
#include "export_dokument.h"
#include "export_zip.h"

#define DOCX_MAX_EBENE 9
#define DOCX_TEXTBREITE_CM 16.5
#define DOCX_EMU_PRO_CM 360000.0

#define NS_W "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
#define NS_R "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
#define NS_REL "http://schemas.openxmlformats.org/package/2006/relationships"

#define XML_KOPF "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"

//Bild, das in word/media/ des Archivs kommt
typedef struct {
	gchar *name;        //"word/media/bild1.png"
	gchar *ziel;        //"media/bild1.png", relativ zu word/
	GBytes *bytes;
} DocxBild;

static void docx_bild_free(gpointer data) {
	DocxBild *b = (DocxBild*) data;

	g_free(b->name);
	g_free(b->ziel);
	g_bytes_unref(b->bytes);
	g_free(b);
}

//XML-Text; ein Tabulator beendet den Textlauf und setzt einen eigenen Tab
static void docx_xml_text(GString *out, const gchar *s) {
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
		case '\t':
			g_string_append(out, "</w:t><w:tab/><w:t xml:space=\"preserve\">");
			break;
		default:
			if ((guchar) *s >= 0x20)
				g_string_append_c(out, *s);
		}
	}
}

static void docx_absatz(GString *out, const gchar *stil, const gchar *text) {
	g_string_append_printf(out, "<w:p><w:pPr><w:pStyle w:val=\"%s\"/></w:pPr>"
			"<w:r><w:t xml:space=\"preserve\">", stil);
	docx_xml_text(out, text);
	g_string_append(out, "</w:t></w:r></w:p>\n");
}

static void docx_leer(GString *out, const gchar *stil) {
	g_string_append_printf(out,
			"<w:p><w:pPr><w:pStyle w:val=\"%s\"/></w:pPr></w:p>\n", stil);
}

//Bild als eigener Absatz, an Textbreite angepasst
static void docx_bild(GString *c, GPtrArray *bilder, const ExportEinheit *u) {
	DocxBild *b = g_new0(DocxBild, 1);
	gdouble breite = u->breite_cm;
	gdouble hoehe = u->hoehe_cm;
	guint nr = bilder->len + 1;
	gint64 cx = 0;
	gint64 cy = 0;

	b->ziel = g_strdup_printf("media/bild%u.%s", nr, u->ext);
	b->name = g_strdup_printf("word/%s", b->ziel);
	b->bytes = g_bytes_ref(u->bild);
	g_ptr_array_add(bilder, b);

	if (breite > DOCX_TEXTBREITE_CM) {
		hoehe = hoehe * DOCX_TEXTBREITE_CM / breite;
		breite = DOCX_TEXTBREITE_CM;
	}
	cx = (gint64) (breite * DOCX_EMU_PRO_CM);
	cy = (gint64) (hoehe * DOCX_EMU_PRO_CM);

	g_string_append_printf(c,
			"<w:p><w:pPr><w:pStyle w:val=\"Bild\"/></w:pPr><w:r><w:drawing>"
			"<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
			"<wp:extent cx=\"%" G_GINT64_FORMAT "\" cy=\"%" G_GINT64_FORMAT "\"/>"
			"<wp:docPr id=\"%u\" name=\"Bild %u\"/>"
			"<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
			"<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
			"<pic:pic><pic:nvPicPr><pic:cNvPr id=\"%u\" name=\"bild%u\"/><pic:cNvPicPr/></pic:nvPicPr>"
			"<pic:blipFill><a:blip r:embed=\"rIdB%u\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
			"<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%" G_GINT64_FORMAT "\" cy=\"%" G_GINT64_FORMAT "\"/></a:xfrm>"
			"<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>"
			"</a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>\n",
			cx, cy, nr, nr, nr, nr, nr, cx, cy);
}

//Dokument des Knotens: Text als Text, Bilder eingebettet, sonst Hinweis
static void docx_dokument(GString *c, GPtrArray *bilder, ExportDokumentCtx *dctx,
		const ExportEintrag *e) {
	GPtrArray *einheiten = export_dokument_einheiten(dctx, e, NULL);

	for (guint i = 0; i < einheiten->len; i++) {
		ExportEinheit *u = g_ptr_array_index(einheiten, i);

		if (u->typ == EXPORT_EINHEIT_BILD)
			docx_bild(c, bilder, u);
		else if (u->typ == EXPORT_EINHEIT_HINWEIS)
			docx_absatz(c, "Hinweis", u->text);
		else {
			gchar **zeilen = g_strsplit(u->text, "\n", -1);

			for (gchar **z = zeilen; *z; z++) {
				if (**z)
					docx_absatz(c, "Dokument", *z);
				else
					docx_leer(c, "Dokument");
			}
			g_strfreev(zeilen);
		}
	}

	g_ptr_array_unref(einheiten);
}

static void docx_content(GString *c, Projekt *zond, GPtrArray *eintraege,
		const ExportOptionen *opt, ExportDokumentCtx *dctx,
		GPtrArray *bilder) {
	g_string_append(c, XML_KOPF
			"<w:document xmlns:w=\"" NS_W "\" xmlns:r=\"" NS_R "\""
			" xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\""
			" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
			" xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">\n"
			"<w:body>\n");

	if (zond->project_name && *zond->project_name)
		docx_absatz(c, "Title", zond->project_name);

	for (guint i = 0; i < eintraege->len; i++) {
		ExportEintrag *e = g_ptr_array_index(eintraege, i);
		GString *kopf = g_string_new(NULL);

		if (opt->nummern && e->nummer)
			g_string_append(kopf, e->nummer);
		if (opt->nodetext && e->titel && *e->titel) {
			if (kopf->len)
				g_string_append_c(kopf, ' ');
			g_string_append(kopf, e->titel);
		}

		if (kopf->len) {
			gchar *stil = g_strdup_printf("Heading%d",
					MIN(e->ebene, DOCX_MAX_EBENE));

			docx_absatz(c, stil, kopf->str);
			g_free(stil);
		}
		g_string_free(kopf, TRUE);

		if (opt->pfad && e->pfad) {
			gchar *zeile = g_strdup_printf("Pfad: %s", e->pfad);

			docx_absatz(c, "Pfad", zeile);
			g_free(zeile);
		}

		if (opt->anbindung && e->datei) {
			gchar *zeile = e->anbindung ?
					g_strdup_printf("Datei: %s, %s", e->datei, e->anbindung) :
					g_strdup_printf("Datei: %s", e->datei);

			docx_absatz(c, "Anbindung", zeile);
			g_free(zeile);
		}

		if (opt->text && e->notiz && *e->notiz) {
			gchar **zeilen = g_strsplit(e->notiz, "\n", -1);

			for (gchar **z = zeilen; *z; z++) {
				if (**z)
					docx_absatz(c, "Notiz", *z);
				else
					docx_leer(c, "Notiz");
			}
			g_strfreev(zeilen);
		}

		if (opt->dokumente && e->datei)
			docx_dokument(c, bilder, dctx, e);
	}

	//A4, Ränder links 2,5 cm, sonst 2 cm - ergibt 16,5 cm Textbreite
	g_string_append(c, "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
			"<w:pgMar w:top=\"1134\" w:right=\"1134\" w:bottom=\"1134\" "
			"w:left=\"1417\" w:header=\"709\" w:footer=\"709\" w:gutter=\"0\"/>"
			"</w:sectPr>\n</w:body>\n</w:document>\n");
}

static void docx_stil(GString *s, const gchar *id, const gchar *name,
		const gchar *ppr, const gchar *rpr) {
	g_string_append_printf(s, "<w:style w:type=\"paragraph\" w:styleId=\"%s\">"
			"<w:name w:val=\"%s\"/><w:basedOn w:val=\"Normal\"/>"
			"<w:next w:val=\"Normal\"/><w:qFormat/>", id, name);
	if (ppr && *ppr)
		g_string_append_printf(s, "<w:pPr>%s</w:pPr>", ppr);
	if (rpr && *rpr)
		g_string_append_printf(s, "<w:rPr>%s</w:rPr>", rpr);
	g_string_append(s, "</w:style>\n");
}

static void docx_styles(GString *s) {
	//Halbpunkte
	static const gint groesse[DOCX_MAX_EBENE] = { 36, 32, 28, 26, 24, 24, 22,
			22, 22 };

	g_string_append(s, XML_KOPF "<w:styles xmlns:w=\"" NS_W "\">\n"
			"<w:docDefaults><w:rPrDefault><w:rPr>"
			"<w:rFonts w:ascii=\"Arial\" w:hAnsi=\"Arial\" w:cs=\"Arial\"/>"
			"<w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/><w:lang w:val=\"de-DE\"/>"
			"</w:rPr></w:rPrDefault><w:pPrDefault><w:pPr>"
			"<w:spacing w:after=\"120\"/></w:pPr></w:pPrDefault></w:docDefaults>\n"
			"<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">"
			"<w:name w:val=\"Normal\"/><w:qFormat/></w:style>\n");

	docx_stil(s, "Title", "Title", "<w:spacing w:after=\"280\"/>",
			"<w:b/><w:sz w:val=\"44\"/><w:szCs w:val=\"44\"/>");

	for (gint i = 1; i <= DOCX_MAX_EBENE; i++) {
		gchar *id = g_strdup_printf("Heading%d", i);
		gchar *name = g_strdup_printf("heading %d", i);
		gchar *ppr = g_strdup_printf("<w:keepNext/>"
				"<w:spacing w:before=\"240\" w:after=\"80\"/>"
				"<w:outlineLvl w:val=\"%d\"/>", i - 1);
		gchar *rpr = g_strdup_printf("<w:b/><w:sz w:val=\"%d\"/>"
				"<w:szCs w:val=\"%d\"/>", groesse[i - 1], groesse[i - 1]);

		docx_stil(s, id, name, ppr, rpr);
		g_free(id);
		g_free(name);
		g_free(ppr);
		g_free(rpr);
	}

	docx_stil(s, "Pfad", "Pfad", "<w:keepNext/><w:spacing w:after=\"60\"/>",
			"<w:color w:val=\"666666\"/><w:sz w:val=\"18\"/><w:szCs w:val=\"18\"/>");
	docx_stil(s, "Anbindung", "Anbindung", "<w:spacing w:after=\"80\"/>",
			"<w:i/><w:sz w:val=\"18\"/><w:szCs w:val=\"18\"/>");
	docx_stil(s, "Notiz", "Notiz", NULL, NULL);
	docx_stil(s, "Dokument", "Dokument",
			"<w:pBdr><w:left w:val=\"single\" w:sz=\"12\" w:space=\"8\" w:color=\"999999\"/></w:pBdr>"
			"<w:ind w:left=\"170\"/>",
			"<w:sz w:val=\"20\"/><w:szCs w:val=\"20\"/>");
	docx_stil(s, "Hinweis", "Hinweis", NULL, "<w:b/><w:color w:val=\"AA0000\"/>");
	docx_stil(s, "Bild", "Bild", "<w:spacing w:before=\"60\" w:after=\"160\"/>",
			NULL);

	g_string_append(s, "</w:styles>\n");
}

static void docx_content_types(GString *t) {
	g_string_append(t, XML_KOPF
			"<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
			"<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
			"<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
			"<Default Extension=\"png\" ContentType=\"image/png\"/>"
			"<Default Extension=\"jpg\" ContentType=\"image/jpeg\"/>"
			"<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
			"<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
			"</Types>\n");
}

static const gchar *docx_rels_paket = XML_KOPF
		"<Relationships xmlns=\"" NS_REL "\">"
		"<Relationship Id=\"rId1\" Type=\"" NS_R "/officeDocument\" Target=\"word/document.xml\"/>"
		"</Relationships>\n";

static void docx_rels_dokument(GString *r, GPtrArray *bilder) {
	g_string_append(r, XML_KOPF "<Relationships xmlns=\"" NS_REL "\">"
			"<Relationship Id=\"rId1\" Type=\"" NS_R "/styles\" Target=\"styles.xml\"/>");

	for (guint i = 0; i < bilder->len; i++) {
		DocxBild *b = g_ptr_array_index(bilder, i);

		g_string_append_printf(r, "<Relationship Id=\"rIdB%u\" Type=\"" NS_R
				"/image\" Target=\"%s\"/>", i + 1, b->ziel);
	}

	g_string_append(r, "</Relationships>\n");
}

static gint docx_zip_schreiben(const gchar *filename, GString *types,
		GString *content, GString *styles, GString *rels, GPtrArray *bilder,
		GError **error) {
	GArray *dateien = g_array_new(FALSE, FALSE, sizeof(ExportZipDatei));
	ExportZipDatei d = { 0 };
	gint rc = 0;

	d = (ExportZipDatei ) { "[Content_Types].xml", types->str, types->len,
					FALSE };
	g_array_append_val(dateien, d);
	d = (ExportZipDatei ) { "_rels/.rels", docx_rels_paket,
					strlen(docx_rels_paket), FALSE };
	g_array_append_val(dateien, d);
	d = (ExportZipDatei ) { "word/document.xml", content->str, content->len,
					FALSE };
	g_array_append_val(dateien, d);
	d = (ExportZipDatei ) { "word/styles.xml", styles->str, styles->len, FALSE };
	g_array_append_val(dateien, d);
	d = (ExportZipDatei ) { "word/_rels/document.xml.rels", rels->str,
					rels->len, FALSE };
	g_array_append_val(dateien, d);

	for (guint i = 0; i < bilder->len; i++) {
		DocxBild *b = g_ptr_array_index(bilder, i);

		d.name = b->name;
		d.data = g_bytes_get_data(b->bytes, &d.len);
		d.store = TRUE; //png und jpeg sind schon komprimiert
		g_array_append_val(dateien, d);
	}

	rc = export_zip_schreiben(filename, (ExportZipDatei*) dateien->data,
			dateien->len, error);

	g_array_free(dateien, TRUE);

	return rc;
}

gint export_docx_schreiben(Projekt *zond, GPtrArray *eintraege,
		const ExportOptionen *opt, const gchar *filename, GError **error) {
	GString *content = g_string_new(NULL);
	GString *styles = g_string_new(NULL);
	GString *types = g_string_new(NULL);
	GString *rels = g_string_new(NULL);
	GPtrArray *bilder = g_ptr_array_new_with_free_func(docx_bild_free);
	ExportDokumentCtx *dctx = opt->dokumente ?
			export_dokument_ctx_new(zond) : NULL;
	gint rc = 0;

	docx_styles(styles);
	docx_content(content, zond, eintraege, opt, dctx, bilder);
	docx_content_types(types);
	docx_rels_dokument(rels, bilder);

	rc = docx_zip_schreiben(filename, types, content, styles, rels, bilder,
			error);

	export_dokument_ctx_free(dctx);
	g_ptr_array_unref(bilder);
	g_string_free(content, TRUE);
	g_string_free(styles, TRUE);
	g_string_free(types, TRUE);
	g_string_free(rels, TRUE);

	return rc;
}
