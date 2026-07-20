#ifndef ZOND_INIT_H_INCLUDED
#define ZOND_INIT_H_INCLUDED

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gio/gio.h>

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

typedef struct _GtkApplication GtkApplication;
typedef struct _GdkPixbuf GdkPixbuf;
typedef struct _GtkWidget GtkWidget;
typedef struct _GSettings GSettings;
typedef struct _GtkLabel GtkLabel;
typedef struct _GtkTreeView GtkTreeView;
typedef struct _GtkTreeSelection GtkTreeSelection;
typedef struct _GtkCellRenderer GtkCellRenderer;
typedef struct _GtkTextView GtkTextView;
typedef struct _GtkTextMark GtkTextMark;
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
typedef struct _DBase_Zond DBaseZond;

typedef struct sqlite3_stmt sqlite3_stmt;
typedef struct sqlite3 sqlite3;

typedef struct _ZondDBase ZondDBase;

typedef struct _SondProcessFileCtx SondProcessFileCtx;

typedef struct pdf_document pdf_document;

typedef int gboolean;
typedef char gchar;
typedef int gint;
typedef unsigned int guint;
typedef unsigned long gulong;
typedef void *gpointer;
typedef double gdouble;

typedef enum BAEUME {
	KEIN_BAUM = -1, BAUM_FS = 0, BAUM_INHALT, BAUM_AUSWERTUNG, NUM_BAUM
} Baum;

//gehört inhaltlich eher zum Viewer, wird aber auch von zond selbst (z.B.
//zond_dbase.c) als Werttyp gebraucht - deshalb hier in der gemeinsamen
//Basis statt in viewer.h definiert
typedef struct _Pdf_Pos {
	gint seite;
	gint index;
} PdfPos;

typedef struct _Anbindung {
	PdfPos von;
	PdfPos bis;
} Anbindung;

typedef struct _Icon {
	const gchar *icon_name;
	const gchar *display_name;
} Icon;

enum {
	ICON_NOTHING = 0,
	ICON_NORMAL,
	ICON_ORDNER,
	ICON_DATEI,
	ICON_PDF_FOLDER,
	ICON_PDF,
	ICON_ANBINDUNG,
	ICON_AKTE,
	ICON_EXE,
	ICON_TEXT,
	ICON_DOC,
	ICON_PPP,
	ICON_SPREAD,
	ICON_IMAGE,
	ICON_VIDEO,
	ICON_AUDIO,
	ICON_EMAIL,
	ICON_HTML, //16
	ICON_DURCHS = 25,
	ICON_ORT,
	ICON_PHONE,
	ICON_WICHTIG,
	ICON_OBS,
	ICON_CD,
	ICON_PERSON,
	ICON_PERSONEN,
	ICON_ORANGE,
	ICON_BLAU,
	ICON_ROT,
	ICON_GRUEN,
	ICON_TUERKIS,
	ICON_MAGENTA,
	NUMBER_OF_ICONS
};

struct _Menu {
	/* Window-level GSimpleActions fuer enable/disable */
	GSimpleAction *speichern;
	GSimpleAction *schliessen;
	GSimpleAction *export_odt;
	GSimpleAction *pdf;
	GSimpleAction *struktur;   /* Gruppe: alle Struktur-Actions */
	GSimpleAction *ansicht;    /* Gruppe: alle Ansicht-Actions */
	GSimpleAction *extras;
};

typedef struct _Menu Menu;

typedef struct _Projekt {
	gchar* exe_dir;

	fz_context *ctx;
	GPtrArray *arr_pv;

	GSettings *settings;

	pdf_document *pv_clip;

	gchar *project_dir;
	gchar *project_name;

	GtkApplication *app;

	SondProcessFileCtx *wctx;

	guint state; //Modifier Mask
	Icon icon[NUMBER_OF_ICONS];

	GtkWidget *app_window;
	GtkLabel *label_status;
	GtkWidget *label_project;
	GtkWidget *popover;

	GtkWidget *hpaned;
	//Baum - Modell, Ansicht mit Selection
	SondTreeview *treeview[3];
	GtkTreeSelection *selection[3];

	Baum baum_active;
	Baum baum_prev;

	gulong cursor_changed_signal;
	gint node_id_act;
	gulong text_buffer_changed_signal;

	GtkWidget *textview;
	GtkWidget *textview_pin_button;
	GtkWidget *textview_jump_button;
	gint node_id_textview;

	DBaseZond *dbase_zond;

	Menu menu;
	GtkWidget *fs_button;
} Projekt;

void zond_init(GtkApplication *app, Projekt *zond);

void zond_cleanup(Projekt* zond);

#endif // ZOND_INIT_H_INCLUDED
