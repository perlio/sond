#ifndef ZOND_PDF_DOCUMENT_H_INCLUDED
#define ZOND_PDF_DOCUMENT_H_INCLUDED

#include <glib-object.h>

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

typedef struct _Anbindung Anbindung;
typedef struct _Displayed_Document DisplayedDocument;
typedef struct _SondFilePartPDF SondFilePartPDF;
typedef struct _ZPDFD_Part ZPDFDPart;
typedef struct fz_context fz_context;

G_BEGIN_DECLS

#define ZOND_TYPE_PDF_DOCUMENT zond_pdf_document_get_type( )
G_DECLARE_DERIVABLE_TYPE(ZondPdfDocument, zond_pdf_document, ZOND, PDF_DOCUMENT,
		GObject)


typedef struct _Pdf_Document_Page {
	ZondPdfDocument *document; //erhält keine ref - muß das mal mit dem const kapieren...
	fz_rect rect;
	gint rotate;
	pdf_page *page;
	fz_display_list *display_list;
	fz_stext_page *stext_page;
	gint thread;
	gpointer thread_pv; //PdfViewer, der gerade rendert (nur Viewer)
	GPtrArray *arr_annots;
	gint page_akt;
	ZPDFDPart* inserted;
	gboolean deleted;
	/* Nur relevant, wenn deleted == TRUE: ob diese Seite beim letzten
	 * physischen Speichern bereits aus der Datei ausgeschlossen wurde
	 * (auf der Platte also definitiv nicht mehr existiert) - unabhängig
	 * davon, ob sie live/im Speicher (arr_pages) noch als "Karteileiche"
	 * herumliegt (das Live-Aufräumen ist reines Best-Effort und nicht
	 * mehr korrektheitsrelevant). Wird von anbindung_get_orig() gebraucht,
	 * um die Live-Numerierung (page_akt) korrekt auf die Numerierung der
	 * jeweils frisch von der Platte geöffneten Speicher-Kopie
	 * abzubilden - s. dort. */
	gboolean on_disk_deleted;
} PdfDocumentPage;

/* Abschnitt eines Dokuments (von mehreren Viewern geteilt). Ein GObject, damit Viewer die Signale
 * ihrer Abschnitte abonnieren können; die Felder sind öffentlich. */
#define ZOND_TYPE_PDF_PART zond_pdf_part_get_type( )
GType zond_pdf_part_get_type(void);

typedef struct _ZPDFD_PartClass {
	GObjectClass parent_class;
} ZPDFDPartClass;

struct _ZPDFD_Part {
	GObject parent;
	ZondPdfDocument *zond_pdf_document;
	gboolean has_anbindung;
	PdfDocumentPage* first_page;
	gint first_index;
	PdfDocumentPage* last_page;
	gint last_index;
	gboolean dirty;
};

/* Art der Änderung für das Signal "page-changed" */
typedef enum _ZPDFD_Part_Change {
	ZPDFD_PART_CHANGE_ROTATED,
	ZPDFD_PART_CHANGE_DELETED,
	ZPDFD_PART_CHANGE_INSERTED,
	ZPDFD_PART_CHANGE_OCR
} ZPDFDPartChange;

typedef struct _Annot_Text_Markup {
	GArray *arr_quads;
} AnnotTextMarkup;

typedef struct _Annot_Text {
	fz_rect rect;
	gboolean open;
	gchar* content;
} AnnotText;

typedef struct _Annot {
	enum pdf_annot_type type;
	union {
		AnnotTextMarkup annot_text_markup;
		AnnotText annot_text;
	};
}Annot;

typedef struct _Pdf_Document_Page_Annot {
	PdfDocumentPage *pdf_document_page; //keine ref!
	gboolean inserted;
	gboolean deleted;
	gboolean on_disk_deleted; //wie PdfDocumentPage->on_disk_deleted, s. dort
	gboolean changed;
	Annot annot;
} PdfDocumentPageAnnot;

struct PagesInserted {
	gint count;
	ZPDFDPart* zpdfd_part;
};
struct AnnotChanged {
	PdfDocumentPageAnnot* pdf_document_page_annot;
	Annot annot_before;
	Annot annot_after;
};
struct Rotate {
	gint winkel;
};
struct OCR {
	fz_buffer* buf_old;
	fz_buffer* buf_new;
};

typedef enum _Journal_Type {
	JOURNAL_TYPE_PAGES_INSERTED,
	JOURNAL_TYPE_PAGE_DELETED,
	JOURNAL_TYPE_ANNOT_CREATED,
	JOURNAL_TYPE_ANNOT_DELETED,
	JOURNAL_TYPE_ANNOT_CHANGED,
	JOURNAL_TYPE_ROTATE,
	JOURNAL_TYPE_OCR
} JournalType;

typedef struct _Journal_Entry {
	PdfDocumentPage* pdf_document_page;
	JournalType type;
	union {
		struct PagesInserted pages_inserted;
		struct AnnotChanged annot_changed;
		struct Rotate rotate;
		struct OCR ocr;
	};
} JournalEntry;

typedef struct _SondFilePart SondFilePart;

struct _ZondPdfDocumentClass {
	GObjectClass parent_class;

	GPtrArray* arr_pdf_documents;
};

gint pdf_document_page_annot_get_index(PdfDocumentPageAnnot*);

pdf_annot* pdf_document_page_annot_get_pdf_annot(PdfDocumentPageAnnot*);

pdf_obj* pdf_document_page_get_page_obj(PdfDocumentPage*, GError**);

Annot annot_deep_copy(Annot);

void annot_free(Annot*);

gint zond_pdf_document_page_load_annots(PdfDocumentPage*, GError**);

gint zond_pdf_document_load_page(PdfDocumentPage*, fz_context*, GError**);

//Gibt Zeiger auf geöffnetes document mit gchar* == path zurück; keine neue ref!
ZondPdfDocument* zond_pdf_document_is_open(SondFilePartPDF*);

//Wie oben, aber Suche über Dateiname statt SondFilePartPDF*; keine neue ref!
ZondPdfDocument* zond_pdf_document_find_by_filename(const gchar*);

void zond_pdf_document_close(ZondPdfDocument*);

pdf_document* zond_pdf_document_get_pdf_doc(ZondPdfDocument*);

GPtrArray* zond_pdf_document_get_arr_pages(ZondPdfDocument const*);

GArray* zond_pdf_document_get_arr_journal(ZondPdfDocument const*);

PdfDocumentPage* zond_pdf_document_get_pdf_document_page(ZondPdfDocument*, gint);

gint zond_pdf_document_get_number_of_pages(ZondPdfDocument*);

fz_context* zond_pdf_document_get_ctx(ZondPdfDocument*);

SondFilePartPDF* zond_pdf_document_get_sfp_pdf(ZondPdfDocument*);

void zond_pdf_document_mutex_lock(const ZondPdfDocument*);

void zond_pdf_document_mutex_unlock(const ZondPdfDocument*);

gint zond_pdf_document_insert_pages(ZondPdfDocument*, gint, ZPDFDPart*,
		pdf_document*, GError**);

gint zond_pdf_document_get_ocr_num(ZondPdfDocument*);

void zond_pdf_document_set_ocr_num(ZondPdfDocument*, gint );

/* Signale eines Abschnitts (ZPDFDPart):
 *   "page-changed" (PdfDocumentPage*, ZPDFDPartChange): Seite des Abschnitts gedreht, gelöscht,
 *                  eingefügt oder per OCR verändert
 *   "annot-changed" (PdfDocumentPage*, Annot*): Annotation einer Seite des Abschnitts geändert
 *   "dirty-changed": dirty hat sich geändert
 * Gesendet werden sie vom Dokument über die zond_pdf_document_notify_*-Funktionen an alle
 * Abschnitte, die die Seite umfassen. */

//Liegt die Seite im Seitenbereich des Abschnitts (Randseiten eingeschlossen)?
gboolean zpdfd_part_umfasst_seite(ZPDFDPart*, PdfDocumentPage*);

//Setzt dirty; sendet "dirty-changed" nur bei einer Änderung
void zpdfd_part_set_dirty(ZPDFDPart*, gboolean);

void zond_pdf_document_notify_page_changed(ZondPdfDocument*, PdfDocumentPage*, ZPDFDPartChange);

void zond_pdf_document_notify_annot_changed(ZondPdfDocument*, PdfDocumentPage*, Annot*);

void zpdfd_part_drop(ZPDFDPart*);

void zpdfd_part_get_anbindung(ZPDFDPart*, Anbindung*);

ZPDFDPart* zpdfd_part_ref(ZPDFDPart*);

ZPDFDPart* zpdfd_part_peek(SondFilePartPDF*, Anbindung*, GError** );

void anbindung_get_orig(ZondPdfDocument*, Anbindung*);

/* Live-Numerierung in die Zählung, die der Viewer anzeigt (ohne gelöschte Seiten) */
void anbindung_get_sichtbar(ZondPdfDocument*, Anbindung*);

void anbindung_korrigieren(ZPDFDPart*, Anbindung*);

void anbindung_aktualisieren(ZondPdfDocument*, Anbindung*);

G_END_DECLS

#endif // ZOND_PDF_DOCUMENT_H_INCLUDED

