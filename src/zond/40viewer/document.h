#ifndef DOCUMENT_H_INCLUDED
#define DOCUMENT_H_INCLUDED

typedef int gint;
typedef char gchar;

typedef struct _Pdf_Document_Page PdfDocumentPage;
typedef struct _Anbindung Anbindung;
typedef struct _Projekt Projekt;
typedef struct _Pdf_Viewer PdfViewer;


typedef struct _Displayed_Document
{
    ZondPdfDocument* zond_pdf_document;
    Anbindung* anbindung;
    struct _Displayed_Document* next;
} DisplayedDocument;


void document_free_displayed_documents( DisplayedDocument* );

DisplayedDocument* document_new_displayed_document( const gchar*,
        Anbindung*, gchar** );

DisplayedDocument* document_get_dd( PdfViewer*, gint, PdfDocumentPage**, gint*, gint* );

gint document_save_dd( DisplayedDocument*, gboolean, GError** );

#endif // DOCUMENT_H_INCLUDED
