#include <stdlib.h>
#include <gtk/gtk.h>
#include <ctype.h>

#include "general.h"
#include "../zond_pdf_document.h"
#include "../40viewer/viewer.h"
//#include "../../misc.h"

gboolean is_pdf(const gchar *file_part) {
	gboolean res = FALSE;

	//ToDo: file_part parsen

	//if ( file_part ist Datei)
	{
		gchar *rel_path = NULL;
		gchar *content_type = NULL;

		rel_path = g_strndup(file_part + 1,
				strlen(file_part + 1) - strlen(g_strrstr(file_part + 1, "//")));
		content_type = g_content_type_guess(rel_path, NULL, 0, NULL);
		g_free(rel_path);

		//Sonderbehandung, falls pdf-Datei
		if ((!g_strcmp0(content_type, ".pdf")
				|| !g_strcmp0(content_type, "application/pdf")))
			res = TRUE;
		g_free(content_type);
	}

	return res;
}

void info_window_kill(InfoWindow *info_window) {
	gtk_widget_destroy(info_window->dialog);

	g_free(info_window);

	return;
}

void info_window_close(InfoWindow *info_window) {
	GtkWidget *button = gtk_dialog_get_widget_for_response(
			GTK_DIALOG(info_window->dialog), GTK_RESPONSE_CANCEL);
	gtk_button_set_label(GTK_BUTTON(button), "Schließen");
	gtk_widget_grab_focus(button);

	gtk_dialog_run(GTK_DIALOG(info_window->dialog));

	info_window_kill(info_window);

	return;
}

void info_window_set_progress_bar_fraction(InfoWindow *info_window,
		gdouble fraction) {
	if (!GTK_IS_PROGRESS_BAR(info_window->last_inserted_widget))
		return;

	gtk_progress_bar_set_fraction(
			GTK_PROGRESS_BAR(info_window->last_inserted_widget), fraction);

	while (gtk_events_pending())
		gtk_main_iteration();

	return;
}

static void info_window_scroll(InfoWindow *info_window) {
	GtkWidget *viewport = NULL;
	GtkWidget *swindow = NULL;
	GtkAdjustment *adj = NULL;

	viewport = gtk_widget_get_parent(info_window->content);
	swindow = gtk_widget_get_parent(viewport);
	adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(swindow));
	gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));

	return;
}

static void info_window_show_widget(InfoWindow *info_window) {
	gtk_widget_show(info_window->last_inserted_widget);
	gtk_box_pack_start(GTK_BOX(info_window->content),
			info_window->last_inserted_widget, FALSE, FALSE, 0);

	while (gtk_events_pending())
		gtk_main_iteration();

	info_window_scroll(info_window);

	return;
}

void info_window_set_progress_bar(InfoWindow *info_window) {
	info_window->last_inserted_widget = gtk_progress_bar_new();
	info_window_show_widget(info_window);

	return;
}

void info_window_set_message(InfoWindow *info_window, const gchar *message) {
	info_window->last_inserted_widget = gtk_label_new(message);
	gtk_widget_set_halign(info_window->last_inserted_widget, GTK_ALIGN_START);

	info_window_show_widget(info_window);

	return;
}

static void cb_info_window_response(GtkDialog *dialog, gint id, gpointer data) {
	InfoWindow *info_window = (InfoWindow*) data;

	if (info_window->cancel)
		return;

	info_window_set_message(info_window, "...abbrechen");
	info_window->cancel = TRUE;

	return;
}

InfoWindow*
info_window_open(GtkWidget *window, const gchar *title) {
	GtkWidget *content = NULL;
	GtkWidget *swindow = NULL;

	InfoWindow *info_window = g_malloc0(sizeof(InfoWindow));

	info_window->dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(window),
			GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL, "Abbrechen",
			GTK_RESPONSE_CANCEL, NULL);

	gtk_window_set_default_size(GTK_WINDOW(info_window->dialog), 900, 190);

	content = gtk_dialog_get_content_area(GTK_DIALOG(info_window->dialog));
	swindow = gtk_scrolled_window_new( NULL, NULL);
	gtk_box_pack_start(GTK_BOX(content), swindow, TRUE, TRUE, 0);

	info_window->content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(swindow), info_window->content);

	gtk_widget_show_all(info_window->dialog);
	while (gtk_events_pending())
		gtk_main_iteration();

	g_signal_connect(GTK_DIALOG(info_window->dialog), "response",
			G_CALLBACK(cb_info_window_response), info_window);

	return info_window;
}

gchar*
get_rel_path_from_file_part(gchar const *file_part) {
	if (!file_part)
		return NULL;
	if (strlen(file_part) < 4)
		return NULL;

	if (strstr(file_part, "//"))
		return g_strndup(file_part + 1,
				strlen(file_part + 1) - strlen(strstr(file_part, "//")));
	else
		return NULL;
}

gboolean anbindung_1_gleich_2(const Anbindung anbindung1,
		const Anbindung anbindung2) {
	if ((anbindung1.von.seite == anbindung2.von.seite)
			&& (anbindung1.bis.seite == anbindung2.bis.seite)
			&& (anbindung1.von.index == anbindung2.von.index)
			&& (anbindung1.bis.index == anbindung2.bis.index))
		return TRUE;
	else
		return FALSE;
}

static gint anbindung_vergleiche_pdf_pos(PdfPos pdf_pos1, PdfPos pdf_pos2) {
	if (pdf_pos1.seite < pdf_pos2.seite)
		return -1;
	else if (pdf_pos1.seite > pdf_pos2.seite)
		return 1;
	//wenn gleiche Seite:
	else if (pdf_pos1.index < pdf_pos2.index)
		return -1;
	else if (pdf_pos1.index > pdf_pos2.index)
		return 1;

	return 0;
}

gboolean anbindung_is_pdf_punkt(Anbindung anbindung) {
	if ((anbindung.von.seite || anbindung.von.index) && !anbindung.bis.seite
			&& !anbindung.bis.index)
		return TRUE;

	return FALSE;
}

gboolean anbindung_1_vor_2(Anbindung anbindung1, Anbindung anbindung2) {
	if (anbindung_1_gleich_2(anbindung1, anbindung2))
		return FALSE;

	if (anbindung_is_pdf_punkt(anbindung1)) {
		if (anbindung_vergleiche_pdf_pos(anbindung1.von, anbindung2.von) == -1)
			return TRUE;
		else
			return FALSE;
	} else if (anbindung_is_pdf_punkt(anbindung2)) {
		if (anbindung_vergleiche_pdf_pos(anbindung1.bis, anbindung2.von) == -1)
			return TRUE;
		else
			return FALSE;

	} else //beides komplette Anbindung
	if (anbindung_vergleiche_pdf_pos(anbindung1.bis, anbindung2.von) == -1)
		return TRUE;

	return FALSE;
}

gboolean anbindung_1_eltern_von_2(Anbindung anbindung1, Anbindung anbindung2) {
	gint pos_anfang = 0;
	gint pos_ende = 0;

	//PdfPunkt kann niemals Eltern sein.
	if (anbindung_is_pdf_punkt(anbindung1))
		return FALSE;

	//Gleiche können nicht Nachfolger sein
	if (anbindung_1_gleich_2(anbindung1, anbindung2))
		return FALSE;

	//wenn Anbindung1 Datei ist, dann ist sie Eltern
	if (!anbindung1.von.seite && !anbindung1.von.index && !anbindung1.bis.seite
			&& !anbindung1.bis.index)
		return FALSE;
	//auch wenn Anbindung2 Datei
	if (!anbindung1.von.seite && !anbindung2.von.index && !anbindung2.bis.seite
			&& !anbindung2.bis.index)
		return FALSE;

	pos_anfang = anbindung_vergleiche_pdf_pos(anbindung1.von, anbindung2.von);
	pos_ende = anbindung_vergleiche_pdf_pos(anbindung1.bis, anbindung2.bis);

	if (pos_anfang > 0)
		return FALSE; //fängt schon später an...
	else //fängt entweder davor oder gleich an
	{
		if (pos_anfang == 0 && pos_ende <= 0)
			return FALSE; //Fängt gleich an, hört nicht später auf...
		else if (pos_anfang < 0 && pos_ende < 0)
			return FALSE; //Fängt vorher an, hört nicht mindestens gleich auf
	}

	return TRUE;
}

static void anbindung_parse_pdf_pos(gchar const *section, PdfPos *pdf_pos) {
	pdf_pos->seite = atoi(section + 1);
	pdf_pos->index = atoi(strstr(section, ",") + 1);

	return;
}

void anbindung_parse_file_section(gchar const *file_section,
		Anbindung *anbindung) {
	if (!file_section)
		return;

	if (g_str_has_prefix(file_section, "{{")) {
		anbindung_parse_pdf_pos(file_section + 1, &anbindung->von);

		anbindung_parse_pdf_pos(strstr(file_section, "}") + 1, &anbindung->bis);
	} else
		anbindung_parse_pdf_pos(file_section + 1, &anbindung->von);

	return;
}

void anbindung_build_file_section(Anbindung anbindung, gchar **section) {
	if (anbindung.bis.seite == 0 && anbindung.bis.index == 0)
		*section = g_strdup_printf("{%d,%d}", anbindung.von.seite,
				anbindung.von.index);
	else
		*section = g_strdup_printf("{{%d,%d}{%d,%d}}", anbindung.von.seite,
				anbindung.von.index, anbindung.bis.seite, anbindung.bis.index);

	return;
}

typedef struct _Inserts {
	gint page_doc;
	gint count;
} Inserts;

static gint sort_func(gconstpointer a, gconstpointer b) {
	Inserts* A = (Inserts*) a;
	Inserts* B = (Inserts*) b;

	if (A->page_doc < B->page_doc) return -1;
	else if (A->page_doc == B->page_doc) return 0;
	//else if (A->page_doc > B->page_doc)

	return 1;
}

void anbindung_aktualisieren_insert_pages(ZondPdfDocument* zond_pdf_document, Anbindung* anbindung) {
	GArray* arr_journal = NULL;
	GArray* arr_insertions = NULL;

	arr_journal = zond_pdf_document_get_arr_journal(zond_pdf_document);
	arr_insertions = g_array_new(FALSE, FALSE, sizeof(Inserts));

	//erst alle entries mit Type == PAGES_INSERTED aussondern
	for (gint i = 0; i < arr_journal->len; i++) {
		JournalEntry entry = { 0 };

		entry = g_array_index(arr_journal, JournalEntry, i);

		if (entry.type == JOURNAL_TYPE_PAGES_INSERTED) {
			gint first_page_inserted = 0;
			Inserts insert = { 0 };

			first_page_inserted = pdf_document_page_get_index(entry.pdf_document_page);
			insert.page_doc = first_page_inserted;
			insert.count = entry.PagesInserted.count;

			g_array_append_val(arr_insertions, insert);
		}
	}

	//dann sortieren
	g_array_sort(arr_insertions, sort_func);

	//dann neu durchlaufen lassen und Anbindung ändern
	for (gint i = 0; i < arr_insertions->len; i++) {
		Inserts insert = { 0 };

		insert = g_array_index(arr_insertions, Inserts, i);
		if (insert.page_doc < anbindung->von.seite) {
			anbindung->von.seite += insert.count;
			if (!anbindung_is_pdf_punkt(*anbindung)) anbindung->bis.seite += insert.count;
		} else if (insert.page_doc < anbindung->bis.seite) anbindung->bis.seite += insert.count;
	}

	g_array_unref(arr_insertions);

	return;
}

static void anbindung_aktualisieren_delete_page(ZondPdfDocument* zond_pdf_document, Anbindung* anbindung) {
	GArray* arr_journal = NULL;

	arr_journal = zond_pdf_document_get_arr_journal(zond_pdf_document);

	for (gint i = 0; i < arr_journal->len; i ++) {
		JournalEntry entry = { 0 };

		entry = g_array_index(arr_journal, JournalEntry, i);

		if (entry.type == JOURNAL_TYPE_PAGE_DELETED) {
			gint page_doc = 0;

			page_doc = pdf_document_page_get_index(entry.pdf_document_page);

			if (page_doc < anbindung->von.seite) {
				anbindung->von.seite--;

				if (!anbindung_is_pdf_punkt(*anbindung))
					anbindung->bis.seite--;
			} else if (!anbindung_is_pdf_punkt(*anbindung) &&
					page_doc <= anbindung->bis.seite)
				anbindung->bis.seite--;
		}
	}

	return;
}

void anbindung_aktualisieren(ZondPdfDocument* zond_pdf_document,
		Anbindung* anbindung) {
	anbindung_aktualisieren_insert_pages(zond_pdf_document, anbindung);
	anbindung_aktualisieren_delete_page(zond_pdf_document, anbindung);

	return;
}
