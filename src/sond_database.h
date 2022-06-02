#ifndef SOND_DATABASE_H_INCLUDED
#define SOND_DATABASE_H_INCLUDED

typedef char gchar;
typedef int gint;

typedef struct _Entity
{
    gint ID;
    gint label;
    GArray* arr_properties;
} Entity;

typedef struct _Property
{
    Entity entity_property;
    gchar* value;
} Property;

typedef struct _Segment
{
    gint direction;
    union
    {
        Entity entity_subject;
        Entity entity_object;
    };
    Entity entity_rel;
} Segment;

typedef struct _Node
{
    Entity entity_subject;
    GArray* arr_properties;

} Node;

gint sond_database_add_to_database( gpointer, gchar** );

gint sond_database_insert_entity( gpointer, gint, gchar** );

gint sond_database_is_admitted_edge( gpointer, gint, gint, gint, gchar** );

gint sond_database_is_admitted_rel( gpointer, gint, gint, gchar** );

gint sond_database_label_is_equal_or_parent( gpointer, gint, gint, gchar** );

gint sond_database_get_ID_label_for_entity( gpointer, gint, gchar** );

gint sond_database_insert_rel( gpointer, gint, gint, gint, gchar** );

gint sond_database_insert_property( gpointer, gint, gint, const gchar*, gchar** );

gint sond_database_get_entities_for_label( gpointer, gint, GArray**, gchar** );

gint sond_database_get_object_for_subject( gpointer, gint, GArray**, gchar**, ... );

gint sond_database_get_entities_for_property( gpointer, gint, const gchar*,
        GArray**, gchar** );

gint sond_database_get_entities_for_properties_and( gpointer, GArray**, gchar**,
        ... );

gint sond_database_get_label_for_ID_label( gpointer, gint, gchar**, gchar** );

gint sond_database_get_property_value( gpointer, gint, gchar**, gchar** );

gint sond_database_get_properties( gpointer, gint, GArray**, gchar** );

gint sond_database_get_first_property_value_for_subject( gpointer, gint, gint,
        gchar**, gchar** );

gint sond_database_get_subject_and_first_property_value_for_labels( gpointer,
        gint, gint, GArray**, GPtrArray**, gchar** );

gint sond_database_get_outgoing_rels( gpointer, gint, GArray**, gchar** );

gint sond_database_get_object_from_rel( gpointer, gint, gchar** );

#endif //SOND_DATABASE_H_INCLUDED
