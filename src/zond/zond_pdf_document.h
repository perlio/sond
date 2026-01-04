#ifndef ZOND_PDF_DOCUMENT_H_INCLUDED
#define ZOND_PDF_DOCUMENT_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

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

typedef struct _Pdf_Viewer PdfViewer;

typedef struct _Pdf_Document_Page {
	ZondPdfDocument *document; //erhält keine ref - muß das mal mit dem const kapieren...
	fz_rect rect;
	gint rotate;
	pdf_page *page;
	fz_display_list *display_list;
	fz_stext_page *stext_page;
	gint thread;
	PdfViewer* thread_pv;
	GPtrArray *arr_annots;
	gint page_akt;
	ZPDFDPart* inserted;
	gboolean deleted;
} PdfDocumentPage;

typedef struct _ZPDFD_Part {
	gint ref;
	ZondPdfDocument *zond_pdf_document;
	gboolean has_anbindung;
	PdfDocumentPage* first_page;
	gint first_index;
	PdfDocumentPage* last_page;
	gint last_index;
	gboolean dirty;
} ZPDFDPart;

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

void zond_pdf_document_page_free(PdfDocumentPage*);

Annot annot_deep_copy(Annot);

void annot_free(Annot*);

gint zond_pdf_document_page_load_annots(PdfDocumentPage*, GError**);

gint zond_pdf_document_load_page(PdfDocumentPage*, fz_context*, gchar**);

ZondPdfDocument* zond_pdf_document_open(SondFilePartPDF*, gint, gint, GError**);

//Gibt Zeiger auf geöffnetes document mit gchar* == path zurück; keine neue ref!
ZondPdfDocument* zond_pdf_document_is_open(SondFilePartPDF*);

void zond_pdf_document_unload_page(PdfDocumentPage*);

gint zond_pdf_document_save(ZondPdfDocument*, GError**);

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

void zpdfd_part_drop(ZPDFDPart*);

void zpdfd_part_get_anbindung(ZPDFDPart*, Anbindung*);

ZPDFDPart* zpdfd_part_ref(ZPDFDPart*);

ZPDFDPart* zpdfd_part_peek(SondFilePartPDF*, Anbindung*, GError** );

G_END_DECLS

#endif // ZOND_PDF_DOCUMENT_H_INCLUDED

