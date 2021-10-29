#ifndef SOJUS_ADRESSEN_HISTBOX_H_INCLUDED
#define SOJUS_ADRESSEN_HISTBOX_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>


G_BEGIN_DECLS

#define SOJUS_TYPE_ADRESSEN_HISTBOX sojus_adressen_histbox_get_type( )
G_DECLARE_DERIVABLE_TYPE (SojusAdressenHistbox, sojus_adressen_histbox, SOJUS,
        ADRESSEN_HISTBOX, GObject)


struct _SojusAdressenHistboxClass
{
    GObjectClass parent_class;
};




G_END_DECLS

#endif // SOJUS_ADRESSEN_HISTBOX_H_INCLUDED



