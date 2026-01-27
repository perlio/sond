#ifndef GLOBAL_TYPES_H_INCLUDED
#define GLOBAL_TYPES_H_INCLUDED

#define ZOOM_MIN 10
#define ZOOM_MAX 400

#define EOP 99999

#define MAJOR "1" //wenn sich Struktur der .znd-Datei ändert
#define MINOR "0" //neues Feature
#define PATCH "0" //irjendwatt

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <glib-object.h>

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

typedef struct sqlite3_stmt sqlite3_stmt;
typedef struct sqlite3 sqlite3;

typedef struct _ZondDBase ZondDBase;

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

typedef struct _Icon {
	const gchar *icon_name;
	const gchar *display_name;
} Icon;

typedef struct _DBase_Zond {
	ZondDBase *zond_dbase_store;
	ZondDBase *zond_dbase_work;
	gboolean changed;
} DBaseZond;

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
	GtkWidget *projekt;
	GtkWidget *speichernitem;
	GtkWidget *schliessenitem;
	GtkWidget *exportitem;
	GtkWidget *item_search_fs;

	GtkWidget *pdf;

	GtkWidget *struktur;

	GtkWidget *ansicht;

	//Menuitem, mit dem Textview aus app-Fenster gelöst werden kann
	GtkWidget *textview_extra;

	GtkWidget *extras;

	GtkWidget *autosave;
};

typedef struct _Menu Menu;

typedef struct _Clipboard Clipboard;

typedef struct _Projekt {
	gchar *base_dir;
	gchar* exe_dir;

	gchar *project_dir;
	gchar *project_name;

#ifndef VIEWER
	guint state; //Modifier Mask
	Icon icon[NUMBER_OF_ICONS];

	GtkWidget *app_window;
	GtkWidget *textview_window;
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
	gint node_id_extra;
	gulong text_buffer_changed_signal;

	GtkWidget *textview;
	GtkWidget *textview_ii;

	DBaseZond *dbase_zond;

	Menu menu;
	GtkWidget *fs_button;

#endif //VIEWER
	fz_context *ctx;

	GSettings *settings;

	//Hier sind alle geöffneten PdfViewer abgelegt
	GPtrArray *arr_pv;
	GPtrArray* arr_dd;
	pdf_document *pv_clip;
	pdf_document* ocr_font; //Dokument soll nur tesseract-Font "f-0-0" beinhalten
} Projekt;

struct _Pdf_Pos {
	gint seite;
	gint index;
};

typedef struct _Pdf_Pos PdfPos;

struct _Anbindung {
	PdfPos von;
	PdfPos bis;
};

typedef struct _Anbindung Anbindung;

struct _Ziel {
	gchar *ziel_id_von;
	gint index_von;
	gchar *ziel_id_bis;
	gint index_bis;
};

typedef struct _Ziel Ziel;

typedef struct _Pdf_Punkt {
	gint seite;
	fz_point punkt;
	gdouble delta_y;
} PdfPunkt;

#endif // GLOBAL_TYPES_H_INCLUDED
