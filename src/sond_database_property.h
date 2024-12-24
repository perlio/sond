#ifndef SOND_DATABASE_PROPERTY_H_INCLUDED
#define SOND_DATABASE_PROPERTY_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>
#include "sond_database_entity.h"

G_BEGIN_DECLS

#define SOND_TYPE_DATABASE_PROPERTY sond_database_property_get_type( )
G_DECLARE_DERIVABLE_TYPE( SondDatabaseProperty, sond_database_property, SOND,
		DATABASE_PROPERTY, SondDatabaseEntity)

struct _SondDatabasePropertyClass {
	SondDatabaseEntityClass parent_class;
};

G_END_DECLS

SondDatabaseProperty* sond_database_property_load_new(gpointer, gint, gchar**);

void sond_database_property_set_editable(SondDatabaseProperty*, gboolean);

#endif // SOND_DATABASE_PROPERTY_H_INCLUDED
