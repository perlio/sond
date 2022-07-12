#ifndef SOND_TEMPLATE_OBJECT_H_INCLUDED
#define SOND_TEMPLATE_OBJECT_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SOND_TYPE_TEMPLATE_OBJECT sond_template_object_get_type( )
G_DECLARE_DERIVABLE_TYPE (SondTemplateObject, sond_template_object, SOND, TEMPLATE_OBJECT, GObject)


struct _SondTemplateObjectClass
{
    GObjectClass parent_class;

    //Signale
    guint signal;
};


G_END_DECLS


#endif // SOND_TEMPLATE_OBJECT_H_INCLUDED
