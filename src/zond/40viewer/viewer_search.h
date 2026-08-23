/*
 zond (viewer_search.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef VIEWER_SEARCH_H_INCLUDED
#define VIEWER_SEARCH_H_INCLUDED

typedef struct _Pdf_Viewer PdfViewer;
typedef struct _GtkWidget GtkWidget;
typedef struct _GError GError;
typedef int gint;
typedef char gchar;

/* Textsuche im angezeigten Dokument (vorwärts/rückwärts, seitenübergreifend) -
 * wird u.a. von den Vor/Zurück-Buttons und dem Such-Entry im Viewer benutzt. */
gint viewer_handle_text_search(PdfViewer* pv, GtkWidget *widget, GError **error);

/* Springt im Viewer zu einer über Zeichenposition (char_pos_in_page, aus dem
 * flachen Seitentext) adressierten Fundstelle von term und markiert sie -
 * wird von der Index-Suche (zond_indexsuche.c) benutzt, um von einem
 * Suchtreffer direkt in die geöffnete Seite zu springen. */
void viewer_highlight_at_char_pos(PdfViewer *pv, gint page_nr,
		gint char_pos_in_page, gchar const *term);

#endif // VIEWER_SEARCH_H_INCLUDED
