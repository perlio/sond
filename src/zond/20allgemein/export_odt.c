/*
 zond (export_odt.c) - Akten, Beweisstücke, Unterlagen
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

#include "export_odt.h"
#include "export_dokument.h"
#include "export_zip.h"

#define ODT_MIMETYPE "application/vnd.oasis.opendocument.text"
#define ODT_MAX_EBENE 10
#define ODT_TEXTBREITE_CM 16.5

#define ODT_NAMESPACES \
	" xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\"" \
	" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\"" \
	" xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\"" \
	" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\"" \
	" xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\"" \
	" xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\"" \
	" xmlns:xlink=\"http://www.w3.org/1999/xlink\"" \
	" office:version=\"1.2\""

//Bild, das in Pictures/ des Archivs kommt
typedef struct {
	gchar *name;        //"Pictures/bild1.png"
	const gchar *mime;
	GBytes *bytes;
} OdtBild;

static void odt_bild_free(gpointer data) {
	OdtBild *b = (OdtBild*) data;

	g_free(b->name);
	g_bytes_unref(b->bytes);
	g_free(b);
}

static void odt_manifest(GString *m, GPtrArray *bilder) {
	g_string_append(m,
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">\n"
			" <manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.2\" manifest:media-type=\"" ODT_MIMETYPE "\"/>\n"
			" <manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>\n"
			" <manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>\n");

	for (guint i = 0; i < bilder->len; i++) {
		OdtBild *b = g_ptr_array_index(bilder, i);

		g_string_append_printf(m,
				" <manifest:file-entry manifest:full-path=\"%s\" manifest:media-type=\"%s\"/>\n",
				b->name, b->mime);
	}

	g_string_append(m, "</manifest:manifest>\n");
}

//Kommazahl unabhängig von der Spracheinstellung (Dezimalpunkt)
static void odt_cm(GString *out, gdouble cm) {
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE] = { 0 };

	g_ascii_formatd(buf, sizeof(buf), "%.2f", cm);
	g_string_append(out, buf);
	g_string_append(out, "cm");
}

//XML-Text: Sonderzeichen maskieren, Steuerzeichen weglassen
static void odt_xml_text(GString *out, const gchar *s) {
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
			g_string_append(out, "<text:tab/>");
			break;
		case '\r':
			break;
		default:
			if ((guchar) *s >= 0x20 || *s == '\n')
				g_string_append_c(out, *s);
		}
	}
}

//Textzeile eines Absatzes: Leerzeichenfolgen bleiben erhalten
static void odt_xml_zeile(GString *out, const gchar *zeile) {
	GString *tmp = g_string_new(NULL);
	const gchar *p = zeile;

	while (*p) {
		if (*p == ' ') {
			gint n = 0;

			while (p[n] == ' ')
				n++;

			if (n == 1 && tmp->len)
				g_string_append_c(tmp, ' ');
			else //führend oder mehrfach
				g_string_append_printf(tmp, "<text:s text:c=\"%d\"/>", n);

			p += n;
		} else {
			const gchar *ende = p;
			gchar *stueck = NULL;

			while (*ende && *ende != ' ')
				ende++;

			stueck = g_strndup(p, ende - p);
			odt_xml_text(tmp, stueck);
			g_free(stueck);
			p = ende;
		}
	}

	g_string_append(out, tmp->str);
	g_string_free(tmp, TRUE);
}

static void odt_absatz(GString *out, const gchar *stil, const gchar *text) {
	g_string_append_printf(out, "<text:p text:style-name=\"%s\">", stil);
	odt_xml_zeile(out, text);
	g_string_append(out, "</text:p>\n");
}

static void odt_styles(GString *s) {
	static const gchar *groesse[ODT_MAX_EBENE] = { "18pt", "16pt", "14pt",
			"13pt", "12pt", "12pt", "11pt", "11pt", "11pt", "11pt" };

	g_string_append(s,
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<office:document-styles" ODT_NAMESPACES ">\n"
			"<office:font-face-decls>"
			"<style:font-face style:name=\"Arial\" svg:font-family=\"Arial\" style:font-family-generic=\"swiss\"/>"
			"</office:font-face-decls>\n"
			"<office:styles>\n"
			"<style:default-style style:family=\"paragraph\">"
			"<style:text-properties style:font-name=\"Arial\" fo:font-size=\"11pt\" fo:language=\"de\" fo:country=\"DE\"/>"
			"</style:default-style>\n"
			"<style:style style:name=\"Standard\" style:family=\"paragraph\" style:class=\"text\"/>\n"
			"<style:style style:name=\"Text_20_body\" style:display-name=\"Text body\" style:family=\"paragraph\" "
			"style:parent-style-name=\"Standard\" style:class=\"text\">"
			"<style:paragraph-properties fo:margin-top=\"0cm\" fo:margin-bottom=\"0.2cm\"/></style:style>\n"
			"<style:style style:name=\"Title\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" "
			"style:next-style-name=\"Text_20_body\" style:class=\"chapter\">"
			"<style:paragraph-properties fo:margin-top=\"0cm\" fo:margin-bottom=\"0.5cm\"/>"
			"<style:text-properties fo:font-size=\"22pt\" fo:font-weight=\"bold\"/></style:style>\n"
			"<style:style style:name=\"Heading\" style:display-name=\"Heading\" style:family=\"paragraph\" "
			"style:parent-style-name=\"Standard\" style:next-style-name=\"Text_20_body\" style:class=\"text\">"
			"<style:paragraph-properties fo:margin-top=\"0.4cm\" fo:margin-bottom=\"0.15cm\" fo:keep-with-next=\"always\"/>"
			"<style:text-properties fo:font-weight=\"bold\"/></style:style>\n");

	for (gint i = 1; i <= ODT_MAX_EBENE; i++)
		g_string_append_printf(s,
				"<style:style style:name=\"Heading_20_%d\" style:display-name=\"Heading %d\" style:family=\"paragraph\" "
				"style:parent-style-name=\"Heading\" style:next-style-name=\"Text_20_body\" "
				"style:default-outline-level=\"%d\" style:class=\"text\">"
				"<style:text-properties fo:font-size=\"%s\"/></style:style>\n",
				i, i, i, groesse[i - 1]);

	g_string_append(s,
			"<style:style style:name=\"Pfad\" style:family=\"paragraph\" style:parent-style-name=\"Standard\">"
			"<style:paragraph-properties fo:margin-bottom=\"0.1cm\" fo:keep-with-next=\"always\"/>"
			"<style:text-properties fo:font-size=\"9pt\" fo:color=\"#666666\"/></style:style>\n"
			"<style:style style:name=\"Anbindung\" style:family=\"paragraph\" style:parent-style-name=\"Standard\">"
			"<style:paragraph-properties fo:margin-bottom=\"0.15cm\"/>"
			"<style:text-properties fo:font-size=\"9pt\" fo:font-style=\"italic\"/></style:style>\n"
			"<style:style style:name=\"Notiz\" style:family=\"paragraph\" style:parent-style-name=\"Text_20_body\"/>\n"
			"<style:style style:name=\"Dokument\" style:family=\"paragraph\" style:parent-style-name=\"Text_20_body\">"
			"<style:paragraph-properties fo:margin-left=\"0.3cm\" fo:padding-left=\"0.3cm\" "
			"fo:border-left=\"1.5pt solid #999999\" fo:border-top=\"none\" fo:border-bottom=\"none\" fo:border-right=\"none\"/>"
			"<style:text-properties fo:font-size=\"10pt\"/></style:style>\n"
			"<style:style style:name=\"Hinweis\" style:family=\"paragraph\" style:parent-style-name=\"Text_20_body\">"
			"<style:text-properties fo:font-weight=\"bold\" fo:color=\"#aa0000\"/></style:style>\n"
			"<style:style style:name=\"Bild\" style:family=\"paragraph\" style:parent-style-name=\"Standard\">"
			"<style:paragraph-properties fo:margin-top=\"0.1cm\" fo:margin-bottom=\"0.3cm\"/></style:style>\n"
			"</office:styles>\n"
			"<office:automatic-styles>"
			"<style:page-layout style:name=\"pm1\">"
			"<style:page-layout-properties fo:page-width=\"21cm\" fo:page-height=\"29.7cm\" "
			"fo:margin-top=\"2cm\" fo:margin-bottom=\"2cm\" fo:margin-left=\"2.5cm\" fo:margin-right=\"2cm\"/>"
			"</style:page-layout>"
			"</office:automatic-styles>\n"
			"<office:master-styles>"
			"<style:master-page style:name=\"Standard\" style:page-layout-name=\"pm1\"/>"
			"</office:master-styles>\n"
			"</office:document-styles>\n");
}

//Bild als Absatz mit Rahmen, an Textbreite angepasst
static void odt_bild(GString *c, GPtrArray *bilder, const ExportEinheit *u) {
	OdtBild *b = g_new0(OdtBild, 1);
	gdouble breite = u->breite_cm;
	gdouble hoehe = u->hoehe_cm;

	b->name = g_strdup_printf("Pictures/bild%u.%s", bilder->len + 1, u->ext);
	b->mime = !g_strcmp0(u->ext, "jpg") ? "image/jpeg" : "image/png";
	b->bytes = g_bytes_ref(u->bild);
	g_ptr_array_add(bilder, b);

	if (breite > ODT_TEXTBREITE_CM) {
		hoehe = hoehe * ODT_TEXTBREITE_CM / breite;
		breite = ODT_TEXTBREITE_CM;
	}

	g_string_append_printf(c, "<text:p text:style-name=\"Bild\">"
			"<draw:frame text:anchor-type=\"as-char\" draw:name=\"Bild%u\" svg:width=\"",
			bilder->len);
	odt_cm(c, breite);
	g_string_append(c, "\" svg:height=\"");
	odt_cm(c, hoehe);
	g_string_append_printf(c, "\"><draw:image xlink:href=\"%s\" xlink:type=\"simple\" "
			"xlink:show=\"embed\" xlink:actuate=\"onLoad\"/></draw:frame></text:p>\n",
			b->name);
}

//Dokument des Knotens: Text als Text, Bilder eingebettet, sonst Hinweis
static void odt_dokument(GString *c, GPtrArray *bilder, ExportDokumentCtx *dctx,
		const ExportEintrag *e) {
	GPtrArray *einheiten = export_dokument_einheiten(dctx, e, NULL);

	for (guint i = 0; i < einheiten->len; i++) {
		ExportEinheit *u = g_ptr_array_index(einheiten, i);

		if (u->typ == EXPORT_EINHEIT_BILD)
			odt_bild(c, bilder, u);
		else if (u->typ == EXPORT_EINHEIT_HINWEIS)
			odt_absatz(c, "Hinweis", u->text);
		else {
			gchar **zeilen = g_strsplit(u->text, "\n", -1);

			for (gchar **z = zeilen; *z; z++) {
				if (**z)
					odt_absatz(c, "Dokument", *z);
				else
					g_string_append(c, "<text:p text:style-name=\"Dokument\"/>\n");
			}
			g_strfreev(zeilen);
		}
	}

	g_ptr_array_unref(einheiten);
}

static void odt_content(GString *c, Projekt *zond, GPtrArray *eintraege,
		const ExportOptionen *opt, ExportDokumentCtx *dctx,
		GPtrArray *bilder) {
	g_string_append(c,
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<office:document-content" ODT_NAMESPACES ">\n"
			"<office:body><office:text>\n");

	if (zond->project_name && *zond->project_name)
		odt_absatz(c, "Title", zond->project_name);

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
			gint ebene = MIN(e->ebene, ODT_MAX_EBENE);

			g_string_append_printf(c,
					"<text:h text:style-name=\"Heading_20_%d\" text:outline-level=\"%d\">",
					ebene, ebene);
			odt_xml_zeile(c, kopf->str);
			g_string_append(c, "</text:h>\n");
		}
		g_string_free(kopf, TRUE);

		if (opt->pfad && e->pfad) {
			gchar *zeile = g_strdup_printf("Pfad: %s", e->pfad);

			odt_absatz(c, "Pfad", zeile);
			g_free(zeile);
		}

		if (opt->anbindung && e->datei) {
			gchar *zeile = e->anbindung ?
					g_strdup_printf("Datei: %s, %s", e->datei, e->anbindung) :
					g_strdup_printf("Datei: %s", e->datei);

			odt_absatz(c, "Anbindung", zeile);
			g_free(zeile);
		}

		if (opt->text && e->notiz && *e->notiz) {
			gchar **zeilen = g_strsplit(e->notiz, "\n", -1);

			for (gchar **z = zeilen; *z; z++) {
				if (**z)
					odt_absatz(c, "Notiz", *z);
				else
					g_string_append(c, "<text:p text:style-name=\"Notiz\"/>\n");
			}
			g_strfreev(zeilen);
		}

		if (opt->dokumente && e->datei)
			odt_dokument(c, bilder, dctx, e);
	}

	g_string_append(c, "</office:text></office:body>\n"
			"</office:document-content>\n");
}

static gint odt_zip_schreiben(const gchar *filename, const gchar *content,
		const gchar *styles, const gchar *manifest, GPtrArray *bilder,
		GError **error) {
	GArray *dateien = g_array_new(FALSE, FALSE, sizeof(ExportZipDatei));
	ExportZipDatei d = { 0 };
	gint rc = 0;

	//mimetype muss die erste Datei sein
	d = (ExportZipDatei ) { "mimetype", ODT_MIMETYPE, strlen(ODT_MIMETYPE),
					TRUE };
	g_array_append_val(dateien, d);
	d = (ExportZipDatei ) { "META-INF/manifest.xml", manifest, strlen(manifest),
					FALSE };
	g_array_append_val(dateien, d);
	d = (ExportZipDatei ) { "content.xml", content, strlen(content), FALSE };
	g_array_append_val(dateien, d);
	d = (ExportZipDatei ) { "styles.xml", styles, strlen(styles), FALSE };
	g_array_append_val(dateien, d);

	for (guint i = 0; i < bilder->len; i++) {
		OdtBild *b = g_ptr_array_index(bilder, i);

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

gint export_odt_schreiben(Projekt *zond, GPtrArray *eintraege,
		const ExportOptionen *opt, const gchar *filename, GError **error) {
	GString *content = g_string_new(NULL);
	GString *styles = g_string_new(NULL);
	GString *manifest = g_string_new(NULL);
	GPtrArray *bilder = g_ptr_array_new_with_free_func(odt_bild_free);
	ExportDokumentCtx *dctx = opt->dokumente ?
			export_dokument_ctx_new(zond) : NULL;
	gint rc = 0;

	odt_styles(styles);
	odt_content(content, zond, eintraege, opt, dctx, bilder);
	odt_manifest(manifest, bilder);

	rc = odt_zip_schreiben(filename, content->str, styles->str, manifest->str,
			bilder, error);

	export_dokument_ctx_free(dctx);
	g_ptr_array_unref(bilder);
	g_string_free(content, TRUE);
	g_string_free(styles, TRUE);
	g_string_free(manifest, TRUE);

	return rc;
}
