/*
 zond (zond_eingang.c) - Akten, Beweisstücke, Unterlagen
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

#include "zond_eingang.h"

#include <gtk/gtk.h>
#include <stdio.h>

#include "../misc.h"
#include "../sond_log_and_error.h"
#include "../sond_treeview.h"
#include "../sond_treeviewfm.h"
#include "../sond_tvfm_item.h"
#include "../sond_eingang_zettel.h"
#include "../sond_eingang_cache.h"

#include "zond_init.h"

/* ========================================================================
 * Auswahl im Dateiverzeichnis
 * ======================================================================== */

typedef struct {
	GPtrArray *pfade;      /* gchar*, relativ zur Library-Wurzel */
	GArray *ordner;        /* gboolean, parallel zu pfade */
	guint uebersprungen;   /* Einträge in Containern */
} Auswahl;

static gint auswahl_foreach(SondTreeview *stv, GtkTreeIter *iter, gpointer data,
		GError **error) {
	Auswahl *auswahl = (Auswahl*) data;
	SondTVFMItem *item = NULL;
	gchar *rel = NULL;
	gboolean is_dir = FALSE;

	item = sond_tvfm_item_from_view(GTK_TREE_VIEW(stv), iter);
	if (!item) //Dummy-Zeile
		return 0;

	if (sond_treeviewfm_item_fs_path(item, &rel, &is_dir)) {
		g_ptr_array_add(auswahl->pfade, rel);
		g_array_append_val(auswahl->ordner, is_dir);
	} else
		auswahl->uebersprungen++;

	g_object_unref(item);

	return 0;
}

static void auswahl_init(Auswahl *auswahl) {
	auswahl->pfade = g_ptr_array_new_with_free_func(g_free);
	auswahl->ordner = g_array_new(FALSE, FALSE, sizeof(gboolean));
	auswahl->uebersprungen = 0;
}

static void auswahl_clear(Auswahl *auswahl) {
	g_ptr_array_unref(auswahl->pfade);
	g_array_unref(auswahl->ordner);
}

/* Markierte Einträge des Dateiverzeichnisses sammeln; FALSE bei Fehler (schon
 * gemeldet) */
static gboolean auswahl_holen(Projekt *zond, Auswahl *auswahl) {
	GError *error = NULL;
	gint rc = 0;

	rc = sond_treeview_selection_foreach(SOND_TREEVIEW(zond->treeview[BAUM_FS]),
			auswahl_foreach, auswahl, &error);
	if (rc == -1) {
		display_message(zond->app_window, "Auswahl nicht lesbar\n\n",
				error ? error->message : "unbekannter Fehler", NULL);
		g_clear_error(&error);

		return FALSE;
	}

	return TRUE;
}

static gboolean eingang_bereit(Projekt *zond) {
	if (zond->eingang_cache)
		return TRUE;

	display_message(zond->app_window, "Eingang\n\n",
			"Für dieses Projekt sind keine Eingangsangaben verfügbar.", NULL);

	return FALSE;
}

/* Zettel in die Library schreiben und in die lokale Tabelle eintragen, danach
 * den Dateibaum auffrischen. */
static gboolean zettel_ablegen(Projekt *zond, SondEingangZettel *zettel) {
	GError *error = NULL;
	gchar *name = NULL;
	gboolean ok = FALSE;

	name = sond_eingang_zettel_write(zettel, zond->project_dir, &error);
	if (!name) {
		display_message(zond->app_window,
				"Eingangsangabe konnte nicht gespeichert werden\n\n",
				error ? error->message : "unbekannter Fehler", NULL);
		g_clear_error(&error);

		return FALSE;
	}

	ok = sond_eingang_cache_apply(zond->eingang_cache, zettel, name, &error);
	if (!ok) {
		display_message(zond->app_window,
				"Eingangsangabe gespeichert, aber nicht übernommen\n\n",
				error ? error->message : "unbekannter Fehler", "\n\n",
				"Mit \"Eingänge neu einlesen\" erneut versuchen.", NULL);
		g_clear_error(&error);
	}
	g_free(name);

	sond_treeviewfm_eingang_refresh(SOND_TREEVIEWFM(zond->treeview[BAUM_FS]));

	return ok;
}

/* ========================================================================
 * Dialog "Eingang setzen"
 * ======================================================================== */

typedef struct {
	GtkWidget *entry_datum;
	GtkWidget *popover;
	GtkWidget *calendar;
	gint cal_year;
	gint cal_month;   /* 0..11 wie bei GtkCalendar */
} DatumWidgets;

static void on_calendar_day_selected(GtkCalendar *calendar, gpointer data) {
	DatumWidgets *w = (DatumWidgets*) data;
	guint year = 0, month = 0, day = 0;
	gchar *text = NULL;
	gboolean same_month = FALSE;

	gtk_calendar_get_date(calendar, &year, &month, &day);

	text = g_strdup_printf("%02u.%02u.%04u", day, month + 1, year);
	gtk_entry_set_text(GTK_ENTRY(w->entry_datum), text);
	g_free(text);

	same_month = ((gint) year == w->cal_year && (gint) month == w->cal_month);
	w->cal_year = (gint) year;
	w->cal_month = (gint) month;

	//Monatswechsel im Kalender lässt das Fenster offen, ein Klick auf einen Tag schließt es
	if (same_month)
		gtk_popover_popdown(GTK_POPOVER(w->popover));
}

/* Beim Öffnen: Kalender auf das Datum im Feld stellen (sonst heute) */
static void on_popover_show(GtkWidget *popover, gpointer data) {
	DatumWidgets *w = (DatumWidgets*) data;
	gchar *iso = NULL;
	gint year = 0, month = 0, day = 0;

	if (sond_eingang_datum_parse(gtk_entry_get_text(GTK_ENTRY(w->entry_datum)),
			&iso)) {
		sscanf(iso, "%d-%d-%d", &year, &month, &day);
	} else {
		GDateTime *jetzt = g_date_time_new_now_local();

		year = g_date_time_get_year(jetzt);
		month = g_date_time_get_month(jetzt);
		day = g_date_time_get_day_of_month(jetzt);
		g_date_time_unref(jetzt);
	}
	g_free(iso);

	w->cal_year = year;
	w->cal_month = month - 1;

	g_signal_handlers_block_by_func(w->calendar, on_calendar_day_selected, w);
	gtk_calendar_select_month(GTK_CALENDAR(w->calendar), (guint) (month - 1),
			(guint) year);
	gtk_calendar_select_day(GTK_CALENDAR(w->calendar), (guint) day);
	g_signal_handlers_unblock_by_func(w->calendar, on_calendar_day_selected, w);
}

/* Fragt Datum und Art ab. TRUE bei OK; *iso ("JJJJ-MM-TT") und *art (nullable)
 * sind dann zu g_free()en. */
static gboolean datum_art_abfragen(Projekt *zond, const gchar *zusammenfassung,
		gint vorbelegung, gchar **iso, gchar **art) {
	GtkWidget *dialog = NULL;
	GtkWidget *content = NULL;
	GtkWidget *grid = NULL;
	GtkWidget *label = NULL;
	GtkWidget *btn_cal = NULL;
	GtkWidget *combo_art = NULL;
	GtkWidget *box_datum = NULL;
	DatumWidgets w = { 0 };
	gboolean ok = FALSE;
	gint response = 0;

	dialog = gtk_dialog_new_with_buttons("Eingang setzen",
			GTK_WINDOW(zond->app_window), GTK_DIALOG_MODAL,
			"_Abbrechen", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

	content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_container_set_border_width(GTK_CONTAINER(content), 12);
	gtk_box_set_spacing(GTK_BOX(content), 8);

	label = gtk_label_new(zusammenfassung);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
	gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);

	grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
	gtk_box_pack_start(GTK_BOX(content), grid, FALSE, FALSE, 0);

	//Datum: Feld und Kalender
	label = gtk_label_new("Eingang am:");
	gtk_label_set_xalign(GTK_LABEL(label), 1.0);
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);

	w.entry_datum = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(w.entry_datum), 11);
	gtk_entry_set_activates_default(GTK_ENTRY(w.entry_datum), TRUE);
	{
		gchar *text = NULL;

		if (vorbelegung > 0)
			text = sond_eingang_int_zu_anzeige(vorbelegung);
		else {
			gchar *heute = sond_eingang_datum_heute();

			text = sond_eingang_datum_anzeige(heute);
			g_free(heute);
		}
		gtk_entry_set_text(GTK_ENTRY(w.entry_datum), text);
		g_free(text);
	}

	w.calendar = gtk_calendar_new();
	w.popover = gtk_popover_new(NULL);
	gtk_container_add(GTK_CONTAINER(w.popover), w.calendar);
	gtk_widget_show_all(w.calendar);
	g_signal_connect(w.calendar, "day-selected",
			G_CALLBACK(on_calendar_day_selected), &w);
	g_signal_connect(w.popover, "show", G_CALLBACK(on_popover_show), &w);

	btn_cal = gtk_menu_button_new();
	gtk_container_add(GTK_CONTAINER(btn_cal),
			gtk_image_new_from_icon_name("x-office-calendar", GTK_ICON_SIZE_BUTTON));
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(btn_cal), w.popover);
	gtk_widget_set_tooltip_text(btn_cal, "Datum im Kalender wählen");

	box_datum = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_box_pack_start(GTK_BOX(box_datum), w.entry_datum, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box_datum), btn_cal, FALSE, FALSE, 0);
	gtk_grid_attach(GTK_GRID(grid), box_datum, 1, 0, 1, 1);

	//Art: Auswahlliste
	label = gtk_label_new("Art:");
	gtk_label_set_xalign(GTK_LABEL(label), 1.0);
	gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);

	combo_art = gtk_combo_box_text_new();
	for (const gchar *const *a = sond_eingang_arten(); *a; a++)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_art), *a);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo_art), 0);
	gtk_grid_attach(GTK_GRID(grid), combo_art, 1, 1, 1, 1);

	gtk_widget_show_all(dialog);

	while (!ok) {
		response = gtk_dialog_run(GTK_DIALOG(dialog));
		if (response != GTK_RESPONSE_OK)
			break;

		if (sond_eingang_datum_parse(
				gtk_entry_get_text(GTK_ENTRY(w.entry_datum)), iso)) {
			gint index = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_art));

			*art = NULL;
			if (index > 0)
				*art = g_strdup(sond_eingang_arten()[index]);
			ok = TRUE;
		} else
			display_message(dialog, "Eingangsdatum ungültig\n\n",
					"Bitte das Datum als TT.MM.JJJJ eingeben (oder im "
					"Kalender wählen).", NULL);
	}

	gtk_widget_destroy(dialog);

	return ok;
}

/* ========================================================================
 * Setzen und Entfernen
 * ======================================================================== */

void zond_eingang_setzen(Projekt *zond) {
	Auswahl auswahl = { 0 };
	gchar *zusammenfassung = NULL;
	gchar *iso = NULL;
	gchar *art = NULL;
	gint vorbelegung = 0;

	if (!eingang_bereit(zond))
		return;

	auswahl_init(&auswahl);
	if (!auswahl_holen(zond, &auswahl)) {
		auswahl_clear(&auswahl);

		return;
	}

	if (auswahl.pfade->len == 0) {
		display_message(zond->app_window, "Eingang setzen\n\n",
				"Bitte Dateien oder Ordner des Dateiverzeichnisses markieren. "
				"Einträge in Containern (ZIP, E-Mail, PDF-Anhänge) lassen sich "
				"nicht stempeln.", NULL);
		auswahl_clear(&auswahl);

		return;
	}

	if (auswahl.pfade->len == 1)
		zusammenfassung = g_strdup_printf("Eingang für %s",
				(const gchar*) g_ptr_array_index(auswahl.pfade, 0));
	else
		zusammenfassung = g_strdup_printf("Eingang für %u markierte Einträge",
				auswahl.pfade->len);
	if (auswahl.uebersprungen > 0) {
		gchar *mit = g_strdup_printf("%s (%u Einträge in Containern werden "
				"übergangen)", zusammenfassung, auswahl.uebersprungen);

		g_free(zusammenfassung);
		zusammenfassung = mit;
	}

	//Vorbelegung: das bisher wirksame Datum des ersten Eintrags
	vorbelegung = sond_eingang_cache_lookup(zond->eingang_cache,
			g_ptr_array_index(auswahl.pfade, 0),
			g_array_index(auswahl.ordner, gboolean, 0));

	if (datum_art_abfragen(zond, zusammenfassung, vorbelegung, &iso, &art)) {
		SondEingangZettel *zettel = sond_eingang_zettel_new(g_get_user_name(),
				"manuell");

		for (guint i = 0; i < auswahl.pfade->len; i++)
			sond_eingang_zettel_add(zettel, g_ptr_array_index(auswahl.pfade, i),
					g_array_index(auswahl.ordner, gboolean, i), iso, art, NULL, 0);

		zettel_ablegen(zond, zettel);
		sond_eingang_zettel_free(zettel);
	}

	g_free(iso);
	g_free(art);
	g_free(zusammenfassung);
	auswahl_clear(&auswahl);

	return;
}

void zond_eingang_umbenannt(Projekt *zond, const gchar *alt, const gchar *neu) {
	SondEingangZettel *zettel = NULL;
	GError *error = NULL;
	gchar *name = NULL;
	guint n = 0;

	if (!zond->eingang_cache || !alt || !neu)
		return;

	zettel = sond_eingang_zettel_new(g_get_user_name(), "umbenennen");
	n = sond_eingang_cache_uebertragen_zettel(zond->eingang_cache, zettel, alt,
			neu, TRUE);
	if (n == 0) { //nichts gestempelt: nichts nachzuführen
		sond_eingang_zettel_free(zettel);

		return;
	}

	name = sond_eingang_zettel_write(zettel, zond->project_dir, &error);
	if (!name || !sond_eingang_cache_apply(zond->eingang_cache, zettel, name,
			&error)) {
		display_message(zond->app_window,
				"Eingangsangaben konnten nicht auf den neuen Namen übertragen "
				"werden\n\n", error ? error->message : "unbekannter Fehler",
				"\n\nBitte mit \"Eingang setzen\" neu erfassen.", NULL);
		g_clear_error(&error);
	}

	g_free(name);
	sond_eingang_zettel_free(zettel);

	return;
}

/* Stempel an rel_path und darunter aufheben (Zettel mit der angegebenen Quelle).
 * Rückgabe: Anzahl der aufgehobenen Stempel; bei einem Fehler 0 und *error. */
static guint stempel_aufheben(Projekt *zond, const gchar *rel_path,
		const gchar *quelle, GError **error) {
	GPtrArray *stempel = NULL;
	SondEingangZettel *zettel = NULL;
	gchar *name = NULL;
	guint n = 0;

	stempel = sond_eingang_cache_stempel_unter(zond->eingang_cache, rel_path);
	if (stempel->len == 0) {
		g_ptr_array_unref(stempel);

		return 0;
	}

	zettel = sond_eingang_zettel_new(g_get_user_name(), quelle);
	for (guint i = 0; i < stempel->len; i++)
		sond_eingang_zettel_add_entfernen(zettel, g_ptr_array_index(stempel, i));

	name = sond_eingang_zettel_write(zettel, zond->project_dir, error);
	if (name && sond_eingang_cache_apply(zond->eingang_cache, zettel, name, error))
		n = stempel->len;

	g_free(name);
	sond_eingang_zettel_free(zettel);
	g_ptr_array_unref(stempel);

	return n;
}

gboolean zond_eingang_verschoben(Projekt *zond, const gchar *alt,
		const gchar *neu, gboolean ist_ordner, gboolean kopiert) {
	SondEingangZettel *zettel = NULL;
	GError *error = NULL;
	gchar *name = NULL;
	gchar *art = NULL;
	guint n = 0;
	gboolean ok = TRUE;

	if (!zond->eingang_cache || !alt)
		return FALSE;

	if (!neu) { //Ziel in einem Container: dort gibt es keine Stempel
		gboolean hatte_datum = sond_eingang_cache_wirksam(zond->eingang_cache,
				alt, NULL) > 0; //eigen oder geerbt

		if (kopiert) { //das Original behält seinen Eingang, die Kopie hat keinen
			if (hatte_datum)
				display_message(zond->app_window, "Eingang\n\n",
						"Die Kopie liegt in einem Container; dort gibt es keine "
						"Eingangsangaben. Das Original behält seinen Eingang.",
						NULL);

			return FALSE;
		}

		n = stempel_aufheben(zond, alt, "verschieben", &error);
		if (error) {
			display_message(zond->app_window,
					"Eingangsangaben konnten nicht aufgehoben werden\n\n",
					error->message, NULL);
			g_clear_error(&error);
		} else if (hatte_datum)
			display_message(zond->app_window, "Eingang\n\n",
					"Der Eintrag liegt jetzt in einem Container; dort gibt es "
					"keine Eingangsangaben.",
					NULL);

		return n > 0;
	}

	zettel = sond_eingang_zettel_new(g_get_user_name(),
			kopiert ? "kopie" : "verschieben");

	//Stempel des Eintrags und darunter ziehen mit bzw. werden mitkopiert
	n = sond_eingang_cache_uebertragen_zettel(zond->eingang_cache, zettel, alt,
			neu, !kopiert);

	/* Ohne eigenen Stempel erbt der Eintrag. Würde er am Ziel etwas anderes
	 * erben, den bisher wirksamen Stempel dort festschreiben (bei einem Ordner
	 * einmal für den Ordner, der Inhalt erbt weiter). */
	if (!sond_eingang_cache_has_own(zond->eingang_cache, alt)) {
		gint bisher = sond_eingang_cache_wirksam(zond->eingang_cache, alt, &art);

		if (bisher > 0 && sond_eingang_cache_wirksam(zond->eingang_cache, neu,
				NULL) != bisher) {
			gchar *iso = g_strdup_printf("%04d-%02d-%02d", bisher / 10000,
					(bisher / 100) % 100, bisher % 100);

			sond_eingang_zettel_add(zettel, neu, ist_ordner, iso, art, NULL, 0);
			g_free(iso);
			n++;
		}
		g_free(art);
	}

	if (n == 0) { //nichts zu übertragen
		sond_eingang_zettel_free(zettel);

		return FALSE;
	}

	name = sond_eingang_zettel_write(zettel, zond->project_dir, &error);
	if (!name || !sond_eingang_cache_apply(zond->eingang_cache, zettel, name,
			&error)) {
		display_message(zond->app_window,
				"Eingangsangaben konnten nicht auf den neuen Ort übertragen "
				"werden\n\n", error ? error->message : "unbekannter Fehler",
				"\n\nBitte mit \"Eingang setzen\" neu erfassen.", NULL);
		g_clear_error(&error);
		ok = FALSE;
	}

	g_free(name);
	sond_eingang_zettel_free(zettel);

	return ok;
}

void zond_eingang_geloescht(Projekt *zond, const gchar *rel_path) {
	GPtrArray *stempel = NULL;
	SondEingangZettel *zettel = NULL;
	GError *error = NULL;
	gchar *name = NULL;

	if (!zond->eingang_cache || !rel_path)
		return;

	stempel = sond_eingang_cache_stempel_unter(zond->eingang_cache, rel_path);
	if (stempel->len == 0) { //nichts gestempelt: kein Zettel
		g_ptr_array_unref(stempel);

		return;
	}

	zettel = sond_eingang_zettel_new(g_get_user_name(), "loeschen");
	for (guint i = 0; i < stempel->len; i++)
		sond_eingang_zettel_add_entfernen(zettel, g_ptr_array_index(stempel, i));

	name = sond_eingang_zettel_write(zettel, zond->project_dir, &error);
	if (!name || !sond_eingang_cache_apply(zond->eingang_cache, zettel, name,
			&error))
		LOG_WARN("%s: Eingangsangaben von '%s' nicht aufgehoben: %s", __func__,
				rel_path, error ? error->message : "?");
	g_clear_error(&error);

	g_free(name);
	sond_eingang_zettel_free(zettel);
	g_ptr_array_unref(stempel);

	return;
}

void zond_eingang_entfernen(Projekt *zond) {
	Auswahl auswahl = { 0 };
	GPtrArray *eigene = NULL;
	gchar *text = NULL;
	gint rc = 0;

	if (!eingang_bereit(zond))
		return;

	auswahl_init(&auswahl);
	if (!auswahl_holen(zond, &auswahl)) {
		auswahl_clear(&auswahl);

		return;
	}

	//nur Einträge mit eigenem Stempel; die übrigen erben ohnehin
	eigene = g_ptr_array_new();
	for (guint i = 0; i < auswahl.pfade->len; i++)
		if (sond_eingang_cache_has_own(zond->eingang_cache,
				g_ptr_array_index(auswahl.pfade, i)))
			g_ptr_array_add(eigene, g_ptr_array_index(auswahl.pfade, i));

	if (eigene->len == 0) {
		display_message(zond->app_window, "Eingang entfernen\n\n",
				"Für die markierten Einträge ist kein eigener Eingang gesetzt "
				"(sie erben ihn von einem übergeordneten Ordner oder haben "
				"keinen).", NULL);
	} else {
		text = g_strdup_printf("Eigenen Eingang von %u Eintrag/Einträgen "
				"entfernen? Sie erben danach den Eingang des übergeordneten "
				"Ordners.", eigene->len);
		rc = abfrage_frage(zond->app_window, "Eingang entfernen", text, NULL);
		g_free(text);

		if (rc == GTK_RESPONSE_YES) {
			SondEingangZettel *zettel = sond_eingang_zettel_new(
					g_get_user_name(), "manuell");

			for (guint i = 0; i < eigene->len; i++)
				sond_eingang_zettel_add_entfernen(zettel,
						g_ptr_array_index(eigene, i));

			zettel_ablegen(zond, zettel);
			sond_eingang_zettel_free(zettel);
		}
	}

	g_ptr_array_unref(eigene);
	auswahl_clear(&auswahl);

	return;
}
