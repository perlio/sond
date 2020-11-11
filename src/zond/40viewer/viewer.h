#ifndef VIEWER_H_INCLUDED
#define VIEWER_H_INCLUDED


#include "../global_types.h"

typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _Projekt Projekt;

typedef int gint;
typedef char gchar;
typedef double gdouble;
typedef int gboolean;
typedef void* gpointer;


fz_rect viewer_get_displayed_rect_from_dd( DisplayedDocument*, gint );

fz_rect viewer_get_displayed_rect( PdfViewer*, gint );

void viewer_springen_zu_pos_pdf( PdfViewer*, PdfPos, gdouble );

void viewer_abfragen_sichtbare_seiten( PdfViewer*, gint*, gint* );

gboolean viewer_page_ist_sichtbar( PdfViewer*, gint );

void viewer_refresh_layouts( GPtrArray* );

void viewer_close_thread_pools( PdfViewer* );

void viewer_schliessen( PdfViewer* );

void viewer_insert_thumb( PdfViewer*, gint, fz_rect );

ViewerPage* viewer_new_viewer_page( PdfViewer* );

gint viewer_get_visible_thumbs( PdfViewer*, gint*, gint* );

gboolean viewer_thumb_ist_sichtbar( PdfViewer*, gint );

gint viewer_get_iter_thumb( PdfViewer*, gint, GtkTreeIter*, gchar** );

gint viewer_reload_document_page( PdfViewer*, Document*, gint, gint, gchar** );

gint viewer_foreach( GPtrArray*, Document*, gint, gint (*) (PdfViewer*, gint,
        gpointer, gchar**), gpointer, gchar** errmsg );

void viewer_init_thread_pools( PdfViewer* );

void viewer_display_document( PdfViewer*, DisplayedDocument* );

PdfViewer* viewer_start_pv( Projekt* );

#endif // VIEWER_H_INCLUDED
