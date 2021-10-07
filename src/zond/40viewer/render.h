#ifndef RENDER_H_INCLUDED
#define RENDER_H_INCLUDED

typedef struct _Pdf_Viewer PdfViewer;
typedef struct _Pdf_Document_Page PdfDocumentPage;

typedef int gint;
typedef char gchar;


gint render_display_list_to_stext_page( fz_context* ctx, PdfDocumentPage*, gchar** );

void render_page_thread( gpointer, gpointer );

#endif // RENDER_H_INCLUDED
