#ifndef VIEWER_H_INCLUDED
#define VIEWER_H_INCLUDED

#define PAGE_SPACE 10
#define VIEWER_WIDTH 950
#define VIEWER_HEIGHT 1050

#ifndef ANNOT_ICON_WIDTH
#define ANNOT_ICON_WIDTH 20
#define ANNOT_ICON_HEIGHT 20
#endif

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gio/gio.h>

#include "../znd/anbindung.h"
#include "../znd/zond_error.h"
#include "viewer_session.h"

typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _GtkEntry GtkEntry;
typedef union _GdkEvent GdkEvent;
typedef struct _GtkWidget GtkWidget;
typedef struct _GdkWindow GdkWindow;
typedef struct _GdkCursor GdkCursor;
typedef struct _GdkPixbuf GdkPixbuf;
typedef struct _GtkAdjustment GtkAdjustment;
typedef struct _GtkAccelGroup GtkAccelGroup;
typedef struct _GSettings GSettings;
typedef struct _Pdf_Document_Page PdfDocumentPage;
typedef struct _ZPDFD_Part ZPDFDPart;
typedef struct _Pdf_Document_Page_Annot PdfDocumentPageAnnot;
typedef struct _Displayed_Document DisplayedDocument;
typedef int gint;
typedef char gchar;
typedef double gdouble;
typedef int gboolean;
typedef void *gpointer;

#define ZOOM_MIN 10
#define ZOOM_MAX 400

#define VIEWER_ERROR viewer_error_quark()
GQuark viewer_error_quark(void);

typedef struct _Page_Quad {
	gint page[1000];
	fz_quad quad[1000];
} PageQuad;

/* Ein Treffer = n aufeinanderfolgende Quads ab arr_quad[first] (mehrere bei
 * Treffern über Zeilenumbruch/Silbentrennung) */
typedef struct _Text_Hit {
	guint first;
	guint n;
} TextHit;

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


//Laufende, in Zeitscheiben (g_idle) abgearbeitete Suche über mehrere Seiten
typedef struct _Text_Search_Run {
	struct _Pdf_Viewer *pv;
	gint dir;
	PdfPos pos; //als nächstes zu durchsuchende Seite/Position
	PdfPunkt punkt; //Ausgangspunkt, um "einmal durch" zu erkennen
	gboolean wrapped;
	gchar *text;
	gint options; //fz_search_options
	guint source; //Idle-Source, 0 = noch nicht eingereiht
} TextSearchRun;

typedef struct _Text_Occ {
	TextSearchRun *run; //NULL, wenn keine Suche läuft
	gint page_act;
	GArray *arr_quad;
	GArray *arr_hit; //TextHit; index_act zählt Treffer, nicht Quads
	gint index_act;
	gboolean not_found;
} TextOcc;

typedef struct _Pdf_Viewer {
	PdfViewerSession *session;

	GtkWidget *widget;  //Kern: Seitenfläche, Vorschau und Werkzeugleiste, ohne Fenster
	GtkWidget *fenster; //Hülle (GtkWindow) um das Widget; NULL, wenn der Viewer eingebettet ist
	GtkAccelGroup *accel_group; //Kürzel Strg+C und Strg+X, am Fenster, in dem das Widget steckt
	GdkCursor *cursor_text;
	GdkCursor *cursor_vtext;
	GdkCursor *cursor_default;
	GdkCursor *cursor_grab;
	GdkCursor *cursor_annot;
	GtkWidget *layout;
	GtkWidget *headerbar;
	GtkWidget *menu_viewer; //Menü des Viewers, Host darf Einträge ergänzen
	GtkWidget *item_schliessen; //nur eigenständiger Viewer
	GtkWidget *item_kopieren;
	GtkWidget *item_ausschneiden;
	GtkWidget *item_drehen;
	GtkWidget *item_einfuegen;
	GtkWidget *item_loeschen;
	GtkWidget *item_ocr;
	GtkWidget *item_copy;

	GtkWidget *entry;
	GtkWidget *label_anzahl;
	GtkWidget *button_speichern;
	GtkWidget *button_zeiger;
	GtkWidget *button_anbindung;
	GtkWidget *button_vorher;
	GtkWidget *entry_search;
	GtkWidget *label_search; //Position des aktuellen Treffers
	GtkWidget *check_diakritika; //Umlaute/Akzente beim Suchen ignorieren
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
	guint idle_layout; //geplanter Neuaufbau des Layouts (seiten.c)
	guint idle_schliessen; //geplantes Schließen, wenn keine Seite mehr da ist (seiten.c)
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

/* Wie viewer_springen_zu_pos_pdf(), aber als g_idle-Callback eingereiht -
 * für den Fall, daß das Layout (viewer_refresh_layout()/viewer_create_
 * layout()) noch nicht fertig aufgebaut ist. Von viewer.c selbst benutzt
 * (viewer_display_document()) sowie von viewer_search.c
 * (viewer_highlight_at_char_pos()). */
void viewer_springen_zu_pos_pdf_idle(PdfViewer*, PdfPos, gdouble);

void viewer_refresh_layout(PdfViewer*, gint);

ViewerPageNew* viewer_new_page(PdfViewer*, DisplayedDocument*, gint);

void viewer_display_document(PdfViewer*, DisplayedDocument*, gint, gint);

void viewer_schliessen(PdfViewer*);

/* Vor dem Schließen ggf. fragen, ob gespeichert werden soll (falls button_speichern aktiv),
 * dann viewer_schliessen(). */
void viewer_save_and_close(PdfViewer*);

/* viewer_save_dirty_dds(): siehe viewer_save.h */

void viewer_get_iter_thumb(PdfViewer*, gint, GtkTreeIter*);

/* Fenster, in dem der Viewer angezeigt wird (Elternfenster für Meldungen, Dialoge, Dateiauswahl,
 * Druck); NULL, wenn der Viewer in keinem Fenster steckt. Nicht freigeben. */
GtkWidget* viewer_parent(PdfViewer*);

//Bringt das Fenster des Viewers nach vorn (ohne Fenster: nichts)
void viewer_fenster_praesentieren(PdfViewer*);

/* viewer_handle_text_search()/viewer_highlight_at_char_pos(): siehe
 * viewer_search.h */

void viewer_handle_page_entry_activated(PdfViewer* pv, GtkEntry *entry);

#endif // VIEWER_H_INCLUDED
