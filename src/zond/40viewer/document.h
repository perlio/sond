#ifndef DOCUMENT_H_INCLUDED
#define DOCUMENT_H_INCLUDED

typedef int gint;
typedef char gchar;

typedef struct _Displayed_Document DisplayedDocument;
typedef struct _Document Document;
typedef struct _Pdf_Document_Page PdfDocumentPage;
typedef struct _Anbindung Anbindung;
typedef struct _Projekt Projekt;
typedef struct _Pdf_Viewer PdfViewer;


void document_free_displayed_documents( DisplayedDocument* );

DisplayedDocument* document_new_displayed_document( const gchar*,
        Anbindung*, gchar** );

gint document_get_num_of_pages_of_dd( DisplayedDocument* );

DisplayedDocument* document_get_dd( PdfViewer*, gint, PdfDocumentPage**, gint*, gint* );


#endif // DOCUMENT_H_INCLUDED
