#ifndef VIEWER_PAGE_H_INCLUDED
#define VIEWER_PAGE_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>
#include <mupdf/fitz.h>

G_BEGIN_DECLS

#define ANNOT_ICON_WIDTH 20
#define ANNOT_ICON_HEIGHT 20

typedef struct _Pdf_Viewer PdfViewer;
typedef struct _Pdf_Document_Page PdfDocumentPage;
typedef struct _GdkPixbuf ViewerPixbuf;


#define VIEWER_TYPE_PAGE viewer_page_get_type( )
G_DECLARE_DERIVABLE_TYPE (ViewerPage, viewer_page, VIEWER, PAGE, GtkImage)


struct _ViewerPageClass
{
    GtkImageClass parent_class;
};


ViewerPage* viewer_page_new_full( PdfViewer*, PdfDocumentPage*, fz_rect );

PdfDocumentPage* viewer_page_get_document_page( ViewerPage* );

fz_rect viewer_page_get_crop( ViewerPage* );

void viewer_page_tilt( ViewerPage* );

void viewer_page_set_pixbuf_page( ViewerPage*, ViewerPixbuf* );

ViewerPixbuf* viewer_page_get_pixbuf_page( ViewerPage* );

void viewer_page_set_pixbuf_thumb( ViewerPage*, ViewerPixbuf* );

ViewerPixbuf* viewer_page_get_pixbuf_thumb( ViewerPage* );

fz_rect viewer_page_clamp_icon_rect( ViewerPage*, fz_rect );

G_END_DECLS


#endif // VIEWER_PAGE_H_INCLUDED
