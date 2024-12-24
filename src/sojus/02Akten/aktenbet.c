#include "../globals.h"
#include "../global_types_sojus.h"

#include "../../misc.h"

GPtrArray*
aktenbet_oeffnen(GtkWidget *akten_window, gint regnr, gint jahr) {
	Sojus *sojus = (Sojus*) g_object_get_data(G_OBJECT(akten_window), "sojus");

	//Aktenbeteiligte
	gchar *sql = g_strdup_printf("SELECT * FROM Aktenbet WHERE "
			"RegNr = %i AND RegJahr = %i;", regnr, jahr);
	gint rc = mysql_query(sojus->db.con, sql);
	g_free(sql);
	if (rc) {
		display_message(akten_window, "Fehler bei aktenbet_oeffnen\n",
				"mysql_query:\n", mysql_error(sojus->db.con), NULL);
		return NULL;
	}

	MYSQL_RES *mysql_res = mysql_store_result(sojus->db.con);
	if (!mysql_res) {
		display_message(akten_window, "Fehler bei aktenbet_oeffnen:\n",
				"mysql_store_result:\n", mysql_error(sojus->db.con), NULL);
		return NULL;
	}

	GPtrArray *arr_aktenbet = g_object_get_data(G_OBJECT(akten_window),
			"arr_aktenbet");

	MYSQL_ROW row = NULL;
	while ((row = mysql_fetch_row(mysql_res))) {
		Aktenbet *aktenbet = g_malloc0(sizeof(Aktenbet));
		g_ptr_array_add(arr_aktenbet, aktenbet);

		aktenbet->ID = (gint) g_ascii_strtoll(row[0], NULL, 10);
		aktenbet->adressnr = (gint) g_ascii_strtoll(row[3], NULL, 10);
		aktenbet->betart = g_strdup(row[4]);
		aktenbet->betreff1 = g_strdup(row[5]);
		aktenbet->betreff2 = g_strdup(row[6]);
		aktenbet->betreff3 = g_strdup(row[7]);
	}

	mysql_free_result(mysql_res);

	return arr_aktenbet;
}

void aktenbet_free(gpointer user_data) {
	Aktenbet *akten_bet = (Aktenbet*) user_data;

	g_free(akten_bet->betreff1);
	g_free(akten_bet->betreff2);
	g_free(akten_bet->betreff3);

	g_free(akten_bet);

	return;
}

void aktenbet_speichern(GtkWidget *akten_window) {
	GPtrArray *arr_aktenbet = g_object_get_data(G_OBJECT(akten_window),
			"arr_aktenbet");
	Sojus *sojus = g_object_get_data(G_OBJECT(akten_window), "sojus");

	for (gint i = 0; i < arr_aktenbet->len; i++) {
		gchar *sql = NULL;
		Aktenbet *aktenbet = g_ptr_array_index(arr_aktenbet, i);

		//Alt-Aktenbeteiligter und Datensatz nicht gelöscht aber geändert
		if ((aktenbet->ID > 0) && (aktenbet->adressnr > 0)
				&& (aktenbet->geaendert))
			sql = g_strdup_printf("UPDATE Aktenbet SET Adressnr=%i, "
					"Beteiligtenart='%s', Betreff1='%s', Betreff2='%s', "
					"Betreff3='%s' WHERE ID=%i;", aktenbet->adressnr,
					aktenbet->betart, aktenbet->betreff1, aktenbet->betreff2,
					aktenbet->betreff3, aktenbet->ID);
		//Alt-Aktenbeteiligter, gelöscht
		else if ((aktenbet->ID > 0) && (aktenbet->adressnr == 0))
			sql = g_strdup_printf("DELETE FROM Aktenbet WHERE ID=%i;",
					aktenbet->ID);
		//neuer Aktenbet
		else if ((aktenbet->ID == 0))
			sql = g_strdup_printf("INSERT INTO Aktenbet (RegNr, RegJahr, "
					"Adressnr, `Beteiligtenart`, Betreff1, Betreff2, Betreff3) "
					"VALUES (%i, %i, %i, '%s', '%s', '%s', '%s');",
					sojus->regnr_akt, sojus->jahr_akt, aktenbet->adressnr,
					aktenbet->betart, aktenbet->betreff1, aktenbet->betreff2,
					aktenbet->betreff3);

		if (sql) {
			gint rc = mysql_query(sojus->db.con, sql);
			if (rc) {
				display_message(akten_window, "Fehler bei aktenbet_speichern\n",
						"mysql_query:\n", sql, mysql_error(sojus->db.con),
						NULL);
				return;
			}

			sql_log(sojus, sql);
			g_free(sql);
		}
	}

	return;
}

Aktenbet*
aktenbet_einlesen(GtkWidget *akten_window) {
	GtkWidget *listbox_aktenbet = g_object_get_data(G_OBJECT(akten_window),
			"listbox_aktenbet");
	GtkListBoxRow *row = gtk_list_box_get_selected_row(
			GTK_LIST_BOX(listbox_aktenbet));

	Aktenbet *aktenbet = (Aktenbet*) g_object_get_data(G_OBJECT(row),
			"aktenbet");

	gchar *entry_adressnr_text =
			(gchar*) gtk_entry_get_text(
					GTK_ENTRY(
							g_object_get_data( G_OBJECT(akten_window), "entry_adressnr" )));
	aktenbet->adressnr = atoi(entry_adressnr_text);

	aktenbet->betart =
			gtk_combo_box_text_get_active_text(
					GTK_COMBO_BOX_TEXT(
							g_object_get_data( G_OBJECT(akten_window), "combo_beteiligtenart" )));

	g_free(aktenbet->betreff1);
	aktenbet->betreff1 =
			g_strdup(
					gtk_entry_get_text( GTK_ENTRY(g_object_get_data( G_OBJECT(akten_window), "entry_betreff1" )) ));

	g_free(aktenbet->betreff2);
	aktenbet->betreff2 =
			g_strdup(
					gtk_entry_get_text( GTK_ENTRY(g_object_get_data( G_OBJECT(akten_window), "entry_betreff2" )) ));

	g_free(aktenbet->betreff3);
	aktenbet->betreff3 =
			g_strdup(
					gtk_entry_get_text( GTK_ENTRY(g_object_get_data( G_OBJECT(akten_window), "entry_betreff3" )) ));

	return aktenbet;
}

Aktenbet*
aktenbet_neu(void) {
	Aktenbet *aktenbet = g_malloc0(sizeof(Aktenbet));

	aktenbet->betart = g_strdup("");
	aktenbet->betreff1 = g_strdup("");
	aktenbet->betreff2 = g_strdup("");
	aktenbet->betreff3 = g_strdup("");

	return aktenbet;
}
