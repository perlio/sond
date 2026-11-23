/*
 zond (seiten_dialoge.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  pelo america

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

//Dialoge der Seitenfunktionen: Seitenauswahl (Drehen, Löschen, OCR) und Einfügen

#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "../../gtk3/sond_dialoge.h"
#include "../../misc_stdlib.h"

#include "../zond_pdf_document.h"

#include "viewer.h"
#include "seiten.h"

static gint compare_gint(gconstpointer a, gconstpointer b) {
	const gint *_a = a;
	const gint *_b = b;

	return *_a - *_b;
}

static GArray*
seiten_parse_text(PdfViewer *pv, gint max, const gchar *text) {
	gint start = 0;
	gint end = 0;
	const gchar *range = NULL;
	gint i = 0;
	gint page = 0;
	gint last_inserted = -1;

	if (!fz_is_page_range( NULL, text))
		return NULL;

	GArray *arr_tmp = g_array_new( FALSE, FALSE, sizeof(gint));
	GArray *arr_pages = g_array_new( FALSE, FALSE, sizeof(gint));

	range = text;
	while ((range = fz_parse_page_range( NULL, range, &start, &end, max))) {
		if (start < end) {
			for (i = start; i <= end; i++)
				g_array_append_val(arr_tmp, i);
		} else if (start > end) {
			for (i = start; i >= end; i--)
				g_array_append_val(arr_tmp, i);
		} else
			g_array_append_val(arr_tmp, start);
	}

	g_array_sort(arr_tmp, (GCompareFunc) compare_gint);

	for (i = 0; i < arr_tmp->len; i++) {
		page = g_array_index( arr_tmp, gint, i ) - 1;
		if (page != last_inserted) {
			g_array_append_val(arr_pages, page);
			last_inserted = page;
		}
	}

	g_array_free(arr_tmp, TRUE);

	return arr_pages;
}

static void cb_seiten_drehen_entry(GtkEntry *entry, gpointer dialog) {
	gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

	return;
}

static void cb_radio_auswahl_toggled(GtkToggleButton *button, gpointer data) {
	gtk_widget_set_sensitive((GtkWidget*) data,
			gtk_toggle_button_get_active(button));

	return;
}

GPtrArray*
seiten_abfrage_seiten(PdfViewer *pv, const gchar *title, gint *winkel,
		gboolean mit_alles) {
	gint rc = 0;
	GtkWidget *radio_90_UZS = NULL;
	GtkWidget *radio_180 = NULL;
	GtkWidget *radio_90_gegen_UZS = NULL;
	gchar *text = NULL;
	GPtrArray *arr_document_pages = NULL;

	GtkWidget *dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(pv->vf),
			GTK_DIALOG_MODAL, "Ok", GTK_RESPONSE_OK, "Abbrechen",
			GTK_RESPONSE_CANCEL, NULL);

	GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	if (winkel) {
		radio_90_UZS = gtk_radio_button_new_with_label( NULL,
				"90° im Uhrzeigersinn");
		radio_180 = gtk_radio_button_new_with_label( NULL, "180°");
		radio_90_gegen_UZS = gtk_radio_button_new_with_label( NULL,
				"90° gegen Uhrzeigersinn");

		gtk_radio_button_join_group(GTK_RADIO_BUTTON(radio_180),
				GTK_RADIO_BUTTON(radio_90_UZS));
		gtk_radio_button_join_group(GTK_RADIO_BUTTON(radio_90_gegen_UZS),
				GTK_RADIO_BUTTON(radio_180));

		gtk_box_pack_start(GTK_BOX(content_area), radio_90_UZS, FALSE, FALSE,
				0);
		gtk_box_pack_start(GTK_BOX(content_area), radio_180, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(content_area), radio_90_gegen_UZS, FALSE,
				FALSE, 0);
	}

	GtkWidget *radio_alle = gtk_radio_button_new_with_label( NULL, "Alle");
	GtkWidget *radio_mark = gtk_radio_button_new_with_label( NULL, "Markierte");
	GtkWidget *radio_auswahl = gtk_radio_button_new_with_label( NULL,
			"Seiten auswählen");

	gtk_radio_button_join_group(GTK_RADIO_BUTTON(radio_alle),
			GTK_RADIO_BUTTON(radio_auswahl));
	gtk_radio_button_join_group(GTK_RADIO_BUTTON(radio_mark),
			GTK_RADIO_BUTTON(radio_alle));

	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(hbox), radio_auswahl, FALSE, FALSE, 0);

	GtkWidget *entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), entry, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(content_area), radio_alle, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(content_area), radio_mark, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(content_area), hbox, FALSE, FALSE, 0);

	g_signal_connect(radio_auswahl, "toggled",
			G_CALLBACK( cb_radio_auswahl_toggled), entry);

	if (!gtk_tree_selection_count_selected_rows(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(pv->tree_thumb)))) {
		gtk_widget_set_sensitive(radio_mark, FALSE);
		gtk_widget_grab_focus(entry);
	} else
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_mark), TRUE);

	if (!mit_alles)
		gtk_widget_set_sensitive(radio_alle, FALSE);

	g_signal_connect(entry, "activate", G_CALLBACK(cb_seiten_drehen_entry),
			(gpointer ) dialog);

	gtk_widget_show_all(dialog);

	rc = my_dialog_run(GTK_DIALOG(dialog));

	if (rc == GTK_RESPONSE_OK) {
		GArray *arr_seiten_pv = NULL;

		if (winkel) {
			if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_90_UZS)))
				*winkel = 90;
			else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_180)))
				*winkel = 180;
			else
				*winkel = -90;
		}

		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_auswahl))) {
			text = g_strdup(gtk_entry_get_text( GTK_ENTRY(entry) ));
			if (text)
				arr_seiten_pv = seiten_parse_text(pv, pv->arr_pages->len, text);
			g_free(text);
		} else if (gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(radio_alle))) {
			arr_seiten_pv = g_array_new( FALSE, FALSE, sizeof(gint));
			for (gint i = 0; i < pv->arr_pages->len; i++)
				g_array_append_val(arr_seiten_pv, i);
		} else if (gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(radio_mark))) {
			arr_seiten_pv = seiten_markierte_thumbs(pv);
		}

		if (arr_seiten_pv && arr_seiten_pv->len) {
			arr_document_pages = seiten_get_document_pages(pv, arr_seiten_pv);
			g_array_unref(arr_seiten_pv);
		}
	}

	gtk_widget_destroy(dialog);

	return arr_document_pages;
}

static void cb_pv_seiten_entry(GtkEntry *entry, gpointer button_datei) {
	gtk_widget_grab_focus((GtkWidget*) button_datei);

	return;
}

gint seiten_abfrage_seitenzahl(PdfViewer *pv, guint *num) {
	gint res = 0;
	gint rc = 0;

	// Dialog erzeugen
	GtkWidget *dialog = gtk_dialog_new_with_buttons("Seiten einfügen:",
			GTK_WINDOW(pv->vf), GTK_DIALOG_MODAL, "Datei", 1, "Clipboard", 2,
			"Abbrechen", GTK_RESPONSE_CANCEL, NULL);

	GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	GtkWidget *frame = gtk_frame_new("nach Seite:");
	GtkWidget *entry = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(frame), entry);
	gtk_box_pack_start(GTK_BOX(content_area), frame, TRUE, TRUE, 0);

	GtkWidget *button_clipboard = gtk_dialog_get_widget_for_response(
			GTK_DIALOG(dialog), 2);
	if (!pv->session->pv_clip)
		gtk_widget_set_sensitive(button_clipboard, FALSE);

	GtkWidget *button_datei = gtk_dialog_get_widget_for_response(
			GTK_DIALOG(dialog), 1);
	g_signal_connect(entry, "activate", G_CALLBACK(cb_pv_seiten_entry),
			button_datei);

	gtk_widget_show_all(dialog);
	gtk_widget_grab_focus(entry);

	res = my_dialog_run(GTK_DIALOG(dialog));

	if (res == 1 || res == 2) {
		rc = string_to_guint(gtk_entry_get_text(GTK_ENTRY(entry)), num);
		if (rc) {
			display_message(pv->vf, "Seiten einfügen\n\n",
					"Ungültige oder fehlende Seitenzahl bei \"nach Seite:\"", NULL);
			res = -1;
		}
	}

	gtk_widget_destroy(dialog);

	if (res != 1 && res != 2)
		return -1;

	return res;
}
