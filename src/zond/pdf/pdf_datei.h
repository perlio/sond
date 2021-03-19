#ifndef PDF_DOCUMENT_H_INCLUDED
#define PDF_DOCUMENT_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PDF_TYPE_DOCUMENT pdf_document_get_type( )
G_DECLARE_DERIVABLE_TYPE (PdfDocument, pdf_document, PDF, DOCUMENT, GObject)


struct _PdfDocumentClass
{
    GObjectClass parent_class;
    GPtrArray* open_docs;
};


G_END_DECLS


#endif // PDF_DOCUMENT_H_INCLUDED
