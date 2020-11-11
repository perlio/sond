#ifndef DOCUMENT_H_INCLUDED
#define DOCUMENT_H_INCLUDED

typedef int gint;
typedef char gchar;

typedef struct _Displayed_Document DisplayedDocument;
typedef struct _Document Document;
typedef struct _Document_Page DocumentPage;
typedef struct _Anbindung Anbindung;
typedef struct _Projekt Projekt;
typedef struct _Pdf_Viewer PdfViewer;


gint document_insert_pages( Document*, gint, gint, gchar** );

Document* document_geoeffnet( Projekt*, const gchar* );

void document_free_displayed_documents( Projekt*, DisplayedDocument* );

DisplayedDocument* document_new_displayed_document( Projekt*, const gchar*,
        Anbindung*, gchar** );

gint document_get_num_of_pages_of_dd( DisplayedDocument* );

gint document_get_num_of_pages_of_pv( PdfViewer* );

DocumentPage* document_get_document_page_from_pv( PdfViewer*, gint );

DisplayedDocument* document_get_dd( PdfViewer*, gint, DocumentPage**, gint*, gint* );

gint document_get_page_dd( DisplayedDocument*, gint );

gint document_get_page_pv( PdfViewer*, DisplayedDocument*, gint );

gint document_get_index_of_document_page( DocumentPage* );

#endif // DOCUMENT_H_INCLUDED
