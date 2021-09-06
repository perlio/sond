#ifndef DBASE_FULL_H_INCLUDED
#define DBASE_FULL_H_INCLUDED

#define ERROR_DBASE_FULL(x) { if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(dbase_full->dbase.db), NULL ), *errmsg ); \
                       return -1; }

#include "../../dbase.h"

typedef struct sqlite3_stmt sqlite3_stmt;

typedef struct _DBase_Full
{
    DBase dbase;
    sqlite3_stmt* insert_node[5];
    sqlite3_stmt* set_node_text[2];
    sqlite3_stmt* set_icon_id[2];
    sqlite3_stmt* stmts[55];

} DBaseFull;

typedef struct _Property
{
    gint ID;
    gchar* label;
    gchar* value;
    GArray* arr_properties;
} Property;

typedef struct _Entity
{
    gint ID;
    gchar* label;
    GArray* arr_properties;
} Entity;

typedef struct _Edge
{
    gint ID;
    gint subject;
    gint object;
} Edge;


gint dbase_full_insert_node( DBaseFull*, Baum, gint, gboolean, const gchar*,
        const gchar*, gchar** );

gint dbase_full_set_node_text( DBaseFull*, Baum, gint, const gchar*, gchar** );

gint dbase_full_set_icon_id( DBaseFull*, Baum, gint, const gchar*, gchar** );

gint dbase_full_insert_entity( DBaseFull*, gint, gchar** );

gint dbase_full_insert_property( DBaseFull*, gint, gint, gchar*, gchar** );

gint dbase_full_get_label_text_for_entity( DBaseFull*, gint, gchar**, gchar** );

gint dbase_full_get_properties( DBaseFull*, gint, GArray**, gchar** );

gint dbase_full_get_outgoing_edges( DBaseFull*, gint, GArray**, gchar** );

gint dbase_full_get_label_text( DBaseFull*, gint, gchar**, gchar** );

gint dbase_full_get_array_children_label( DBaseFull*, gint, GArray**, gchar** );

gint dbase_full_get_array_nodes( DBaseFull*, gint, GArray**, gchar** );

gint dbase_full_get_incoming_edges( DBaseFull*, gint, GArray**, gchar** );

gint dbase_full_get_adm_entities( DBaseFull*, gint, GArray**, gchar** );

gint dbase_full_get_label_for_entity( DBaseFull*, gint, gchar** );

gint dbase_full_insert_edge( DBaseFull*, gint, gint, gint, gchar** );

gint dbase_full_get_entity( DBaseFull*, gint, Entity**, gchar** );

gint dbase_full_prepare_stmts( DBaseFull*, gchar** );

gint dbase_full_create( const gchar*, DBaseFull**, gboolean, gboolean, gchar** );

#endif // DBASE_FULL_H_INCLUDED
