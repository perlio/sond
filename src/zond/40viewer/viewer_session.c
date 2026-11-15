#include "viewer_session.h"

G_DEFINE_TYPE(PdfViewerSession, pdf_viewer_session, G_TYPE_OBJECT)

enum {
	SIGNAL_ANBINDUNG_REQUESTED,
	SIGNAL_PAGES_DELETING,
	SIGNAL_SAVE_STARTED,
	SIGNAL_SAVE_PREPARE,
	SIGNAL_SAVE_FINISHED,
	SIGNAL_SAVE_DONE,
	NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0 };

static void pdf_viewer_session_finalize(GObject *self) {
	PdfViewerSession *session = PDF_VIEWER_SESSION(self);

	if (session->pv_clip && session->ctx)
		pdf_drop_document(session->ctx, session->pv_clip);

	g_ptr_array_unref(session->arr_pv);

	G_OBJECT_CLASS(pdf_viewer_session_parent_class)->finalize(self);

	return;
}

static void pdf_viewer_session_init(PdfViewerSession *self) {
	self->arr_pv = g_ptr_array_new();

	return;
}

static void pdf_viewer_session_class_init(PdfViewerSessionClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = pdf_viewer_session_finalize;

	signals[SIGNAL_ANBINDUNG_REQUESTED] = g_signal_new("anbindung-requested",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
			g_signal_accumulator_first_wins, NULL, NULL, G_TYPE_INT, 2,
			G_TYPE_POINTER, G_TYPE_POINTER);

	signals[SIGNAL_PAGES_DELETING] = g_signal_new("pages-deleting",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
			g_signal_accumulator_first_wins, NULL, NULL, G_TYPE_INT, 3,
			G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_POINTER);

	signals[SIGNAL_SAVE_STARTED] = g_signal_new("save-started",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 1, G_TYPE_POINTER);

	signals[SIGNAL_SAVE_PREPARE] = g_signal_new("save-prepare",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
			g_signal_accumulator_first_wins, NULL, NULL, G_TYPE_INT, 3,
			G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_POINTER);

	signals[SIGNAL_SAVE_FINISHED] = g_signal_new("save-finished",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
			g_signal_accumulator_first_wins, NULL, NULL, G_TYPE_INT, 4,
			G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_BOOLEAN, G_TYPE_POINTER);

	signals[SIGNAL_SAVE_DONE] = g_signal_new("save-done",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 1, G_TYPE_POINTER);

	return;
}

PdfViewerSession* pdf_viewer_session_new(fz_context *ctx, GSettings *settings) {
	PdfViewerSession *session = g_object_new(PDF_TYPE_VIEWER_SESSION, NULL);

	session->ctx = ctx;
	session->settings = settings;

	return session;
}

gboolean pdf_viewer_session_wants_anbindung(PdfViewerSession *session) {
	return g_signal_has_handler_pending(session,
			signals[SIGNAL_ANBINDUNG_REQUESTED], 0, TRUE);
}

gint pdf_viewer_session_anbindung_requested(PdfViewerSession *session,
		PdfViewer *pv, GError **error) {
	gint rc = 0;

	g_signal_emit(session, signals[SIGNAL_ANBINDUNG_REQUESTED], 0, pv, error,
			&rc);

	return rc;
}

gint pdf_viewer_session_pages_deleting(PdfViewerSession *session,
		PdfViewer *pv, GPtrArray *arr_document_page, GError **error) {
	gint rc = 0;

	g_signal_emit(session, signals[SIGNAL_PAGES_DELETING], 0, pv,
			arr_document_page, error, &rc);

	return rc;
}

void pdf_viewer_session_save_started(PdfViewerSession *session, PdfViewer *pv) {
	g_signal_emit(session, signals[SIGNAL_SAVE_STARTED], 0, pv);

	return;
}

gint pdf_viewer_session_save_prepare(PdfViewerSession *session, PdfViewer *pv,
		DisplayedDocument *dd, GError **error) {
	gint rc = 0;

	g_signal_emit(session, signals[SIGNAL_SAVE_PREPARE], 0, pv, dd, error,
			&rc);

	return rc;
}

gint pdf_viewer_session_save_finished(PdfViewerSession *session, PdfViewer *pv,
		DisplayedDocument *dd, gboolean ok, GError **error) {
	gint rc = 0;

	g_signal_emit(session, signals[SIGNAL_SAVE_FINISHED], 0, pv, dd, ok, error,
			&rc);

	return rc;
}

void pdf_viewer_session_save_done(PdfViewerSession *session, PdfViewer *pv) {
	g_signal_emit(session, signals[SIGNAL_SAVE_DONE], 0, pv);

	return;
}
