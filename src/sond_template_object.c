/*
sond (sond_template_object.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2022  pelo america

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

#include "sond_template_object.h"

typedef enum
{
    PROP_1 = 1,
    N_PROPERTIES
} SondTemplateObjectProperty;


typedef struct
{
    gchar* prop_1;
} SondTemplateObjectPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondTemplateObject, sond_template_object, G_TYPE_OBJECT)



static void
sond_template_object_finalize( GObject* self )
{
    G_OBJECT_CLASS(sond_template_object_parent_class)->finalize( self );

    return;
}


static void
sond_template_object_constructed( GObject* self )
{
    G_OBJECT_CLASS(sond_template_object_parent_class)->constructed( self );

    return;
}


static void
sond_template_object_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    SondTemplateObject* self = SOND_TEMPLATE_OBJECT(object);
    SondTemplateObjectPrivate* priv = sond_template_object_get_instance_private( self );

    switch ((SondTemplateObjectProperty) property_id)
    {
        case PROP_1:
          priv->prop_1= g_strdup( g_value_get_string(value) );
          break;

        default:
          /* We don't have any other property... */
          G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
          break;
    }
}


static void
sond_template_object_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    SondTemplateObject* self = SOND_TEMPLATE_OBJECT(object);
    SondTemplateObjectPrivate* priv = sond_template_object_get_instance_private( self );

    switch ((SondTemplateObjectProperty) property_id)
    {
        case PROP_1:
                g_value_set_string( value, priv->prop_1 );
                break;

        default:
                /* We don't have any other property... */
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
    }
}


static void
sond_template_object_class_init( SondTemplateObjectClass* klass )
{
    GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    klass->signal = g_signal_new( "signal",
                                    SOND_TYPE_TEMPLATE_OBJECT,
                                    G_SIGNAL_RUN_LAST,
                                    0,
                                    NULL,
                                    NULL,
                                    NULL,
                                    G_TYPE_NONE,
                                    4,
                                    GTK_TYPE_WIDGET,
                                    G_TYPE_BOOLEAN, //ob active oder nicht
                                    G_TYPE_STRING, //label von CheckButton
                                    G_TYPE_INT //ID_entity
                                    );

    object_class->finalize = sond_template_object_finalize;
    object_class->constructed = sond_template_object_constructed;

    object_class->set_property = sond_template_object_set_property;
    object_class->get_property = sond_template_object_get_property;

    obj_properties[PROP_1] =
            g_param_spec_string( "path",
                                 "gchar*",
                                 "Pfad zur Datei.",
                                 NULL,
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    g_object_class_install_properties(object_class,
                                      N_PROPERTIES,
                                      obj_properties);

    return;
}


static void
sond_template_object_signal( GObject* object, gpointer data )
{
    GtkWidget* widget = NULL;
    gboolean boolean = FALSE;
    gchar* string = NULL;
    gint integer = 0;

    g_signal_emit( data, SOND_TEMPLATE_OBJECT_GET_CLASS(data)->signal, 0,
            widget, boolean, string, integer );

    return;
}


static void
sond_template_object_init( SondTemplateObject* self )
{

    return;
}
