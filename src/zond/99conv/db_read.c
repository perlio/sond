#include "../global_types.h"
#include "../enums.h"
#include "../error.h"

#include "../99conv/general.h"

#include "../20allgemein/ziele.h"

#include <sqlite3.h>
#include <glib/gstdio.h>


/** Rückgabe:
*** Fehler: -1
*** node_id nicht vorhanden: -2
*** ansonsten: parent_id */
gint
db_get_parent( Projekt* zond, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint parent_id = 0;

    for ( gint i = 0; i < 1; i++ )
    {
        sqlite3_reset( zond->stmts.db_get_parent[i] );
        sqlite3_clear_bindings( zond->stmts.db_get_parent[i] );
    }

    rc = sqlite3_bind_int( zond->stmts.db_get_parent[0 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_get_parent[0 + (gint) baum] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_SQL( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) parent_id =
            sqlite3_column_int( zond->stmts.db_get_parent[0 + (gint) baum], 0 );

    if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strconcat( "node_id existiert nicht", NULL ) );
        //Denn jeder Knoten hat Eltern
        return -2;
    }

    return parent_id;
}


/**
*** Rückgabe:
*** Fehler: -1
*** node_id existiert nicht: -2
*** ansonsten: older_sibling_id
**/
gint
db_get_older_sibling( Projekt* zond, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint older_sibling_id = 0;

    for ( gint i = 0; i < 1; i++ )
    {
        sqlite3_reset( zond->stmts.db_get_older_sibling[i] );
        sqlite3_clear_bindings( zond->stmts.db_get_older_sibling[i] );
    }

    rc = sqlite3_bind_int( zond->stmts.db_get_older_sibling[0 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_get_older_sibling[0 + (gint) baum] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_SQL( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) older_sibling_id =
            sqlite3_column_int( zond->stmts.db_get_older_sibling[0 + (gint) baum], 0 );

    if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strconcat( "node_id existiert nicht", NULL ) );
        //Denn jeder Knoten hat ein älteres Geschwister, auch wenn es 0 ist

        return -2;
    }

    return older_sibling_id;
}


gint
db_get_younger_sibling( Projekt* zond, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint younger_sibling_id = 0;

    for ( gint i = 0; i < 2; i++ )
    {
        sqlite3_reset( zond->stmts.db_get_younger_sibling[i] );
        sqlite3_clear_bindings( zond->stmts.db_get_younger_sibling[i] );
    }

    rc = sqlite3_bind_int( zond->stmts.db_get_younger_sibling[0 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_get_younger_sibling[0 + (gint) baum] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_SQL( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) younger_sibling_id =
            sqlite3_column_int( zond->stmts.db_get_younger_sibling[0 + (gint) baum], 1 );

    if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = g_strdup( "node_id existiert nicht" );

        return -2;
    }

    return younger_sibling_id;
}


gint
db_get_first_child( Projekt* zond, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint first_child_id = 0;

    for ( gint i = 0; i < 2; i++ )
    {
        sqlite3_reset( zond->stmts.db_get_first_child[i] );
        sqlite3_clear_bindings( zond->stmts.db_get_first_child[i] );
    }

    rc = sqlite3_bind_int( zond->stmts.db_get_first_child[0 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_get_first_child[0 + (gint) baum] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_SQL( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) first_child_id =
            sqlite3_column_int( zond->stmts.db_get_first_child[0 + (gint) baum], 1 );

    if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = g_strdup( "node_id existiert nicht" );

        return -2;
    }

    return first_child_id;
}


gint
db_get_icon_name_and_node_text( Projekt* zond, Baum baum, gint node_id, gchar** icon_name,
        gchar** node_text, gchar** errmsg )
{
    gint rc = 0;

    for ( gint i = 0; i < 2; i++ )
    {
        sqlite3_reset( zond->stmts.db_get_icon_name_and_node_text[i] );
        sqlite3_clear_bindings( zond->stmts.db_get_icon_name_and_node_text[i] );
    }

    rc = sqlite3_bind_int( zond->stmts.db_get_icon_name_and_node_text[0 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_get_icon_name_and_node_text[0 + (gint) baum] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_SQL( "sqlite3_step" )

    if ( rc == SQLITE_ROW )
    {
        gchar* buf_icon_name = (gchar*)
                sqlite3_column_text( zond->stmts.db_get_icon_name_and_node_text[0 + (gint) baum], 0 );
        if ( buf_icon_name && icon_name ) *icon_name= g_strdup( buf_icon_name );
        else if ( node_text ) *node_text = g_strdup( "" );

        gchar* buf_node_text = (gchar*)
                sqlite3_column_text( zond->stmts.db_get_icon_name_and_node_text[0 + (gint) baum], 1 );
        if ( buf_node_text && node_text ) *node_text = g_strdup( buf_node_text );
        else if ( node_text ) *node_text = g_strdup( "" );
    }
    else if ( rc == SQLITE_DONE ) return 1;

    return 0;
}


gint
db_get_ref_id( Projekt* zond, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint ref_id = 0;

    sqlite3_reset( zond->stmts.db_get_ref_id[0] );

    rc = sqlite3_bind_int( zond->stmts.db_get_ref_id[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_get_ref_id[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_SQL( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) ref_id =
            sqlite3_column_int( zond->stmts.db_get_ref_id[0], 0 );
    else if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strconcat( "node_id existiert nicht", NULL ) );

        return -2;
    }

    return ref_id;
}


/** Rückgabewert:
    Wenn alles ok: 0
    Wenn Fehler (inkl. node_id existiert nicht: -1 - errmsg wird - wenn != NULL - gesetzt
    Wenn kein rel_path zur node_id: -2
**/
gint
db_get_rel_path( Projekt* zond, Baum baum, gint node_id, gchar** rel_path,
        gchar** errmsg )
{
    gchar* text = NULL;

    if ( baum == BAUM_AUSWERTUNG )
    {
        node_id = db_get_ref_id( zond, node_id, errmsg );
        if ( node_id < 0 ) ERROR_PAO( "db_get_ref_id" )
    }

    gint rc = 0;

    sqlite3_reset( zond->stmts.db_get_rel_path[0] );

    rc = sqlite3_bind_int( zond->stmts.db_get_rel_path[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_get_rel_path[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_SQL( "sqlite3_step" )
    else if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = g_strdup( "node_id existiert nicht" );

        return -1;
    }

    text = (gchar*) sqlite3_column_text( zond->stmts.db_get_rel_path[0], 1 );

    if ( !text || !g_strcmp0( text, "" ) ) return -2;

    if ( rel_path ) *rel_path = g_strdup( (const gchar*) text );

    return 0;
}


/** Rückgabewert:
    Falls kein ziel: 0, *ziel unverändert
    Fehler: -1
**/
gint
db_get_ziel( Projekt* zond, Baum baum, gint node_id, Ziel** ziel, gchar** errmsg )
{
    *ziel = NULL;

    if ( baum == BAUM_AUSWERTUNG )
    {
        node_id = db_get_ref_id( zond, node_id, errmsg );
        if ( node_id < 0 ) ERROR_PAO( "db_get_ref_id" )
    }

    gint rc = 0;

    sqlite3_reset( zond->stmts.db_get_ziel[0] );

    rc = sqlite3_bind_int( zond->stmts.db_get_ziel[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_get_ziel[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_SQL( "sqlite3_step" )
    else if ( rc == SQLITE_DONE ) return 0;

    *ziel = g_malloc0( sizeof( Ziel ) );
    gchar* buf = NULL;

    //ziel_id_von
    buf = (gchar*) sqlite3_column_text( zond->stmts.db_get_ziel[0], 0 );
    (*ziel)->ziel_id_von = g_strdup( buf );

    //ziel_id_bis
    buf = (gchar*) sqlite3_column_text( zond->stmts.db_get_ziel[0], 2 );
    (*ziel)->ziel_id_bis = g_strdup( buf );

    //index_von und -bis
    (*ziel)->index_von = sqlite3_column_int( zond->stmts.db_get_ziel[0], 1 );
    (*ziel)->index_bis = sqlite3_column_int( zond->stmts.db_get_ziel[0], 3 );

    return 0;
}


gint
db_knotentyp_abfragen( Projekt* zond, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gchar* rel_path = NULL;
    Ziel* ziel = NULL;

    rc = db_get_rel_path( zond, baum, node_id, &rel_path, errmsg );
    if ( rc == -1 ) ERROR_PAO( "db_get_rel_path" )

    if ( rc == -2 ) return 0;

    g_free( rel_path );

    rc = db_get_ziel( zond, baum, node_id, &ziel, errmsg );
    if ( rc ) ERROR_PAO( "db_get_ziel" )

    if ( !ziel ) return 1;

    ziele_free( ziel );

    return 2;
}


//betrifft nur tabelle baum_auswertung!
gint
db_get_text( Projekt* zond, gint node_id, gchar** text, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( zond->stmts.db_get_text[0] );

    rc = sqlite3_bind_int( zond->stmts.db_get_text[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_get_text[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_SQL( "sqlite3_step" )
    else if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strconcat( "node_id existiert nicht", NULL ) );

        return -2;
    }

    if ( rc == SQLITE_ROW && text ) *text =
            g_strdup( (const gchar*) sqlite3_column_text( zond->stmts.db_get_text[0], 1 ) );

    return 0;
}


/*  Gibt 0 zurück, wenn rel_path nicht in Tabelle vorhanden
**  Bei Fehler: -1  */
gint
db_get_node_id_from_rel_path( Projekt* zond, const gchar* rel_path, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;

    sqlite3_reset( zond->stmts.db_get_node_id_from_rel_path[0] );

    rc = sqlite3_bind_text( zond->stmts.db_get_node_id_from_rel_path[0], 1, rel_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text (rel_path)" )

    rc = sqlite3_step( zond->stmts.db_get_node_id_from_rel_path[0] );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) node_id =
            sqlite3_column_int( zond->stmts.db_get_node_id_from_rel_path[0], 0 );

    return node_id;
}


gint
db_check_id( Projekt* zond, const gchar* id, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( zond->stmts.db_check_id[0] );

    rc = sqlite3_bind_text( zond->stmts.db_check_id[0], 1, id, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text (id)" )

    rc = sqlite3_step( zond->stmts.db_check_id[0] );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) return 1;

    return 0;
}


