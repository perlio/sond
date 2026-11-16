#ifndef ZOND_INIT_H_INCLUDED
#define ZOND_INIT_H_INCLUDED

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gio/gio.h>

#include "anbindung.h"
#include "zond_error.h"
#include "../sond_eingang_cache.h"


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
typedef struct _PdfViewerSession PdfViewerSession;
typedef struct _SondChatCtx SondChatCtx;

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
	GSimpleAction *struktur;   /* Gruppe: alle Struktur-Actions */
	GSimpleAction *ansicht;    /* Gruppe: alle Ansicht-Actions */
	GSimpleAction *extras;
	GSimpleAction *db_pruefen;
};

typedef struct _Menu Menu;

/* Anwendung: lebt, solange zond läuft */
typedef struct _ZondApp {
	gchar* exe_dir;

	fz_context *ctx;
	PdfViewerSession *viewer_session;

	GSettings *settings;

	GtkApplication *app;

	/* Chat-Modell (Generierung): anders als wctx/index_ctx nicht an ein
	 * offenes Projekt gebunden (dasselbe Modell bleibt beim Wechsel des
	 * Projekts nutzbar) - daher hier auf Anwendungsebene, lazy angelegt
	 * beim ersten "Chat"-Aufruf (siehe zond_chat.c) und in zond_cleanup()
	 * wieder freigegeben. NULL, solange noch nicht benutzt. */
	SondChatCtx *chat_ctx;

	Icon icon[NUMBER_OF_ICONS];
} ZondApp;

/* Projektzustand: gilt für das geöffnete Projekt (dbase_zond == NULL: keines) */
typedef struct _ZondProject {
	gchar *project_dir;
	gchar *project_name;

	DBaseZond *dbase_zond;

	SondProcessFileCtx *wctx;

	/* Eingangsangaben (Stempel) des Projekts: lokale Tabelle, aus den Zetteln
	 * in .sond/eingang der Library aufgebaut (s. sond_eingang_cache.h).
	 * NULL, wenn kein Projekt offen ist oder sich der Cache nicht anlegen ließ
	 * (dann zeigt der Dateibaum keine Eingangsangaben). */
	SondEingangCache *eingang_cache;

	//Schreibschutz und Verständigung mit sond_client (s. sond_zond_ctl.h)
	gboolean readonly; //Projekt schreibgeschützt: es wird nicht in die Projektdatei gespeichert
	guint ctl_source; //Takt, der die Steuerdateien auswertet (0 = keiner)
	gpointer ctl_data; //Benutzerdaten des Takts (Zeiger auf das Projekt)
	gboolean ctl_in_tick;
	gboolean ro_hinweis_gezeigt;
} ZondProject;

/* Hauptfenster */
typedef struct _ZondWindow {
	GtkWidget *app_window;
	GtkLabel *label_status;
	GtkWidget *label_project;
	GtkWidget *popover;

	GtkWidget *hpaned;
	GtkWidget *fs_button;

	Menu menu;

	guint state; //Modifier Mask
} ZondWindow;

/* Bäume und Notizfeld */
typedef struct _ZondTrees {
	//Baum - Modell, Ansicht mit Selection
	SondTreeview *treeview[3];
	GtkTreeSelection *selection[3];

	Baum baum_zuletzt; //zuletzt fokussierter Baum, s. zond_baum_aktuell()

	gulong cursor_changed_signal;
	gint node_id_act;
	gulong text_buffer_changed_signal;

	GtkWidget *textview;
	GtkWidget *textview_pin_button;
	GtkWidget *textview_jump_button;
	gint node_id_textview;
} ZondTrees;

typedef struct _Projekt {
	ZondApp app;
	ZondProject proj;
	ZondWindow win;
	ZondTrees trees;
} Projekt;

void zond_init(GtkApplication *app, Projekt *zond);

void zond_cleanup(Projekt* zond);

#endif // ZOND_INIT_H_INCLUDED
