#ifndef SOND_DATABASE_ENTITY_H_INCLUDED
#define SOND_DATABASE_ENTITY_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SOND_TYPE_DATABASE_ENTITY sond_database_entity_get_type( )
G_DECLARE_DERIVABLE_TYPE (SondDatabaseEntity, sond_database_entity, SOND, DATABASE_ENTITY, GtkBox)


struct _SondDatabaseEntityClass
{

};

gint sond_database_entity_load( SondDatabaseEntity*, gpointer, gint, gchar** );

SondDatabaseEntity* sond_database_entity_load_new( gpointer, gint, gchar** );

GtkWidget* sond_database_entity_get_prop_box( SondDatabaseEntity* );


G_END_DECLS


#endif // SOND_DATABASE_ENTITY_H_INCLUDED
