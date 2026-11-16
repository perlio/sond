#ifndef SEITEN_H_INCLUDED
#define SEITEN_H_INCLUDED

typedef struct _GtkMenuItem GtkMenuItem;

typedef void *gpointer;
typedef struct _GPtrArray GPtrArray;
typedef struct _Pdf_Viewer PdfViewer;
typedef char gchar;
typedef int gboolean;
typedef int gint;

/* Fragt die Seiten ab (Dialog); NULL bei Abbruch. winkel: nur beim Drehen.
 * Liefert PdfDocumentPage* (Freigabe mit g_ptr_array_unref). */
GPtrArray* seiten_abfrage_seiten(PdfViewer*, const gchar *title, gint *winkel,
		gboolean);

void cb_pv_seiten_loeschen(GtkMenuItem*, gpointer);

void cb_pv_seiten_einfuegen(GtkMenuItem*, gpointer);

void cb_pv_seiten_drehen(GtkMenuItem*, gpointer);

void cb_seiten_ausschneiden(GtkMenuItem*, gpointer);

void cb_seiten_kopieren(GtkMenuItem*, gpointer);

#endif // SEITEN_H_INCLUDED
