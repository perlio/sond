#ifndef GLOBAL_TYPES_H_INCLUDED
#define GLOBAL_TYPES_H_INCLUDED

#define ZOOM_MIN 10
#define ZOOM_MAX 400

#define EOP 99999

#include "enums.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <glib.h>


typedef struct _GdkPixbuf GdkPixbuf;
typedef struct _GtkWidget GtkWidget;
typedef struct _GSettings GSettings;
typedef struct _GtkLabel GtkLabel;
typedef struct _GtkTreeView GtkTreeView;
typedef struct _GtkTreeSelection GtkTreeSelection;
typedef struct _GtkCellRenderer GtkCellRenderer;
typedef struct _GtkTextView GtkTextView;
typedef struct _GArray GArray;
typedef struct _GPtrArray GPtrArray;
typedef struct _GdkWindow GdkWindow;
typedef struct _GdkCursor GdkCursor;
typedef struct _GtkAdjustment GtkAdjustment;
typedef struct _GList GList;
typedef struct _SondTreeview SondTreeview;
typedef struct _ViewerThumblist ViewerThumblist;
typedef struct _ZondPdfDocument ZondPdfDocument;
typedef struct _Pdf_Document_Page_Annot PdfDocumentPageAnnot;

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



struct _Menu
{
    GtkWidget* projekt;
    GtkWidget* speichernitem;
    GtkWidget* schliessenitem;
    GtkWidget* exportitem;

    GtkWidget* pdf;

    GtkWidget* struktur;

    GtkWidget* ansicht;

    GtkWidget* extras;

    GtkWidget* internal_vieweritem;
};

typedef struct _Menu Menu;


typedef struct _Clipboard Clipboard;


typedef struct _Projekt
{
#ifndef VIEWER
    guint state; //Modifier Mask

    Icon icon[NUMBER_OF_ICONS];

    GtkWidget* app_window;
    GtkLabel* label_status;
    GtkWidget* label_project;
    GtkWidget* popover;

    GtkWidget* hpaned;
    //Baum - Modell, Ansicht mit Selection
    SondTreeview* treeview[3];
    GtkTreeSelection* selection[3];

    Baum baum_active;
    Baum baum_prev;

    gulong cursor_changed_signal;
    gulong text_buffer_changed_signal;
    gulong treeview_focus_in_signal[3];
    gulong key_press_signal;

    GtkTextView* textview;

    DBaseZond* dbase_zond;

    Menu menu;
    GtkWidget* fs_button;

#endif //VIEWER
    fz_context* ctx;

    GSettings* settings;

    //Hier sind alle geöffneten PdfViewer abgelegt
    GPtrArray* arr_pv;
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


typedef struct _Displayed_Document
{
    ZondPdfDocument* zond_pdf_document;
    Anbindung* anbindung;
    struct _Displayed_Document* next;
} DisplayedDocument;


typedef struct _Text_Occ
{
    fz_quad quad;
    gint page;
} TextOcc;



#endif // GLOBAL_TYPES_H_INCLUDED
