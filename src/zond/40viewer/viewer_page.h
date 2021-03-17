#ifndef VIEWER_PAGE_H_INCLUDED
#define VIEWER_PAGE_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _Pdf_Viewer PdfViewer;


#define VIEWER_TYPE_PAGE viewer_page_get_type( )
G_DECLARE_DERIVABLE_TYPE (ViewerPage, viewer_page, VIEWER, PAGE, GtkImage)


struct _ViewerPageClass
{
    GtkImageClass parent_class;
};


ViewerPage* viewer_page_new( PdfViewer* );

GArray* viewer_page_get_arr_text_found( ViewerPage* );

void viewer_page_empty_arr_text_found( ViewerPage* );

G_END_DECLS


#endif // VIEWER_PAGE_H_INCLUDED
