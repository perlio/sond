/*
 sond (sond_render_doc.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_render_doc.h"

#include <cairo.h>
#include <pango/pangocairo.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

#include "sond_log_and_error.h"
#include "sond_fileparts.h"
#include "sond_text_extract.h"

#define DEFAULT_RENDER_WIDTH 650

/**
 * Gibt ein RenderedDocument frei
 */
void sond_render_doc_free(RenderedDocument *doc) {
    if (!doc) return;

    if (doc->surface) {
        cairo_surface_destroy(doc->surface);
    }
    if (doc->searchable_text) {
        g_free(doc->searchable_text);
    }
    g_free(doc);
}

/* gdk_cairo_set_source_pixbuf ist in GTK4 als veraltet markiert, funktioniert aber in beiden */
static void set_source_pixbuf(cairo_t *cr, GdkPixbuf *pixbuf) {
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
    G_GNUC_END_IGNORE_DEPRECATIONS
}

/* Obergrenze für den Text, der an Pango geht. Base64-kodierte Anhänge im
 * Rohtext (z.B. eine als Plaintext erkannte Mail) ergeben extrem lange,
 * leerzeichen-/umbruchpunktfreie "Wörter" - dafür brauchen Pango/HarfBuzz
 * beim Zeilenumbruch/Shaping pathologisch lange Zeit (pango_layout_get_
 * pixel_size()). Die max_height-Begrenzung weiter unten greift erst NACH
 * dieser Berechnung und schützt daher nicht davor. Der Text wird deshalb
 * hier hart gekappt, BEVOR er an Pango geht - zentral statt in den einzelnen
 * Aufrufern (render_html/_plain_text/_doc/...), damit alle einheitlich
 * geschützt sind. searchable_text bleibt bei allen Aufrufern der volle,
 * ungekappte Text (wie bei der max_height-Kappung, die ebenfalls nur die
 * Anzeige, nicht die Volltextsuche einschränkt). */
#define SOND_RENDER_TEXT_MAX_CHARS 300000

static cairo_surface_t* render_text_to_surface(
        const char *text, PangoAttrList *attrs,
        int width, int max_height,
        int *out_height) {
    gchar *truncated_copy = NULL;

    if (text && g_utf8_strlen(text, -1) > SOND_RENDER_TEXT_MAX_CHARS) {
        const gchar *cut = g_utf8_offset_to_pointer(text,
                SOND_RENDER_TEXT_MAX_CHARS);
        truncated_copy = g_strndup(text, cut - text);
        text = truncated_copy;
    }

    /* Höhe berechnen */
    cairo_surface_t *tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *tmp_cr = cairo_create(tmp);
    PangoLayout *layout = pango_cairo_create_layout(tmp_cr);
    pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text, -1);
    if (attrs) pango_layout_set_attributes(layout, attrs);
    int pw, ph;
    pango_layout_get_pixel_size(layout, &pw, &ph);
    g_object_unref(layout);
    cairo_destroy(tmp_cr);
    cairo_surface_destroy(tmp);

    gboolean truncated = (ph > max_height) || (truncated_copy != NULL);
    if (ph > max_height) ph = max_height;
    int height = ph + 20;
    if (out_height) *out_height = truncated ? max_height + 1 : height;

    /* Rendern */
    cairo_surface_t *surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        LOG_WARN("render_text_to_surface: cairo_image_surface_create "
                 "fehlgeschlagen (width=%d height=%d): %s",
                 width, height,
                 cairo_status_to_string(cairo_surface_status(surface)));
        cairo_surface_destroy(surface);
        g_free(truncated_copy);
        return NULL;
    }

    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_paint(cr);

	if (truncated) {
		cairo_set_source_rgb(cr, 0.8, 0.0, 0.0);
		PangoLayout *hint = pango_cairo_create_layout(cr);
		pango_layout_set_text(hint,
				"[Darstellung abgeschnitten — Inhalt zu lang]", -1);
		cairo_move_to(cr, 10, 10);
		pango_cairo_show_layout(cr, hint);
		g_object_unref(hint);
	}

	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	layout = pango_cairo_create_layout(cr);
	pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
	pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_text(layout, text, -1);
	if (attrs) pango_layout_set_attributes(layout, attrs);
	cairo_move_to(cr, 10, truncated ? 30 : 10);
	pango_cairo_show_layout(cr, layout);
	g_object_unref(layout);

	cairo_destroy(cr);
    g_free(truncated_copy);
    return surface;
}

// === TYPE DETECTION ===

// === HTML RENDERING ===

/*
 * Text-Extraktion kommt aus sond_text_extract.c - dieselbe Funktion, die
 * auch beim Indizieren verwendet wird. Nur so bleiben die char_pos-
 * Offsets aus der Indexsuche und der hier angezeigte Text konsistent
 * (siehe sond_text_extract.h).
 */
static RenderedDocument* render_html(guchar const *buf, gsize size, int width) {
    GPtrArray *segs = sond_text_extract_html(buf, size);
    if (segs->len == 0) { g_ptr_array_unref(segs); return NULL; }

    SondTextSegment *seg = g_ptr_array_index(segs, 0);

    int height = 0;
    cairo_surface_t *surface = render_text_to_surface(
            seg->text, NULL, width, 30000, &height);
    if (!surface) { g_ptr_array_unref(segs); return NULL; }

    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = width;
    result->height = height;
    result->searchable_text = g_strdup(seg->text);
    result->type = DOC_TYPE_HTML;

    g_ptr_array_unref(segs);
    return result;
}

// === IMAGE RENDERING ===

static RenderedDocument* render_image(GBytes *bytes, int max_width) {
    gsize len;
    const guchar *data = g_bytes_get_data(bytes, &len);

    GError *error = NULL;
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();

    if (!gdk_pixbuf_loader_write(loader, data, len, &error)) {
    	LOG_WARN("Failed to load image: %s", error->message);
        g_error_free(error);
        g_object_unref(loader);
        return NULL;
    }

    gdk_pixbuf_loader_close(loader, NULL);
    GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);

    if (!pixbuf) {
        g_object_unref(loader);
        return NULL;
    }

    int img_width = gdk_pixbuf_get_width(pixbuf);
    int img_height = gdk_pixbuf_get_height(pixbuf);

    // Skaliere wenn zu breit
    double scale = 1.0;
    if (img_width > max_width) {
        scale = (double)max_width / img_width;
    }

    int display_width = (int)(img_width * scale);
    int display_height = (int)(img_height * scale);

    // Cairo Surface erstellen
    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, display_width, display_height);
    cairo_t *cr = cairo_create(surface);

    // Weißer Hintergrund
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Bild skalieren und zeichnen
    if (scale != 1.0) {
        cairo_scale(cr, scale, scale);
    }

    set_source_pixbuf(cr, pixbuf);
    cairo_paint(cr);

    cairo_destroy(cr);
    g_object_unref(loader);

    // RenderedDocument erstellen
    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = display_width;
    result->height = display_height;
    result->searchable_text = g_strdup("[Image - No searchable text]");
    result->type = DOC_TYPE_IMAGE;

    return result;
}


// === ODT/DOCX RENDERING (Generisch) ===

/*
 * Extraktion (ZIP/XML-Parsing) kommt aus sond_text_extract.c - dieselbe
 * Funktion, die auch beim Indizieren verwendet wird. out_attrs liefert
 * zusätzlich die Fett/Überschriften-Formatierung für die Anzeige, ohne
 * dass der Text dafür ein zweites Mal (potenziell abweichend) extrahiert
 * werden muss.
 */
static RenderedDocument* render_office_document(GBytes *bytes,
                                                 int width, DocumentType type) {
    gsize len;
    const guchar *data = g_bytes_get_data(bytes, &len);

    PangoAttrList *attr_list = NULL;
    GPtrArray *segs = (type == DOC_TYPE_ODT)
            ? sond_text_extract_odt(data, len, &attr_list)
            : sond_text_extract_docx(data, len, &attr_list);

    if (segs->len == 0) {
        LOG_WARN("render_office_document: Extraktion fehlgeschlagen (%s)",
                type == DOC_TYPE_ODT ? "ODT" : "DOCX");
        g_ptr_array_unref(segs);
        if (attr_list) pango_attr_list_unref(attr_list);
        return NULL;
    }

    SondTextSegment *seg = g_ptr_array_index(segs, 0);

    int height = 0;
    cairo_surface_t *surface = render_text_to_surface(
            seg->text, attr_list, width, 30000, &height);
    if (attr_list) pango_attr_list_unref(attr_list);
    if (!surface) { g_ptr_array_unref(segs); return NULL; }

    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = width;
    result->height = height;
    result->searchable_text = g_strdup(seg->text);
    result->type = type;

    g_ptr_array_unref(segs);
    return result;
}

// === PLAIN TEXT RENDERING ===

/*
 * Extraktion (Encoding-Korrektur + CRLF -> LF) kommt aus
 * sond_text_extract.c - dieselbe Funktion, die auch beim Indizieren
 * verwendet wird (siehe sond_text_extract.h). Vorher unterschied sich
 * diese Normalisierung von der beim Indizieren verwendeten, wodurch die
 * char_pos-Offsets aus der Indexsuche hier nicht mehr passten.
 */
static RenderedDocument* render_plain_text(guchar const *data, gsize len, int width) {
    GPtrArray *segs = sond_text_extract_plain(data, len);
    if (segs->len == 0) { g_ptr_array_unref(segs); return NULL; }

    SondTextSegment *seg = g_ptr_array_index(segs, 0);

    int height = 0;
    cairo_surface_t *surface = render_text_to_surface(
            seg->text, NULL, width, 30000, &height);
    if (!surface) { g_ptr_array_unref(segs); return NULL; }

    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = width;
    result->height = height;
    result->searchable_text = g_strdup(seg->text);
    result->type = DOC_TYPE_PLAIN;

    g_ptr_array_unref(segs);
    return result;
}

// === DOC RENDERING (Fallback) ===

static RenderedDocument* render_doc(int width) {
    GString *text = g_string_new("Microsoft Word DOC Format\n\n");
    g_string_append(text, "Dieses Format (.doc) kann nicht direkt dargestellt werden.\n\n");
    g_string_append(text, "Bitte konvertieren Sie die Datei in DOCX oder ODT.");

    char *text_str = g_string_free(text, FALSE);
    int height = 0;
    cairo_surface_t *surface = render_text_to_surface(
            text_str, NULL, width, 30000, &height);
    if (!surface) { g_free(text_str); return NULL; }

    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = width;
    result->height = height;
    result->searchable_text = text_str;
    result->type = DOC_TYPE_DOC;
    return result;
}

// === HAUPTFUNKTION ===

/**
 * Rendert einen Dokument-Stream zu einer Cairo Surface
 *
 * @param bytes GBytes mit Dokumentdaten
 * @param render_width Gewünschte Breite (0 = Standard 650px)
 * @return RenderedDocument oder NULL bei Fehler
 */
/*
 * Text (Header + Body) kommt komplett aus sond_text_extract.c
 * (sond_text_extract_gmessage) - dieselbe Funktion, die auch beim
 * Indizieren verwendet wird. Vorher bauten Indexer und Renderer den
 * E-Mail-Text getrennt und unterschiedlich formatiert auf (Indexer nur
 * Header ohne Body, Renderer Header+Body in anderer Reihenfolge), so
 * dass char_pos-Offsets aus der Indexsuche hier nie an der richtigen
 * Stelle landeten. Bild-Anhänge betreffen nur die Darstellung und werden
 * separat über sond_text_extract_gmessage_images() geholt.
 */
static RenderedDocument* render_gmessage(GBytes* bytes, int width) {
    if (!bytes) return NULL;
    if (width <= 0) width = DEFAULT_RENDER_WIDTH;

    gsize len;
    const guchar* data = g_bytes_get_data(bytes, &len);

    GPtrArray *segs = sond_text_extract_gmessage(data, len);
    if (segs->len == 0) {
        LOG_WARN("render_gmessage: Extraktion fehlgeschlagen oder leere Nachricht");
        g_ptr_array_unref(segs);
        return NULL;
    }
    SondTextSegment *seg = g_ptr_array_index(segs, 0);
    const gchar *full_text = seg->text;

    /* ---- Bild-Parts holen und als Pixbufs vorbereiten ---- */
    GPtrArray* images = sond_text_extract_gmessage_images(data, len);

    GPtrArray* image_surfaces = g_ptr_array_new_with_free_func(
            (GDestroyNotify)cairo_surface_destroy);
    GPtrArray* image_labels = g_ptr_array_new_with_free_func(g_free);

    for (guint i = 0; i < images->len; i++) {
        SondEmlImage* img = g_ptr_array_index(images, i);

        GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
        GError* perr = NULL;
        gdk_pixbuf_loader_write(loader, img->image_data, img->image_len, &perr);
        g_clear_error(&perr);
        gdk_pixbuf_loader_close(loader, NULL);

        GdkPixbuf* pb = gdk_pixbuf_loader_get_pixbuf(loader);
        if (!pb) {
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            GdkPixbufAnimation* anim = gdk_pixbuf_loader_get_animation(loader);
            if (anim)
                pb = gdk_pixbuf_animation_get_static_image(anim);
            G_GNUC_END_IGNORE_DEPRECATIONS
        }
        if (pb) {
            int iw = gdk_pixbuf_get_width(pb);
            int ih = gdk_pixbuf_get_height(pb);
            double scale = 1.0;
            if (iw > width - 20)
                scale = (double)(width - 20) / iw;
            int dw = (int)(iw * scale);
            int dh = (int)(ih * scale);
            cairo_surface_t* img_surf = cairo_image_surface_create(
                    CAIRO_FORMAT_ARGB32, dw, dh);
            cairo_t* ic = cairo_create(img_surf);
            cairo_scale(ic, scale, scale);
            set_source_pixbuf(ic, pb);
            cairo_paint(ic);
            cairo_destroy(ic);
            g_ptr_array_add(image_surfaces, img_surf);
            g_ptr_array_add(image_labels,
                    img->filename
                    ? g_strdup_printf("%s — %s", img->mime_type, img->filename)
                    : g_strdup(img->mime_type));
        } else {
            LOG_WARN("%s\nBild-Part konnte nicht geladen werden: %s",
                    __func__, img->mime_type ? img->mime_type : "?");
        }
        g_object_unref(loader);
    }
    g_ptr_array_unref(images);

    /* ---- PangoAttrList für Header (fett + Monospace bis zur ersten
     * Leerzeile) ---- */
    const char* sep_pos = strstr(full_text, "\n\n");
    guint header_end = sep_pos
            ? (guint)(sep_pos - full_text)
            : 0;

    PangoAttrList* attrs = pango_attr_list_new();
    if (header_end > 0) {
        PangoAttribute* bold = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
        bold->start_index = 0;
        bold->end_index   = header_end;
        pango_attr_list_insert(attrs, bold);

        PangoAttribute* mono = pango_attr_family_new("Monospace");
        mono->start_index = 0;
        mono->end_index   = header_end;
        pango_attr_list_insert(attrs, mono);
    }

    /* ---- Text rendern ---- */
    int height = 0;
    cairo_surface_t* surface = render_text_to_surface(
            full_text, attrs, width, 30000, &height);
    pango_attr_list_unref(attrs);
    if (!surface) {
        g_ptr_array_unref(segs);
        g_ptr_array_unref(image_surfaces);
        g_ptr_array_unref(image_labels);
        return NULL;
    }

    /* ---- Bilder unter Text anfügen ---- */
    int total_height = height;
    int img_gap = 12;

    for (guint i = 0; i < image_surfaces->len; i++) {
        cairo_surface_t* is = g_ptr_array_index(image_surfaces, i);
        total_height += img_gap + 18 + img_gap
                + cairo_image_surface_get_height(is);
    }

    if (image_surfaces->len > 0) {
        if (total_height > 30000) total_height = 30000;

        cairo_surface_t* combined = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, width, total_height);
        if (cairo_surface_status(combined) != CAIRO_STATUS_SUCCESS) {
            LOG_WARN("%s\ncombined surface fehlgeschlagen", __func__);
            cairo_surface_destroy(combined);
        } else {
            cairo_t* cc = cairo_create(combined);
            cairo_set_source_rgb(cc, 1.0, 1.0, 1.0);
            cairo_paint(cc);

            cairo_set_source_surface(cc, surface, 0, 0);
            cairo_paint(cc);
            cairo_surface_destroy(surface);

            int y_off = height;
            cairo_set_source_rgb(cc, 0.0, 0.0, 0.0);
            for (guint i = 0; i < image_surfaces->len; i++) {
                cairo_surface_t* is = g_ptr_array_index(image_surfaces, i);
                const char* lbl = g_ptr_array_index(image_labels, i);
                int iw = cairo_image_surface_get_width(is);
                int ih = cairo_image_surface_get_height(is);

                cairo_set_line_width(cc, 1.0);
                cairo_move_to(cc, 10, y_off + img_gap / 2.0);
                cairo_line_to(cc, width - 10, y_off + img_gap / 2.0);
                cairo_stroke(cc);
                y_off += img_gap;

                PangoLayout* lbl_layout = pango_cairo_create_layout(cc);
                pango_layout_set_text(lbl_layout, lbl, -1);
                PangoAttrList* lbl_attrs = pango_attr_list_new();
                PangoAttribute* lbl_bold = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                lbl_bold->start_index = 0;
                lbl_bold->end_index   = G_MAXUINT;
                pango_attr_list_insert(lbl_attrs, lbl_bold);
                pango_layout_set_attributes(lbl_layout, lbl_attrs);
                pango_attr_list_unref(lbl_attrs);
                cairo_move_to(cc, 10, y_off);
                pango_cairo_show_layout(cc, lbl_layout);
                g_object_unref(lbl_layout);
                y_off += 18;

                int x_off = (width - iw) / 2;
                if (x_off < 0) x_off = 0;
                cairo_set_source_surface(cc, is, x_off, y_off + img_gap / 2);
                cairo_paint(cc);
                y_off += img_gap + ih;

                cairo_set_source_rgb(cc, 0.0, 0.0, 0.0);

                if (y_off >= total_height) break;
            }

            cairo_destroy(cc);
            surface = combined;
            height  = total_height;
        }
    }

    g_ptr_array_unref(image_surfaces);
    g_ptr_array_unref(image_labels);

    RenderedDocument* result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width   = width;
    result->height  = height;
    result->searchable_text = g_strdup(full_text);
    result->type = DOC_TYPE_EML;

    g_ptr_array_unref(segs);
    return result;
}

RenderedDocument* sond_render_doc_new(GBytes *bytes, int render_width,
        SondFilePart *sfp) {
    if (!bytes) {
    	LOG_WARN("Invalid bytes");
        return NULL;
    }

    if (render_width <= 0) {
        render_width = DEFAULT_RENDER_WIDTH;
    }

    /* Dokumenttyp: wenn sfp bekannt, direkt daraus ableiten */
    DocumentType type;
    if (sfp && SOND_IS_FILE_PART_GMESSAGE(sfp)) {
        type = DOC_TYPE_EML;
    } else if (sfp && (SOND_IS_FILE_PART_PDF(sfp) || SOND_IS_FILE_PART_ZIP(sfp))) {
        type = DOC_TYPE_UNKNOWN; /* wird nicht hier gerendert */
    } else if (sfp && SOND_IS_FILE_PART_LEAF(sfp)) {
        /* MIME-Type des Leaf direkt auswerten — wurde bereits durch
         * mime_guess_content_type (libmagic) korrekt bestimmt */
        const gchar *mime = sond_file_part_leaf_get_mime_type(
                SOND_FILE_PART_LEAF(sfp));
        if (!mime)
            type = DOC_TYPE_PLAIN;
        else if (g_str_has_prefix(mime, "image/"))
            type = DOC_TYPE_IMAGE;
        else if (g_str_equal(mime, "text/html"))
            type = DOC_TYPE_HTML;
        else if (g_str_equal(mime, "application/vnd.oasis.opendocument.text"))
            type = DOC_TYPE_ODT;
        else if (g_str_equal(mime, "application/vnd.openxmlformats-officedocument.wordprocessingml.document"))
            type = DOC_TYPE_DOCX;
        else if (g_str_equal(mime, "application/msword"))
            type = DOC_TYPE_DOC;
        else if (g_str_equal(mime, "message/rfc822"))
            type = DOC_TYPE_EML;
        else if (g_str_has_prefix(mime, "text/"))
            type = DOC_TYPE_PLAIN;
        else
            type = DOC_TYPE_PLAIN; /* unbekannt → als Text darstellen */
    } else {
        /* kein sfp — sollte nicht vorkommen, Fallback: Plain */
        type = DOC_TYPE_PLAIN;
    }

    RenderedDocument *result = NULL;

    // Je nach Typ rendern
    switch (type) {
    case DOC_TYPE_HTML: {
        gsize len;
        const guchar *data = g_bytes_get_data(bytes, &len);
        /* Encoding-Erkennung (UTF-8 / <meta charset> / windows-1252-
         * Fallback) passiert in sond_text_extract_html() - identisch
         * zur Indizierung. */
        result = render_html(data, len, render_width);
        break;
    }
    case DOC_TYPE_IMAGE:
            result = render_image(bytes, render_width);
            break;

        case DOC_TYPE_ODT:
        case DOC_TYPE_DOCX:
            result = render_office_document(bytes, render_width, type);
            break;

        case DOC_TYPE_DOC:
            result = render_doc(render_width);
            break;

        case DOC_TYPE_PLAIN:
        case DOC_TYPE_UNKNOWN:
        default: {
            gsize len;
            const guchar *data = g_bytes_get_data(bytes, &len);
            result = render_plain_text(data, len, render_width);
            break;
        }

        case DOC_TYPE_EML:
            result = render_gmessage(bytes, render_width);
            break;
    }

    return result;
}

/**
 * Typ als String zurückgeben
 */
const char* sond_render_doc_type_string(DocumentType type) {
    switch (type) {
        case DOC_TYPE_HTML: return "HTML";
        case DOC_TYPE_ODT: return "ODT";
        case DOC_TYPE_DOCX: return "DOCX";
        case DOC_TYPE_DOC: return "DOC (Legacy)";
        case DOC_TYPE_IMAGE: return "Image";
        case DOC_TYPE_PLAIN: return "Plain Text";
        case DOC_TYPE_EML: return "E-Mail";
        case DOC_TYPE_UNKNOWN:
        default: return "Unknown";
    }
}

/* =========================================================================
 * HIGHLIGHT-RENDERING
 * ======================================================================= */

/* Faltet einen Text für die Suche: je Zeichen Zerlegung (NFD) ohne die
 * kombinierenden Akzent-Zeichen, dann Casefold (ü -> u, ß -> ss, Ä -> a).
 * Mit out_map (optional) kommt zu jedem Byte des Ergebnisses das
 * Byte-Offset des Originalzeichens, aus dem es stammt (zu verwenden mit
 * g_free(); text_len wird nicht angehängt, die Länge ist strlen(Ergebnis)).
 * Rückgabe: neu alloziert. */
static gchar*
fold_for_search(gchar const *text, gsize text_len, gsize **out_map) {
    GString *out = g_string_new(NULL);
    GArray  *map = out_map ? g_array_new(FALSE, FALSE, sizeof(gsize)) : NULL;
    gchar const *end = text + text_len;

    for (gchar const *p = text; p < end; ) {
        gchar const *next = g_utf8_next_char(p);
        gchar *nfd = NULL;
        GString *plain = g_string_new(NULL);
        gchar *folded = NULL;
        gsize off = (gsize)(p - text);

        if (next > end)
            next = end;

        nfd = g_utf8_normalize(p, (gssize)(next - p), G_NORMALIZE_NFD);
        for (gchar const *q = nfd ? nfd : ""; *q; q = g_utf8_next_char(q)) {
            gunichar c = g_utf8_get_char(q);

            if (g_unichar_combining_class(c) == 0)
                g_string_append_unichar(plain, c);
        }
        g_free(nfd);

        folded = g_utf8_casefold(plain->str, (gssize) plain->len);
        g_string_free(plain, TRUE);
        g_string_append(out, folded);
        for (gsize i = 0; map && i < strlen(folded); i++)
            g_array_append_val(map, off);
        g_free(folded);

        p = next;
    }

    if (out_map)
        *out_map = (gsize*) g_array_free(map, FALSE);

    return g_string_free(out, FALSE);
}

/*
 * Rendert wie sond_render_doc_new, übergibt aber zusätzlich
 * einen Highlight-Term und eine Byte-Position im Flat-Text des Dokuments.
 * Der Term wird im Pango-Layout gelb hinterlegt; der Viewer scrollt beim
 * Öffnen zur ersten Fundstelle.
 *
 * char_pos_in_doc  – Byte-Offset im extrahierten searchable_text
 *                    (wie er vom Index gespeichert wird).
 *                    -1  → einfach zum ersten Vorkommen von term springen.
 *
 * WICHTIG: sfp muss durchgereicht werden, sonst kann
 * sond_render_doc_new() den Dokumenttyp nicht bestimmen und
 * rendert JEDES Dokument (auch HTML/DOCX/ODT/E-Mail) als Rohtext - dann
 * stimmt weder die Darstellung noch der char_pos-Offset (das war vorher
 * der Fall: sfp wurde hier fest auf NULL gesetzt).
 */
RenderedDocument *
sond_render_doc_new_with_term(GBytes *bytes, SondFilePart *sfp, int render_width,
        const char *term, int char_pos_in_doc,
        int *out_highlight_y)
{
    if (!bytes || !term || !*term) return NULL;
    if (render_width <= 0) render_width = DEFAULT_RENDER_WIDTH;

    /* Erst normal rendern um den searchable_text zu bekommen */
    RenderedDocument *rd = sond_render_doc_new(bytes, render_width, sfp);
    if (!rd) return NULL;
    if (!rd->searchable_text) return rd; /* kein Text → unverändert zurück */

    /* ---------------------------------------------------------------
     * Trefferposition im searchable_text bestimmen
     * ------------------------------------------------------------- */
    const char *text    = rd->searchable_text;
    gsize       text_len = strlen(text);

    /* Groß-/Kleinschreibung und Akzente ignorieren (wie die Volltextsuche:
     * "Munchen" findet "München"): beide Seiten gefaltet. Die Länge ändert
     * sich dabei (ß -> ss, ü -> u), deshalb merkt sich text_map zu jedem
     * Byte des gefalteten Textes die Position im Original. */
    gsize *text_map = NULL;
    char  *text_cf  = fold_for_search(text, text_len, &text_map);
    char  *term_cf  = fold_for_search(term, strlen(term), NULL);
    gsize  cf_len   = strlen(text_cf);
    gsize  term_cf_len = strlen(term_cf);

    /* Startposition: ab char_pos_in_doc (Byte-Offset im Originaltext, wie im
     * Index gespeichert), sonst erstes Vorkommen */
    const char *hit = NULL;
    if (term_cf_len > 0) {
        gsize from = 0;

        if (char_pos_in_doc >= 0) {
            while (from < cf_len && text_map[from] < (gsize) char_pos_in_doc)
                from++;
            hit = strstr(text_cf + from, term_cf);
        }
        if (!hit) /* Fallback: erstes Vorkommen */
            hit = strstr(text_cf, term_cf);
    }

    if (!hit) {
        g_free(text_cf);
        g_free(term_cf);
        g_free(text_map);
        if (out_highlight_y) *out_highlight_y = 0;
        return rd;
    }

    /* zurück ins Original: Anfang des ersten und Ende des letzten Zeichens
     * des Treffers */
    gsize cf_start  = (gsize)(hit - text_cf);
    gsize cf_last   = cf_start + term_cf_len - 1;
    gsize cf_after  = cf_last + 1;
    while (cf_after < cf_len && text_map[cf_after] == text_map[cf_last])
        cf_after++;
    gsize byte_start = text_map[cf_start];
    gsize byte_end   = (cf_after < cf_len) ? text_map[cf_after] : text_len;
    g_free(text_cf);
    g_free(term_cf);
    g_free(text_map);

    /* ---------------------------------------------------------------
     * Surface neu mit Highlight-Attribut rendern
     * Wir bauen ein Pango-Layout auf dem original searchable_text
     * mit einem gelben Background-Attribut an byte_start..byte_end.
     * ------------------------------------------------------------- */
    int width  = rd->width;
    int height = rd->height;

    cairo_surface_t *surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);

    /* Weißer Hintergrund */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text, -1);

    /* Gelbes Highlight-Attribut */
    PangoAttrList *attrs = pango_attr_list_new();
    PangoAttribute *bg = pango_attr_background_new(
            0xFFFF, 0xEE00, 0x0000); /* RGB16: gelb */
    bg->start_index = (guint)byte_start;
    bg->end_index   = (guint)byte_end;
    pango_attr_list_insert(attrs, bg);
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);

    cairo_move_to(cr, 10, 10);
    pango_cairo_show_layout(cr, layout);

    /* Y-Position des Treffers für Scroll ermitteln */
    if (out_highlight_y) {
        PangoRectangle rect = {0};
        int idx = (int)byte_start;
        pango_layout_index_to_pos(layout, idx, &rect);
        *out_highlight_y = (int)(rect.y / PANGO_SCALE) + 10; /* +10 Rand */
    }

    g_object_unref(layout);
    cairo_destroy(cr);

    /* Alte Surface ersetzen */
    cairo_surface_destroy(rd->surface);
    rd->surface = surface;

    return rd;
}
