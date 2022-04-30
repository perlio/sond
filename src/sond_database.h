#ifndef SOND_DATABASE_H_INCLUDED
#define SOND_DATABASE_H_INCLUDED

typedef char gchar;
typedef int gint;

typedef enum _Type
{
    NODE,
    REL,
    PROPERTY
} Type;

typedef struct _Entity
{
    gint ID;
    gint label;
} Entity;

typedef struct _Property
{
    Entity rel;
    GArray* properties;
    Entity property;
    gchar* value;
} Property;

typedef struct _Node
{
    Entity subject;
    GArray* statements;
    GArray* properties;
} Node;

typedef struct _Statement
{
    Entity rel;
    GArray* properties;
    Node object;
} Statement;



gint sond_database_add_to_database( gpointer, gchar** );

gint sond_database_insert_entity( gpointer, gint, gchar** );

gint sond_database_is_admitted_edge( gpointer, gint, gint, gint, gchar** );

gint sond_database_is_admitted_rel( gpointer, gint, gint, gchar** );

gint sond_database_get_ID_label_for_entity( gpointer, gint, gchar** );

gint sond_database_insert_property( gpointer, gint, gint, const gchar*, gchar** );

gint sond_database_get_entities_for_property( gpointer, gint, const gchar*,
        GArray**, gchar** );

#endif //SOND_DATABASE_H_INCLUDED
