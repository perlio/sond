/*
 sond (sond_render_doc.h) - Akten, Beweisstücke, Unterlagen
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

/**
 * @file sond_render_doc.h
 * @brief Dokument (HTML, Office, Bild, Text, E-Mail) in eine Cairo-Surface rendern, ohne Widgets
 */

#ifndef SOND_RENDER_DOC_H_INCLUDED
#define SOND_RENDER_DOC_H_INCLUDED

#include <glib.h>
#include <cairo.h>

typedef struct _SondFilePart SondFilePart;

typedef enum {
	DOC_TYPE_UNKNOWN,
	DOC_TYPE_HTML,
	DOC_TYPE_ODT,
	DOC_TYPE_DOCX,
	DOC_TYPE_DOC,
	DOC_TYPE_IMAGE,
	DOC_TYPE_PLAIN,
	DOC_TYPE_EML
} DocumentType;

typedef struct {
	cairo_surface_t *surface;
	int width;
	int height;
	char *searchable_text;
	DocumentType type;
} RenderedDocument;

/* Rendert bytes; der Typ ergibt sich aus sfp. render_width <= 0: Standardbreite. NULL bei Fehler. */
RenderedDocument* sond_render_doc_new(GBytes *bytes, int render_width, SondFilePart *sfp);

/* Wie sond_render_doc_new, der Treffer term wird gelb hinterlegt. char_pos_in_doc: Byte-Offset im
 * extrahierten Text (-1: erstes Vorkommen). out_highlight_y (optional): Y-Position des Treffers. */
RenderedDocument* sond_render_doc_new_with_term(GBytes *bytes, SondFilePart *sfp, int render_width,
		const char *term, int char_pos_in_doc, int *out_highlight_y);

void sond_render_doc_free(RenderedDocument *doc);

const char* sond_render_doc_type_string(DocumentType type);

#endif /* SOND_RENDER_DOC_H_INCLUDED */
