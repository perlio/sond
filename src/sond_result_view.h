#ifndef SOND_RESULT_VIEW_H_INCLUDED
#define SOND_RESULT_VIEW_H_INCLUDED

/*
 * sond_result_view.h — Allgemeines Ergebnisfenster mit konfigurierbaren Spalten
 *
 * Ersetzt result_listbox_new (GtkListBox mit Labels) durch einen GtkTreeView
 * mit beliebig vielen Textspalten. Kann für Dateisuche (1 Spalte) wie auch
 * für Index-Suche (Datei | Seite | Fundstelle) verwendet werden.
 *
 * Verwendung:
 *
 *   gchar const *cols[] = { "Datei", "Seite", "Fundstelle", NULL };
 *   GtkWidget *rv = sond_result_view_new(GTK_WINDOW(parent), "Suchergebnis",
 *                                        cols, cb, userdata);
 *
 *   gchar const *row[] = { "akte/vernehmung.pdf", "12", "...Müller sagte...", NULL };
 *   sond_result_view_append(rv, row);
 *
 *   gtk_widget_show_all(rv);
 */

#include <gtk/gtk.h>

G_BEGIN_DECLS

/**
 * sond_result_view_new:
 * @parent:           Elternfenster (darf NULL sein)
 * @title:            Fenstertitel
 * @column_titles:    NULL-terminiertes Array von Spaltentiteln
 * @row_activated_cb: Callback wenn Zeile doppelgeklickt/Enter gedrückt wird.
 *                    Signatur: void cb(GtkTreeView*, GtkTreePath*,
 *                                      GtkTreeViewColumn*, gpointer userdata)
 *                    Darf NULL sein.
 * @cb_data:          userdata für den Callback
 *
 * Erstellt ein neues Ergebnisfenster. Das Fenster ist noch nicht sichtbar —
 * nach dem Befüllen gtk_widget_show_all() aufrufen.
 *
 * Returns: (transfer none) Das GtkWindow-Widget.
 */
GtkWidget* sond_result_view_new(GtkWindow       *parent,
                                 gchar const     *title,
                                 gchar const    **column_titles,
                                 GCallback        row_activated_cb,
                                 gpointer         cb_data);

/**
 * sond_result_view_append:
 * @result_view: Das von sond_result_view_new() zurückgegebene Fenster
 * @values:      NULL-terminiertes Array von Strings, eines pro Spalte
 *
 * Fügt eine Zeile ein. Überzählige Werte werden ignoriert,
 * fehlende als "" behandelt.
 */
void sond_result_view_append(GtkWidget    *result_view,
                              gchar const **values);

/**
 * sond_result_view_get_treeview:
 * @result_view: Das Ergebnisfenster
 *
 * Returns: (transfer none) Der interne GtkTreeView.
 */
GtkWidget* sond_result_view_get_treeview(GtkWidget *result_view);

/**
 * sond_result_view_get_selected_value:
 * @result_view: Das Ergebnisfenster
 * @column:      Spaltenindex (0-basiert)
 *
 * Returns: (transfer full) Wert der gewählten Zeile, mit g_free() freigeben.
 *          NULL wenn keine Zeile gewählt.
 */
gchar* sond_result_view_get_selected_value(GtkWidget *result_view,
                                            gint       column);

G_END_DECLS

#endif /* SOND_RESULT_VIEW_H_INCLUDED */
