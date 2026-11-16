#ifndef VIEWER_MOUSE_H_INCLUDED
#define VIEWER_MOUSE_H_INCLUDED

#include "viewer.h"

gint viewer_abfragen_pdf_punkt(PdfViewer *pv, fz_point punkt,
		PdfPunkt *pdf_punkt);

void viewer_set_cursor(PdfViewer *pv, gint rc, ViewerPageNew *viewer_page,
		PdfDocumentPageAnnot *pdf_document_page_annot, PdfPunkt pdf_punkt);

void viewer_handle_layout_motion_notify(PdfViewer* pv, GdkEvent *event);

gint viewer_handle_button_press(PdfViewer* pv,
		GdkEvent *event, GError** error);

#endif // VIEWER_MOUSE_H_INCLUDED
