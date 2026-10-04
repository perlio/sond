#ifndef EXPORT_DOKUMENT_H_INCLUDED
#define EXPORT_DOKUMENT_H_INCLUDED

#include <glib.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "export_selection.h"
#include "export_pdf_seiten.h"

/* Inhalts-Schicht des Exports: macht aus dem angebundenen Dokument eines
 * Knotens Ausgabeeinheiten, die von jedem Zielformat dargestellt werden
 * können (Text, Bild, Hinweis). Öffnet jede Datei nur einmal. */

typedef struct _ExportDokumentCtx ExportDokumentCtx;

typedef enum {
	EXPORT_EINHEIT_TEXT,
	EXPORT_EINHEIT_BILD,
	EXPORT_EINHEIT_HINWEIS
} ExportEinheitTyp;

typedef struct _ExportEinheit {
	ExportEinheitTyp typ;
	gchar *text;        //TEXT, HINWEIS
	GBytes *bild;       //BILD: png oder jpeg
	const gchar *ext;   //BILD: "png" oder "jpg"
	gdouble breite_cm;  //BILD: natürliche Größe
	gdouble hoehe_cm;
} ExportEinheit;

void export_einheit_free(gpointer);

ExportDokumentCtx* export_dokument_ctx_new(Projekt *zond);
void export_dokument_ctx_free(ExportDokumentCtx *dctx);

/* Öffnet datei (einmal je Datei). Für eine PDF liefert *doc das Dokument
 * (Besitz beim Ctx bzw. beim Viewer), sonst NULL. Ist die PDF im Viewer
 * offen, wird dieses Dokument genommen (*zpdfd gesetzt, sonst NULL): es
 * enthält eingefügte Seiten und die Anbindungen werden auf seine
 * Seitenzählung umgerechnet. *hinweis (Besitz beim Ctx) ist gesetzt, wenn
 * die Datei nicht zu öffnen war. */
void export_dokument_oeffnen(ExportDokumentCtx *dctx, const gchar *datei,
		pdf_document **doc, ZondPdfDocument **zpdfd, const gchar **hinweis);

/* Seitenbereich der Anbindung von e in src (bei zpdfd != NULL in der
 * Live-Zählung des Viewers, gelöschte Seiten stehen in b->auslassen).
 * Keine Anbindung: ganze Datei. Ein Punkt steht für seine ganze Seite.
 * Rückgabe 0, 1 (Bereich außerhalb der Datei, *hinweis gesetzt, g_free())
 * oder -1. b danach mit export_dokument_bereich_clear() freigeben. */
gint export_dokument_bereich(fz_context *ctx, pdf_document *src,
		ZondPdfDocument *zpdfd, const ExportEintrag *e, ExportPdfBereich *b,
		gchar **hinweis, GError **error);

void export_dokument_bereich_clear(ExportPdfBereich *b);

/* Sperre des Viewer-Dokuments während des Lesens; bei zpdfd == NULL ohne
 * Wirkung */
void export_dokument_sperren(ZondPdfDocument *zpdfd);
void export_dokument_entsperren(ZondPdfDocument *zpdfd);

/* Ausgabeeinheiten des Dokuments von e (GPtrArray mit export_einheit_free).
 * Fehler beim Darstellen werden zu Hinweis-Einheiten; NULL nur bei
 * internen Fehlern. */
GPtrArray* export_dokument_einheiten(ExportDokumentCtx *dctx,
		const ExportEintrag *e, GError **error);

#endif // EXPORT_DOKUMENT_H_INCLUDED
