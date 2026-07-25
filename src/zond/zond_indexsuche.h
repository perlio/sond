#ifndef ZOND_INDEXSUCHE_H_INCLUDED
#define ZOND_INDEXSUCHE_H_INCLUDED

#include <gtk/gtk.h>

typedef struct _Projekt Projekt;
typedef struct _SondTreeviewFM SondTreeviewFM;

void zond_indexsuche_activate(GtkMenuItem *item, gpointer data);

void zond_indexsuche_activate_with_selection(GtkMenuItem *item,
		GHashTable* ht_fileparts, gpointer data);

/**
 * zond_indexsuche_row_activated:
 *
 * row-activated-Callback für ein sond_result_view mit den Spalten
 * Datei | Seite | Fundstelle | char_pos_in_page (versteckt) |
 * rohe Seite (versteckt) - springt zum Treffer (Anbindung wird unmittelbar
 * vor der Navigation live gegen anhängige Page-Inserts/-Deletes im ggf.
 * offenen Viewer übersetzt). Erwartet optional am Toplevel-Widget des
 * treeview die widget-data "index-search-term" (Suchbegriff für Highlighting
 * im Viewer) - fehlt sie (z.B. bei semantischen Chat-Treffern ohne
 * wörtlichen Suchbegriff), wird ohne Highlighting navigiert.
 *
 * Wird sowohl von zond_indexsuche.c als auch von zond_chat.c verwendet
 * (dieselbe Spaltenkonvention, dieselbe Navigationslogik).
 */
void zond_indexsuche_row_activated(GtkTreeView *treeview, GtkTreePath *tree_path,
		GtkTreeViewColumn *col, gpointer data);

#endif /* ZOND_INDEXSUCHE_H_INCLUDED */
