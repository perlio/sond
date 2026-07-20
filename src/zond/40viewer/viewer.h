#ifndef VIEWER_H_INCLUDED
#define VIEWER_H_INCLUDED

#define PAGE_SPACE 10
#define VIEWER_WIDTH 950
#define VIEWER_HEIGHT 1050

#define ANNOT_ICON_WIDTH 20
#define ANNOT_ICON_HEIGHT 20

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gio/gio.h>

typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _GtkEntry GtkEntry;
typedef union _GdkEvent GdkEvent;
typedef struct _GtkWidget GtkWidget;
typedef struct _GdkWindow GdkWindow;
typedef struct _GdkCursor GdkCursor;
typedef struct _GdkPixbuf GdkPixbuf;
typedef struct _GtkAdjustment GtkAdjustment;
typedef struct _GSettings GSettings;
typedef struct _Pdf_Document_Page PdfDocumentPage;
typedef struct _Pdf_Document_Page_Annot PdfDocumentPageAnnot;
typedef struct _Displayed_Document DisplayedDocument;
typedef int gint;
typedef char gchar;
typedef double gdouble;
typedef int gboolean;
typedef void *gpointer;

#define ZOOM_MIN 10
#define ZOOM_MAX 400

#define EOP 99999

#define VIEWER_ERROR viewer_error_quark()
G_DEFINE_QUARK(viewer-error-quark, viewer_error)

typedef struct _Page_Quad {
	gint page[1000];
	fz_quad quad[1000];
} PageQuad;

typedef struct _Text_Occ {
	gint page_act;
	GArray *arr_quad;
	gint index_act;
	gboolean not_found;
} TextOcc;

typedef struct _RenderResponse {
	gint page_pv;
	gint error;
	gchar *error_message;
} RenderResponse;

typedef struct _Pdf_Punkt {
	gint seite;
	fz_point punkt;
	gdouble delta_y;
} PdfPunkt;

#ifdef VIEWER
typedef struct _Projekt {
    gchar        *base_dir;
    gchar        *exe_dir;
    fz_context   *ctx;
    GPtrArray    *arr_pv;
    pdf_document *pv_clip;
    GSettings* settings;
} Projekt;

#define ZOND_ERROR zond_error_quark()
G_DEFINE_QUARK(zond-error-quark,zond_error)

enum ZondError
{
	ZOND_ERROR_IO,
	ZOND_ERROR_JSON_NO_OBJECT,
	ZOND_ERROR_VTAG_NOT_FOUND,
	ZOND_ERROR_CURL,
	ZOND_ERROR_ZIP,
	NUM_ZOND_ERROR
};

//im eigenständigen "viewer"-Build (-DVIEWER) wird zond_init.h nicht
//eingebunden - PdfPos/Anbindung deshalb hier lokal für diesen Build
typedef struct _Pdf_Pos {
	gint seite;
	gint index;
} PdfPos;

typedef struct _Anbindung {
	PdfPos von;
	PdfPos bis;
} Anbindung;
#else
#include "../zond_init.h"
#endif

typedef struct _Pdf_Viewer {
	Projekt *zond;

	GtkWidget *vf;
	GdkWindow *gdk_window;
	GdkCursor *cursor_text;
	GdkCursor *cursor_vtext;
	GdkCursor *cursor_default;
	GdkCursor *cursor_grab;
	GdkCursor *cursor_annot;
	GtkWidget *layout;
	GtkWidget *headerbar;
	GtkWidget *item_schliessen;
	GtkWidget *item_kopieren;
	GtkWidget *item_ausschneiden;
	GtkWidget *item_drehen;
	GtkWidget *item_einfuegen;
	GtkWidget *item_loeschen;
	GtkWidget *item_entnehmen;
	GtkWidget *item_ocr;
	GtkWidget *item_copy;

	GtkWidget *entry;
	GtkWidget *label_anzahl;
	GtkWidget *button_speichern;
	GtkWidget *button_zeiger;
	GtkWidget *button_anbindung;
	GtkWidget *button_vorher;
	GtkWidget *entry_search;
	GtkWidget *button_nachher;

	GtkAdjustment *v_adj;
	GtkAdjustment *h_adj;

	gdouble zoom;

	GtkWidget *swindow_tree;
	GtkWidget *tree_thumb;

	gint state;

	Anbindung anbindung;

	gdouble x;
	gdouble y;

	gboolean click_on_text;
	PdfPunkt click_pdf_punkt;

	gint von_alt;
	gint bis_alt;

	PdfDocumentPageAnnot *clicked_annot;
	GtkWidget *annot_pop;
	GtkWidget *annot_label;

	GtkWidget *annot_pop_edit;
	GtkWidget *annot_textview;

	DisplayedDocument *dd;
	GPtrArray *arr_pages;

	TextOcc text_occ;

	GThreadPool *thread_pool_page;
	GArray *arr_rendered;
	GMutex mutex_arr_rendered;
	guint idle_source;
	gint count_active_thread;

	PageQuad highlight;
} PdfViewer;

typedef struct _Viewer_Page_New {
	PdfViewer *pdfv;
	DisplayedDocument* dd;
	PdfDocumentPage* pdf_document_page;
	fz_rect crop;
	gint y_pos;
	GdkPixbuf *pixbuf_page;
	GdkPixbuf *pixbuf_thumb;
	GtkWidget *image_page;
	gint thread;
} ViewerPageNew;

void viewer_springen_zu_pos_pdf(PdfViewer*, PdfPos, gdouble);

void viewer_refresh_layout(PdfViewer*, gint);

ViewerPageNew* viewer_new_page(PdfViewer*, DisplayedDocument*, gint);

void viewer_display_document(PdfViewer*, DisplayedDocument*, gint, gint);

void viewer_schliessen(PdfViewer*);

gint viewer_save_dirty_dds(PdfViewer*, GError**);

void viewer_save_and_close(PdfViewer*);

void viewer_get_iter_thumb(PdfViewer*, gint, GtkTreeIter*);

gint viewer_abfragen_pdf_punkt(PdfViewer *pv, fz_point punkt,
		PdfPunkt *pdf_punkt);

gint viewer_handle_text_search(PdfViewer* pv, GtkWidget *widget, GError **error);

void viewer_highlight_at_char_pos(PdfViewer *pv, gint page_nr,
		gint char_pos_in_page, gchar const *term);

void viewer_handle_page_entry_activated(PdfViewer* pv, GtkEntry *entry);

void viewer_set_cursor(PdfViewer *pv, gint rc, ViewerPageNew *viewer_page,
		PdfDocumentPageAnnot *pdf_document_page_annot, PdfPunkt pdf_punkt);

void viewer_foreach(PdfViewer*, PdfDocumentPage*,
		gint (*)(PdfViewer*, ViewerPageNew* viewer_page, gint, gpointer),
		gpointer);

void viewer_handle_layout_motion_notify(PdfViewer* pv, GdkEvent *event);

gint viewer_handle_button_press(PdfViewer* pv,
		GdkEvent *event, GError** error);

#endif // VIEWER_H_INCLUDED
