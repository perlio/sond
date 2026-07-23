/*
 sond (sond_text_extract.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SRC_SOND_TEXT_EXTRACT_H_
#define SRC_SOND_TEXT_EXTRACT_H_

/*
 * Gemeinsame Text-Extraktion für Indizierung UND Anzeige/Suche.
 *
 * Hintergrund: sond_index_search() liefert char_pos-Byte-Offsets, die
 * beim Indizieren (sond_index.c) aus dem extrahierten Flat-Text eines
 * Dokuments berechnet wurden. Der Renderer (sond_renderer.c) muss beim
 * Öffnen einer Fundstelle denselben Offset im selben Flat-Text
 * wiederfinden, um die Stelle korrekt zu markieren. Das funktioniert nur,
 * wenn BEIDE Seiten exakt denselben Text (byteidentisch) extrahieren.
 *
 * Deshalb gibt es hier für jeden unterstützten Dateityp genau EINE
 * Extraktionsfunktion, die von sond_index.c und sond_renderer.c
 * gleichermaßen aufgerufen wird. Zwei unabhängige Implementierungen
 * (wie ursprünglich) laufen sonst zwangsläufig auseinander (CRLF-
 * Behandlung, Encoding-Erkennung, E-Mail-Header-Format, ...).
 */

#include <glib.h>
#include <mupdf/fitz.h>
#include <pango/pango.h>

G_BEGIN_DECLS

/**
 * SondTextSegment:
 *
 * Ein zusammenhängender Textabschnitt eines Dokuments.
 *
 * @text:     Extrahierter Text, UTF-8, CRLF bereits nach LF normalisiert.
 * @page_nr:  Seitennummer (0-basiert). -1 wenn nicht zutreffend (nur PDF
 *            liefert echte Seiten - ein Segment pro Seite; alle anderen
 *            Typen liefern genau ein Segment mit page_nr = -1).
 * @char_pos: Byte-Offset dieses Segments im Gesamtdokument (bei PDF:
 *            Offset innerhalb des Flat-Texts aller vorherigen Seiten).
 */
typedef struct _SondTextSegment {
    gchar *text;
    gint   page_nr;
    gint   char_pos;
} SondTextSegment;

SondTextSegment* sond_text_segment_new(gchar *text, gint page_nr, gint char_pos);
void sond_text_segment_free(gpointer seg);

/* Logging-Callback, wie bereits an mehreren Stellen im Projekt verwendet
 * (z.B. sond_ocr.h, sond_index.h). NULL ist erlaubt - dann wird intern
 * auf LOG_WARN zurückgegriffen. */
typedef void (*SondLogFunc)(gpointer log_data, gchar const *format, ...);

/**
 * sond_text_extract_pdf:
 * @seite_von: erste zu extrahierende Seite (0-basiert), -1 = ab Seite 0
 * @seite_bis: letzte zu extrahierende Seite (0-basiert, inklusive),
 *             -1 = bis letzte Seite
 *
 * Ein Segment pro Seite (nur im Bereich [seite_von, seite_bis]), Text via
 * MuPDF stext (FZ_TEXT_FLATTEN_ALL, identisch zu fz_search_stext_page).
 */
GPtrArray* sond_text_extract_pdf(fz_context *ctx, guchar const *buf, gsize size,
        SondLogFunc log_func, gpointer log_data, gint seite_von, gint seite_bis);

/**
 * sond_text_extract_html:
 *
 * Ein Segment mit dem sichtbaren Text (lexbor). Encoding wird erkannt
 * (UTF-8 direkt, sonst charset= aus <meta>, sonst windows-1252-Fallback).
 */
GPtrArray* sond_text_extract_html(guchar const *buf, gsize size);

/**
 * sond_text_extract_plain:
 *
 * Ein Segment Rohtext. Encoding-Korrektur wie bei HTML (UTF-8/windows-
 * 1252-Fallback), CRLF -> LF normalisiert.
 */
GPtrArray* sond_text_extract_plain(guchar const *buf, gsize size);

/**
 * sond_text_extract_gmessage:
 *
 * Ein Segment = Header (Von/An/CC/BCC/Betreff/Datum) + Trennlinie +
 * Body aller inline Text-Parts (HTML-Teile werden über
 * sond_text_extract_html() zu Text geflacht, Text-Teile CRLF-
 * normalisiert wie sond_text_extract_plain()). Identische Funktion für
 * Index UND Anzeige - Bild-Parts siehe sond_text_extract_gmessage_images().
 */
GPtrArray* sond_text_extract_gmessage(guchar const *buf, gsize size);

/**
 * SondEmlImage: Bild-Anhang einer E-Mail, für die Anzeige im Renderer.
 * Betrifft nur die Darstellung, nicht den durchsuchbaren Text.
 */
typedef struct {
    gchar  *mime_type;
    gchar  *filename;   /* NULL wenn kein Dateiname bekannt */
    guchar *image_data;
    gsize   image_len;
} SondEmlImage;

void sond_eml_image_free(gpointer p);

GPtrArray* sond_text_extract_gmessage_images(guchar const *buf, gsize size);

/**
 * sond_text_extract_docx / sond_text_extract_odt:
 *
 * Ein Segment mit dem sichtbaren Text. out_attrs (optional, darf NULL
 * sein) liefert zusätzlich Formatierungshinweise (fett/Überschriften)
 * für die Anzeige - die Byte-Offsets darin beziehen sich auf denselben
 * Text wie das zurückgegebene Segment, weil beides in einem einzigen
 * Baumdurchlauf entsteht (kein zweiter, potenziell abweichender Durchlauf
 * nötig).
 */
GPtrArray* sond_text_extract_docx(guchar const *buf, gsize size, PangoAttrList **out_attrs);
GPtrArray* sond_text_extract_odt(guchar const *buf, gsize size, PangoAttrList **out_attrs);

G_END_DECLS

#endif /* SRC_SOND_TEXT_EXTRACT_H_ */
