#include "../global_types.h"
#include "../error.h"

#include "../99conv/general.h"

#include "../99conv/db_read.h"

#include "../20allgemein/project.h"

#include <sqlite3.h>
#include <glib/gstdio.h>


gint
db_begin( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( zond->stmts.transaction[0] );

    rc = sqlite3_step( zond->stmts.transaction[0] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step" )

    return 0;
}


gint
db_begin_both( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    rc = db_begin( zond, errmsg );
    if ( rc != SQLITE_OK ) ERROR_SQL( "db_begin (db)")

    sqlite3_reset( zond->stmts.transaction_store[0] );

    rc = sqlite3_step( zond->stmts.transaction_store[0] );
    if ( rc != SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_step (db_store):\n",
                sqlite3_errmsg( zond->db_store ), NULL );

        return -1;
    }

    return 0;
}


gint
db_commit( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( zond->stmts.transaction[1] );

    rc = sqlite3_step( zond->stmts.transaction[1] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step" )

    return 0;
}


gint
db_commit_both( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    rc = db_commit( zond, errmsg );
    if ( rc != SQLITE_OK ) ERROR_SQL( "db_commit (db)")

    sqlite3_reset( zond->stmts.transaction_store[1] );

    rc = sqlite3_step( zond->stmts.transaction_store[1] );
    if ( rc != SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_step (db_store):\n",
                sqlite3_errmsg( zond->db_store ), NULL );

        return -1;
    }

    return 0;
}


gint
db_rollback( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( zond->stmts.transaction[2] );

    rc = sqlite3_step( zond->stmts.transaction[2] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step" )

    return 0;
}


gint
db_rollback_both( Projekt* zond, gchar** errmsg )
{
    gint rc1 = 0;
    gint rc2 = 0;

    rc1 = db_rollback( zond, errmsg );

    sqlite3_reset( zond->stmts.transaction_store[2] );

    rc2 = sqlite3_step( zond->stmts.transaction_store[2] );

    if ( rc1 != SQLITE_OK && errmsg ) *errmsg = prepend_string( *errmsg, g_strdup( "Bei Aufruf von db_rollback(db):\n" ) );
    if ( rc2 != SQLITE_DONE && errmsg )
    {
        *errmsg = add_string( *errmsg, g_strdup( "\n\nBei Aufruf sqlite3_step (db_store):\n" ) );
        *errmsg = add_string( *errmsg, g_strdup( sqlite3_errmsg( zond->db_store ) ) );
    }

    if ( rc1 != SQLITE_OK || rc2 != SQLITE_DONE ) return -1;

    return 0;
}


gint
db_remove_node( Projekt* zond, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;

    for ( gint i = 0; i < 4; i++ )
    {
        sqlite3_reset( zond->stmts.db_remove_node[i] );
        sqlite3_clear_bindings( zond->stmts.db_remove_node[i] );
    }

    rc = sqlite3_bind_int( zond->stmts.db_remove_node[0 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_remove_node[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [0/1]" )

    rc = sqlite3_bind_int( zond->stmts.db_remove_node[2 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_remove_node[2 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [2/3]" )

    return 0;
}


gint
db_insert_node( Projekt* zond, Baum baum, gint node_id, gboolean child,
        const gchar* icon_name, const gchar* node_text, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;

    for ( gint i = 0; i < 5; i++ )
    {
        sqlite3_reset( zond->stmts.db_insert_node[i] );
        sqlite3_clear_bindings( zond->stmts.db_insert_node[i] );
    }

    rc = sqlite3_bind_int( zond->stmts.db_insert_node[0 + (gint) baum], 1, child );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (child)" )

    rc = sqlite3_bind_int( zond->stmts.db_insert_node[0 + (gint) baum], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_bind_text( zond->stmts.db_insert_node[0 + (gint) baum], 3,
            icon_name, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (icon_name)" )

    rc = sqlite3_bind_text( zond->stmts.db_insert_node[0 + (gint) baum], 4, node_text,
            -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text (node_text)" )

    rc = sqlite3_step( zond->stmts.db_insert_node[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [0/1]" )

    rc = sqlite3_step( zond->stmts.db_insert_node[2 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step ([2/3])" )

    rc = sqlite3_step( zond->stmts.db_insert_node[4] );
    if ( rc != SQLITE_ROW ) ERROR_SQL( "sqlite3_step ([4])" )

    new_node_id = sqlite3_column_int( zond->stmts.db_insert_node[4], 0 );

    return new_node_id;
}


gint
db_set_datei( Projekt* zond, gint node_id, gchar* rel_path, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( zond->stmts.db_set_datei[0] );

    rc = sqlite3_bind_text( zond->stmts.db_set_datei[0], 1, rel_path,
            -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text (rel_path)" )

    rc = sqlite3_bind_int( zond->stmts.db_set_datei[0], 2, node_id);
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_set_datei[0] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [0]" )

    return 0;
}


/*  Rückgabe bei Fehler: -1
    Sämtliche Fehler lösen Rollback aus
    ansonsten: ID des im BAUM_AUSWERTUNG erzeugten Knotens  */
gint
db_kopieren_nach_auswertung( Projekt* zond, Baum baum_von, gint node_id_von,
        gint node_id_nach, gboolean child, gchar** errmsg )
{
    gint rc = 0;
    for ( gint i = 0; i < 3; i++ )
    {
        sqlite3_reset( zond->stmts.db_kopieren_nach_auswertung[i] );
        sqlite3_clear_bindings( zond->stmts.db_kopieren_nach_auswertung[i] );
    }

    rc = sqlite3_bind_int( zond->stmts.db_kopieren_nach_auswertung[0 + (gint) baum_von], 1, child );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (child)" )

    rc = sqlite3_bind_int( zond->stmts.db_kopieren_nach_auswertung[0 + (gint) baum_von], 2, node_id_nach );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id_nach)" )

    rc = sqlite3_bind_int( zond->stmts.db_kopieren_nach_auswertung[0 + (gint) baum_von], 3, node_id_von );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id_von)" )

    rc = sqlite3_step( zond->stmts.db_kopieren_nach_auswertung[0 + (gint) baum_von] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [0/1]" )

    rc = sqlite3_step( zond->stmts.db_kopieren_nach_auswertung[2] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step ([2])" )

    return sqlite3_last_insert_rowid( zond->db );
}


gint
db_kopieren_nach_auswertung_mit_kindern( Projekt* zond,
        gboolean with_younger_siblings, Baum baum_von, gint node_von,
        gint node_nach, gboolean kind, gchar** errmsg )
{
    gint rc = 0;
    gint first_child_id = 0;
    gint new_node_id = 0;

    new_node_id = db_kopieren_nach_auswertung( zond, baum_von, node_von,
            node_nach, kind, errmsg );
    if ( new_node_id == -1 ) ERROR_PAO( "db_kopieren_nach_auswertung" )

    //Prüfen, ob Kind- oder Geschwisterknoten vorhanden
    first_child_id = db_get_first_child( zond, baum_von, node_von, errmsg );
    if ( first_child_id < 0 ) ERROR_PAO( "db_get_first_child" )
    if ( first_child_id > 0 )
    {
        rc = db_kopieren_nach_auswertung_mit_kindern( zond, TRUE, baum_von,
                first_child_id, new_node_id, TRUE, errmsg );
        if ( rc == -1  ) ERROR_PAO( "db_kopieren_nach_auswertung_mit_kindern" )
    }

    gint younger_sibling_id = 0;
    younger_sibling_id = db_get_younger_sibling( zond, baum_von, node_von,
            errmsg );
    if ( younger_sibling_id < 0 ) ERROR_PAO( "db_get_younger_sibling" )
    if ( younger_sibling_id > 0 && with_younger_siblings )
    {
        rc = db_kopieren_nach_auswertung_mit_kindern( zond, TRUE, baum_von,
                younger_sibling_id, new_node_id, FALSE, errmsg );
        if ( rc == -1 ) ERROR_PAO( "db_kopieren_nach_auswertung_mit_kindern" )
    }

    return new_node_id;
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
        sqlite3_reset( zond->stmts.db_verschieben_knoten[i] );
        sqlite3_clear_bindings( zond->stmts.db_verschieben_knoten[i] );
    }

    rc = sqlite3_bind_int( zond->stmts.db_verschieben_knoten[0 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [0, 1]" )

    rc = sqlite3_step( zond->stmts.db_verschieben_knoten[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [0]" )

    rc = sqlite3_bind_int( zond->stmts.db_verschieben_knoten[2 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [2, 1]" )

    rc = sqlite3_bind_int( zond->stmts.db_verschieben_knoten[2 + (gint) baum], 2, new_parent_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [2, 2]" )

    rc = sqlite3_bind_int( zond->stmts.db_verschieben_knoten[2 + (gint) baum], 3, new_older_sibling_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [2, 3]" )

    rc = sqlite3_step( zond->stmts.db_verschieben_knoten[2 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [2]" )

    rc = sqlite3_bind_int( zond->stmts.db_verschieben_knoten[4 + (gint) baum], 1, new_parent_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [4, 1]" )

    rc = sqlite3_bind_int( zond->stmts.db_verschieben_knoten[4 + (gint) baum], 2, new_older_sibling_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [4, 2]" )

    rc = sqlite3_bind_int( zond->stmts.db_verschieben_knoten[4 + (gint) baum], 3, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int [4, 3]" )

    rc = sqlite3_step( zond->stmts.db_verschieben_knoten[4 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step [4]" )

    return 0;
}


/** Die folgenden zwei Funktionen, mit denen einzelnen Felder eines Knotens
*** verändert werden können, geben auch dann 0 zurück, wenn der Knoten gar nicht
*** existiert   **/

gint
db_set_node_text( Projekt* zond, Baum baum, gint node_id, const gchar* node_text,
        gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( zond->stmts.db_set_node_text[0] );
    sqlite3_reset( zond->stmts.db_set_node_text[1] );

    rc = sqlite3_bind_text( zond->stmts.db_set_node_text[0 + (gint) baum], 1,
            node_text, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text (text)" )

    rc = sqlite3_bind_int( zond->stmts.db_set_node_text[0 + (gint) baum], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_set_node_text[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step" )

    return 0;
}


gint
db_set_icon_id( Projekt* zond, Baum baum, gint node_id, const gchar* icon_name,
        gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( zond->stmts.db_set_icon_id[0] );
    sqlite3_reset( zond->stmts.db_set_icon_id[1] );

    rc = sqlite3_bind_text( zond->stmts.db_set_icon_id[0 + (gint) baum], 1,
            icon_name, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text (icon_name)" )

    rc = sqlite3_bind_int( zond->stmts.db_set_icon_id[0 + (gint) baum], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts.db_set_icon_id[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step" )

    return 0;
}


gint
db_speichern_textview( Projekt* zond, gint node_id, gchar* text, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( zond->stmts.db_speichern_textview[0] );
    sqlite3_clear_bindings( zond->stmts.db_speichern_textview[0] );

    rc = sqlite3_bind_text( zond->stmts.db_speichern_textview[0], 1, text, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text (text)" )

    rc = sqlite3_bind_int( zond->stmts.db_speichern_textview[0], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int ( node_id)" )

    rc = sqlite3_step( zond->stmts.db_speichern_textview[0] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step" )

    return 0;
}


gint
db_update_path( Projekt* zond, const gchar* old_path, const gchar* new_path, gchar** errmsg )
{
    gint rc = 0;
    gboolean changed = FALSE;

    changed = zond->changed;

    sqlite3_reset( zond->stmts.db_update_path[0] );
    sqlite3_reset( zond->stmts.db_update_path[1] );

    rc = sqlite3_bind_text( zond->stmts.db_update_path[0], 1,
            old_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text[0] (old_path)" )

    rc = sqlite3_bind_text( zond->stmts.db_update_path[0], 2,
            new_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text[0] (new_path)" )

    rc = sqlite3_step( zond->stmts.db_update_path[0] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step[0]" )

    //auch in zond->db_store schreiben
    rc = sqlite3_bind_text( zond->stmts.db_update_path[1], 1,
            old_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text[1] (old_path)" )

    rc = sqlite3_bind_text( zond->stmts.db_update_path[1], 2,
            new_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_text[1] (new_path)" )

    rc = sqlite3_step( zond->stmts.db_update_path[1] );
    if ( rc != SQLITE_DONE ) ERROR_SQL( "sqlite3_step[1]" )

    //falls nicht geändert, hierdurch auch nicht
    if ( !changed ) reset_project_changed( zond );

    return 0;
}
