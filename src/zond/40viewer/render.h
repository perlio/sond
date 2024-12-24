#ifndef RENDER_H_INCLUDED
#define RENDER_H_INCLUDED

#define ERROR_THREAD(x) { fprintf( stderr, "Thread error: %s\n", x); \
                          fz_drop_context( ctx ); return; }

typedef struct _Pdf_Viewer PdfViewer;
typedef struct _Pdf_Document_Page PdfDocumentPage;

typedef int gint;
typedef char gchar;

gint render_stext_page_from_display_list(fz_context *ctx, PdfDocumentPage*,
		gchar**);

void render_page_thread(gpointer, gpointer);

#endif // RENDER_H_INCLUDED
