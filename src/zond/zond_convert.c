static void
ziele_free( Ziel* ziel )
{
    if ( !ziel ) return;

    g_free( ziel->ziel_id_von );
    g_free( ziel->ziel_id_bis );

    g_free( ziel );

    return;
}



//Baum ist Baum Von!
gint
zond_dbase_kopieren_nach_auswertung( ZondDBase* zond_dbase, Baum baum, gint node_id_von,
        gint node_id_nach, gboolean child, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "INSERT INTO baum_auswertung "
            "(parent_id, older_sibling_id, icon_name, node_text, ref_id) "
            "VALUES ("
                "CASE ?1 " //child
                    "WHEN 0 THEN (SELECT parent_id FROM baum_auswertung WHERE node_id=?2) "
                    "WHEN 1 THEN ?2 " //node_id_nach
                "END, "
                "CASE ?1 " //older_sibling_id
                    "WHEN 0 THEN ?2 "
                    "WHEN 1 THEN 0 "
                "END, "
                "(SELECT icon_name FROM baum_inhalt WHERE node_id=?3), " //node_id_von
                "(SELECT node_text FROM baum_inhalt WHERE node_id=?3), "//node_text
                "(CASE "
                    "WHEN " //datei_id zu node_id_von?
                        "(SELECT dateien.rel_path FROM dateien LEFT JOIN ziele "
                        "ON dateien.rel_path=ziele.rel_path "
                        "JOIN baum_inhalt "
                        "ON baum_inhalt.node_id=dateien.node_id OR "
                        "baum_inhalt.node_id=ziele.node_id "
                        "WHERE baum_inhalt.node_id=?3)"
                            "IS NULL THEN 0 "
                    "ELSE ?3 "
                "END)); ",

            "INSERT INTO baum_auswertung "
            "(parent_id, older_sibling_id, icon_name, node_text, text, ref_id) "
            "VALUES ("
                "CASE ?1 " //child
                    "WHEN 0 THEN (SELECT parent_id FROM baum_auswertung WHERE node_id=?2) "
                    "WHEN 1 THEN ?2 " //node_id_nach
                "END, "
                "CASE ?1 " //older_sibling_id
                    "WHEN 0 THEN ?2 "
                    "WHEN 1 THEN 0 "
                "END, "
                "(SELECT icon_name FROM baum_auswertung WHERE node_id=?3), " //node_id_von
                "(SELECT node_text FROM baum_auswertung WHERE node_id=?3), "//node_text
                "(SELECT text FROM baum_auswertung WHERE node_id=?3), "//text
                "(CASE "
                    "WHEN " //datei_id zu node_id_von?
                        "(SELECT dateien.rel_path FROM dateien LEFT JOIN ziele "
                        "ON dateien.rel_path=ziele.rel_path "
                        "JOIN baum_inhalt "
                        "ON baum_inhalt.node_id=dateien.node_id OR "
                        "baum_inhalt.node_id=ziele.node_id "
                        "WHERE baum_inhalt.node_id="
                            "(SELECT ref_id FROM baum_auswertung WHERE node_id=?3)"
                            ") IS NULL THEN 0 "
                    "ELSE "
                        "(SELECT ref_id FROM baum_auswertung WHERE node_id=?3) "
                "END)); ",

            "UPDATE baum_auswertung SET "
                "older_sibling_id=last_insert_rowid() "
            "WHERE "
                "parent_id=(SELECT parent_id FROM baum_auswertung WHERE node_id=last_insert_rowid()) "
            "AND "
                "older_sibling_id=(SELECT older_sibling_id FROM baum_auswertung WHERE node_id=last_insert_rowid()) "
            "AND "
                "node_id!=last_insert_rowid() "
            "AND "
                "node_id!=0; " };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, child );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (child)" )

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 2, node_id_nach );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id_nach)" )

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 3, node_id_von );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id_von)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0/1]" )

    rc = sqlite3_step( stmt[2] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step ([2])" )

    return sqlite3_last_insert_rowid( zond_dbase_get_dbase( zond_dbase ) );
}


gint
zond_dbase_verschieben_knoten( ZondDBase* zond_dbase, Baum baum, gint node_id, gint new_parent_id,
        gint new_older_sibling_id, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE baum_inhalt SET older_sibling_id="
            "(SELECT older_sibling_id FROM baum_inhalt WHERE node_id=?1)" //node_id
            "WHERE older_sibling_id=?1; ",

            "UPDATE baum_auswertung SET older_sibling_id="
            "(SELECT older_sibling_id FROM baum_auswertung WHERE node_id=?1)"
            "WHERE older_sibling_id=?1; ",

            "UPDATE baum_inhalt SET older_sibling_id=?1 WHERE node_id=" //node_id
                "(SELECT node_id FROM baum_inhalt WHERE parent_id=?2 AND older_sibling_id=?3); ", //new_parent_id/new_older_s_id

            "UPDATE baum_auswertung SET older_sibling_id=?1 WHERE node_id=" //node_id
                "(SELECT node_id FROM baum_auswertung WHERE parent_id=?2 AND older_sibling_id=?3); ", //new_parent_id/new_older_s_id

            "UPDATE baum_inhalt SET parent_id=?1, older_sibling_id=?2 WHERE node_id=?3; ",

            "UPDATE baum_auswertung SET parent_id=?1, older_sibling_id=?2 WHERE node_id=?3; " };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [0, 1]" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0]" )

    rc = sqlite3_bind_int( stmt[2 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [2, 1]" )

    rc = sqlite3_bind_int( stmt[2 + OFFSET], 2, new_parent_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [2, 2]" )

    rc = sqlite3_bind_int( stmt[2 + OFFSET], 3, new_older_sibling_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [2, 3]" )

    rc = sqlite3_step( stmt[2 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [2]" )

    rc = sqlite3_bind_int( stmt[4 + OFFSET], 1, new_parent_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [4, 1]" )

    rc = sqlite3_bind_int( stmt[4 + OFFSET], 2, new_older_sibling_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [4, 2]" )

    rc = sqlite3_bind_int( stmt[4 + OFFSET], 3, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [4, 3]" )

    rc = sqlite3_step( stmt[4 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [4]" )

    return 0;
}


gint
zond_dbase_get_icon_name_and_node_text( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** icon_name,
        gchar** node_text, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT icon_name, node_text, node_id FROM baum_inhalt WHERE node_id=?;",

            "SELECT icon_name, node_text, node_id FROM baum_auswertung WHERE node_id=?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW )
    {
        gchar* buf_icon_name = (gchar*)
                sqlite3_column_text( stmt[0 + OFFSET], 0 );
        if ( buf_icon_name && icon_name ) *icon_name= g_strdup( buf_icon_name );
        else if ( node_text ) *node_text = g_strdup( "" );

        gchar* buf_node_text = (gchar*)
                sqlite3_column_text( stmt[0 + OFFSET], 1 );
        if ( buf_node_text && node_text ) *node_text = g_strdup( buf_node_text );
        else if ( node_text ) *node_text = g_strdup( "" );
    }
    else if ( rc == SQLITE_DONE ) return 1;

    return 0;
}



//betrifft nur tabelle baum_auswertung!
gint
zond_dbase_get_text( ZondDBase* zond_dbase, gint node_id, gchar** text, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT node_id, text FROM baum_auswertung WHERE node_id = ?;"
       };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )
    else if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strconcat( "node_id existiert nicht", NULL ) );

        return -2;
    }

    if ( rc == SQLITE_ROW && text ) *text =
            g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 1 ) );

    return 0;
}


/** Rückgabe:
*** Fehler: -1
*** node_id nicht vorhanden: -2
*** ansonsten: parent_id */
gint
zond_dbase_get_parent( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;
    gint parent_id = 0;

    const gchar* sql[] = {
        //...
            "SELECT parent_id FROM baum_inhalt WHERE node_id = ?;",

            "SELECT parent_id FROM baum_auswertung WHERE node_id = ?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) parent_id =
            sqlite3_column_int( stmt[0 + OFFSET], 0 );

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
zond_dbase_get_older_sibling( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint older_sibling_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT older_sibling_id FROM baum_inhalt WHERE node_id = ?;",

            "SELECT older_sibling_id FROM baum_auswertung WHERE node_id = ?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) older_sibling_id =
            sqlite3_column_int( stmt[0 + OFFSET], 0 );

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
zond_dbase_get_younger_sibling( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint younger_sibling_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_inhalt AS inhalt1 "
                "LEFT JOIN baum_inhalt AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.older_sibling_id "
                "WHERE inhalt1.node_id > 0 AND inhalt1.node_id = ?;",

            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_auswertung AS inhalt1 "
                "LEFT JOIN baum_auswertung AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.older_sibling_id "
                "WHERE inhalt1.node_id > 0 AND inhalt1.node_id = ?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) younger_sibling_id =
            sqlite3_column_int( stmt[0 + OFFSET], 1 );

    if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = g_strdup( "node_id existiert nicht" );

        return -2;
    }

    return younger_sibling_id;
}


gint
zond_dbase_get_first_child( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint first_child_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_inhalt AS inhalt1 "
                "LEFT JOIN baum_inhalt AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.parent_id AND inhalt2.older_sibling_id = 0 "
                    "AND inhalt2.node_id != 0 "
                "WHERE inhalt1.node_id = ?;",

            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_auswertung AS inhalt1 "
                "LEFT JOIN baum_auswertung AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.parent_id AND inhalt2.older_sibling_id = 0 "
                    "AND inhalt2.node_id != 0 "
                "WHERE inhalt1.node_id = ?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) first_child_id =
            sqlite3_column_int( stmt[0 + OFFSET], 1 );

    if ( rc == SQLITE_DONE ) ERROR_S_MESSAGE_VAL( "node_id existiert nicht", -2 )

    return first_child_id;
}


gint
zond_dbase_get_ref_id( ZondDBase* zond_dbase, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint ref_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT ref_id FROM baum_auswertung WHERE node_id=?"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) ref_id =
            sqlite3_column_int( stmt[0], 0 );
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
    Wenn kein rel_path zur node_id: 1
**/
gint
zond_dbase_get_rel_path( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** rel_path,
        gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;
    gchar* text = NULL;

    const gchar* sql[] = {
        //...
            "SELECT baum_inhalt.node_id, rel_path FROM baum_inhalt "
            "LEFT JOIN "
            "(SELECT dateien.rel_path AS rel_path, dateien.node_id AS d_node_id, "
            "ziele.node_id AS z_node_id FROM dateien LEFT JOIN ziele "
            "ON dateien.rel_path=ziele.rel_path) "
            "ON baum_inhalt.node_id=d_node_id OR "
            "baum_inhalt.node_id=z_node_id "
            "WHERE baum_inhalt.node_id=?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    if ( baum == BAUM_AUSWERTUNG )
    {
        node_id = zond_dbase_get_ref_id( zond_dbase, node_id, errmsg );
        if ( node_id < 0 ) ERROR_S
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )
    else if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = g_strdup( "node_id existiert nicht" );

        return -1;
    }

    text = (gchar*) sqlite3_column_text( stmt[0], 1 );

    if ( !text || !g_strcmp0( text, "" ) ) return 1;

    if ( rel_path ) *rel_path = g_strdup( (const gchar*) text );

    return 0;
}


/** Rückgabewert:
    Falls kein ziel: 1, *ziel unverändert
    Fall ziel: 0, *ziel wird allociert, falls != NULL
    Fehler: -1
**/
gint
zond_dbase_get_ziel( ZondDBase* zond_dbase, Baum baum, gint node_id, Ziel* ziel, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT ziel_id_von, index_von, ziel_id_bis, index_bis FROM ziele "
            "WHERE node_id=?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    if ( baum == BAUM_AUSWERTUNG )
    {
        node_id = zond_dbase_get_ref_id( zond_dbase, node_id, errmsg );
        if ( node_id < 0 ) ERROR_S
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )
    else if ( rc == SQLITE_DONE ) return 1;

    if ( !ziel ) return 0;

    gchar* buf = NULL;

    //ziel_id_von
    buf = (gchar*) sqlite3_column_text( stmt[0], 0 );
    ziel->ziel_id_von = g_strdup( buf );

    //ziel_id_bis
    buf = (gchar*) sqlite3_column_text( stmt[0], 2 );
    ziel->ziel_id_bis = g_strdup( buf );

    //index_von und -bis
    ziel->index_von = sqlite3_column_int( stmt[0], 1 );
    ziel->index_bis = sqlite3_column_int( stmt[0], 3 );

    return 0;
}


/*  Gibt 0 zurück, wenn rel_path nicht in Tabelle vorhanden
**  Bei Fehler: -1  */
gint
zond_dbase_get_node_id_from_rel_path( ZondDBase* zond_dbase, const gchar* rel_path, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    //...
        "SELECT node_id FROM dateien WHERE rel_path=?;"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0], 1, rel_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (rel_path)" )

    rc = sqlite3_step( stmt[0] );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) node_id = sqlite3_column_int( stmt[0], 0 );

    return node_id;
}


gint
zond_dbase_check_id( ZondDBase* zond_dbase, const gchar* id, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    //...
        "SELECT ziel_id_von FROM ziele WHERE ziel_id_von=?1;"
        "UNION "
        "SELECT ziel_id_bis FROM ziele WHERE ziel_id_bis=?1;"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0], 1, id, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (id)" )

    rc = sqlite3_step( stmt[0] );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) return 1;

    return 0;
}


gint
zond_dbase_set_node_text( ZondDBase* zond_dbase, Baum baum, gint node_id,
        const gchar* node_text, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    //...
        "UPDATE baum_inhalt SET node_text = ?1 WHERE node_id = ?2;",

        "UPDATE baum_auswertung SET node_text = ?1 WHERE node_id = ?2;"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0 + OFFSET], 1,
            node_text, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (text)" )

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


gint
zond_dbase_set_link( ZondDBase* zond_dbase, const gint baum_id,
        const gint node_id, const gchar* projekt_dest, const gint baum_id_dest,
        const gint node_id_dest, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

/*  set_link  */
    const gchar* sql[] = {
            "INSERT INTO links (baum_id, node_id, projekt_target, "
                "baum_id_target, node_id_target) "
                "VALUES (?, ?, ?, ?, ?); "
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0], 1, baum_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (baum_id)" )

    rc = sqlite3_bind_int( stmt[0], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_bind_text( stmt[0], 3, projekt_dest,
            -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (projekt)" )

    rc = sqlite3_bind_int( stmt[0], 4, baum_id_dest );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (baum_id_dest)" )

    rc = sqlite3_bind_int( stmt[0], 5, node_id_dest );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id_dest)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


gint
zond_dbase_check_link( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    //...
        "SELECT ID FROM links WHERE baum_id=?1 AND node_id=?2;"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0], 1, baum );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (baum)" )

    rc = sqlite3_bind_int( stmt[0], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) return sqlite3_column_int( stmt[0], 0 );

    return 0;
}


gint
zond_dbase_get_link( ZondDBase* zond_dbase, gint ID, Baum* baum,
        gint* node_id, gchar** project, Baum* baum_target, gint* node_id_target,
        gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    //...
        "SELECT baum_id, node_id, projekt_target, baum_id_target, node_id_target FROM links "
            "WHERE ID=?1;"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0], 1, ID );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_DONE ) return 1;

    if ( baum ) *baum = sqlite3_column_int( stmt[0], 0 );
    if ( node_id ) *node_id = sqlite3_column_int( stmt[0], 1 );
    if ( project ) *project = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 2 ) );
    if ( baum_target ) *baum_target = sqlite3_column_int( stmt[0], 3 );
    if ( node_id_target ) *node_id_target = sqlite3_column_int( stmt[0], 4 );

    return 0;
}


gint
zond_dbase_remove_link( ZondDBase* zond_dbase, const gint baum_id, const gint
        node_id, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    /*  remove_link */
            "DELETE FROM links WHERE baum_id=? AND node_id=?; "
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0], 1, baum_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (baum_id)" )

    rc = sqlite3_bind_int( stmt[0], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


gint
zond_dbase_update_path( ZondDBase* zond_dbase, const gchar* old_path, const gchar* new_path, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE dateien SET rel_path = "
            "REPLACE( SUBSTR( rel_path, 1, LENGTH( ?1 ) ), ?1, ?2 ) || "
            "SUBSTR( rel_path, LENGTH( ?1 ) + 1 );"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0], 1, old_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (old_path)" )

    rc = sqlite3_bind_text( stmt[0], 2, new_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (new_path)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


/*  Gibt 0 zurück, wenn rel_path in db nicht vorhanden, wenn doch: 1
**  Bei Fehler: -1  */
/*
zond (treeviews.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2020  pelo america

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


#include <gtk/gtk.h>
#include <mupdf/fitz.h>

#include "treeviews.h"
#include "project.h"

#include "../global_types.h"
#include "../zond_tree_store.h"
#include "../zond_dbase.h"
#include "../zond_treeview.h"
#include "../zond_pdf_document.h"

#include "../99conv/pdf.h"

#include "../../misc.h"


Baum
treeviews_get_baum_iter( Projekt* zond, GtkTreeIter* iter )
{
    ZondTreeStore* tree_store = NULL;

    tree_store = zond_tree_store_get_tree_store( iter );

    if ( tree_store == ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) )) ) return BAUM_INHALT;
    else if ( tree_store == ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG]) )) ) return BAUM_AUSWERTUNG;

    return KEIN_BAUM;
}


static gint
treeviews_get_page_num_from_dest_doc( fz_context* ctx, pdf_document* doc, const gchar* dest, gchar** errmsg )
{
    pdf_obj* obj_dest_string = NULL;
    pdf_obj* obj_dest = NULL;
    pdf_obj* pageobj = NULL;
    gint page_num = 0;

    obj_dest_string = pdf_new_string( ctx, dest, strlen( dest ) );
    fz_try( ctx ) obj_dest = pdf_lookup_dest( ctx, doc, obj_dest_string);
    fz_always( ctx ) pdf_drop_obj( ctx, obj_dest_string );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_lookup_dest" )

	pageobj = pdf_array_get( ctx, obj_dest, 0 );

	if ( pdf_is_int( ctx, pageobj ) ) page_num = pdf_to_int( ctx, pageobj );
	else
	{
		fz_try( ctx ) page_num = pdf_lookup_page_number( ctx, doc, pageobj );
		fz_catch( ctx ) ERROR_MUPDF( "pdf_lookup_page_number" )
	}

    return page_num;
}


static gint
treeviews_get_page_num_from_dest( fz_context* ctx, const gchar* rel_path,
        const gchar* dest, gchar** errmsg )
{
    pdf_document* doc = NULL;
    gint page_num = 0;

    fz_try( ctx ) doc = pdf_open_document( ctx, rel_path );
    fz_catch( ctx ) ERROR_MUPDF( "fz_open_document" )

    page_num = treeviews_get_page_num_from_dest_doc( ctx, doc, dest, errmsg );
	pdf_drop_document( ctx, doc );
    if ( page_num < 0 ) ERROR_S

    return page_num;
}


/** Gibt nur bei Fehler NULL zurück, sonst immer Zeiger auf Anbindung **/
static Anbindung*
treeviews_ziel_zu_anbindung( fz_context* ctx, const gchar* rel_path, Ziel* ziel, gchar** errmsg )
{
    gint page_num = 0;

    Anbindung* anbindung = g_malloc0( sizeof( Anbindung ) );

    page_num = treeviews_get_page_num_from_dest( ctx, rel_path, ziel->ziel_id_von, errmsg );
    if ( page_num == -1 )
    {
        g_free( anbindung );
        ERROR_S_VAL( NULL )
    }
    else if ( page_num == -2 )
    {
        if ( errmsg ) *errmsg = g_strdup( "NamedDest nicht in Dokument vohanden" );
        g_free( anbindung );

        return NULL;
    }
    else anbindung->von.seite = page_num;

    page_num = treeviews_get_page_num_from_dest( ctx, rel_path, ziel->ziel_id_bis,
            errmsg );
    if ( page_num == -1 )
    {
        g_free( anbindung );

        ERROR_S_VAL( NULL )
    }
    else if ( page_num == -2 )
    {
        if ( errmsg ) *errmsg = g_strdup( "NamedDest nicht in Dokument vohanden" );
        g_free( anbindung );

        return NULL;
    }
    else anbindung->bis.seite = page_num;

    anbindung->von.index = ziel->index_von;
    anbindung->bis.index = ziel->index_bis;

    return anbindung;
}


/** Keine Datei mit node_id verknüpft: 2
    Kein ziel mit node_id verknüpft: 1
    Datei und ziel verknüpft: 0
    Fehler (inkl. node_id existiert nicht): -1

    Funktion sollte thread-safe sein! **/
gint
treeviews_get_rel_path_and_anbindung( Projekt* zond, Baum baum, gint node_id,
        gchar** rel_path, Anbindung** anbindung, gchar** errmsg )
{
    gint rc = 0;
    Ziel ziel = { 0, };
    gchar* rel_path_intern = NULL;
    Anbindung* anbindung_intern = NULL;

    rc = zond_dbase_get_rel_path( zond->dbase_zond->zond_dbase_work, baum, node_id, &rel_path_intern, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc == 1 ) return 2;

    rc = zond_dbase_get_ziel( zond->dbase_zond->zond_dbase_work, baum, node_id, &ziel, errmsg );
    if ( rc == -1 )
    {
        g_free( rel_path_intern );
        ERROR_S
    }
    else if ( rc == 1 )
    {
        if ( rel_path ) *rel_path = rel_path_intern;
        else g_free( rel_path_intern );

        return 1;
    }
/*  Muß nicht mit mutex geschützt werden
        ->Zugriff auf gleiche Datei aus mehreren threads m.E. zulässig, wenn unterschiedliches pdf_document

    const ZondPdfDocument* zond_pdf_document = zond_pdf_document_is_open( rel_path_intern );
    if ( zond_pdf_document ) zond_pdf_document_mutex_lock( zond_pdf_document );
*/
    anbindung_intern = treeviews_ziel_zu_anbindung( zond->ctx, rel_path_intern, &ziel, errmsg );

//    if ( zond_pdf_document ) zond_pdf_document_mutex_unlock( zond_pdf_document );

    if ( !anbindung_intern )
    {
        g_free( rel_path_intern );
        ERROR_S
    }

    if ( rel_path ) *rel_path = rel_path_intern;
    else g_free( rel_path_intern );

    if ( anbindung ) *anbindung = anbindung_intern;
    else g_free( anbindung_intern );

    return 0;
}


