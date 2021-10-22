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



const gchar* sond_database_sql_create_database( void );

const gchar* sond_database_sql_insert_labels( void );

const gchar* sond_database_sql_insert_adm_rels( void );

#endif //SOND_DATABASE_H_INCLUDED
