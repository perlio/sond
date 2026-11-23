#ifndef PDF_ANNOT_OPS_H_INCLUDED
#define PDF_ANNOT_OPS_H_INCLUDED

#include <glib.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "../zond_pdf_document.h"

//Größe des Symbols einer Text-Annotation in PDF-Einheiten
#ifndef ANNOT_ICON_WIDTH
#define ANNOT_ICON_WIDTH 20
#define ANNOT_ICON_HEIGHT 20
#endif

//Berührt der Zustand der Annotation das Rechteck?
gboolean pdf_annot_ops_is_in_rect(Annot* annot, fz_rect rect);

//Zustand annot in die MuPDF-Annotation schreiben
gint pdf_annot_ops_change(fz_context* ctx, pdf_annot* pdf_annot, gint rotate, Annot annot,
		GError** error);

/* Legt die Annotation auf der Seite an. *created (optional) ist TRUE, sobald sie physisch
 * existiert, auch wenn ein späterer Schritt scheitert (Rückgabe dann NULL). */
pdf_annot* pdf_annot_ops_create(fz_context* ctx, pdf_page* pdf_page, gint rotate, Annot annot,
		gboolean* created, GError** error);

#endif /* PDF_ANNOT_OPS_H_INCLUDED */
