/*
 sond (gtk4/sond_eingang_dialog.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_eingang_dialog.h"

#include <stdio.h>

#include "../sond_eingang_zettel.h"

typedef struct {
	GtkWidget *window;
	GtkWidget *entry_datum;
	GtkWidget *popover;
	GtkWidget *calendar;
	GtkWidget *dropdown;
	GtkWidget *fehler;
	gint cal_year;
	gint cal_month;   //1..12
	SondEingangDialogFunc func;
	gpointer data;
} Dialog;

static void on_calendar_day_selected(GtkCalendar *calendar, gpointer data) {
	Dialog *d = data;
	GDateTime *datum = gtk_calendar_get_date(calendar);
	gchar *text = NULL;
	gint year = g_date_time_get_year(datum);
	gint month = g_date_time_get_month(datum);
	gboolean same_month = (year == d->cal_year && month == d->cal_month);

	text = g_strdup_printf("%02d.%02d.%04d", g_date_time_get_day_of_month(datum), month, year);
	gtk_editable_set_text(GTK_EDITABLE(d->entry_datum), text);
	g_free(text);
	g_date_time_unref(datum);

	d->cal_year = year;
	d->cal_month = month;

	//Monatswechsel im Kalender lässt das Fenster offen, ein Klick auf einen Tag schließt es
	if (same_month)
		gtk_popover_popdown(GTK_POPOVER(d->popover));
}

//Beim Öffnen: Kalender auf das Datum im Feld stellen (sonst heute)
static void on_popover_show(GtkWidget *popover, gpointer data) {
	Dialog *d = data;
	gchar *iso = NULL;
	gint year = 0, month = 0, day = 0;
	GDateTime *datum = NULL;

	(void) popover;

	if (sond_eingang_datum_parse(gtk_editable_get_text(GTK_EDITABLE(d->entry_datum)), &iso)) {
		sscanf(iso, "%d-%d-%d", &year, &month, &day);
	} else {
		GDateTime *jetzt = g_date_time_new_now_local();

		year = g_date_time_get_year(jetzt);
		month = g_date_time_get_month(jetzt);
		day = g_date_time_get_day_of_month(jetzt);
		g_date_time_unref(jetzt);
	}
	g_free(iso);

	d->cal_year = year;
	d->cal_month = month;

	datum = g_date_time_new_local(year, month, day, 0, 0, 0);
	if (datum) {
		g_signal_handlers_block_by_func(d->calendar, on_calendar_day_selected, d);
		gtk_calendar_set_date(GTK_CALENDAR(d->calendar), datum);
		g_signal_handlers_unblock_by_func(d->calendar, on_calendar_day_selected, d);
		g_date_time_unref(datum);
	}
}

static void on_ok(GtkWidget *widget, gpointer data) {
	Dialog *d = data;
	gchar *iso = NULL;
	SondEingangDialogFunc func = d->func;
	gpointer func_data = d->data;

	(void) widget;

	if (!sond_eingang_datum_parse(gtk_editable_get_text(GTK_EDITABLE(d->entry_datum)), &iso)) {
		gtk_label_set_text(GTK_LABEL(d->fehler), "Bitte das Datum als TT.MM.JJJJ eingeben (oder im "
				"Kalender wählen).");
		gtk_widget_set_visible(d->fehler, TRUE);

		return;
	}

	{
		guint index = gtk_drop_down_get_selected(GTK_DROP_DOWN(d->dropdown));
		gchar const *art = index > 0 ? sond_eingang_arten()[index] : NULL;

		//das Schließen gibt d frei
		gtk_window_close(GTK_WINDOW(d->window));
		if (func)
			func(iso, art, func_data);
	}

	g_free(iso);
}

static void on_cancel(GtkWidget *widget, gpointer data) {
	Dialog *d = data;

	(void) widget;

	gtk_window_close(GTK_WINDOW(d->window));
}

static void on_destroy(GtkWidget *widget, gpointer data) {
	(void) widget;

	g_free(data);
}

void sond_eingang_dialog_show(GtkWindow *parent, gchar const *zusammenfassung, gint vorbelegung,
		SondEingangDialogFunc func, gpointer data) {
	Dialog *d = g_new0(Dialog, 1);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *label = NULL;
	GtkWidget *grid = gtk_grid_new();
	GtkWidget *box_datum = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	GtkWidget *btn_cal = gtk_menu_button_new();
	GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *cancel = gtk_button_new_with_label("Abbrechen");
	GtkWidget *ok = gtk_button_new_with_label("OK");
	gchar *text = NULL;

	d->func = func;
	d->data = data;

	d->window = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(d->window), "Eingang setzen");
	gtk_window_set_modal(GTK_WINDOW(d->window), TRUE);
	if (parent)
		gtk_window_set_transient_for(GTK_WINDOW(d->window), parent);
	g_signal_connect(d->window, "destroy", G_CALLBACK(on_destroy), d);

	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	gtk_widget_set_margin_top(box, 12);
	gtk_widget_set_margin_bottom(box, 12);
	gtk_window_set_child(GTK_WINDOW(d->window), box);

	label = gtk_label_new(zusammenfassung);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
	gtk_box_append(GTK_BOX(box), label);

	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
	gtk_box_append(GTK_BOX(box), grid);

	//Datum: Feld und Kalender
	label = gtk_label_new("Eingang am:");
	gtk_label_set_xalign(GTK_LABEL(label), 1.0);
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);

	d->entry_datum = gtk_entry_new();
	gtk_editable_set_width_chars(GTK_EDITABLE(d->entry_datum), 11);
	if (vorbelegung > 0)
		text = sond_eingang_int_zu_anzeige(vorbelegung);
	else {
		gchar *heute = sond_eingang_datum_heute();

		text = sond_eingang_datum_anzeige(heute);
		g_free(heute);
	}
	gtk_editable_set_text(GTK_EDITABLE(d->entry_datum), text);
	g_free(text);
	g_signal_connect(d->entry_datum, "activate", G_CALLBACK(on_ok), d);

	d->calendar = gtk_calendar_new();
	d->popover = gtk_popover_new();
	gtk_popover_set_child(GTK_POPOVER(d->popover), d->calendar);
	g_signal_connect(d->calendar, "day-selected", G_CALLBACK(on_calendar_day_selected), d);
	g_signal_connect(d->popover, "show", G_CALLBACK(on_popover_show), d);

	gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(btn_cal), "x-office-calendar-symbolic");
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(btn_cal), d->popover);
	gtk_widget_set_tooltip_text(btn_cal, "Datum im Kalender wählen");

	gtk_box_append(GTK_BOX(box_datum), d->entry_datum);
	gtk_box_append(GTK_BOX(box_datum), btn_cal);
	gtk_grid_attach(GTK_GRID(grid), box_datum, 1, 0, 1, 1);

	//Art: Auswahlliste, an erster Stelle "keine"
	label = gtk_label_new("Art:");
	gtk_label_set_xalign(GTK_LABEL(label), 1.0);
	gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);

	d->dropdown = gtk_drop_down_new_from_strings(sond_eingang_arten());
	gtk_drop_down_set_selected(GTK_DROP_DOWN(d->dropdown), 0);
	gtk_grid_attach(GTK_GRID(grid), d->dropdown, 1, 1, 1, 1);

	d->fehler = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(d->fehler), 0.0);
	gtk_label_set_wrap(GTK_LABEL(d->fehler), TRUE);
	gtk_widget_add_css_class(d->fehler, "error");
	gtk_widget_set_visible(d->fehler, FALSE);
	gtk_box_append(GTK_BOX(box), d->fehler);

	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	gtk_widget_add_css_class(ok, "suggested-action");
	g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel), d);
	g_signal_connect(ok, "clicked", G_CALLBACK(on_ok), d);
	gtk_box_append(GTK_BOX(buttons), cancel);
	gtk_box_append(GTK_BOX(buttons), ok);
	gtk_box_append(GTK_BOX(box), buttons);
	gtk_window_set_default_widget(GTK_WINDOW(d->window), ok);

	gtk_window_present(GTK_WINDOW(d->window));
	gtk_widget_grab_focus(d->entry_datum);
}
