#ifndef VIEWER_H_INCLUDED
#define VIEWER_H_INCLUDED


#include "../global_types.h"

typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _Projekt Projekt;
typedef struct _Pdf_Document_Page PdfDocumentPage;

typedef int gint;
typedef char gchar;
typedef double gdouble;
typedef int gboolean;
typedef void* gpointer;


void viewer_springen_zu_pos_pdf( PdfViewer*, PdfPos, gdouble );

void viewer_abfragen_sichtbare_seiten( PdfViewer*, gint*, gint* );

void viewer_close_thread_pool( PdfViewer* );

void viewer_thread_render( PdfViewer*, gint );

void viewer_einrichten_layout( PdfViewer* );

void viewer_insert_thumb( PdfViewer*, gint );

void viewer_display_document( PdfViewer*, DisplayedDocument*, gint, gint );

void viewer_save_and_close( PdfViewer* );

gint viewer_get_visible_thumbs( PdfViewer*, gint*, gint* );

gint viewer_get_iter_thumb( PdfViewer*, gint, GtkTreeIter* );

gboolean viewer_page_ist_sichtbar( PdfViewer*, gint );

gint viewer_foreach( GPtrArray*, PdfDocumentPage*, gint (*) (PdfViewer*, gint,
        gpointer, gchar**), gpointer, gchar** errmsg );

PdfViewer* viewer_start_pv( Projekt* );

#endif // VIEWER_H_INCLUDED
