/*
 zond (viewer_annot.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef VIEWER_ANNOT_H_INCLUDED
#define VIEWER_ANNOT_H_INCLUDED

gint viewer_annot_handle_delete(PdfViewer* pv, GError** error);

gint viewer_handle_annot_edit_closed(PdfViewer* pdfv, GtkWidget *popover, GError** error);

gint viewer_annot_handle_release_clicked_annot(PdfViewer* pv,
		PdfPunkt pdf_punkt, GError** error);

gint viewer_annot_create_markup(PdfViewer *pv, PdfPunkt pdf_punkt, GError **error);

#endif /* VIEWER_ANNOT_H_INCLUDED */
