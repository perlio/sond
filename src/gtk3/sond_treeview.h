#ifndef SOND_TREEVIEW_H_INCLUDED
#define SOND_TREEVIEW_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include "../sond_gtk_compat.h" /* SOND_GET_TOPLEVEL, SOND_ICON_THEME_FOR_WIDGET */

G_BEGIN_DECLS

#define SOND_TYPE_TREEVIEW sond_treeview_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondTreeview, sond_treeview, SOND, TREEVIEW, GtkTreeView)

typedef struct _Clipboard {
	SondTreeview *tree_view;
	gboolean ausschneiden;
	GPtrArray *arr_ref;
} Clipboard;

struct _SondTreeviewClass {
	GtkTreeViewClass parent_class;
	Clipboard *clipboard;
	GMenu *gmenu;
	void (*render_text_cell)(GtkTreeViewColumn*, GtkCellRenderer*,
			GtkTreeModel*, GtkTreeIter*, gpointer);
	void (*text_edited)(SondTreeview *stv, GtkTreeIter *iter,
			gchar const *new_text);
};

/* Haengt die Basis-Section (Kopieren/Ausschneiden) an gmenu.
 * Wird von abgeleiteten Klassen in deren class_init aufgerufen,
 * nachdem sie ein eigenes GMenu angelegt haben. */
void sond_treeview_add_base_menu(GMenu *gmenu);

void sond_treeview_set_id(SondTreeview*, gint);

gint sond_treeview_get_id(SondTreeview*);

GtkCellRenderer* sond_treeview_get_cell_renderer_icon(SondTreeview*);

GtkCellRenderer* sond_treeview_get_cell_renderer_text(SondTreeview*);

GtkWidget* sond_treeview_get_contextmenu(SondTreeview*);

GMenu* sond_treeview_get_gmenu(SondTreeview*);

GSimpleActionGroup* sond_treeview_get_action_group(SondTreeview*);

void sond_treeview_expand_row(SondTreeview*, GtkTreeIter*);

void sond_treeview_expand_to_row(SondTreeview*, GtkTreeIter*);

gboolean sond_treeview_row_expanded(SondTreeview*, GtkTreeIter*);

gboolean sond_treeview_get_cursor(SondTreeview*, GtkTreeIter*);

void sond_treeview_set_cursor(SondTreeview*, GtkTreeIter*);

void sond_treeview_set_cursor_on_text_cell(SondTreeview *stv, GtkTreeIter *iter);

gboolean sond_treeview_test_cursor_descendant(SondTreeview*, gboolean);

void sond_treeview_copy_or_cut_selection(SondTreeview*, gboolean);

gint sond_treeview_clipboard_foreach(
		gint (*)(SondTreeview*, GtkTreeIter*, gpointer, GError**), gpointer,
		GError**);

gint sond_treeview_selection_foreach(SondTreeview*,
		gint (*)(SondTreeview*, GtkTreeIter*, gpointer, GError**), gpointer,
		GError**);

G_END_DECLS

#endif // SOND_TREEVIEW_H_INCLUDED
