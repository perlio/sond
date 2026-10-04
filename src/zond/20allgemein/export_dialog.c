/*
 zond (export_dialog.c) - Akten, Beweisstücke, Unterlagen
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

#include <gtk/gtk.h>

#include "export_dialog.h"

#define EXPORT_MAX_TIEFE 50

typedef struct {
	GtkWidget *check_alle;
	GtkWidget *spin_tiefe;
} TiefeWidgets;

static void cb_alle_toggled(GtkToggleButton *button, gpointer data) {
	TiefeWidgets *tw = (TiefeWidgets*) data;

	gtk_widget_set_sensitive(tw->spin_tiefe,
			!gtk_toggle_button_get_active(button));
}

static GtkWidget* export_dialog_check(GtkWidget *box, const gchar *label,
		gboolean aktiv, const gchar *tooltip) {
	GtkWidget *check = gtk_check_button_new_with_label(label);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), aktiv);
	if (tooltip)
		gtk_widget_set_tooltip_text(check, tooltip);
	gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);

	return check;
}

static GtkWidget* export_dialog_frame(GtkWidget *box, const gchar *titel) {
	GtkWidget *frame = gtk_frame_new(titel);
	GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

	gtk_container_set_border_width(GTK_CONTAINER(inner), 8);
	gtk_container_add(GTK_CONTAINER(frame), inner);
	gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);

	return inner;
}

gboolean export_dialog_run(Projekt *zond, Baum baum, ExportOptionen *opt) {
	GtkWidget *dialog = NULL;
	GtkWidget *box = NULL;
	GtkWidget *inner = NULL;
	GtkWidget *radio_sel = NULL;
	GtkWidget *radio_ganz = NULL;
	GtkWidget *hbox = NULL;
	GtkWidget *combo_format = NULL;
	GtkWidget *check_nodetext = NULL;
	GtkWidget *check_text = NULL;
	GtkWidget *check_anbindung = NULL;
	GtkWidget *check_pfad = NULL;
	GtkWidget *check_nummern = NULL;
	GtkWidget *check_dokumente = NULL;
	TiefeWidgets tw = { 0 };
	gchar *label = NULL;
	gint anzahl = 0;
	gint response = 0;

	anzahl = export_selection_anzahl_markiert(zond, baum);

	dialog = gtk_dialog_new_with_buttons("Export", GTK_WINDOW(zond->app_window),
			GTK_DIALOG_MODAL, "_Abbrechen", GTK_RESPONSE_CANCEL, "_Exportieren",
			GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(
			GTK_DIALOG(dialog))), box);

	//Quelle
	label = g_strdup_printf("Quelle (%s)",
			baum == BAUM_INHALT ? "Inhaltsbaum" : "Auswertungsbaum");
	inner = export_dialog_frame(box, label);
	g_free(label);

	label = g_strdup_printf("Markierte Punkte (%d)", anzahl);
	radio_sel = gtk_radio_button_new_with_label(NULL, label);
	g_free(label);
	radio_ganz = gtk_radio_button_new_with_label_from_widget(
			GTK_RADIO_BUTTON(radio_sel), "Ganzer Baum");
	gtk_box_pack_start(GTK_BOX(inner), radio_sel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(inner), radio_ganz, FALSE, FALSE, 0);

	if (anzahl == 0) {
		gtk_widget_set_sensitive(radio_sel, FALSE);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_ganz), TRUE);
	} else
		gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(opt->ganzer_baum ? radio_ganz : radio_sel),
				TRUE);

	//Ebenentiefe
	inner = export_dialog_frame(box, "Ebenentiefe");
	tw.check_alle = export_dialog_check(inner, "Alle Ebenen unter jedem Punkt",
			opt->tiefe == EXPORT_TIEFE_ALLE, NULL);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start(GTK_BOX(hbox),
			gtk_label_new("Ebenen unter jedem Punkt (0 = nur der Punkt):"),
			FALSE, FALSE, 0);
	tw.spin_tiefe = gtk_spin_button_new_with_range(0, EXPORT_MAX_TIEFE, 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(tw.spin_tiefe),
			opt->tiefe == EXPORT_TIEFE_ALLE ? 0 : opt->tiefe);
	gtk_box_pack_start(GTK_BOX(hbox), tw.spin_tiefe, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(inner), hbox, FALSE, FALSE, 0);

	gtk_widget_set_sensitive(tw.spin_tiefe, opt->tiefe != EXPORT_TIEFE_ALLE);
	g_signal_connect(tw.check_alle, "toggled", G_CALLBACK(cb_alle_toggled),
			&tw);

	//Knoten-Info
	inner = export_dialog_frame(box, "Info aus den Knoten");
	check_nodetext = export_dialog_check(inner, "Knotentext", opt->nodetext,
			NULL);
	check_text = export_dialog_check(inner, "Text (Notiz)", opt->text, NULL);
	check_anbindung = export_dialog_check(inner, "Anbindung (Datei und Seiten)",
			opt->anbindung, NULL);
	check_pfad = export_dialog_check(inner, "Pfad (Vorfahren des markierten Punkts)",
			opt->pfad, "Hilfreich, wenn Punkte ohne ihre übergeordneten "
			"Punkte exportiert werden");

	//Gliederung
	inner = export_dialog_frame(box, "Gliederung");
	check_nummern = export_dialog_check(inner,
			"Gliederungsnummern (relativ zur Markierung)", opt->nummern, NULL);

	//Dokumente
	inner = export_dialog_frame(box, "Dokumente");
	check_dokumente = export_dialog_check(inner,
			"Angebundene Dokumente ausgeben", opt->dokumente,
			"PDF-Seiten werden im PDF-Ziel als Seiten übernommen, in odt und "
			"docx als Bild. Text, HTML, odt/docx, Bilder und E-Mails werden "
			"dargestellt, alles andere erscheint als Hinweis. Unterseitige "
			"Anbindungen werden beschnitten.");

	//Format
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Format:"), FALSE, FALSE, 0);
	combo_format = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_format),
			"odt (Writer)");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_format), "PDF");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_format),
			"docx (Word)");
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo_format), opt->format);
	gtk_box_pack_start(GTK_BOX(hbox), combo_format, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, FALSE, 0);

	gtk_widget_show_all(dialog);
	response = gtk_dialog_run(GTK_DIALOG(dialog));

	if (response == GTK_RESPONSE_OK) {
		opt->ganzer_baum = gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(radio_ganz));
		opt->tiefe = gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(tw.check_alle)) ? EXPORT_TIEFE_ALLE :
				gtk_spin_button_get_value_as_int(
						GTK_SPIN_BUTTON(tw.spin_tiefe));
		opt->nodetext = gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(check_nodetext));
		opt->text = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_text));
		opt->anbindung = gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(check_anbindung));
		opt->pfad = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_pfad));
		opt->nummern = gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(check_nummern));
		opt->format = (ExportFormat) gtk_combo_box_get_active(
				GTK_COMBO_BOX(combo_format));
		opt->dokumente = gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(check_dokumente));
	}

	gtk_widget_destroy(dialog);

	return response == GTK_RESPONSE_OK;
}
