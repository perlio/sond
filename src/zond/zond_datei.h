#ifndef ZOND_DATEI_H_INCLUDED
#define ZOND_DATEI_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define ZOND_TYPE_DATEI zond_datei_get_type( )
G_DECLARE_DERIVABLE_TYPE (ZondDatei, zond_datei, ZOND, DATEI, GObject)


struct _ZondDateiClass
{
    GObjectClass parent_class;

    GPtrArray* arr_dateien;
};



G_END_DECLS

#endif // ZOND_DATEI_H_INCLUDED



