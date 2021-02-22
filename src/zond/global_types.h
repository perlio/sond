#ifndef GLOBAL_TYPES_H_INCLUDED
#define GLOBAL_TYPES_H_INCLUDED

#define ZOOM_MIN 10
#define ZOOM_MAX 400

#define DOCUMENT_PAGE(u) ((DocumentPage*) g_ptr_array_index( dd->document->pages, u ))
#define VIEWER_PAGE(u) ((ViewerPage*) g_ptr_array_index( pv->arr_pages, u ))

#define EOP 999999

#include "enums.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <glib.h>


typedef struct _GdkPixbuf GdkPixbuf;
typedef struct _GtkWidget GtkWidget;
typedef struct _GSettings GSettings;
typedef struct _GtkLabel GtkLabel;
typedef struct _GtkTreeView GtkTreeView;
typedef struct _GtkTreeStore GtkTreeStore;
typedef struct _GtkTreeSelection GtkTreeSelection;
typedef struct _GtkCellRenderer GtkCellRenderer;
typedef struct _GtkTextView GtkTextView;
typedef struct _GArray GArray;
typedef struct _GPtrArray GPtrArray;
typedef struct _GdkWindow GdkWindow;
typedef struct _GdkCursor GdkCursor;
typedef struct _GtkAdjustment GtkAdjustment;
typedef struct _GList GList;

typedef struct sqlite3_stmt sqlite3_stmt;
typedef struct sqlite3 sqlite3;

typedef struct pdf_document pdf_document;

typedef struct _DBase_Zond DBaseZond;
typedef struct _FM FM;

typedef int gboolean;
typedef char gchar;
typedef int gint;
typedef unsigned int guint;
typedef unsigned long gulong;
typedef void* gpointer;
typedef double gdouble;


typedef struct _Icon
{
    const gchar* icon_name;
    const gchar* display_name;
} Icon;



typedef struct _Info_Window
{
    GtkWidget* dialog;
    GtkWidget* content;
    gboolean cancel;
} InfoWindow;


struct _Menu
{
    GtkWidget* projekt;
    GtkWidget* speichernitem;
    GtkWidget* schliessenitem;
    GtkWidget* exportitem;

    GtkWidget* pdf;

    GtkWidget* struktur;

    GtkWidget* suchen;

    GtkWidget* ansicht;

    GtkWidget* extras;

    GtkWidget* internal_vieweritem;
};

typedef struct _Menu Menu;


typedef struct _STMTS
{
    sqlite3_stmt* db_speichern_textview[1];
    sqlite3_stmt* db_set_datei[1];
    sqlite3_stmt* ziele_einfuegen[1];
    sqlite3_stmt* db_kopieren_nach_auswertung[3];
    sqlite3_stmt* db_get_icon_name_and_node_text[2];
    sqlite3_stmt* db_remove_node[4];
    sqlite3_stmt* db_verschieben_knoten[6];
    sqlite3_stmt* db_get_parent[2];
    sqlite3_stmt* db_get_older_sibling[2];
    sqlite3_stmt* db_get_younger_sibling[2];
    sqlite3_stmt* db_get_ref_id[1];
    sqlite3_stmt* db_get_ziel[1];
    sqlite3_stmt* db_get_text[1];
    sqlite3_stmt* db_get_rel_path[1];
    sqlite3_stmt* db_set_icon_id[2];
    sqlite3_stmt* db_get_first_child[2];
    sqlite3_stmt* db_get_node_id_from_rel_path[1];
    sqlite3_stmt* db_check_id[1];
} STMTS;

typedef struct _Clipboard Clipboard;

typedef struct _Database
{
    sqlite3* db;
    sqlite3* db_store;
    STMTS stmts;
} Database;


typedef struct _Projekt
{
#ifndef VIEWER
    guint state; //Modifier Mask

    Icon icon[NUMBER_OF_ICONS];

    GtkWidget* app_window;
    GtkLabel* label_status;

    GtkWidget* hpaned;
    //Baum - Modell, Ansicht mit Selection
    GtkTreeView* treeview[3];
    GtkTreeSelection* selection[3];
    GtkCellRenderer* renderer_text[3];

    Baum last_baum;

    FM* fm;

    gulong cursor_changed_signal;
    gulong text_buffer_changed_signal;
    gulong treeview_focus_in_signal[3];

    GtkTextView* textview;

    Clipboard* clipboard;

    //Working-copy project
    sqlite3* db;

    Database* dbase;
    DBaseZond* dbase_zond;

    Menu menu;
    GtkWidget* fs_button;

#endif //VIEWER
    fz_context* ctx;

    GSettings* settings;

    //Hier sind alle geöffneten PdfViewer abgelegt
    GPtrArray* arr_pv;
    GPtrArray* arr_docs;
    pdf_document* pv_clip;
} Projekt;


struct _Pdf_Pos
{
    gint seite;
    gint index;
};

typedef struct _Pdf_Pos PdfPos;


struct _Anbindung
{
    PdfPos von;
    PdfPos bis;
};

typedef struct _Anbindung Anbindung;

struct _Ziel
{
    gchar* ziel_id_von;
    gint index_von;
    gchar* ziel_id_bis;
    gint index_bis;
};

typedef struct _Ziel Ziel;


typedef struct _Pdf_Punkt
{
    gint seite;
    fz_point punkt;
    gdouble delta_y;
} PdfPunkt;


typedef struct _PV_Quad PVQuad;

struct _PV_Quad
{
    fz_quad quad;
    PVQuad* next;
};

typedef struct _PV_Annot PVAnnot;

struct _PV_Annot
{
    PVAnnot* prev;
    PVAnnot* next;

    gint idx;
    enum pdf_annot_type type;

    fz_rect annot_rect;

    gint n_quad;
    PVQuad* first;
};

typedef struct _PV_Annot_Page
{
    gint n;
    PVAnnot* first;
    PVAnnot* last;
} PVAnnotPage;

typedef struct _Viewer_Page
{
    //fz_pixmap* pixmap;
    GtkWidget* image;
} ViewerPage;

typedef struct _Document Document;

typedef struct _Document_Page
{
    Document* document;
    fz_page* page;
    fz_rect rect;
    fz_display_list* display_list;
    fz_stext_page* stext_page;
    PVAnnotPage* pv_annot_page;
} DocumentPage;

struct _Document
{
    GMutex mutex_doc;
    fz_context* ctx;
    gint ref_count;
    fz_document* doc;
    gboolean dirty;
    gchar* path;
    GPtrArray* pages; //array von DocumentPage*
};


typedef struct _Displayed_Document
{
    Document* document;
    Anbindung* anbindung;
    struct _Displayed_Document* next;
} DisplayedDocument;


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
    PVAnnot* clicked_annot;
    PdfPunkt click_pdf_punkt;

    DisplayedDocument* dd;
    GPtrArray* arr_pages; //array von ViewerPage*

    GThreadPool* thread_pool_page;

    fz_quad highlight[1000];
    gint highlight_index;

    GArray* arr_text_treffer;
} PdfViewer;


#endif // GLOBAL_TYPES_H_INCLUDED
