#ifndef SOND_DATABASE_NODE_H_INCLUDED
#define SOND_DATABASE_NODE_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SOND_TYPE_DATABASE_NODE sond_database_node_get_type( )
G_DECLARE_DERIVABLE_TYPE( SondDatabaseNode, sond_database_node, SOND,
		DATABASE_NODE, GtkBox)

struct _SondDatabaseNodeClass {
	GtkBoxClass parent_class;
};

G_END_DECLS

GtkWidget* sond_database_node_new(void);

gint sond_database_node_load(SondDatabaseNode*, gpointer, gint, gchar**);

GtkWidget* sond_database_node_load_new(gpointer, gint, gchar**);

#endif // #ifndef SOND_DATABASE_NODE_H_INCLUDED
