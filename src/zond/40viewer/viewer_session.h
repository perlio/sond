#ifndef VIEWER_SESSION_H_INCLUDED
#define VIEWER_SESSION_H_INCLUDED

#include <glib-object.h>
#include <gio/gio.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

typedef struct _Pdf_Viewer PdfViewer;
typedef struct _Displayed_Document DisplayedDocument;

G_BEGIN_DECLS

/* Gemeinsamer Zustand aller Viewer-Fenster eines Programms (zond oder
 * eigenständiger Viewer) und Schnittstelle zum Host: das Programm, das die
 * Viewer einbettet, verbindet sich mit den Signalen, um Projektwissen
 * beizusteuern (Anbindungen, Datenbank, Index). Ohne verbundene Handler
 * arbeitet der Viewer für sich allein. Alle Signale kommen aus dem
 * Hauptthread. */
#define PDF_TYPE_VIEWER_SESSION pdf_viewer_session_get_type( )
G_DECLARE_FINAL_TYPE(PdfViewerSession, pdf_viewer_session, PDF, VIEWER_SESSION,
		GObject)

struct _PdfViewerSession {
	GObject parent_instance;

	fz_context *ctx; //gehört dem Aufrufer; auch für pv_clip
	GSettings *settings; //gehört dem Aufrufer (Schlüssel "zoom")
	GPtrArray *arr_pv; //PdfViewer*: alle offenen Viewer
	pdf_document *pv_clip; //Seiten-Zwischenablage
	guint state; //Modifier-Maske des letzten Klicks im Host (Alt: Sprung)
};

PdfViewerSession* pdf_viewer_session_new(fz_context *ctx, GSettings *settings);

/* Signale (Rückgabe gint: 0 = in Ordnung, -1 = Fehler mit error, 1 = nur bei
 * pages-deleting: abgelehnt, Begründung in error):
 *
 *   anbindung-requested (PdfViewer*, GError**) -> gint
 *      Doppelklick hat eine Anbindung (pv->anbindung) festgelegt, der Host
 *      legt sie an. Ohne Handler reagiert der Viewer auf Doppelklicks nicht.
 *   pages-deleting (PdfViewer*, GPtrArray* PdfDocumentPage*, GError**) -> gint
 *      Vor dem Löschen von Seiten: der Host darf ablehnen.
 *   save-started (PdfViewer*)
 *      Vor dem Speichern aller geänderten Abschnitte.
 *   save-prepare (PdfViewer*, DisplayedDocument*, GError**) -> gint
 *      Vor dem physischen Speichern eines Abschnitts (z.B. Transaktion
 *      beginnen). Bei -1 wird nicht gespeichert; der Handler hat dann schon
 *      selbst aufgeräumt.
 *   save-finished (PdfViewer*, DisplayedDocument*, gboolean ok, GError**) -> gint
 *      Nach dem Speichern des Abschnitts: ok = Erfolg (z.B. Commit) oder
 *      Misserfolg (z.B. Rollback).
 *   save-done (PdfViewer*)
 *      Alle Abschnitte erfolgreich gespeichert. */
gboolean pdf_viewer_session_wants_anbindung(PdfViewerSession*);

gint pdf_viewer_session_anbindung_requested(PdfViewerSession*, PdfViewer*,
		GError**);

gint pdf_viewer_session_pages_deleting(PdfViewerSession*, PdfViewer*,
		GPtrArray*, GError**);

void pdf_viewer_session_save_started(PdfViewerSession*, PdfViewer*);

gint pdf_viewer_session_save_prepare(PdfViewerSession*, PdfViewer*,
		DisplayedDocument*, GError**);

gint pdf_viewer_session_save_finished(PdfViewerSession*, PdfViewer*,
		DisplayedDocument*, gboolean ok, GError**);

void pdf_viewer_session_save_done(PdfViewerSession*, PdfViewer*);

G_END_DECLS

#endif // VIEWER_SESSION_H_INCLUDED
