/*
 zond (viewer_ocr.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2020  pelo america

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <tesseract/capi.h>

#include "../sond_log_and_error.h"
#include "../sond_ocr.h"
#include "../sond_pdf_helper.h"
#include "../gtk3/sond_dialoge.h"
#include "../gtk3/sond_info_window.h"
#include "../misc_stdlib.h"

#include "zond_pdf_document.h"

#include "viewer.h"
#include "viewer_ocr.h"
#include "viewer_render.h"
#include "seiten.h"

/*
 **  Seiten OCR
 */
/* Rückfrage bei Seiten, die bereits eine Textebene enthalten -
 * Rückgabe GTK_RESPONSE_YES: verwerfen und neu OCRen, GTK_RESPONSE_NO:
 * Seite überspringen, GTK_RESPONSE_CANCEL ("Abbrechen"-Button oder Fenster
 * per "X"/delete-event geschlossen - my_dialog_run() verknüpft das
 * delete-event bereits generisch mit GTK_RESPONSE_CANCEL): gesamten
 * OCR-Lauf abbrechen, nicht nur diese Seite überspringen (sonst müsste man
 * sich bei vielen bereits-OCRten Seiten durch jede einzelne durchklicken).
 * *remember_out: TRUE, wenn "für alle weiteren Seiten übernehmen" angehakt
 * war - der Aufrufer übernimmt die Entscheidung dann für den Rest des
 * Durchlaufs, ohne erneut zu fragen (gilt nur für YES/NO, nicht für den
 * Abbruch-Fall). */
static gint seiten_ocr_abfrage_hidden_text(PdfViewer *pv, guint seitenzahl,
		gboolean *remember_out) {
	GtkWidget *dialog = NULL;
	GtkWidget *content_area = NULL;
	GtkWidget *label = NULL;
	GtkWidget *check_remember = NULL;
	gchar *text = NULL;
	gint res = 0;

	dialog = gtk_dialog_new_with_buttons("Seite enthält bereits Text",
			GTK_WINDOW(viewer_parent(pv)), GTK_DIALOG_MODAL,
			"Verwerfen und neu OCRen", GTK_RESPONSE_YES,
			"Seite überspringen", GTK_RESPONSE_NO,
			"Abbrechen", GTK_RESPONSE_CANCEL, NULL);

	content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	text = g_strdup_printf("Seite %u enthält bereits eine Textebene.\n"
			"Verwerfen und neu OCRen, oder Seite überspringen?",
			seitenzahl);
	label = gtk_label_new(text);
	g_free(text);
	gtk_box_pack_start(GTK_BOX(content_area), label, FALSE, FALSE, 0);

	check_remember = gtk_check_button_new_with_label(
			"Für alle weiteren Seiten übernehmen");
	gtk_box_pack_start(GTK_BOX(content_area), check_remember, FALSE, FALSE, 0);

	gtk_widget_show_all(dialog);

	res = my_dialog_run(GTK_DIALOG(dialog));
	*remember_out = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(check_remember));

	gtk_widget_destroy(dialog);

	return res;
}

/* Lädt die Seite, falls noch nicht geschehen. 0 oder -1 (Meldung im InfoWindow). */
static gint seiten_ocr_seite_laden(InfoWindow *info_window, PdfDocumentPage *pdf_document_page,
		fz_context *ctx) {
	GError *error = NULL;
	gint rc_load = 0;

	//läuft für die Seite gerade ein Render-Task, würde er sie ebenfalls laden
	viewer_render_wait_for_transfer(pdf_document_page);

	if (pdf_document_page->page)
		return 0;

	/* zond_pdf_document_load_page() statt isoliertem pdf_load_page():
	 * legt dabei auch arr_annots an. page != NULL ohne arr_annots
	 * würde die Invariante brechen, dass beide immer zusammen
	 * gesetzt werden (siehe seiten_drehen()/viewer.c). */
	zond_pdf_document_mutex_lock(pdf_document_page->document);
	rc_load = zond_pdf_document_load_page(pdf_document_page, ctx, &error);
	zond_pdf_document_mutex_unlock(pdf_document_page->document);
	if (!rc_load)
		pdf_document_page->thread |= 2; //Seite geladen: der Renderer lädt sie nicht noch einmal
	if (rc_load) {
		info_window_set_message(info_window,
				"Seite %u konnte nicht geladen werden: %s",
				pdf_document_page->page_akt + 1, error->message);
		g_clear_error(&error);

		return -1;
	}

	return 0;
}

/* Bestimmt den OCR-Modus für die Seite: ohne Text CHECK, mit Text (verstecktem) per Rückfrage oder
 * aus der gemerkten Entscheidung (*mode_remembered, SOND_OCR_MODE_NONE = noch nichts gemerkt).
 * 0: *mode gesetzt, 1: Seite überspringen (Fehler, Meldung gesetzt), 2: ganzen Lauf abbrechen. */
static gint seiten_ocr_modus(PdfViewer *pv, InfoWindow *info_window,
		PdfDocumentPage *pdf_document_page, fz_context *ctx, SondOcrMode *mode_remembered,
		SondOcrMode *mode) {
	GError *error = NULL;
	gboolean has_text = FALSE;
	gint rc_hidden = 0;

	*mode = SOND_OCR_MODE_CHECK;

	rc_hidden = pdf_page_has_text(ctx, pdf_document_page->page, &has_text, NULL, &error);
	if (rc_hidden) {
		info_window_set_message(info_window,
				"Seite %u konnte nicht auf vorhandenen Text geprüft werden: %s",
				pdf_document_page->page_akt + 1, error->message);
		g_clear_error(&error);

		return 1;
	}

	if (!has_text)
		return 0;

	if (*mode_remembered != SOND_OCR_MODE_NONE) {
		*mode = *mode_remembered;

		return 0;
	}

	{
		gint res = 0;
		gboolean remember = FALSE;

		res = seiten_ocr_abfrage_hidden_text(pv, pdf_document_page->page_akt + 1, &remember);
		if (res == GTK_RESPONSE_CANCEL) {
			/* "Abbrechen" (Button oder Fenster per "X"
			 * geschlossen) - kompletten Lauf abbrechen, nicht
			 * nur diese Seite. Dieselbe cancel-Flag wie beim
			 * Abbrechen-Button des InfoWindow setzen (s.
			 * cb_abbrechen_clicked()/cb_info_window_delete_event()
			 * in gtk3/sond_info_window.c) - konsistent mit dem übrigen
			 * Abbruch-Mechanismus, auch wenn hier noch kein
			 * Hintergrund-Task für diese Seite läuft. Diese
			 * Seite wurde noch nicht angefasst (weder
			 * buf_content noch Task angelegt), daher reicht ein
			 * direktes Abbrechen. */
			if (!g_atomic_int_get(info_window->cancel)) {
				info_window_set_message(info_window, "...abgebrochen");
				g_atomic_int_set(info_window->cancel, 1);
			}

			return 2;
		}

		*mode = (res == GTK_RESPONSE_YES) ? SOND_OCR_MODE_FORCE : SOND_OCR_MODE_CHECK;

		if (remember)
			*mode_remembered = *mode;
	}

	return 0;
}

/* Besorgt das Objekt der SOND-Schrift im Dokument der Seite (legt sie an, falls es sie noch nicht
 * gibt). *font_ref kommt mit eigener Referenz, der Aufrufer gibt sie frei. 0 oder -1 (Meldung im
 * InfoWindow gesetzt). */
static gint seiten_ocr_font_intern(InfoWindow *info_window, PdfDocumentPage *pdf_document_page,
		fz_context *ctx, pdf_obj **font_ref) {
	GError *error = NULL;
	gint font_num = zond_pdf_document_get_ocr_num(pdf_document_page->document);

	*font_ref = NULL;

	if (font_num) {
		fz_try(ctx)
			*font_ref = pdf_new_indirect(ctx, zond_pdf_document_get_pdf_doc(
					pdf_document_page->document), font_num, 0);
		fz_catch(ctx) {
			info_window_set_message(info_window, "Konnte Font-Objekt nicht erzeugen: %s",
					fz_caught_message(ctx));

			return -1;
		}

		return 0;
	}

	if (pdf_get_sond_font(ctx, zond_pdf_document_get_pdf_doc(pdf_document_page->document),
			font_ref, &error)) {
		info_window_set_message(info_window, "Suchen Font-Objekt fehlgeschlagen: %s",
				error->message);
		g_clear_error(&error);

		return -1;
	}

	if (*font_ref) { //gefunden - pdf_dict_gets() liefert nur eine geliehene Referenz
		pdf_keep_obj(ctx, *font_ref);

		return 0;
	}

	*font_ref = pdf_put_sond_font(ctx,
			zond_pdf_document_get_pdf_doc(pdf_document_page->document), &error);
	if (!*font_ref) {
		info_window_set_message(info_window,
				"SOND-Schriftart konnte nicht erzeugt werden: %s", error->message);
		g_clear_error(&error);

		return -1;
	}

	fz_try(ctx)
		font_num = pdf_to_num(ctx, *font_ref);
	fz_catch(ctx) {
		info_window_set_message(info_window,
				"Num des Font_Objektes konnte nicht ermittelt werden: %s",
				fz_caught_message(ctx));
		pdf_drop_obj(ctx, *font_ref);
		*font_ref = NULL;

		return -1;
	}

	zond_pdf_document_set_ocr_num(pdf_document_page->document, font_num);

	return 0;
}

//Das Dokument wird auch von den Render-Threads benutzt: nur unter dem Dokument-Mutex ändern
static gint seiten_ocr_font(InfoWindow *info_window, PdfDocumentPage *pdf_document_page,
		fz_context *ctx, pdf_obj **font_ref) {
	gint rc = 0;

	zond_pdf_document_mutex_lock(pdf_document_page->document);
	rc = seiten_ocr_font_intern(info_window, pdf_document_page, ctx, font_ref);
	zond_pdf_document_mutex_unlock(pdf_document_page->document);

	return rc;
}

/* Führt die OCR für die Seite aus (ein Task im Pool). Nimmt die Referenz auf font_ref und gibt sie
 * frei. *content_changed: die Seite hat neuen Inhalt, dann steht dessen Buffer in *buf_content_new
 * (mit eigener Referenz). *stop_after: Abbrechen wurde geklickt, während der Task lief - die Seite
 * wird noch normal fertig verarbeitet, danach ist Schluss. 0 oder -1 (Meldung gesetzt). */
static gint seiten_ocr_ausfuehren(InfoWindow *info_window, SondOcrPool *pool, SondOcrMode mode,
		PdfDocumentPage *pdf_document_page, fz_context *ctx, pdf_obj *font_ref,
		gboolean *content_changed, fz_buffer **buf_content_new, gboolean *stop_after) {
	GError *error = NULL;
	SondOcrTask *task = NULL;
	GPtrArray *arr_tasks = NULL;
	gint rc = 0;

	*content_changed = FALSE;
	*buf_content_new = NULL;
	*stop_after = FALSE;

	task = sond_ocr_task_new(ctx,
			zond_pdf_document_get_pdf_doc(pdf_document_page->document),
			pdf_document_page->page_akt, font_ref,
			(void(*)(void*, gchar const*, ...)) info_window_set_message_thread_safe,
			(gpointer) info_window, &error);
	pdf_drop_obj(ctx, font_ref);
	if (!task) {
		info_window_set_message(info_window, "OCR-Task konnte nicht erzeugt werden: %s",
				error->message);
		g_clear_error(&error);

		return -1;
	}

	arr_tasks = g_ptr_array_new_with_free_func((GDestroyNotify) sond_ocr_task_free);
	g_ptr_array_add(arr_tasks, task);

	rc = sond_ocr_do_tasks(arr_tasks, pool, mode, &error);
	*content_changed = task->content_changed; //vor dem unref auslesen - der danach freigegeben wird
	*buf_content_new = task->buf_content_new; //übernimmt ref - task->buf_content_new deshalb NULLen,
	task->buf_content_new = NULL;             //sonst droppt sond_ocr_task_free() ihn gleich mit
	g_ptr_array_unref(arr_tasks);
	if (rc == -1) {
		info_window_set_message(info_window, "OCR-Task gescheitert: %s", error->message);
		g_clear_error(&error);
		if (*buf_content_new) {
			fz_drop_buffer(ctx, *buf_content_new);
			*buf_content_new = NULL;
		}
		*content_changed = FALSE;

		return -1;
	}

	/* rc == 1: Abbrechen wurde geklickt, während der (einzige) Task dieses
	 * Aufrufs bereits lief - der wird von sond_ocr_do_tasks() ganz
	 * normal zu Ende abgewartet (s. dort) und kann noch erfolgreich
	 * fertig geworden sein (content_changed/buf_content_new dann
	 * korrekt gesetzt). Nicht sofort abbrechen, sonst geht eine bereits
	 * physisch in die Seite geschriebene Änderung (Journal-Eintrag,
	 * Aufräumen) verloren bzw. buf_content_new leakt. Stattdessen diese
	 * eine Seite noch normal fertig verarbeiten und erst danach
	 * abbrechen. */
	if (rc == 1)
		*stop_after = TRUE;

	return 0;
}

/* Liest /Rotate der Seite (geerbt oder eigen) aus dem Dokument. 0 oder -1. */
static gint seiten_ocr_get_rotate(PdfDocumentPage *pdf_document_page, fz_context *ctx,
		gint *rotate) {
	pdf_obj *rotate_obj = NULL;

	fz_try(ctx) {
		rotate_obj = pdf_dict_get_inheritable(ctx, pdf_document_page->page->obj,
				PDF_NAME(Rotate));
		*rotate = rotate_obj ? pdf_to_int(ctx, rotate_obj) : 0;
	}
	fz_catch(ctx)
		return -1;

	return 0;
}

/* Die OCR dreht eine Seite selbst, wenn die Texterkennung (OSD) sie für falsch ausgerichtet hält
 * (sond_ocr_osd()), und ändert dabei nur /Rotate im Dokument. Der Textlayer ist dann für die
 * gedrehte Seite angelegt, ohne Nachführung bliebe die Drehung aber ohne Journal-Eintrag und ginge
 * beim Speichern verloren. Hier wird sie wie eine Drehung im Viewer nachgeführt. rotate_vorher:
 * /Rotate vor der OCR. */
static void seiten_ocr_drehung_nachfuehren(PdfViewer *pv, PdfDocumentPage *pdf_document_page,
		fz_context *ctx, gint rotate_vorher) {
	gint rotate_nachher = 0;
	gint winkel = 0;

	if (seiten_ocr_get_rotate(pdf_document_page, ctx, &rotate_nachher))
		return;

	winkel = (((rotate_nachher - rotate_vorher) % 360) + 360) % 360;
	if (!winkel)
		return;

	if (winkel == 270)
		winkel = -90;

	pdf_document_page->rotate = rotate_nachher;
	seiten_drehung_nachfuehren(pv, pdf_document_page, winkel);
}

/* Trägt das Ergebnis der OCR in das Journal ein und verwirft die gerenderten Daten der Seite.
 * entry.ocr.buf_old ist gesetzt, buf_content_new geht (mit seiner Referenz) in den Eintrag. */
static void seiten_ocr_einpflegen(PdfViewer *pv, PdfDocumentPage *pdf_document_page,
		JournalEntry *entry, fz_buffer *buf_content_new) {
	GArray *arr_entries = NULL;

	entry->ocr.buf_new = buf_content_new; //übernimmt ref

	arr_entries = zond_pdf_document_get_arr_journal(pdf_document_page->document);
	g_array_append_val(arr_entries, *entry);

	//fz_stext_list droppen und auf NULL setzen
	viewer_render_wait_for_transfer(pdf_document_page);

	fz_drop_display_list(zond_pdf_document_get_ctx(pdf_document_page->document),
			pdf_document_page->display_list);
	pdf_document_page->display_list = NULL;

	fz_drop_stext_page(zond_pdf_document_get_ctx(pdf_document_page->document),
			pdf_document_page->stext_page);
	pdf_document_page->stext_page = NULL;

	pdf_document_page->thread &= 2;

	//Damit speichern angeht
	zond_pdf_document_notify_page_changed(pdf_document_page->document, pdf_document_page,
			ZPDFD_PART_CHANGE_OCR, NULL);
}

/* TODO (geplant): Auswahl des OCR-Modus, analog "Index erstellen".
 *   1. OCR neu (bestehenden Text verwerfen, Seite neu erkennen) statt
 *      pauschal überspringen bei bereits vorhandenem verstecktem Text -
 *      UMGESETZT: Rückfrage je Seite mit verstecktem Text
 *      (seiten_ocr_abfrage_hidden_text(), s.o.), inkl. "für alle weiteren
 *      Seiten übernehmen" (mode_remembered, s.u.).
 *   2. Bisherigen und neu erkannten Tesseract-Text gegenüberstellen und
 *      auswählen lassen - weiterhin offen, nicht Teil dieser Änderung. */
void cb_pv_seiten_ocr(GtkMenuItem *item, gpointer data) {
	GError* error = NULL;
	gchar *title = NULL;
	GPtrArray *arr_document_page = NULL;
	InfoWindow *info_window = NULL;
	gchar* datadir = NULL;
	gchar* exe_dir = NULL;
	SondOcrPool* pool = NULL;
	gint progress = 0;
	gint cancel = 0;
	//gemerkte Entscheidung aus vorheriger Rückfrage ("für alle weiteren
	//Seiten übernehmen") - SOND_OCR_MODE_NONE = noch nichts gemerkt
	SondOcrMode mode_remembered = SOND_OCR_MODE_NONE;

	PdfViewer *pv = (PdfViewer*) data;

	//zu OCRende Seiten holen
	title = g_strdup_printf("Seiten OCR (1 - %i):", pv->arr_pages->len);
	arr_document_page = seiten_abfrage_seiten(pv, title, NULL, TRUE);
	g_free(title);

	if (!arr_document_page)
		return;

	info_window = info_window_open(viewer_parent(pv), &cancel, "OCR");

	exe_dir = get_exe_dir();
	datadir = g_build_filename(exe_dir, "../share/tessdata", NULL);
	g_free(exe_dir);
	pool = sond_ocr_pool_new(datadir, "deu", 1,
			info_window->cancel, &progress, &error);
	g_free(datadir);
	if (!pool) {
		info_window_set_message(info_window,
				"Thread-Pool konnte nicht initialisiert werden: %s",
				error->message, NULL);
		g_error_free(error);
		g_ptr_array_unref(arr_document_page);

		info_window_close(info_window);

		return;
	}

	for (gint i = 0; i < arr_document_page->len; i++) {
		pdf_obj *font_ref = NULL;
		JournalEntry entry = { 0 };
		fz_buffer *buf_content = NULL;
		fz_buffer *buf_content_new = NULL;
		gboolean content_changed = FALSE;
		gboolean stop_after_this_page = FALSE;
		SondOcrMode mode = SOND_OCR_MODE_CHECK;
		gint rc = 0;
		gint rotate_vorher = 0;

		PdfDocumentPage *pdf_document_page = g_ptr_array_index(
				arr_document_page, i);

		fz_context *ctx = zond_pdf_document_get_ctx(
				pdf_document_page->document);

		if (seiten_ocr_seite_laden(info_window, pdf_document_page, ctx))
			continue;

		//OCR-Modus für diese Seite bestimmen - bereits Text vorhanden?
		rc = seiten_ocr_modus(pv, info_window, pdf_document_page, ctx, &mode_remembered,
				&mode);
		if (rc == 1)
			continue;
		else if (rc == 2)
			break;

		zond_pdf_document_mutex_lock(pdf_document_page->document);
		buf_content = pdf_get_content_stream_as_buffer(ctx, pdf_document_page->page->obj, &error);
		zond_pdf_document_mutex_unlock(pdf_document_page->document);
		if (!buf_content) {
			info_window_set_message(info_window, "Alter Content-Stream nicht gefunden: %s",
					error->message, NULL);
			g_clear_error(&error);
			continue;
		}

		//entry vorbereiten
		entry.type = JOURNAL_TYPE_OCR;
		entry.pdf_document_page = pdf_document_page;

		entry.ocr.buf_old = buf_content; //übernimmt ref

		if (seiten_ocr_font(info_window, pdf_document_page, ctx, &font_ref)) {
			fz_drop_buffer(ctx, entry.ocr.buf_old);
			continue;
		}

		//wird die Seite von der OCR gedreht, muss das im Journal stehen (auch bei Fehlschlag danach)
		if (seiten_ocr_get_rotate(pdf_document_page, ctx, &rotate_vorher))
			rotate_vorher = 0;

		rc = seiten_ocr_ausfuehren(info_window, pool, mode, pdf_document_page, ctx, font_ref,
				&content_changed, &buf_content_new, &stop_after_this_page);

		seiten_ocr_drehung_nachfuehren(pv, pdf_document_page, ctx, rotate_vorher);

		if (rc) {
			fz_drop_buffer(ctx, entry.ocr.buf_old);
			continue;
		}

		/* Seite wegen bereits vorhandenem verstecktem Text übersprungen (oder
		 * Einfügen der OCR-Daten fehlgeschlagen) - Content-Stream unverändert.
		 * Keinen Journal-Eintrag anlegen und die Datei nicht als geändert
		 * markieren (sonst würde bloßes Prüfen bereits-OCRter Seiten den
		 * Speichern-Button aktivieren, obwohl nichts zu speichern ist). */
		if (!content_changed) {
			fz_drop_buffer(ctx, entry.ocr.buf_old);
			if (stop_after_this_page)
				break;
			continue;
		}

		/* buf_content_new kommt direkt aus add_ocr_layer_to_page() (via
		 * task->buf_content_new) - derselbe Buffer, der bereits in die
		 * Seite geschrieben wurde. Kein erneutes, fehleranfälliges Lesen
		 * des Content-Streams mehr nötig; bei content_changed==TRUE ist er
		 * immer gesetzt (beides wird an derselben Stelle in sond_ocr.c
		 * zusammen gesetzt). Defensiv trotzdem prüfen. */
		if (!buf_content_new) {
			info_window_set_message(info_window,
					"Neuer Content-Stream nicht verfügbar - Seite %u übersprungen",
					pdf_document_page->page_akt + 1);
			fz_drop_buffer(ctx, entry.ocr.buf_old);
			if (stop_after_this_page)
				break;
			continue;
		}

		seiten_ocr_einpflegen(pv, pdf_document_page, &entry, buf_content_new);

		if (stop_after_this_page)
			break;
	}

	g_ptr_array_unref(arr_document_page);
	info_window_close(info_window);
	sond_ocr_pool_free(pool);

	//damit Text von Cursor "erkannt" wird
	g_signal_emit_by_name(pv->v_adj, "value-changed", NULL);

	return;
}
