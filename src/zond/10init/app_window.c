/*
 zond (app_window.c) - Akten, Beweisstücke, Unterlagen
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
#include <mupdf/fitz.h>

#include "../../misc.h"
#include "../zond_init.h"
#include "../zond_dbase.h"
#include "../zond_treeview.h"
#include "../zond_treeviewfm.h"

#include "../20allgemein/project.h"
#include "../20allgemein/suchen.h"

/* =============================================================================
 * KONSTANTEN
 * ========================================================================== */

#define APP_WINDOW_DEFAULT_WIDTH  800
#define APP_WINDOW_DEFAULT_HEIGHT 1000
#define PANED_LEFT_WIDTH          400
#define PANED_AUSWERTUNG_HEIGHT   500
#define TEXTVIEW_WINDOW_MIN_WIDTH 400
#define TEXTVIEW_WINDOW_MIN_HEIGHT 250
#define MIN_SEARCH_TEXT_LENGTH    3

/* =============================================================================
 * HILFS-FUNKTIONEN
 * ========================================================================== */

/**
 * Zeigt eine Fehlermeldung im Hauptfenster an
 */
static void show_error_message(GtkWidget *window, const gchar *context,
                                const gchar *error_msg) {
    display_message(window, context, error_msg, NULL);
}

/**
 * Holt die aktuelle node_id abhängig vom aktiven Textview
 */
static gint get_active_node_id(Projekt *zond, GtkTextBuffer *buffer) {
    if (buffer == gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview))) {
        return zond->node_id_act;
    } else {
        return zond->node_id_extra;
    }
}

/**
 * Speichert den Text aus einem TextBuffer in die Datenbank
 */
static void save_text_buffer_to_database(Projekt *zond, GtkTextBuffer *buffer) {
    GtkTextIter start, end;
    GError *error = NULL;
    gchar *text = NULL;
    gint node_id = 0;
    gint rc = 0;

    // Text aus Buffer holen
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    // Node-ID ermitteln
    node_id = get_active_node_id(zond, buffer);

    // In Datenbank speichern
    rc = zond_dbase_update_text(zond->dbase_zond->zond_dbase_work,
                                node_id, text, &error);
    g_free(text);

    if (rc) {
        show_error_message(zond->app_window,
                          "Fehler beim Speichern des Textes:\n\n"
                          "Bei Aufruf zond_dbase_update_text:\n",
                          error->message);
        g_error_free(error);
    }
}

/* =============================================================================
 * CALLBACK-FUNKTIONEN - FENSTER
 * ========================================================================== */

/**
 * Callback: Hauptfenster wird geschlossen
 */
static gboolean cb_delete_event(GtkWidget *app_window, GdkEvent *event,
                                gpointer user_data) {
    Projekt *zond = (Projekt*) user_data;
    gchar *errmsg = NULL;
    gint rc = 0;

    rc = project_close(zond, &errmsg);

    if (rc == -1) {
        show_error_message(zond->app_window,
                          "Fehler beim Schließen des Projekts -\n\n"
                          "Bei Aufruf projekt_schliessen:\n",
                          errmsg);
        g_free(errmsg);
        return TRUE;
    } else if (rc == 1) {
        return TRUE;
    }

    g_application_quit(G_APPLICATION(
        gtk_window_get_application(GTK_WINDOW(zond->app_window))));

    return TRUE;
}

/**
 * Callback: Extra-Textview-Fenster wird geschlossen
 */
static gboolean cb_close_textview(GtkWidget *window_textview, GdkEvent *event,
                                  gpointer user_data) {
    Projekt *zond = (Projekt*) user_data;

    gtk_widget_hide(window_textview);
    gtk_widget_set_sensitive(zond->menu.textview_extra, TRUE);
    zond->node_id_extra = 0;

    return TRUE;
}

/**
 * Callback: Maus-Button-Event (zum Tracken des Button-States)
 */
static gboolean cb_button_event(GtkWidget *app_window, GdkEvent *event,
                               gpointer data) {
    ((Projekt*) data)->state = event->button.state;
    return FALSE;
}

/* =============================================================================
 * CALLBACK-FUNKTIONEN - TEXTVIEW
 * ========================================================================== */

/**
 * Callback: TextBuffer wurde geändert
 */
static void cb_text_buffer_changed(GtkTextBuffer *buffer, gpointer data) {
    Projekt *zond = (Projekt*) data;
    save_text_buffer_to_database(zond, buffer);
}

/**
 * Callback: Textview erhält Focus
 */
static gboolean cb_textview_focus_in(GtkWidget *textview, GdkEvent *event,
                                    gpointer user_data) {
    Projekt *zond = (Projekt*) user_data;

    zond->text_buffer_changed_signal = g_signal_connect(
         gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview)),
         "changed",
         G_CALLBACK(cb_text_buffer_changed),
         zond);

     return FALSE;
 }

/**
 * Callback: Textview verliert Focus
 */
static gboolean cb_textview_focus_out(GtkWidget *textview, GdkEvent *event,
                                     gpointer user_data) {
    Projekt *zond = (Projekt*) user_data;

    if (zond->text_buffer_changed_signal) {
        g_signal_handler_disconnect(
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview)),
            zond->text_buffer_changed_signal);
        zond->text_buffer_changed_signal = 0;
    }

    gtk_widget_queue_draw(GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]));

    return FALSE;
}

/* =============================================================================
 * CALLBACK-FUNKTIONEN - TREEVIEW
 * ========================================================================== */

/**
 * Callback: TreeView verliert Focus
 */
static gboolean cb_treeview_focus_out(GtkWidget *treeview, GdkEvent *event,
                                      gpointer user_data) {
    Projekt *zond = (Projekt*) user_data;
    Baum baum = (Baum) sond_treeview_get_id(SOND_TREEVIEW(treeview));

    zond->baum_active = KEIN_BAUM;

    // Cursor-changed-Signal ausschalten (außer für BAUM_FS)
    if (baum != BAUM_FS && zond->cursor_changed_signal) {
        g_signal_handler_disconnect(treeview, zond->cursor_changed_signal);
        zond->cursor_changed_signal = 0;
    }

    // CellRenderer nicht mehr editierbar machen
    g_object_set(sond_treeview_get_cell_renderer_text(zond->treeview[baum]),
                "editable", FALSE, NULL);

    // NICHT MEHR: TextView hier deaktivieren
    // (wird in cb_treeview_focus_in gesteuert)

    zond->baum_prev = baum;

    return FALSE;
}

/**
 * Callback: TreeView erhält Focus
 */
static gboolean cb_treeview_focus_in(GtkWidget *treeview, GdkEvent *event,
                                    gpointer user_data) {
    Projekt *zond = (Projekt*) user_data;

    zond->baum_active = (Baum) sond_treeview_get_id(SOND_TREEVIEW(treeview));

    // Cursor-changed-Signal anschalten (außer für BAUM_FS)
    if (zond->baum_active != BAUM_FS) {
        zond->cursor_changed_signal = g_signal_connect(
            treeview,
            "cursor-changed",
            G_CALLBACK(zond_treeview_cursor_changed),
            zond);

        g_signal_emit_by_name(treeview, "cursor-changed", user_data, NULL);
    }

    // Selection in vorherigem TreeView löschen, wenn sich Baum geändert hat
    if (zond->baum_active != zond->baum_prev) {
        gtk_tree_selection_unselect_all(zond->selection[zond->baum_prev]);

        // Cursor im neuen TreeView selektieren
        GtkTreePath *path = NULL;
        gtk_tree_view_get_cursor(GTK_TREE_VIEW(treeview), &path, NULL);
        if (path) {
            gtk_tree_selection_select_path(
                zond->selection[zond->baum_active], path);
            gtk_tree_path_free(path);
        }
    }

    // CellRenderer editierbar machen
     g_object_set(
         sond_treeview_get_cell_renderer_text(zond->treeview[zond->baum_active]),
         "editable", TRUE, NULL);

     // TextView-Steuerung abhängig vom aktiven Baum
     if (zond->baum_active == BAUM_AUSWERTUNG) {
         // BAUM_AUSWERTUNG hat Focus: TextView aktivieren (wenn Row gewählt)
         GtkTreePath *path = NULL;
         gtk_tree_view_get_cursor(GTK_TREE_VIEW(treeview), &path, NULL);
         if (path) {
             gtk_widget_set_sensitive(GTK_WIDGET(zond->textview), TRUE);
             gtk_tree_path_free(path);
         }
     } else {
         // Anderer Baum hat Focus: TextView deaktivieren (aber Inhalt behalten!)
         gtk_widget_set_sensitive(GTK_WIDGET(zond->textview), FALSE);
     }

     return FALSE;
 }

/**
 * Callback: Taste in TreeView gedrückt
 */
static gboolean cb_treeview_key_press(GtkWidget *treeview, GdkEventKey event,
                                     gpointer data) {
    Projekt *zond = (Projekt*) data;

    // Nur normale Zeichen öffnen das Suchfeld
    if (event.is_modifier ||
        (event.state & GDK_CONTROL_MASK) ||
        (event.keyval < 0x21) ||
        (event.keyval > 0x7e)) {
        return FALSE;
    }

    gtk_popover_popup(GTK_POPOVER(zond->popover));

    return FALSE;
}

/* =============================================================================
 * CALLBACK-FUNKTIONEN - SUCHE
 * ========================================================================== */

/**
 * Callback: Suche ausführen
 */
static void cb_entry_search(GtkWidget *entry, gpointer data) {
    Projekt *zond = (Projekt*) data;
    const gchar *text = NULL;
    gchar *search_text = NULL;
    gchar *errmsg = NULL;
    gint rc = 0;

    text = gtk_entry_get_text(GTK_ENTRY(entry));

    // Mindestens 3 Zeichen erforderlich
    if (!text || strlen(text) < MIN_SEARCH_TEXT_LENGTH) {
        return;
    }

    // Wildcards hinzufügen
    search_text = g_strconcat("%", text, "%", NULL);
    rc = suchen_treeviews(zond, search_text, &errmsg);
    g_free(search_text);

    if (rc) {
        show_error_message(zond->app_window,
                          "Fehler bei der Suche -\n\n"
                          "Bei Aufruf suchen_treeviews:\n",
                          errmsg);
        g_free(errmsg);
    }

    gtk_popover_popdown(GTK_POPOVER(zond->popover));
}

/* =============================================================================
 * INITIALISIERUNGS-FUNKTIONEN
 * ========================================================================== */

/**
 * Initialisiert alle TreeViews und deren Selections
 */
static void init_treeviews(Projekt *zond) {
    // TreeViews erstellen
    zond->treeview[BAUM_FS] = SOND_TREEVIEW(zond_treeviewfm_new(zond));
    zond->treeview[BAUM_INHALT] = SOND_TREEVIEW(
        zond_treeview_new(zond, (gint) BAUM_INHALT));
    zond->treeview[BAUM_AUSWERTUNG] = SOND_TREEVIEW(
        zond_treeview_new(zond, (gint) BAUM_AUSWERTUNG));

    // Für jeden Baum: Selection holen und Callbacks verbinden
    for (Baum baum = BAUM_FS; baum < NUM_BAUM; baum++) {
        zond->selection[baum] = gtk_tree_view_get_selection(
            GTK_TREE_VIEW(zond->treeview[baum]));

        g_signal_connect(zond->treeview[baum], "focus-in-event",
                        G_CALLBACK(cb_treeview_focus_in), zond);
        g_signal_connect(zond->treeview[baum], "focus-out-event",
                        G_CALLBACK(cb_treeview_focus_out), zond);
    }
}

/**
 * Erstellt einen TextView mit Standard-Einstellungen
 */
static GtkWidget* create_configured_textview(Projekt *zond) {
    GtkWidget *text_view = gtk_text_view_new();

    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(text_view), FALSE);

    g_signal_connect(text_view, "focus-in-event",
                    G_CALLBACK(cb_textview_focus_in), zond);
    g_signal_connect(text_view, "focus-out-event",
                    G_CALLBACK(cb_textview_focus_out), zond);

    return text_view;
}

/**
 * Erstellt ein ScrolledWindow mit Widget
 */
static GtkWidget* create_scrolled_window(GtkWidget *child) {
    GtkWidget *swindow = gtk_scrolled_window_new(NULL, NULL);

    if (child) {
        gtk_container_add(GTK_CONTAINER(swindow), child);
    }

    return swindow;
}

/**
 * Initialisiert das Suchfeld (Popover)
 */
static void init_search_popover(Projekt *zond) {
    GtkWidget *entry_search = NULL;

    zond->popover = gtk_popover_new(GTK_WIDGET(zond->treeview[BAUM_INHALT]));
    entry_search = gtk_entry_new();
    gtk_widget_show(entry_search);
    gtk_container_add(GTK_CONTAINER(zond->popover), entry_search);

    g_signal_connect(entry_search, "activate",
                    G_CALLBACK(cb_entry_search), zond);
}

/**
 * Initialisiert das Extra-Textview-Fenster
 */
static void init_extra_textview_window(Projekt *zond) {
    GtkWidget *swindow = NULL;

    // Neues Fenster erstellen
    zond->textview_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_size_request(zond->textview_window,
                               TEXTVIEW_WINDOW_MIN_WIDTH,
                               TEXTVIEW_WINDOW_MIN_HEIGHT);

    // ScrolledWindow
    swindow = create_scrolled_window(NULL);
    gtk_container_add(GTK_CONTAINER(zond->textview_window), swindow);

    // TextView erstellen und hinzufügen
    zond->textview_ii = create_configured_textview(zond);
    gtk_container_add(GTK_CONTAINER(swindow), GTK_WIDGET(zond->textview_ii));

    // Close-Signal
    g_signal_connect(zond->textview_window, "delete-event",
                    G_CALLBACK(cb_close_textview), zond);
}

/**
 * Initialisiert die TreeView-Ansichten in der GUI
 */
static void init_treeview_layout(Projekt *zond) {
    GtkWidget *swindow_baum_fs = NULL;
    GtkWidget *swindow_baum_inhalt = NULL;
    GtkWidget *hpaned_inner = NULL;
    GtkWidget *paned_baum_auswertung = NULL;
    GtkWidget *swindow_treeview_auswertung = NULL;
    GtkWidget *swindow_textview = NULL;

    // BAUM_FS (linkes Panel)
    swindow_baum_fs = create_scrolled_window(
        GTK_WIDGET(zond->treeview[BAUM_FS]));
    gtk_paned_add1(GTK_PANED(zond->hpaned), swindow_baum_fs);

    // Innerer HPaned für rechte Seite
    hpaned_inner = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_add2(GTK_PANED(zond->hpaned), hpaned_inner);

    // BAUM_INHALT
    swindow_baum_inhalt = create_scrolled_window(
        GTK_WIDGET(zond->treeview[BAUM_INHALT]));

    // BAUM_AUSWERTUNG mit vertikalem Paned
    paned_baum_auswertung = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_position(GTK_PANED(paned_baum_auswertung),
                          PANED_AUSWERTUNG_HEIGHT);

    swindow_treeview_auswertung = create_scrolled_window(
        GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]));
    gtk_paned_pack1(GTK_PANED(paned_baum_auswertung),
                   swindow_treeview_auswertung, TRUE, TRUE);

    // TextView in unterem Teil des VPaned
    swindow_textview = create_scrolled_window(NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swindow_textview),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_paned_pack2(GTK_PANED(paned_baum_auswertung),
                   swindow_textview, TRUE, TRUE);

    // TextView erstellen und konfigurieren
    zond->textview = create_configured_textview(zond);

    // Mark am Ende des Buffers erstellen
    GtkTextIter text_iter = { 0 };
    gtk_text_buffer_get_end_iter(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)),
        &text_iter);
    gtk_text_buffer_create_mark(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)),
        "ende-text", &text_iter, FALSE);

    gtk_container_add(GTK_CONTAINER(swindow_textview),
                     GTK_WIDGET(zond->textview));

    // Layout zusammenfügen: Links BAUM_INHALT, rechts BAUM_AUSWERTUNG
    gtk_paned_pack1(GTK_PANED(hpaned_inner), swindow_baum_inhalt, TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(hpaned_inner), paned_baum_auswertung, TRUE, TRUE);
}

/**
 * Initialisiert die Statuszeile
 */
static void init_statusbar(Projekt *zond, GtkWidget *vbox) {
    zond->label_status = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(zond->label_status, 0.02);
    gtk_box_pack_end(GTK_BOX(vbox), GTK_WIDGET(zond->label_status),
                    FALSE, FALSE, 0);
}

/* =============================================================================
 * HAUPT-INITIALISIERUNGS-FUNKTION
 * ========================================================================== */

/**
 * Initialisiert das Hauptfenster der Anwendung
 *
 * Erstellt die komplette GUI-Struktur mit:
 * - Hauptfenster mit TreeViews
 * - TextViews für Notizen
 * - Suchfunktion
 * - Statuszeile
 * - Extra-Textview-Fenster
 */
void init_app_window(Projekt *zond) {
    GtkWidget *vbox = NULL;
    SondTreeviewClass *stv_class = NULL;

    // Hauptfenster erstellen
    zond->app_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(zond->app_window),
                               APP_WINDOW_DEFAULT_WIDTH,
                               APP_WINDOW_DEFAULT_HEIGHT);

    // Vertikale Box für Layout
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(zond->app_window), vbox);

    // Horizontaler Paned (Hauptbereich)
    zond->hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(zond->hpaned), PANED_LEFT_WIDTH);
    gtk_box_pack_start(GTK_BOX(vbox), zond->hpaned, TRUE, TRUE, 0);

    // TreeView-Klasse konfigurieren (für Key-Press-Callback)
    stv_class = g_type_class_ref(SOND_TYPE_TREEVIEW);
    stv_class->callback_key_press_event = cb_treeview_key_press;
    stv_class->callback_key_press_event_func_data = zond;

    // TreeViews initialisieren
    init_treeviews(zond);

    // Layout der TreeViews erstellen
    init_treeview_layout(zond);

    // Maus-Button-Events für Haupt-Fenster
    g_signal_connect(zond->app_window, "button-press-event",
                    G_CALLBACK(cb_button_event), zond);
    g_signal_connect(zond->app_window, "button-release-event",
                    G_CALLBACK(cb_button_event), zond);

    // Suchfeld initialisieren
    init_search_popover(zond);

    // Statuszeile erstellen
    init_statusbar(zond, vbox);

    // Delete-Event für Hauptfenster
    g_signal_connect(zond->app_window, "delete-event",
                    G_CALLBACK(cb_delete_event), zond);

    // Extra-Textview-Fenster initialisieren
    init_extra_textview_window(zond);

    // BUGFIX: TextView initial deaktivieren
    // Wird aktiviert wenn Row in BAUM_AUSWERTUNG gewählt wird
    gtk_widget_set_sensitive(GTK_WIDGET(zond->textview), FALSE);
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)), "", -1);
    zond->node_id_act = 0;
}
