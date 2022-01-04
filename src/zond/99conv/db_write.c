#include "../global_types.h"
#include "../error.h"

#include "../99conv/general.h"

#include "../99conv/db_read.h"

#include "../20allgemein/project.h"
#include "../20allgemein/dbase_full.h"

#include <sqlite3.h>
#include <glib/gstdio.h>


gint
db_remove_node( Projekt* zond, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;

    for ( gint i = 0; i < 4; i++ )
    {
        sqlite3_reset( zond->dbase->stmts.db_remove_node[i] );
        sqlite3_clear_bindings( zond->dbase->stmts.db_remove_node[i] );
    }

    rc = sqlite3_bind_int( zond->dbase->stmts.db_remove_node[0 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->dbase->stmts.db_remove_node[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [0/1]" )

    rc = sqlite3_bind_int( zond->dbase->stmts.db_remove_node[2 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->dbase->stmts.db_remove_node[2 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [2/3]" )

    return 0;
}


gint
db_kopieren_nach_auswertung( Projekt* zond, Baum baum_von, gint node_id_von,
        gint node_id_nach, gboolean child, gchar** errmsg )
{
    gint rc = 0;
    for ( gint i = 0; i < 3; i++ )
    {
        sqlite3_reset( zond->dbase->stmts.db_kopieren_nach_auswertung[i] );
        sqlite3_clear_bindings( zond->dbase->stmts.db_kopieren_nach_auswertung[i] );
    }

    rc = sqlite3_bind_int( zond->dbase->stmts.db_kopieren_nach_auswertung[0 + (gint) baum_von], 1, child );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (child)" )

    rc = sqlite3_bind_int( zond->dbase->stmts.db_kopieren_nach_auswertung[0 + (gint) baum_von], 2, node_id_nach );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id_nach)" )

    rc = sqlite3_bind_int( zond->dbase->stmts.db_kopieren_nach_auswertung[0 + (gint) baum_von], 3, node_id_von );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id_von)" )

    rc = sqlite3_step( zond->dbase->stmts.db_kopieren_nach_auswertung[0 + (gint) baum_von] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [0/1]" )

    rc = sqlite3_step( zond->dbase->stmts.db_kopieren_nach_auswertung[2] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step ([2])" )

    return sqlite3_last_insert_rowid( zond->dbase_zond->dbase_work->dbase.db );
}


/*
** node_id                   - zu verschiebender Knoten
** new_parent_id             - neuer Elternknoten, 0 wenn 1. Ebene
** new_older_sibling_id      - älteres Geschwister, 0 wenn 1. Kind
**/
gint
db_verschieben_knoten( Projekt* zond, Baum baum, gint node_id, gint new_parent_id,
        gint new_older_sibling_id, gchar** errmsg )
{
    gint rc = 0;

    for ( gint i = 0; i < 6; i++ )
    {
        sqlite3_reset( zond->dbase->stmts.db_verschieben_knoten[i] );
        sqlite3_clear_bindings( zond->dbase->stmts.db_verschieben_knoten[i] );
    }

    rc = sqlite3_bind_int( zond->dbase->stmts.db_verschieben_knoten[0 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [0, 1]" )

    rc = sqlite3_step( zond->dbase->stmts.db_verschieben_knoten[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [0]" )

    rc = sqlite3_bind_int( zond->dbase->stmts.db_verschieben_knoten[2 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [2, 1]" )

    rc = sqlite3_bind_int( zond->dbase->stmts.db_verschieben_knoten[2 + (gint) baum], 2, new_parent_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [2, 2]" )

    rc = sqlite3_bind_int( zond->dbase->stmts.db_verschieben_knoten[2 + (gint) baum], 3, new_older_sibling_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [2, 3]" )

    rc = sqlite3_step( zond->dbase->stmts.db_verschieben_knoten[2 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [2]" )

    rc = sqlite3_bind_int( zond->dbase->stmts.db_verschieben_knoten[4 + (gint) baum], 1, new_parent_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [4, 1]" )

    rc = sqlite3_bind_int( zond->dbase->stmts.db_verschieben_knoten[4 + (gint) baum], 2, new_older_sibling_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [4, 2]" )

    rc = sqlite3_bind_int( zond->dbase->stmts.db_verschieben_knoten[4 + (gint) baum], 3, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [4, 3]" )

    rc = sqlite3_step( zond->dbase->stmts.db_verschieben_knoten[4 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [4]" )

    return 0;
}

