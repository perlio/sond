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

gboolean viewer_annot_is_in_rect(Annot* annot, fz_rect rect);

gint viewer_annot_handle_delete(PdfViewer* pv, GError** error);

gint viewer_annot_handle_edit_closed(PdfViewer* pdfv, GtkWidget *popover, GError** error);

gint viewer_annot_do_change(fz_context* ctx, pdf_annot* pdf_annot, gint rotate,
		Annot annot, GError** error);

gint viewer_annot_handle_release_clicked_annot(PdfViewer* pv, ViewerPageNew* viewer_page,
		PdfPunkt pdf_punkt, GError** error);

pdf_annot* viewer_annot_do_create(fz_context* ctx, pdf_page* pdf_page, gint rotate,
		Annot annot, GError** error);

gint viewer_annot_create(ViewerPageNew *viewer_page, gchar **errmsg);

gint viewer_annot_create_markup(PdfViewer *pv, ViewerPageNew* viewer_page,
		PdfPunkt pdf_punkt, GError **error);

#endif /* VIEWER_ANNOT_H_INCLUDED */
