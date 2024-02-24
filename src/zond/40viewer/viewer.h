#ifndef VIEWER_H_INCLUDED
#define VIEWER_H_INCLUDED

#define PAGE_SPACE 10
#define VIEWER_WIDTH 950
#define VIEWER_HEIGHT 1050

#define ANNOT_ICON_WIDTH 20
#define ANNOT_ICON_HEIGHT 20

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

typedef struct _Text_Occ
{
    gint page_act;
    GArray* arr_quad;
    gint index_act;
    gboolean not_found;
} TextOcc;
typedef struct _RenderResponse
{
    gint page;
    gint error;
    gchar* error_message;
} RenderResponse;

typedef struct _Pdf_Viewer
{
    Projekt* zond;

    const gchar* rel_path;

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
    GtkWidget* button_zeiger;
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

    //hier werden die Seitenzahlen gespeichert, die zwischen Click und letzter Mausbewgung liegen
    gint von_alt;
    gint bis_alt;

    PdfDocumentPageAnnot* clicked_annot;
    GtkWidget* annot_pop;
    GtkWidget* annot_label;

    GtkWidget* annot_pop_edit;
    GtkWidget* annot_textview;

    DisplayedDocument* dd;
    GPtrArray* arr_pages; //array von ViewerPage*

    TextOcc text_occ;

    GThreadPool* thread_pool_page;
    GArray* arr_rendered;
    GMutex mutex_arr_rendered;
    guint idle_source;
    gint count_active_thread;

    PageQuad highlight;
} PdfViewer;

typedef struct _Viewer_Page_New
{
    PdfViewer* pdfv;
    PdfDocumentPage* pdf_document_page;
    fz_rect crop;
    gint y_pos;
    GdkPixbuf* pixbuf_page;
    GdkPixbuf* pixbuf_thumb;
    GtkWidget* image_page;
    gint thread;
} ViewerPageNew;



void viewer_springen_zu_pos_pdf( PdfViewer*, PdfPos, gdouble );

void viewer_close_thread_pool_and_transfer( PdfViewer* );

void viewer_refresh_layout( PdfViewer*, gint );

ViewerPageNew* viewer_new_page( PdfViewer*, ZondPdfDocument*, gint );

void viewer_display_document( PdfViewer*, DisplayedDocument*, gint, gint );

void viewer_save_and_close( PdfViewer* );

gint viewer_get_iter_thumb( PdfViewer*, gint, GtkTreeIter* );

void viewer_transfer_rendered( PdfViewer*, gboolean );

gint viewer_render_stext_page_fast( fz_context*, PdfDocumentPage*, gchar** );

gint viewer_foreach( PdfViewer*, PdfDocumentPage*, gint (*) (PdfViewer*, gint,
        gpointer, gchar**), gpointer, gchar** errmsg );

PdfViewer* viewer_start_pv( Projekt* );

#endif // VIEWER_H_INCLUDED
