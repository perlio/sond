#ifndef PDF_DOCUMENTPAGE_H_INCLUDED
#define PDF_DOCUMENTPAGE_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PDF_TYPE_DOCUMENTPAGE pdf_documentpage_get_type( )
G_DECLARE_DERIVABLE_TYPE (PdfDocumentpage, pdf_documentpage, PDF, DOCUMENTPAGE, GObject)


struct _PdfDocumentpageClass
{
    GObjectClass parent_class;
};



G_END_DECLS


#endif // PDF_DOCUMENTPAGE_H_INCLUDED
