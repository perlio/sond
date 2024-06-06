/*
zond (zond_convert.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2024  pelo america

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
#include <glib/gstdio.h>
#include <mupdf/fitz.h>
#include <sqlite3.h>

#include "zond_dbase.h"
#include "zond_treeview.h"

#include "99conv/pdf.h"



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
zond_convert_get_younger_sibling_0( ZondDBase* zond_dbase, Baum baum, gint node_id, GError** error )
{
    gint rc = 0;
    gint younger_sibling = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT node_id "
                "FROM old.baum_inhalt WHERE older_sibling_id = ?1;",

            "SELECT node_id "
                "FROM old.baum_auswertung WHERE older_sibling_id = ?1;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc ) ERROR_Z

    rc = sqlite3_bind_int( stmt[baum - 1], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_Z_DBASE

    rc = sqlite3_step( stmt[baum - 1] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_Z_DBASE

    if ( rc == SQLITE_ROW ) younger_sibling =
            sqlite3_column_int( stmt[baum - 1], 0 );

    return younger_sibling;
}


static gint
zond_convert_get_first_child_0( ZondDBase* zond_dbase, Baum baum, gint node_id, GError** error )
{
    gint rc = 0;
    gint first_child_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT node_id FROM old.baum_inhalt "
                "WHERE parent_id=? AND older_sibling_id=0 AND node_id!=0;",

            "SELECT node_id FROM old.baum_auswertung "
                "WHERE parent_id=? AND older_sibling_id=0 AND node_id!=0;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc ) ERROR_Z

    rc = sqlite3_bind_int( stmt[baum - 1], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_Z_DBASE

    rc = sqlite3_step( stmt[baum - 1] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_Z_DBASE

    if ( rc == SQLITE_ROW ) first_child_id =
            sqlite3_column_int( stmt[baum - 1], 0 );

    return first_child_id;
}


static gint
zond_convert_get_node_from_baum_auswertung_0( ZondDBase* zond_dbase, gint node_id, gint* link_id, gchar** icon_name,
        gchar** node_text, gchar** text, gint* ref_id, gint* link_id_target, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    gchar const* sql[] = {
            "SELECT icon_name, node_text, text, ref_id, links_target.node_id, links_origin.node_id "
                    "FROM old.baum_auswertung "
                    "LEFT JOIN old.links AS links_target "
                            "ON links_target.baum_id_target=2 AND links_target.node_id_target=old.baum_auswertung.node_id "
                    "LEFT JOIN old.links AS links_origin ON links_origin.node_id=old.baum_auswertung.node_id "
                    "WHERE old.baum_auswertung.node_id=?1; "
                    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc ) ERROR_Z

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_Z_DBASE

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_Z_DBASE

    if ( rc == SQLITE_ROW )
    {
        *icon_name = g_strdup( (gchar const*) sqlite3_column_text( stmt[0], 0 ) );
        *node_text = g_strdup( (gchar const*) sqlite3_column_text( stmt[0], 1 ) );
        *text = g_strdup( (gchar const*) sqlite3_column_text( stmt[0], 2 ) );
        *ref_id = sqlite3_column_int( stmt[0], 3 );
        *link_id_target = sqlite3_column_int( stmt[0], 4 );
        *link_id = sqlite3_column_int( stmt[0], 5 );
    }
    else
    {
        if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\nnode_id nicht in baum_auswertung gefunden", __func__ );
        return -1;
    }

    return 0;
}


static gint
zond_convert_get_node_from_baum_inhalt_0( ZondDBase* zond_dbase, gint node_id, gchar** icon_name,
        gchar** node_text, gchar** rel_path, gchar** rel_path_ziel, gchar** ziel_von, gint* index_von,
        gchar** ziel_bis, gint* index_bis, gint* link_id, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    gchar const* sql[] = {
            "SELECT icon_name, node_text, old.dateien.rel_path, old.ziele.rel_path, ziel_id_von, index_von, ziel_id_bis, index_bis, old.links.node_id "
                "FROM old.baum_inhalt "
                "LEFT JOIN old.dateien ON old.baum_inhalt.node_id=old.dateien.node_id "
                "LEFT JOIN old.ziele ON old.baum_inhalt.node_id=old.ziele.node_id "
                "LEFT JOIN old.links ON old.baum_inhalt.node_id=old.links.node_id_target AND old.links.baum_id_target=1 "
                "WHERE old.baum_inhalt.node_id=?1;"
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc ) ERROR_Z

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_Z_DBASE

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_Z_DBASE

    if ( rc == SQLITE_ROW )
    {
        *icon_name = g_strdup( (gchar const*) sqlite3_column_text( stmt[0], 0 ) );
        *node_text = g_strdup( (gchar const*) sqlite3_column_text( stmt[0], 1 ) );
        *rel_path = g_strdup( (gchar const*) sqlite3_column_text( stmt[0], 2 ) );
        *rel_path_ziel = g_strdup( (gchar const*) sqlite3_column_text( stmt[0], 3 ) );
        *ziel_von = g_strdup( (gchar const*) sqlite3_column_text( stmt[0], 4 ) );
        *index_von = sqlite3_column_int( stmt[0], 5 );
        *ziel_bis = g_strdup( (gchar const*) sqlite3_column_text( stmt[0], 6 ) );
        *index_bis = sqlite3_column_int( stmt[0], 7 );
        *link_id = sqlite3_column_int( stmt[0], 8 );
    }
    else
    {
        if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\nnode_id nicht in baum_inhalt gefunden", __func__ );
        return -1;
    }

    return 0;
}


typedef struct _DataConvert
{
    fz_context* ctx;
    pdf_document* doc;
} DataConvert;

typedef struct _Target
{
    gint id_link;
    gint baum_target;
    gint id_target;
    gint id_target_new;
} Target;

gint zond_convert_0_to_1_baum_inhalt( ZondDBase*, gint, gboolean, gint, DataConvert*, GArray*, gint*, GError** );

static gint
zond_convert_0_to_1_baum_inhalt_insert( ZondDBase* zond_dbase, gint anchor_id, gboolean child, gint node_id,
        gchar const* icon_name, gchar const* node_text, gchar const* rel_path, gchar const* rel_path_ziel, gchar const* ziel_von,
        gint index_von, gchar const* ziel_bis, gint index_bis, DataConvert* data_convert,
        GArray* arr_targets, gint* node_inserted, GError** error )
{
    if ( !rel_path && !ziel_von ) //STRUKT
    {
        *node_inserted = zond_dbase_insert_node( zond_dbase, anchor_id, child, ZOND_DBASE_TYPE_BAUM_STRUKT,
                0, NULL, NULL, icon_name, node_text, NULL, error );
        if ( *node_inserted == -1 ) ERROR_Z
    }
    else if ( rel_path && !ziel_von ) //datei
    {
        gint rc = 0;
        gint first_child_file = 0;
        gint node_inserted_root = 0;

        //FILE_PART_ROOT einfügen
        rc = zond_dbase_create_file_root( zond_dbase, rel_path, icon_name, node_text, NULL, &node_inserted_root, error );
        if ( rc ) ERROR_Z

        //BAUM_INHALT_FILE_PART einfügen
        *node_inserted = zond_dbase_insert_node( zond_dbase, anchor_id, child, ZOND_DBASE_TYPE_BAUM_INHALT_FILE,
                node_inserted_root, NULL, NULL, NULL, NULL, NULL, error );
        if ( *node_inserted == -1 ) ERROR_Z

        //neue Rekursion mit FILE_PART_ROOT starten
        first_child_file = zond_convert_get_first_child_0( zond_dbase, BAUM_INHALT, node_id, error );
        if ( first_child_file == -1 ) ERROR_Z

        if ( first_child_file )
        {
            gint rc = 0;
            gchar* errmsg = NULL;
            gint node_inserted_file = 0;

            data_convert->ctx = fz_new_context( NULL, NULL, FZ_STORE_UNLIMITED );
            if ( !data_convert->ctx )
            {
                if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\nfz_context konnte nicht initialisiert werden", __func__ );
                return -1;
            }

            rc = pdf_open_and_authen_document( data_convert->ctx, TRUE, rel_path, NULL, &(data_convert->doc), NULL, &errmsg );
            if ( rc )
            {
                fz_drop_context( data_convert->ctx );
                if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\n%s", __func__, errmsg );
                g_free( errmsg );

                return -1;
            }

            rc = zond_convert_0_to_1_baum_inhalt( zond_dbase, node_inserted_root, TRUE, first_child_file, data_convert,
                    arr_targets, &node_inserted_file, error );
            pdf_drop_document( data_convert->ctx, data_convert->doc );
            fz_drop_context( data_convert->ctx );
            if ( rc ) ERROR_Z
        }
    }
    else if ( !rel_path && ziel_von )
    {
        gchar* file_part = NULL;
        gchar* section = NULL;
        Anbindung anbindung = { 0 };
        gchar* errmsg = NULL;

        anbindung.von.seite = treeviews_get_page_num_from_dest_doc( data_convert->ctx, data_convert->doc,
                ziel_von, &errmsg );
        if ( anbindung.von.seite == -1 )
        {
            if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\n%s", __func__, errmsg );
            g_free( errmsg );

            return -1;
        }

        anbindung.bis.seite = treeviews_get_page_num_from_dest_doc( data_convert->ctx, data_convert->doc,
                ziel_bis, &errmsg );
        if ( anbindung.bis.seite == -1 )
        {
            if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\n%s", __func__, errmsg );
            g_free( errmsg );

            return -1;
        }

        zond_treeview_build_file_section( anbindung, &section );
        file_part = g_strdup_printf( "/%s//", rel_path_ziel );
        *node_inserted = zond_dbase_insert_node( zond_dbase, anchor_id, child, ZOND_DBASE_TYPE_FILE_PART,
                0, file_part, section, icon_name, node_text, NULL, error );
        g_free( section );
        g_free( file_part );
        if ( *node_inserted == -1 ) ERROR_Z
    }

    return 0;
}


gint
zond_convert_0_to_1_baum_inhalt( ZondDBase* zond_dbase, gint anchor_id, gboolean child,
        gint node_id, DataConvert* data_convert, GArray* arr_targets, gint* node_inserted, GError** error )
{
    gint rc = 0;
    gchar* icon_name = NULL;
    gchar* node_text = NULL;
    gchar* rel_path = NULL;
    gchar* rel_path_ziel = NULL;
    gchar* ziel_von = NULL;
    gint index_von = 0;
    gchar* ziel_bis = NULL;
    gint index_bis = 0;
    gint id_link = 0;
    gint younger_sibling = 0;
    gboolean stop_rec = FALSE;

    rc = zond_convert_get_node_from_baum_inhalt_0( zond_dbase, node_id, &icon_name, &node_text, &rel_path,
            &rel_path_ziel, &ziel_von, &index_von, &ziel_bis, &index_bis, &id_link, error );
    if ( rc ) ERROR_Z

    rc = zond_convert_0_to_1_baum_inhalt_insert( zond_dbase, anchor_id, child, node_id, icon_name, node_text,
            rel_path, rel_path_ziel, ziel_von, index_von, ziel_bis, index_bis, data_convert, arr_targets, node_inserted, error );
    if ( rel_path ) stop_rec = TRUE; //datei-root
    g_free( icon_name );
    g_free( node_text );
    g_free( rel_path );
    g_free( rel_path_ziel );
    g_free( ziel_von );
    g_free( ziel_bis );
    if ( rc ) ERROR_Z

    if ( id_link )
    {
        Target target = { id_link, BAUM_INHALT, node_id, *node_inserted };
        g_array_append_val( arr_targets, target );
    }

    if ( !stop_rec ) //weil ist Datei; neuer Zweig wird eingefügt
    {
        gint first_child = 0;

        first_child = zond_convert_get_first_child_0( zond_dbase, BAUM_INHALT, node_id, error );
        if ( first_child == -1 ) ERROR_Z

        if ( first_child > 0 )
        {
            gint rc = 0;
            gint node_inserted_loop = 0;

            rc = zond_convert_0_to_1_baum_inhalt( zond_dbase, *node_inserted, TRUE, first_child,
                    data_convert, arr_targets, &node_inserted_loop, error );
            if ( rc ) ERROR_Z
        }
    }

    younger_sibling = zond_convert_get_younger_sibling_0( zond_dbase, BAUM_INHALT, node_id, error );
    if ( younger_sibling == -1 ) ERROR_Z

    if ( younger_sibling > 0 )
    {
        gint rc = 0;
        gint node_inserted_loop = 0;

        rc = zond_convert_0_to_1_baum_inhalt( zond_dbase, *node_inserted, FALSE, younger_sibling,
                data_convert, arr_targets, &node_inserted_loop, error );
        if ( rc ) ERROR_Z
    }

    return 0;
}


typedef struct _Links
{
    gint id_link;
    gint id_link_new;
} Links;

static gint
zond_convert_0_to_1_baum_auswertung( ZondDBase* zond_dbase, gint anchor_id, gboolean child, gint node_id,
        GArray* arr_links, GArray* arr_targets, GError** error )
{
    gint rc = 0;
    gchar* icon_name = NULL;
    gchar* node_text = NULL;
    gchar* text = NULL;
    gint ref_id = 0;
    gint link_id = 0;
    gint link_id_target = 0;
    gint first_child = 0;
    gint younger_sibling = 0;
    gint node_inserted = 0;

    rc = zond_convert_get_node_from_baum_auswertung_0( zond_dbase, node_id, &link_id, &icon_name, &node_text, &text, &ref_id,
            &link_id_target, error );
    if ( rc ) ERROR_Z

    if ( link_id )
    {
        node_inserted = zond_dbase_insert_node( zond_dbase, anchor_id, child, ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK,
                0, NULL, NULL, NULL, NULL, NULL, error );
        if ( node_inserted == -1 ) ERROR_Z

        Links link = { link_id, node_inserted };
        g_array_append_val( arr_links, link );
    }
    else
    {
        if ( !ref_id && !link_id_target ) //STRUKT
        {
            node_inserted = zond_dbase_insert_node( zond_dbase, anchor_id, child, ZOND_DBASE_TYPE_BAUM_STRUKT,
                    0, NULL, NULL, icon_name, node_text, text, error );
            if ( node_inserted == -1 ) ERROR_Z
        }
        else if ( ref_id ) //datei
        {
            node_inserted = zond_dbase_insert_node( zond_dbase, anchor_id, child, ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY,
                    ref_id, NULL, NULL, icon_name, node_text, text, error );
            if ( node_inserted == -1 ) ERROR_Z
        }

        if ( link_id_target )
        {
            Target target = { link_id_target, BAUM_AUSWERTUNG, node_id, node_inserted };
            g_array_append_val( arr_targets, target );
        }

        first_child = zond_convert_get_first_child_0( zond_dbase, BAUM_AUSWERTUNG, node_id, error );
        if ( first_child == -1 ) ERROR_Z

        if ( first_child > 0 )
        {
            gint rc = 0;

            rc = zond_convert_0_to_1_baum_auswertung( zond_dbase, node_inserted, TRUE,
                    first_child, arr_links, arr_targets, error );
            if ( rc ) ERROR_Z
        }

        younger_sibling = zond_convert_get_younger_sibling_0( zond_dbase, BAUM_AUSWERTUNG, node_id, error );
        if ( younger_sibling == -1 ) ERROR_Z

        if ( younger_sibling > 0 )
        {
            gint rc = 0;

            rc = zond_convert_0_to_1_baum_auswertung( zond_dbase, node_inserted, FALSE,
                    younger_sibling, arr_links, arr_targets, error );
            if ( rc ) ERROR_Z
        }
    }

    return 0;
}


gint
zond_convert_0_to_1_update_link( ZondDBase* zond_dbase, gint node_id,
        gint link, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE knoten "
            "SET link=?2 WHERE ID=?1; "
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc ) ERROR_Z

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_Z_DBASE

    rc = sqlite3_bind_int( stmt[0], 2,
            link );
    if ( rc != SQLITE_OK ) ERROR_Z_DBASE

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_Z_DBASE

    return 0;
}


gint
zond_convert_0_to_1( ZondDBase* zond_dbase, GError** error )
{
    gint first_child = 0;
    DataConvert data_convert = { 0 };
    GArray* arr_targets = NULL;
    GArray* arr_links = NULL;
    gchar* project_dir = NULL;

    //ersten Knoten Baum_inhalt
    first_child = zond_convert_get_first_child_0( zond_dbase, BAUM_INHALT, 0, error );
    if ( first_child == -1 ) ERROR_Z

    arr_targets = g_array_new( FALSE, FALSE, sizeof( Target ) );

    project_dir = g_path_get_dirname( zond_dbase_get_path( zond_dbase ) );
    g_chdir( project_dir );
    g_free( project_dir );

    if ( first_child )
    {
        gint rc = 0;
        gint node_inserted = 0;

        rc = zond_convert_0_to_1_baum_inhalt( zond_dbase, 1, TRUE, first_child,
                &data_convert, arr_targets, &node_inserted, error );
        if ( rc )
        {
            g_array_unref( arr_targets );
            ERROR_Z
        }
    }

    //ersten Knoten Baum_auswertung
    first_child = zond_convert_get_first_child_0( zond_dbase, BAUM_AUSWERTUNG, 0, error );
    if ( first_child == -1 )
    {
        g_array_unref( arr_targets );
        ERROR_Z
    }

    if ( first_child )
    {
        gint rc = 0;

        arr_links = g_array_new( FALSE, FALSE, sizeof( Links ) );

        rc = zond_convert_0_to_1_baum_auswertung( zond_dbase, 2, TRUE, first_child, arr_links, arr_targets, error );
        if ( rc )
        {
            g_array_unref( arr_targets );
            g_array_unref( arr_links );
            ERROR_Z
        }
    }

    //arr_targets durchgehen
    for ( gint i = 0; i < arr_links->len; i++ )
    {
        Links link = { 0 };

        link = g_array_index( arr_links, Links, i );

        for ( gint u = 0; u < arr_targets->len; u++ )
        {
            Target target = { 0 };

            target = g_array_index( arr_targets, Target, u );

            if ( link.id_link == target.id_link )
            {
                gint rc = 0;

                rc = zond_convert_0_to_1_update_link( zond_dbase, link.id_link_new, target.id_target_new, error );
                if ( rc )
                {
                    g_array_unref( arr_targets );
                    g_array_unref( arr_links );

                    ERROR_Z
                }

                break;
            }
        }
    }

    g_array_unref( arr_targets );
    g_array_unref( arr_links );

    return 0;
}



