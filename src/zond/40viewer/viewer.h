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

typedef struct _Page_Quad
{
    gint page[1000];
    fz_quad quad[1000];
} PageQuad;

typedef struct _Pdf_Viewer
{
    Projekt* zond;

    GtkWidget* vf;
    GdkWindow* gdk_window;
    GdkCursor* cursor_text;
    GdkCursor* cursor_vtext;
    GdkCursor* cursor_default;
    GdkCursor* cursor_grab;
    GdkCursor* cursor_annot;
    GtkWidget* layout;
    GtkWidget* headerbar;
    GtkWidget* item_schliessen;
    GtkWidget* item_kopieren;
    GtkWidget* item_ausschneiden;
    GtkWidget* item_drehen;
    GtkWidget* item_einfuegen;
    GtkWidget* item_loeschen;
    GtkWidget* item_entnehmen;
    GtkWidget* item_ocr;
    GtkWidget* item_copy;

    GtkWidget* entry;
    GtkWidget* label_anzahl;
    GtkWidget* button_speichern;
    GtkWidget* button_anbindung;
    GtkWidget* button_vorher;
    GtkWidget* entry_search;
    GtkWidget* button_nachher;

    GtkAdjustment* v_adj;
    GtkAdjustment* h_adj;

    gdouble zoom;

    GtkWidget* swindow_tree;
    GtkWidget* tree_thumb;

    //gewähltes Werkzeug wird hier gespeichert
    gint state;

    //Bei Doppelklick wird hier die 1. und zweite PosPdf gespeichert
    Anbindung anbindung;

    //Position des Mauszeigers
    gdouble x;
    gdouble y;

    //Beim Klick
    gboolean click_on_text;
    PdfPunkt click_pdf_punkt;

    PdfDocumentPageAnnot* clicked_annot;
    GtkWidget* annot_pop;
    GtkWidget* annot_label;

    DisplayedDocument* dd;
    GPtrArray* arr_pages; //array von ViewerPage*
    GArray* arr_text_occ;
    gint text_occ_act;
    gint text_occ_search_completed;

    GThreadPool* thread_pool_page;
    GArray* arr_rendered;
    GMutex mutex_arr_rendered;

    PageQuad highlight;
} PdfViewer;


void viewer_springen_zu_pos_pdf( PdfViewer*, PdfPos, gdouble );

void viewer_abfragen_sichtbare_seiten( PdfViewer*, gint*, gint* );

void viewer_close_thread_pool( PdfViewer* );

void viewer_close_thread_pool_and_transfer( PdfViewer* );

void viewer_thread_render( PdfViewer*, gint );

void viewer_einrichten_layout( PdfViewer* );

void viewer_insert_thumb( PdfViewer*, gint );

void viewer_display_document( PdfViewer*, DisplayedDocument*, gint, gint );

void viewer_save_and_close( PdfViewer* );

gint viewer_get_visible_thumbs( PdfViewer*, gint*, gint* );

gint viewer_get_iter_thumb( PdfViewer*, gint, GtkTreeIter* );

gint viewer_foreach( GPtrArray*, PdfDocumentPage*, gint (*) (PdfViewer*, gint,
        gpointer, gchar**), gpointer, gchar** errmsg );

PdfViewer* viewer_start_pv( Projekt* );

#endif // VIEWER_H_INCLUDED
