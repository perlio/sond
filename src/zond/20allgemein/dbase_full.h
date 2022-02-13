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
    sqlite3_stmt* speichern_textview;
    sqlite3_stmt* set_datei;
    sqlite3_stmt* set_link;
    sqlite3_stmt* get_link_target;
    sqlite3_stmt* get_links;
    sqlite3_stmt* remove_link;
    sqlite3_stmt* stmts[55];

} DBaseFull;



gint dbase_full_set_icon_id( DBaseFull*, Baum, gint, const gchar*, gchar** );

gint dbase_full_speichern_textview( DBaseFull*, gint, gchar*, gchar** );

gint dbase_full_set_datei( DBaseFull*, gint, const gchar*, gchar** );

gint dbase_full_set_link( DBaseFull*, const gint, const gint, const gchar*,
        const gint, const gint, gchar** );

gint dbase_full_get_links( DBaseFull*, const gchar*, const gint, const gint,
        GArray**, gchar** );

gint dbase_full_get_link_target( DBaseFull*, const gint, const gint, gchar**, gint*,
        gint*, gchar** );

gint dbase_full_remove_link( DBaseFull*, const gint, const gint, gchar** );

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

//gint dbase_full_get_entity( DBaseFull*, gint, Entity**, gchar** );

gint dbase_full_prepare_stmts( DBaseFull*, gchar** );

gint dbase_full_create_with_stmts( const gchar*, DBaseFull**, sqlite3*, gchar** );

#endif // DBASE_FULL_H_INCLUDED
