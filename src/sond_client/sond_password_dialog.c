/*
 sond (sond_dokumente.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_password_dialog.h"

enum {
    ANTWORT_OFFEN = -1,
    ANTWORT_OK = 0,
    ANTWORT_UEBERSPRINGEN,
    ANTWORT_ALLE_UEBERSPRINGEN,
    ANTWORT_ABBRECHEN
};

typedef struct {
    GMainLoop *loop;
    gint antwort;
} PwDlg;

static void antwort_setzen(PwDlg *dlg, gint antwort) {
    if (dlg->antwort != ANTWORT_OFFEN)
        return;

    dlg->antwort = antwort;
    g_main_loop_quit(dlg->loop);
}

static void on_knopf(GtkButton *knopf, gpointer user_data) {
    antwort_setzen(user_data, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(knopf), "antwort")));
}

static void on_aktiv(GtkWidget *eingabe, gpointer user_data) {
    (void) eingabe;

    antwort_setzen(user_data, ANTWORT_OK);
}

static gboolean on_schliessen(GtkWindow *fenster, gpointer user_data) {
    (void) fenster;

    antwort_setzen(user_data, ANTWORT_ABBRECHEN);

    return TRUE; //zerstört wird nach der Schleife
}

static GtkWidget* knopf_neu(GtkWidget *zeile, const gchar *text, gint antwort, PwDlg *dlg) {
    GtkWidget *knopf = gtk_button_new_with_label(text);

    g_object_set_data(G_OBJECT(knopf), "antwort", GINT_TO_POINTER(antwort));
    g_signal_connect(knopf, "clicked", G_CALLBACK(on_knopf), dlg);
    gtk_box_append(GTK_BOX(zeile), knopf);

    return knopf;
}

//das gerade aktive Fenster, sonst irgendeines
static GtkWindow* aktives_fenster(void) {
    GListModel *fenster = gtk_window_get_toplevels();
    GtkWindow *erstes = NULL;

    for (guint i = 0; i < g_list_model_get_n_items(fenster); i++) {
        GtkWindow *w = g_list_model_get_item(fenster, i);

        g_object_unref(w);
        if (gtk_window_is_active(w))
            return w;
        if (!erstes && gtk_widget_get_visible(GTK_WIDGET(w)))
            erstes = w;
    }

    return erstes;
}

static SondPromptResult passwort_dialog(gchar const *was, gboolean falsch, gboolean ueberspringen_erlaubt,
        gchar **passwort, gpointer user_data) {
    PwDlg dlg = { g_main_loop_new(NULL, FALSE), ANTWORT_OFFEN };
    GtkWindow *eltern = aktives_fenster();
    GtkWidget *fenster = gtk_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *label = NULL;
    GtkWidget *eingabe = gtk_password_entry_new();
    GtkWidget *zeile = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *ok = NULL;
    SondPromptResult ergebnis = SOND_PROMPT_CANCEL;

    (void) user_data;

    gtk_window_set_title(GTK_WINDOW(fenster), "Passwort");
    gtk_window_set_modal(GTK_WINDOW(fenster), TRUE);
    if (eltern)
        gtk_window_set_transient_for(GTK_WINDOW(fenster), eltern);
    gtk_window_set_resizable(GTK_WINDOW(fenster), FALSE);
    g_signal_connect(fenster, "close-request", G_CALLBACK(on_schliessen), &dlg);

    gtk_widget_set_margin_start(box, 14);
    gtk_widget_set_margin_end(box, 14);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_window_set_child(GTK_WINDOW(fenster), box);

    label = gtk_label_new(was);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 50);
    gtk_box_append(GTK_BOX(box), label);

    if (falsch) {
        label = gtk_label_new("Das Passwort ist falsch.");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_add_css_class(label, "error");
        gtk_box_append(GTK_BOX(box), label);
    }

    gtk_widget_set_size_request(eingabe, 320, -1);
    g_signal_connect(eingabe, "activate", G_CALLBACK(on_aktiv), &dlg);
    gtk_box_append(GTK_BOX(box), eingabe);

    gtk_widget_set_halign(zeile, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(box), zeile);
    if (ueberspringen_erlaubt) {
        knopf_neu(zeile, "Alle überspringen", ANTWORT_ALLE_UEBERSPRINGEN, &dlg);
        knopf_neu(zeile, "Überspringen", ANTWORT_UEBERSPRINGEN, &dlg);
    }
    knopf_neu(zeile, "Abbrechen", ANTWORT_ABBRECHEN, &dlg);
    ok = knopf_neu(zeile, "OK", ANTWORT_OK, &dlg);
    gtk_widget_add_css_class(ok, "suggested-action");

    gtk_window_present(GTK_WINDOW(fenster));
    gtk_widget_grab_focus(eingabe);

    g_main_loop_run(dlg.loop);

    switch (dlg.antwort) {
    case ANTWORT_OK: {
        const gchar *text = gtk_editable_get_text(GTK_EDITABLE(eingabe));

        //leere Eingabe zählt nicht als Passwort
        if (text && *text) {
            *passwort = g_strdup(text);
            ergebnis = SOND_PROMPT_OK;
        }
        break;
    }
    case ANTWORT_UEBERSPRINGEN:
        ergebnis = SOND_PROMPT_SKIP;
        break;
    case ANTWORT_ALLE_UEBERSPRINGEN:
        ergebnis = SOND_PROMPT_SKIP_ALL;
        break;
    default:
        break;
    }

    gtk_editable_set_text(GTK_EDITABLE(eingabe), ""); //Eingabepuffer leeren
    gtk_window_destroy(GTK_WINDOW(fenster));
    g_main_loop_unref(dlg.loop);

    return ergebnis;
}

void sond_client_password_dialog_init(void) {
    sond_password_set_prompt_func(passwort_dialog, NULL);
}
