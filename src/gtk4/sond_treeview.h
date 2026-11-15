/*
 sond (gtk4/sond_treeview.h) - Akten, Beweisstücke, Unterlagen
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

/**
 * @file gtk4/sond_treeview.h
 * @brief Baum-Widget für GTK4 (Basisklasse)
 *
 * Neufassung von src/sond_treeview.h für GTK4: GtkColumnView auf einem
 * GtkTreeListModel statt GtkTreeView/GtkTreeStore. Die Zeilen sind beliebige
 * GObjects (im Dateibaum SondTVFMItem); Kinder liefert die Unterklasse über
 * create_children(). Das Widget ist eine GtkBox, weil GtkColumnView nicht
 * ableitbar ist.
 *
 * Die GTK3-Fassung bleibt in src/, solange Zond sie braucht. Beide haben bewusst
 * dieselben Typnamen; sie landen nie im selben Programm.
 */

#ifndef SOND_GTK4_TREEVIEW_H_INCLUDED
#define SOND_GTK4_TREEVIEW_H_INCLUDED

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SOND_TYPE_TREEVIEW sond_treeview_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondTreeview, sond_treeview, SOND, TREEVIEW, GtkBox)

struct _SondTreeviewClass {
	GtkBoxClass parent_class;

	/* Kinder eines Eintrags: neue Liste (GListModel von GObjects; Ownership geht
	 * an den Aufrufer) oder NULL, wenn der Eintrag keine hat. Wird beim
	 * Aufklappen aufgerufen. */
	GListModel* (*create_children)(SondTreeview*, gpointer item);
};

/* Setzt die Einträge der obersten Ebene (Ownership von roots geht an das Widget).
 * Ersetzt einen bisherigen Baum samt Auswahl. */
void sond_treeview_set_roots(SondTreeview*, GListModel *roots);

GtkColumnView* sond_treeview_get_column_view(SondTreeview*);

/* Hängt eine Spalte an. factory: Zellaufbau (die Zeilenobjekte sind
 * GtkTreeListRow; sond_treeview_listitem_get_item() liefert das Objekt dahinter).
 * sorter (nullable): Vergleich der Objekte (ohne GtkTreeListRow-Hülle). Die
 * Referenzen auf factory und sorter gehen an die Spalte (beide frisch erzeugt
 * übergeben, danach nicht mehr freigeben). */
GtkColumnViewColumn* sond_treeview_add_column(SondTreeview*, const gchar *title,
		GtkListItemFactory *factory, GtkSorter *sorter, gboolean expand);

/* Das Objekt hinter dem GtkListItem einer Zeile; neue Referenz (g_object_unref()) */
gpointer sond_treeview_listitem_get_item(GtkListItem*);

/* Kontextmenü: Modell (die Unterklasse füllt es) und Aktionen (Präfix "tv") */
GMenu* sond_treeview_get_menu(SondTreeview*);

GActionMap* sond_treeview_get_action_map(SondTreeview*);

/* Macht cell zum Auslöser des Kontextmenüs (Rechtsklick): die Zeile wird
 * markiert, falls sie es nicht war, dann erscheint das Menü. */
void sond_treeview_attach_context_menu(GtkWidget *cell, GtkListItem *listitem);

/* Die markierten Objekte (Referenzen, g_ptr_array_unref()) */
GPtrArray* sond_treeview_get_selected_items(SondTreeview*);

/* Die markierten Zeilen (GtkTreeListRow, Referenzen, g_ptr_array_unref()), in der
 * Reihenfolge der Anzeige */
GPtrArray* sond_treeview_get_selected_rows(SondTreeview*);

/* Die Liste, in der die Zeile steht (Kinder der Elternzeile bzw. die oberste
 * Ebene); nicht freigeben. Änderungen an der Liste erscheinen im Baum. */
GListModel* sond_treeview_row_get_container(SondTreeview*, GtkTreeListRow*);

/* Die oberste Ebene (die bei sond_treeview_set_roots() übergebene Liste); nicht
 * freigeben */
GListModel* sond_treeview_get_roots(SondTreeview*);

/* Findet unter den angezeigten Zeilen die erste, deren Objekt match erfüllt */
typedef gboolean (*SondTreeviewMatchFunc)(gpointer item, gpointer user_data);

/* Die Zeile (neue Referenz, g_object_unref()) oder NULL; *position (optional) ist
 * ihre Stelle in der angezeigten (sortierten) Liste */
GtkTreeListRow* sond_treeview_find_row(SondTreeview*, SondTreeviewMatchFunc match,
		gpointer user_data, guint *position);

/* Markiert die gefundene Zeile allein und blättert dorthin; FALSE, wenn keine
 * angezeigt wird */
gboolean sond_treeview_select_matching(SondTreeview*, SondTreeviewMatchFunc match,
		gpointer user_data);

/* Markiert das Objekt (sofern sichtbar) allein und blättert dorthin */
gboolean sond_treeview_select_item(SondTreeview*, gpointer item);

G_END_DECLS

#endif // SOND_GTK4_TREEVIEW_H_INCLUDED
