#ifndef SOND_TREEVIEW_H_INCLUDED
#define SOND_TREEVIEW_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

/* --------------------------------------------------------------------------
 * Hilfsmakros fuer GTK3/4-Kompatibilitaet
 * -------------------------------------------------------------------------- */
#if GTK_MAJOR_VERSION >= 4
#  define SOND_GET_TOPLEVEL(w) \
		GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(w)))
#  define SOND_ICON_THEME_FOR_WIDGET(w) \
		gtk_icon_theme_get_for_display(gtk_widget_get_display(GTK_WIDGET(w)))
#else
#  define SOND_GET_TOPLEVEL(w) \
		gtk_widget_get_toplevel(GTK_WIDGET(w))
#  define SOND_ICON_THEME_FOR_WIDGET(w) \
		gtk_icon_theme_get_for_screen(gtk_widget_get_screen(GTK_WIDGET(w)))
#endif

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
	void (*render_text_cell)(GtkTreeViewColumn*, GtkCellRenderer*,
			GtkTreeModel*, GtkTreeIter*, gpointer);
	void (*text_edited)(SondTreeview *stv, GtkTreeIter *iter,
			gchar const *new_text);
	gboolean (*callback_key_press_event)(GtkWidget*, guint keyval,
			GdkModifierType state, gpointer);
	gpointer callback_key_press_event_func_data;
};

void sond_treeview_set_id(SondTreeview*, gint);

gint sond_treeview_get_id(SondTreeview*);

GtkCellRenderer* sond_treeview_get_cell_renderer_icon(SondTreeview*);

GtkCellRenderer* sond_treeview_get_cell_renderer_text(SondTreeview*);

GtkWidget* sond_treeview_get_contextmenu(SondTreeview*);

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
