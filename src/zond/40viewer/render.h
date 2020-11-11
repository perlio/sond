#ifndef RENDER_H_INCLUDED
#define RENDER_H_INCLUDED

typedef struct _Pdf_Viewer PdfViewer;

typedef int gint;
typedef char gchar;


gint render_display_list_to_stext_page( fz_context* ctx, DocumentPage*, gchar** );

void render_page_thread( gpointer, gpointer );

void render_sichtbare_seiten( PdfViewer* );

void render_sichtbare_thumbs( PdfViewer* );

#endif // RENDER_H_INCLUDED
