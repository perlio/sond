#ifndef SOND_DATABASE_H_INCLUDED
#define SOND_DATABASE_H_INCLUDED

typedef char gchar;
typedef int gint;

typedef enum _Direction
{
    INCOMING,
    OUTGOING
} Direction;

typedef struct _Entity
{
    gint ID;
    gint label;
} Entity;

typedef struct _Property
{
    Entity entity;
    gchar* value;
    GArray* properties;
} Property;

typedef struct _Node
{
    Entity entity;
    GArray* properties;
    GArray* outgoing_rels;
} Node;

typedef struct _Rel
{
    Entity rel;
    GArray* properties;
    Node object;
} Rel;


const gchar* sond_database_sql_create_database( void );

const gchar* sond_database_sql_insert_labels( void );

const gchar* sond_database_sql_insert_adm_rels( void );

#endif //SOND_DATABASE_H_INCLUDED
