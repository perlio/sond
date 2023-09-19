/*
sond (sond_treeviewfm.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2022  pelo america

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

#include "misc.h"

#include "sond_treeviewfm.h"

#include "zond/zond_dbase.h"
#include "zond/99conv/general.h"



typedef struct
{
    gchar* root;
    GtkTreeViewColumn* column_eingang;
    ZondDBase* zond_dbase;
} SondTreeviewFMPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondTreeviewFM, sond_treeviewfm, SOND_TYPE_TREEVIEW)


static gint
sond_treeviewfm_dbase_begin( SondTreeviewFM* stvfm, gchar** errmsg )
{
    gint rc = 0;
    SondTreeviewFMPrivate* priv = sond_treeviewfm_get_instance_private( stvfm );

    rc = zond_dbase_begin( priv->zond_dbase, errmsg );
    if ( rc ) ERROR_S

    return 0;
}


static gint
sond_treeviewfm_dbase_test( SondTreeviewFM* stvfm, const gchar* source, gchar** errmsg )
{
    gint rc = 0;

    SondTreeviewFMPrivate* priv = sond_treeviewfm_get_instance_private( stvfm );

    rc = zond_dbase_test_path( priv->zond_dbase, source, errmsg );
    if ( rc == -1 ) ERROR_S

    return rc;
}


static gint
sond_treeviewfm_dbase_update_path( SondTreeviewFM* stvfm, const gchar* source,
        const gchar* dest, gchar** errmsg )
{
    gint rc = 0;

    SondTreeviewFMPrivate* priv = sond_treeviewfm_get_instance_private( stvfm );

    rc = zond_dbase_update_path( priv->zond_dbase, source, dest, errmsg );
    if ( rc ) ERROR_ROLLBACK( priv->zond_dbase )

    return 0;
}


static gint
sond_treeviewfm_dbase_end( SondTreeviewFM* stvfm, gboolean suc, gchar** errmsg )
{
    SondTreeviewFMPrivate* priv = sond_treeviewfm_get_instance_private( stvfm );

    if ( suc )
    {
        gint rc = 0;

        rc = zond_dbase_commit( priv->zond_dbase, errmsg );
        if ( rc ) ERROR_ROLLBACK( priv->zond_dbase )
    }
    else
    {
        gint rc = 0;

        rc = zond_dbase_rollback( priv->zond_dbase, errmsg );
        if ( rc ) ERROR_S
    }

    return 0;
}


static gboolean
sond_treeviewfm_other_fm( SondTreeviewFM* stvfm )
{
    Clipboard* clipboard = NULL;
    ZondDBase* dbase_source = NULL;
    ZondDBase* dbase_dest = NULL;

    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;
    dbase_source = sond_treeviewfm_get_dbase( SOND_TREEVIEWFM(clipboard->tree_view) );

    dbase_dest = stvfm_priv->zond_dbase;

    if ( dbase_dest == dbase_source ) return FALSE;

    return TRUE;
}


static gint
sond_treeviewfm_dbase( SondTreeviewFM* stvfm, gint mode, const gchar* rel_path_source,
        const gchar* rel_path_dest, gchar** errmsg )
{
    gint rc = 0;
    Clipboard* clipboard = NULL;

    clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;

    if ( mode == 2 && sond_treeviewfm_other_fm( stvfm ) )
    {
        rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_test( SOND_TREEVIEWFM(clipboard->tree_view), rel_path_source, errmsg );
        if ( rc ) //aufräumen...
        {
            if ( rc == -1 ) ERROR_S
            else if ( rc == 1 ) return 1;
        }
    }

    if ( mode == 4 )
    {
        gint rc = 0;

        rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_test( stvfm, rel_path_dest, errmsg );
        if ( rc )
        {
            if ( rc == -1 ) ERROR_S
            else if ( rc == 1 ) return 1;
        }
    }

    rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_begin( stvfm, errmsg );
    if ( rc ) ERROR_S

    if ( mode == 2 || mode == 3 ) //mode == 2: beyond wurde schon ausgeschlossen - mode == 3: ausgeschlossen
    {
        rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_update_path( stvfm,
                rel_path_source, rel_path_dest, errmsg );
        if ( rc ) ERROR_S
    }

    return 0;
}


/** mode:
    0 - insert dir
    1 - copy file
    2 - move file oder dir
    3 - row edited
    4 - delete
    Rückgabe:
    -1- Fehler - *errmsg wird "gefüllt"
    0 - Aktion erfolgreich abgeschlossen
    1 - keine Veränderung am Filesystem
    2 - keine Veränderung am Filesystem - Abbruch gewählt
**/
static gint
sond_treeviewfm_move_copy_create_delete( SondTreeviewFM* stvfm, GFile* file_source,
        GFile** file_dest, gint mode, gchar** errmsg )
{
    gint zaehler = 0;
    gchar* basename = NULL;

    basename = g_file_get_basename( *file_dest );

    while ( 1 )
    {
        GError* error = NULL;
        gboolean suc = FALSE;

        if ( mode == 0 ) suc = g_file_make_directory( *file_dest, NULL, &error );
        else //if ( mode == 1 || mode == 2 || mode == 3 || mode == 4 )
        {
            gint rc = 0;
            Clipboard* clipboard = NULL;
            gchar* rel_path_source = NULL;
            gchar* rel_path_dest = NULL;

            clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;

            if ( mode == 1 || mode == 2 ) rel_path_source =
                    get_rel_path_from_file( sond_treeviewfm_get_root(
                    SOND_TREEVIEWFM(clipboard->tree_view) ), file_source );
            else if ( mode == 3 ) rel_path_source =  get_rel_path_from_file(
                    sond_treeviewfm_get_root( stvfm ), file_source );
            rel_path_dest = get_rel_path_from_file( sond_treeviewfm_get_root( stvfm ), *file_dest );

            rc = sond_treeviewfm_dbase( stvfm, mode, rel_path_source, rel_path_dest,
                    errmsg );

            g_free( rel_path_dest );
            g_free( rel_path_source );

            if ( rc == -1 )
            {
                g_free( basename );
                ERROR_S
            }
            else if ( rc == 1 ) return 1;

            if ( mode == 1 ) suc = g_file_copy ( file_source, *file_dest,
                    G_FILE_COPY_NONE, NULL, NULL, NULL, &error );
            else if ( mode == 2 || mode == 3 ) suc = g_file_move ( file_source,
                    *file_dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &error );
            else if ( mode == 4 ) suc = g_file_delete( *file_dest, NULL, &error );

            rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_end( stvfm, suc, errmsg );
            if ( rc )
            {
                g_free( basename );
                ERROR_SOND( "dbase_end" )
            }
        }

        if ( suc ) break;
        else
        {
            if ( g_error_matches( error, G_IO_ERROR, G_IO_ERROR_EXISTS) )
            {
                GFile* file_parent = NULL;

                g_clear_error( &error );
                if ( mode == 3 ) //Filename editiert
                {
                    g_free( basename );
                    return 1; //nichts geändert!
                }

                gchar* basename_new_try = NULL;
                gchar* zusatz = NULL;

                if ( mode == 1 && zaehler == 0 ) basename_new_try =
                        g_strconcat( basename, " - Kopie", NULL );
                else if ( mode == 1 && zaehler > 0 )
                {
                    zusatz = g_strdup_printf( " (%i)", zaehler + 1 );
                    basename_new_try = g_strconcat( basename, "- Kopie", zusatz, NULL );
                }
                else if ( mode == 2 || mode == 0 )
                {
                    zusatz = g_strdup_printf( " (%i)", zaehler + 2 );
                    basename_new_try = g_strconcat( basename, zusatz, NULL );
                }

                g_free( zusatz );

                zaehler++;

                file_parent = g_file_get_parent( *file_dest );
                g_object_unref( *file_dest );

                *file_dest = g_file_get_child( file_parent, basename_new_try );
                g_object_unref( file_parent );
                g_free( basename_new_try );

                continue;
            }
            else if ( g_error_matches( error, G_IO_ERROR,
                    G_IO_ERROR_PERMISSION_DENIED ) )
            {
                g_clear_error( &error );

                gint res = dialog_with_buttons( gtk_widget_get_toplevel(
                        GTK_WIDGET(stvfm) ), "Zugriff nicht erlaubt",
                        "Datei möglicherweise geöffnet", NULL, "Erneut versuchen", 1,
                        "Überspringen", 2, "Abbrechen", 3, NULL );

                if ( res == 1 ) continue; //Namensgleichheit - wird oben behandelt
                else if ( res == 2 )
                {
                    g_free( basename );
                    return 1; //Überspringen
                }
                else if ( res == 3)
                {
                    g_free( basename );
                    return 2;
                }
            }
            else
            {
                g_free( basename );
                if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_...:\n",
                        error->message, NULL );
                g_error_free( error );

                return -1;
            }
        }
    }

    g_free( basename );

    return 0;
}


static void
sond_treeviewfm_text_edited( GtkCellRenderer* cell, gchar* path_string, gchar* new_text,
        gpointer data )
{
    gchar* errmsg = NULL;
    gchar* path_old = NULL;
    gchar* path_new = NULL;
    GtkTreeModel* model = NULL;
    GtkTreeIter iter;
    gchar* root = NULL;
    const gchar* old_text = NULL;
    GtkTreeIter iter_parent = { 0 };
    GFileInfo* info = NULL;
    GFile* file_source = NULL;
    GFile* file_dest = NULL;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;
    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    model = gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) );
    gtk_tree_model_get_iter_from_string( gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) ), &iter, path_string );

    gtk_tree_model_get( model, &iter, 0, &info, -1 );
    old_text = g_file_info_get_name( info );

    if ( !g_strcmp0( old_text, new_text ) ) return;

    if ( gtk_tree_model_iter_parent( model, &iter_parent, &iter ) ) root =
            sond_treeviewfm_get_full_path( stvfm, &iter_parent );
    else root = g_strdup( stvfm_priv->root );

    path_old = g_strconcat( root, "/", old_text, NULL );
    g_object_unref( info );

    path_new = g_strconcat( root, "/", new_text, NULL );

    g_free( root );

    file_source = g_file_new_for_path( path_old );
    file_dest = g_file_new_for_path( path_new );

    g_free( path_old );
    g_free( path_new );

    gint rc = 0;

    rc = sond_treeviewfm_move_copy_create_delete( stvfm, file_source, &file_dest,
            3, &errmsg );

    if ( rc == -1 )
            display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
            "Umbenennen nicht möglich\n\nBei Aufruf sond_treeviewfm_move_copy_create_delete:\n",
            errmsg, NULL );
    else if ( rc == 0 )
    {
        GError* error = NULL;
        GFileInfo* info = NULL;

        info = g_file_query_info( file_dest, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
        if ( !info )
        {
            display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
            "Umbenennen nicht möglich\n\nBei Aufruf g_file_query_info:\n",
            error->message, NULL );
            g_error_free( error );
        }
        else
        {
            gtk_tree_store_set( GTK_TREE_STORE(model), &iter, 0, info, -1 );
            gtk_tree_view_columns_autosize( GTK_TREE_VIEW(stvfm) );
            g_object_unref( info );
        }
    } //wenn rc_edit == 1 oder 2: einfach zurück

    g_object_unref( file_dest );

    return;
}


static void
sond_treeviewfm_row_collapsed( GtkTreeView* tree_view, GtkTreeIter* iter,
        GtkTreePath* path, gpointer data )
{
    GtkTreeIter iter_child;
    gboolean not_empty = TRUE;

    gtk_tree_model_iter_children( gtk_tree_view_get_model( tree_view ),
            &iter_child, iter );

    do {
        not_empty = gtk_tree_store_remove(
                GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )), &iter_child );
    } while ( not_empty );

    //dummy einfügen, dir ist ja nicht leer
    gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )),
            &iter_child, iter, -1 );

    gtk_tree_view_columns_autosize( tree_view );

    return;
}


static const gchar*
sond_treeviewfm_get_name( SondTreeviewFM* stvfm, GtkTreeIter* iter )
{
    GFileInfo* info = NULL;
    const gchar* name = NULL;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) ), iter,
            0, &info, -1 );

    if ( !info ) return NULL;

    name = g_file_info_get_name( info );
    g_object_unref( info );

    return name;
}


/** iter zeigt auf Verzeichnis, was zu füllen ist
    Es wurde bereits getestet, ob das Verzeichnis bereits geladen wurde
**/
static gint
sond_treeviewfm_load_dir_foreach( SondTreeviewFM* stvfm, GtkTreeIter* iter, GFile* file,
        GFileInfo* info, gpointer data, gchar** errmsg )
{
    GtkTreeIter iter_new = { 0 };

    GtkTreeIter* iter_dir = (GtkTreeIter*) data;

    //child in tree einfügen
    gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
            &iter_new, iter_dir, -1 );
    gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
            &iter_new, 0, info , -1 );

    //falls directory: überprüfen ob leer, falls nicht, dummy als child
    GFileType type = g_file_info_get_file_type( info );
    if ( type == G_FILE_TYPE_DIRECTORY )
    {
        GError* error = NULL;

        GFileEnumerator* enumer_child = g_file_enumerate_children( file, "*",
                G_FILE_QUERY_INFO_NONE, NULL, &error );
        if ( !enumer_child )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                    error->message, NULL );
            g_error_free( error );

            return -1;
        }

        GFile* grand_child = NULL;
        GtkTreeIter newest_iter;

        if ( !g_file_enumerator_iterate( enumer_child, NULL, &grand_child, NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( enumer_child );

            return -1;
        }
        g_object_unref( enumer_child );

        if ( grand_child ) gtk_tree_store_insert(
                GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
                &newest_iter, &iter_new, -1 );
    }

    return 0;
}


/** rc == -1: Fähler
    rc == 0: alles ausgeführt, sämtliche Callbacks haben 0 zurückgegeben
    rc == 1: alles ausgeführt, mindestens ein Callback hat 1 zurückgegeben
    rc == 2: nicht alles ausgeführt, Callback hat 2 zurückgegeben -> sofortiger Abbruch
    **/
static gint
sond_treeviewfm_dir_foreach( SondTreeviewFM* stvfm, GtkTreeIter* iter_dir,
        GFile* file, gboolean rec,
        gint (*foreach) ( SondTreeviewFM*, GtkTreeIter*, GFile*, GFileInfo*, gpointer, gchar** ),
        gpointer data, gchar** errmsg )
{
    GError* error = NULL;
    gboolean flag = FALSE;
    GFileEnumerator* enumer = NULL;

    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    enumer = g_file_enumerate_children( file, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
    if ( !enumer )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    while ( 1 )
    {
        GFile* file_child = NULL;
        GFileInfo* info_child = NULL;

        if ( !g_file_enumerator_iterate( enumer, &info_child, &file_child, NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( enumer );

            return -1;
        }

        if ( file_child ) //es gibt noch Datei in Verzeichnis
        {
            gint rc = 0;
            GtkTreeIter iter_file = { 0 };
            gboolean found = FALSE;
            gboolean root = FALSE;

            if ( !iter_dir )
            {
                GFile* file_root = NULL;

                file_root = g_file_new_for_path( stvfm_priv->root );
                if ( g_file_equal( file, file_root ) ) root = TRUE;
                g_object_unref( file_root );
            }

            if ( (iter_dir || root) && gtk_tree_model_iter_children(
                    gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) ), &iter_file, iter_dir ) )
            {
                do
                {
                    //den Namen des aktuellen Kindes holen
                    if ( !g_strcmp0( sond_treeviewfm_get_name( stvfm, &iter_file ),
                            g_file_info_get_name( info_child ) ) )
                            found = TRUE;//paßt?

                    if ( found ) break;
                } while ( gtk_tree_model_iter_next( gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) ), &iter_file ) );
            }

            rc = foreach( stvfm, (found) ? &iter_file : NULL, file_child, info_child, data, errmsg );
            if ( rc == -1 )
            {
                g_object_unref( enumer );
                if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf foreach:\n" ),
                        *errmsg );

                return -1;
            }
            else if ( rc == 1 ) flag = TRUE;
            else if ( rc == 2 ) //Abbruch gewählt
            {
                g_object_unref( enumer );
                return 2;
            }

            if ( rec && g_file_info_get_file_type( info_child ) == G_FILE_TYPE_DIRECTORY )
            {
                gint rc = 0;

                rc = sond_treeviewfm_dir_foreach( stvfm,
                        (found) ? &iter_file : NULL, file_child, TRUE, foreach,
                        data, errmsg );
                if ( rc == -1 )
                {
                    g_object_unref( enumer );
                    if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf foreach:\n" ),
                            *errmsg );

                    return -1;
                }
                else if ( rc == 1 ) flag = TRUE;//Abbruch gewählt
                else if ( rc == 2 )
                {
                    g_object_unref( enumer );
                    return 2;
                }
            }
        } //ende if ( file_child )
        else break;
    }

    g_object_unref( enumer );

    return (flag) ? 1 : 0;
}


static gint
sond_treeviewfm_load_dir( SondTreeviewFM* stvfm, GtkTreeIter* iter, gchar** errmsg )
{
    gint rc = 0;
    GFile* file = NULL;

    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    if ( iter )
    {
        gchar* full_path = NULL;

        full_path = sond_treeviewfm_get_full_path( stvfm, iter );
        file = g_file_new_for_path( full_path );
        g_free( full_path );
    }
    else file = g_file_new_for_path( stvfm_priv->root );

    //erst ganzes dir laden, dann ordnen, nach Kriterium,




    //fm_load_dir_foreach gibt 0 oder -1 zurück
    rc = sond_treeviewfm_dir_foreach( stvfm, iter, file, FALSE,
            sond_treeviewfm_load_dir_foreach, iter, errmsg );
    g_object_unref( file );
    if ( rc == -1 )
    {
        if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf fm_dir_foreach:\n" ),
                *errmsg );

        return -1;
    }

    return 0;
}


static void
sond_treeviewfm_row_expanded( GtkTreeView* tree_view, GtkTreeIter* iter,
        GtkTreePath* path, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    GtkTreeIter new_iter = { 0 };
    GFileInfo* info = NULL;

    gtk_tree_model_iter_nth_child( gtk_tree_view_get_model( tree_view ), &new_iter, iter, 0 );
    gtk_tree_model_get( gtk_tree_view_get_model( tree_view ), &new_iter, 0, &info, -1 );
    if ( !info ) //child ist dummy
    {
        rc = sond_treeviewfm_load_dir( SOND_TREEVIEWFM(tree_view), iter, &errmsg );
        if ( rc )
        {
            display_message( gtk_widget_get_toplevel( GTK_WIDGET(tree_view) ),
                    "Directory konnte nicht geladen werden\n\n"
                    "Bei Aufruf fm_load_dir:\n", errmsg, NULL );
            g_free( errmsg );
        }
        gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )),
                &new_iter );
    }
    else g_object_unref( info );

    gtk_tree_view_columns_autosize( tree_view );

    return;
}




typedef struct _FindPath
{
    const gchar* basename;
    GtkTreeIter* iter;
} FindPath;


static gint
sond_treeviewfm_find_path( SondTreeviewFM* stvfm, GtkTreeIter* iter,
        GFile* file, GFileInfo* info, gpointer data, gchar** errmsg )
{
    FindPath* find_path = (FindPath*) data;
    gchar* basename = NULL;

    basename = g_file_get_basename( file );

    if ( !g_strcmp0( find_path->basename, basename ) )
    {
        *(find_path->iter) = *iter;

        g_free( basename );

        return 2; //kann sofort abgebrochen werden
    }

    g_free( basename );

    return 0;
}


gint
sond_treeviewfm_set_cursor_on_path( SondTreeviewFM* stvfm, const gchar* path, gchar** errmsg )
{
    gchar** arr_path_segs = NULL;
    GtkTreeIter iter_file = { 0 };
    GFile* file = NULL;
    FindPath find_path = { 0 };
    gint i = 0;

    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    find_path.iter = &iter_file;

    arr_path_segs = g_strsplit_set( path, "/\\", -1 );

    if ( !arr_path_segs[0] ) return 0;

    file = g_file_new_for_path( stvfm_priv->root );

    do
    {
        gint rc = 0;
        GFile* file_tmp = NULL;

        find_path.basename = arr_path_segs[i];

        rc = sond_treeviewfm_dir_foreach( stvfm, (i == 0) ? NULL : find_path.iter,
                file, FALSE, sond_treeviewfm_find_path, &find_path, errmsg );
        if ( rc == -1 )
        {
            g_object_unref( file );
            ERROR_S
        }
        else if ( rc == 0 )
        {
            g_object_unref( file );
            ERROR_S_MESSAGE( "Pfad nicht gefunden" )
        }
        else if ( rc == 2 && arr_path_segs[i + 1] )
        {
            sond_treeview_expand_row( SOND_TREEVIEW(stvfm), find_path.iter );

            file_tmp = g_file_get_child( file, arr_path_segs[i] );
            g_object_unref( file );
            file = file_tmp;
        }
    } while ( arr_path_segs[++i] );

    g_object_unref( file );
    g_strfreev( arr_path_segs );

    //cursor setzen
    sond_treeview_set_cursor( SOND_TREEVIEW(stvfm), find_path.iter );

    return 0;
}


static void
sond_treeviewfm_results_row_activated( GtkWidget* listbox, GtkWidget* row, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    GtkWidget* label = NULL;
    const gchar* path = NULL;
    const gchar* path_rel = NULL;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) user_data;
    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    label = gtk_bin_get_child( GTK_BIN(row) );
    path = gtk_label_get_label( GTK_LABEL(label) );
    path_rel = path + strlen( stvfm_priv->root ) + 1;

    rc = sond_treeviewfm_set_cursor_on_path( stvfm, path_rel, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
                "Datei nicht gefunden\n\n", errmsg, NULL );

        g_free( errmsg );
    }

    return;
}


static void
sond_treeviewfm_render_file_name( GtkTreeViewColumn* column,
        GtkCellRenderer* renderer, GtkTreeModel* model, GtkTreeIter* iter, gpointer data )
{
    SondTreeviewFM* stvfm = SOND_TREEVIEWFM(data);

    g_object_set( G_OBJECT(sond_treeview_get_cell_renderer_text( SOND_TREEVIEW(stvfm) )), "text",
            sond_treeviewfm_get_name( SOND_TREEVIEWFM(stvfm), iter ), NULL );

    gchar* rel_path = NULL;

    rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );
    if ( rel_path )
    {
        gint rc = 0;
        gchar* errmsg = NULL;

        rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_test( stvfm, rel_path, &errmsg );
        g_free( rel_path );
        if ( rc == -1 )
        {
            display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
                    "Warnung -\n\nBei Aufruf dbase_test:\n",
                    errmsg, NULL );
            g_free( errmsg );
        }
        else if ( rc == 0 ) g_object_set(
                G_OBJECT(sond_treeview_get_cell_renderer_text( SOND_TREEVIEW(stvfm) )),
                "background-set", FALSE, NULL );
        else g_object_set( G_OBJECT(sond_treeview_get_cell_renderer_text(
                SOND_TREEVIEW(stvfm) )), "background-set", TRUE, NULL );
    }

    return;
}


static void
sond_treeviewfm_finalize( GObject* g_object )
{
    Clipboard* clipboard = NULL;

    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( SOND_TREEVIEWFM(g_object) );

    g_free( stvfm_priv->root );

    clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;
    if ( G_OBJECT(clipboard->tree_view) == g_object )
            g_ptr_array_remove_range( clipboard->arr_ref, 0, clipboard->arr_ref->len );

    G_OBJECT_CLASS (sond_treeviewfm_parent_class)->finalize (g_object);

    return;
}


static void
sond_treeviewfm_constructed( GObject* self )
{
    //Text-Spalte wird editiert
    g_signal_connect( sond_treeview_get_cell_renderer_text( SOND_TREEVIEW(self) ),
            "edited", G_CALLBACK(SOND_TREEVIEWFM_GET_CLASS(self)->text_edited), self); //Klick in textzelle = Datei umbenennen

    G_OBJECT_CLASS(sond_treeviewfm_parent_class)->constructed( self );

    return;
}


static void
sond_treeviewfm_class_init( SondTreeviewFMClass* klass )
{
    G_OBJECT_CLASS(klass)->finalize = sond_treeviewfm_finalize;
    G_OBJECT_CLASS(klass)->constructed = sond_treeviewfm_constructed;

    SOND_TREEVIEW_CLASS(klass)->render_text_cell = sond_treeviewfm_render_file_name;

    klass->dbase_begin = sond_treeviewfm_dbase_begin;
    klass->dbase_test = sond_treeviewfm_dbase_test;
    klass->dbase_update_path = sond_treeviewfm_dbase_update_path;
    klass->dbase_end = sond_treeviewfm_dbase_end;
    klass->text_edited = sond_treeviewfm_text_edited;
    klass->results_row_activated = sond_treeviewfm_results_row_activated;

    return;
}


GtkTreeIter*
sond_treeviewfm_insert_node( SondTreeviewFM* stvfm, GtkTreeIter* iter, gboolean child )
{
    GtkTreeIter new_iter;
    GtkTreeStore* treestore = GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) ));

    //Hauptknoten erzeugen
    if ( !child ) gtk_tree_store_insert_after( treestore, &new_iter, NULL, iter );
    //Unterknoten erzeugen
    else gtk_tree_store_insert_after( treestore, &new_iter, iter, NULL );

    GtkTreeIter* ret_iter = gtk_tree_iter_copy( &new_iter );

    return ret_iter; //muß nach Gebrauch gtk_tree_iter_freed werden!!!
}


gint
sond_treeviewfm_create_dir( SondTreeviewFM* stvfm, gboolean child, gchar** errmsg )
{
    gint rc = 0;
    GFileInfo* info = NULL;
    gchar* full_path = NULL;
    GFile* file = NULL;
    GFile* parent = NULL;
    GFileType type = G_FILE_TYPE_UNKNOWN;
    GtkTreeIter iter = { 0 };

    if ( !sond_treeview_get_cursor( SOND_TREEVIEW(stvfm), &iter ) ) return 0;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) ), &iter, 0, &info, -1 );

    type = g_file_info_get_file_type( info );
    g_object_unref( info );
    if ( !(type == G_FILE_TYPE_DIRECTORY) && child ) return 0;

    full_path = sond_treeviewfm_get_full_path( stvfm, &iter );
    file = g_file_new_for_path( full_path );
    g_free( full_path );

    if ( child ) parent = file;
    else
    {
        parent = g_file_get_parent( file );
        g_object_unref( file );
    } //nur noch parent muß unrefed werden - file wurde übernommen

    GFile* file_dir = g_file_get_child( parent, "Neues Verzeichnis" );
    g_object_unref( parent );

    rc = sond_treeviewfm_move_copy_create_delete( stvfm, NULL, &file_dir, 0, errmsg );
    //anderer Fall tritt nicht ein
    if ( rc == -1 ) ERROR_S

    //In Baum tun
    GtkTreeIter* iter_new = NULL;
    GFileInfo* info_new = NULL;
    GError* error = NULL;

    iter_new = sond_treeviewfm_insert_node( stvfm, &iter, child );

    info_new = g_file_query_info( file_dir, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
    g_object_unref( file_dir );
    if ( !info_new )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_query_info:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(stvfm) )), iter_new, 0, info_new, -1 );

    g_object_unref( info_new );

    sond_treeview_set_cursor( SOND_TREEVIEW(stvfm), iter_new );

    gtk_tree_iter_free( iter_new );

    return 0;
}


static void
sond_treeviewfm_punkt_einfuegen_activate( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gboolean child = FALSE;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    child = (gboolean) GPOINTER_TO_INT(g_object_get_data(
            G_OBJECT(item), "kind" ));

    rc = sond_treeviewfm_create_dir( stvfm, child, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
                "Verzeichnis kann nicht eingefügt werden\n\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
sond_treeviewfm_paste_activate( GtkMenuItem* item, gpointer data )
{
    gboolean kind = FALSE;
    gint rc = 0;
    gchar* errmsg = NULL;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    kind = (gboolean) GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item),
            "kind" ));

    if ( sond_treeview_test_cursor_descendant( SOND_TREEVIEW(stvfm), kind ) )
            display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
            "Unzulässiges Ziel: Abkömmling von zu verschiebendem Knoten", NULL );

    rc = sond_treeviewfm_paste_clipboard( stvfm , kind, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
                "Einfügen nicht möglich\n\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static gint
sond_treeviewfm_remove_node( SondTreeviewFM* stvfm, GtkTreeIter* iter_file, GFile* file,
        GFileInfo* info_file, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    GFileType type = G_FILE_TYPE_UNKNOWN;

    type = g_file_info_get_file_type( info_file );

    if ( type == G_FILE_TYPE_DIRECTORY )
    {
        rc = sond_treeviewfm_dir_foreach( stvfm, iter_file, file, FALSE,
                sond_treeviewfm_remove_node, data, errmsg );
        if ( rc == -1 ) ERROR_S
        else if ( rc ) return rc; //Verzeichnis nicht leer, weil
                                    //Datei angebunden war, übersprungen wurde oder Abbruch gewählt
    }

    rc = sond_treeviewfm_move_copy_create_delete( stvfm, NULL, &file, 4, errmsg );
    if ( rc == -1 ) ERROR_SOND( "sond_treeviewfm_move_copy_create_delete" )
    else if ( rc == 1 ) return 1;

    if ( iter_file ) gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
            iter_file );

    return 0;
}


static gint
sond_treeviewfm_foreach_loeschen( SondTreeview* stv, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gint rc = 0;
    GFileInfo* info = NULL;
    GFile* file = NULL;
    gchar* full_path = NULL;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ), iter, 0, &info, -1 );

    full_path = sond_treeviewfm_get_full_path( SOND_TREEVIEWFM(stv), iter );
    file = g_file_new_for_path( full_path );
    g_free( full_path );

    rc = sond_treeviewfm_remove_node( SOND_TREEVIEWFM(stv), iter, file, info, data, errmsg );
    g_object_unref( info );
    g_object_unref( file );
    if ( rc == -1 ) ERROR_SOND( "sond_treeviewfm_remove_node" )
    else if ( rc == 2 ) return 1;

    return 0;
}


static void
sond_treeviewfm_loeschen_activate( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    rc = sond_treeview_selection_foreach( SOND_TREEVIEW(stvfm),
            sond_treeviewfm_foreach_loeschen, NULL, &errmsg );
    if ( rc == -1 )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
                "Löschen nicht möglich\n\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static gint
sond_treeviewfm_open_path( GtkTreeView* tree_view, GtkTreePath* tree_path,
        gboolean open_with, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter;
    gchar* path = NULL;

    gtk_tree_model_get_iter( gtk_tree_view_get_model( tree_view ), &iter, tree_path );

    path = sond_treeviewfm_get_full_path( SOND_TREEVIEWFM(tree_view), &iter );
    rc = misc_datei_oeffnen( path, open_with, errmsg );
    g_free( path );
    if ( rc ) ERROR_S

    return 0;
}


static void
sond_treeviewfm_datei_oeffnen_activate( GtkMenuItem* item, gpointer data )
{
    GtkTreePath* path = NULL;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    gtk_tree_view_get_cursor( GTK_TREE_VIEW(stvfm), &path, NULL );

    g_signal_emit_by_name( stvfm, "row-activated", path, NULL );

    gtk_tree_path_free( path );

    return;
}


static void
sond_treeviewfm_datei_oeffnen_mit_activate( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    GtkTreePath* path = NULL;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    gtk_tree_view_get_cursor( GTK_TREE_VIEW(stvfm), &path, NULL );

    rc = sond_treeviewfm_open_path( GTK_TREE_VIEW(stvfm), path, TRUE, &errmsg );
    gtk_tree_path_free( path );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
                "Öffnen nicht möglich\n\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
sond_treeviewfm_show_hits( SondTreeviewFM* stvfm, GPtrArray* arr_hits )
{
    GtkWidget* window = NULL;
    GtkWidget* listbox = NULL;
    SondTreeviewFMClass* klass = SOND_TREEVIEWFM_GET_CLASS(stvfm);

    //Fenster erzeugen
    window = result_listbox_new( GTK_WINDOW(gtk_widget_get_toplevel( GTK_WIDGET(stvfm) )), "Suchergebnis" );

    listbox = (GtkWidget*) g_object_get_data( G_OBJECT(window), "listbox" );

    g_signal_connect( listbox, "row-activated", G_CALLBACK(klass->results_row_activated),
            (gpointer) stvfm );

    for ( gint i = 0; i < arr_hits->len; i++ )
    {
        gchar* path = NULL;
        GtkWidget* label = NULL;

        path = g_ptr_array_index( arr_hits, i );

        label = gtk_label_new( (const gchar*) path );

        gtk_list_box_insert( GTK_LIST_BOX(listbox), label, -1 );
    }

    gtk_widget_show_all( window );

    return;
}


typedef struct _SearchFS
{
    gchar* needle;
    gboolean exact_match;
    gboolean case_sens;
    GPtrArray* arr_hits;
    InfoWindow* info_window;
    volatile gint* atom_ready;
    volatile gint* atom_cancelled;
} SearchFS;


static gint
sond_treeviewfm_search_needle( SondTreeviewFM* stvfm, GtkTreeIter* iter,
        GFile* file, GFileInfo* info, gpointer data, gchar** errmsg )
{
    gchar* path = NULL;
    gchar* basename = NULL;
    gboolean found = FALSE;

    SearchFS* search_fs = (SearchFS*) data;

    basename = g_file_get_basename( file );

    if ( !search_fs->case_sens )
    {
        gchar* basename_tmp = NULL;

        basename_tmp = g_ascii_strdown( basename, -1 );
        g_free( basename );
        basename = basename_tmp;
    }

    if ( search_fs->exact_match == TRUE )
    {
        if ( !g_strcmp0( basename, search_fs->needle ) ) found = TRUE;
    }
    else if ( strstr( basename, search_fs->needle ) ) found = TRUE;
    g_free( basename );

    if ( found )
    {
        path = g_file_get_path( file );
        g_ptr_array_add( search_fs->arr_hits, path );
    }

    if ( g_atomic_int_get( search_fs->atom_cancelled ) ) g_atomic_int_set( search_fs->atom_ready, 1 );

    return 0;
}


typedef struct _DataThread
{
    SearchFS* search_fs;
    SondTreeview* stv;
    GtkTreeIter* iter;
    GFile* file;
    gchar** errmsg;
} DataThread;


static gpointer
sond_treeviewfm_thread_search( gpointer data )
{
    DataThread* data_thread = (DataThread*) data;
    gchar** errmsg = data_thread->errmsg;

    if ( data_thread->iter ) //nur, wenn nicht root-Verzeichnis
    {
        gint rc = 0;

        rc = sond_treeviewfm_search_needle( SOND_TREEVIEWFM(data_thread->stv), data_thread->iter, data_thread->file,
                NULL, data_thread->search_fs, data_thread->errmsg );
        if ( rc )
        {
            g_atomic_int_set( data_thread->search_fs->atom_ready, 1 );
            ERROR_S_VAL(GINT_TO_POINTER(-1))
        }
    }

    if ( g_file_query_file_type( data_thread->file, G_FILE_QUERY_INFO_NONE, NULL )
            == G_FILE_TYPE_DIRECTORY )
    {
        gint rc = 0;

        rc = sond_treeviewfm_dir_foreach( SOND_TREEVIEWFM(data_thread->stv), data_thread->iter, data_thread->file, TRUE,
                sond_treeviewfm_search_needle, data_thread->search_fs, data_thread->errmsg );
        if ( rc == -1 )
        {
            g_atomic_int_set( data_thread->search_fs->atom_ready, 1 );
            ERROR_S_VAL(GINT_TO_POINTER(-1))
        }
    }

    g_atomic_int_set( data_thread->search_fs->atom_ready, 1 );

    return NULL;
}


static gint
sond_treeviewfm_search( SondTreeview* stv, GtkTreeIter* iter, gpointer data,
        gchar** errmsg )
{
    GFile* file = NULL;
    gchar* path_root = NULL;
    GThread* thread_search = NULL;
    gpointer res_thread = NULL;

    SearchFS* search_fs = (SearchFS*) data;

    path_root = sond_treeviewfm_get_full_path( SOND_TREEVIEWFM(stv), iter );
    info_window_set_message( search_fs->info_window, path_root );
    file = g_file_new_for_path( path_root );
    g_free( path_root );

    DataThread data_thread = { search_fs, stv, iter, file, errmsg };
    thread_search = g_thread_new( NULL, sond_treeviewfm_thread_search, &data_thread );

    while( !g_atomic_int_get( search_fs->atom_ready ) )
           { if ( search_fs->info_window->cancel )
            g_atomic_int_set( search_fs->atom_cancelled, 1 );}

    res_thread = g_thread_join( thread_search );
    g_object_unref( file );
    if ( GPOINTER_TO_INT(res_thread) == -1 ) ERROR_S

    return 0;
}


static void
sond_treeviewfm_search_activate( GtkMenuItem* item, gpointer data )
{
    gboolean only_sel = FALSE;
    gint rc = 0;
    gchar* errmsg = NULL;
    gchar* search_text = NULL;
    SearchFS search_fs = { 0 };
    gint ready = 0;
    gint cancelled = 0;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    only_sel = (gboolean) GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item), "sel" ));

    if ( only_sel && !gtk_tree_selection_count_selected_rows(
            gtk_tree_view_get_selection( GTK_TREE_VIEW(stvfm) ) ) )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
                "Keine Punkte ausgewählt", NULL );
        return;
    }

    rc = abfrage_frage( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
            "Dateisuche", "Bitte Suchtext eingeben", &search_text );
    if ( rc != GTK_RESPONSE_YES ) return;
    else if ( !g_strcmp0( search_text, "" ) )
    {
        g_free( search_text );

        return;
    }

    search_fs.arr_hits = g_ptr_array_new_with_free_func( g_free );
    search_fs.exact_match = FALSE;
    search_fs.case_sens = FALSE;
    search_fs.atom_ready = &ready;
    search_fs.atom_cancelled = &cancelled;

    if ( !search_fs.case_sens ) search_fs.needle = g_utf8_strdown( search_text, -1 );
    else search_fs.needle = g_strdup( search_text );

    g_free( search_text );

    search_fs.info_window = info_window_open( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
            "Projektverzeichnis durchduchen" );

    if ( only_sel ) rc = sond_treeview_selection_foreach( SOND_TREEVIEW(stvfm),
            sond_treeviewfm_search, &search_fs, &errmsg );
    else rc = sond_treeviewfm_search(SOND_TREEVIEW(stvfm), NULL, &search_fs, &errmsg );

    info_window_kill( search_fs.info_window );

    g_free( search_fs.needle );

    if ( rc == -1 )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
                "Fehler bei Dateisuche\n\n", errmsg, NULL );
        g_free( errmsg );
        g_ptr_array_unref( search_fs.arr_hits );

        return;
    }

    if ( search_fs.arr_hits->len == 0 )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
                "Keine Datei gefunden", NULL );
        g_ptr_array_unref( search_fs.arr_hits );

        return;
    }

    sond_treeviewfm_show_hits( stvfm, search_fs.arr_hits );

    g_ptr_array_unref( search_fs.arr_hits );

    return;
}


static void
sond_treeviewfm_init_contextmenu( SondTreeviewFM* stvfm )
{
    GtkWidget* contextmenu = NULL;

    contextmenu = sond_treeview_get_contextmenu( SOND_TREEVIEW(stvfm) );

    //Trennblatt
    GtkWidget* item_separator_0 = gtk_separator_menu_item_new();
    gtk_menu_shell_prepend( GTK_MENU_SHELL(contextmenu), item_separator_0 );

    //Punkt einfügen
    GtkWidget* item_punkt_einfuegen = gtk_menu_item_new_with_label( "Punkt einfügen" );

    GtkWidget* menu_punkt_einfuegen = gtk_menu_new();

    GtkWidget* item_punkt_einfuegen_ge = gtk_menu_item_new_with_label(
            "Gleiche Ebene" );
    g_object_set_data( G_OBJECT(contextmenu), "item-punkt-einfuegen-ge",
            item_punkt_einfuegen_ge );
    g_signal_connect( G_OBJECT(item_punkt_einfuegen_ge), "activate",
            G_CALLBACK(sond_treeviewfm_punkt_einfuegen_activate), (gpointer) stvfm );

    GtkWidget* item_punkt_einfuegen_up = gtk_menu_item_new_with_label(
            "Unterebene" );
    g_object_set_data( G_OBJECT(contextmenu), "item-punkt-einfuegen-up",
            item_punkt_einfuegen_up );
    g_object_set_data( G_OBJECT(item_punkt_einfuegen_up), "kind", GINT_TO_POINTER(1) );
    g_signal_connect( G_OBJECT(item_punkt_einfuegen_up), "activate",
            G_CALLBACK(sond_treeviewfm_punkt_einfuegen_activate), (gpointer) stvfm );

    gtk_menu_shell_append( GTK_MENU_SHELL(menu_punkt_einfuegen),
            item_punkt_einfuegen_ge );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_punkt_einfuegen),
            item_punkt_einfuegen_up );

    gtk_menu_item_set_submenu( GTK_MENU_ITEM(item_punkt_einfuegen),
            menu_punkt_einfuegen );

    gtk_menu_shell_prepend( GTK_MENU_SHELL(contextmenu), item_punkt_einfuegen );

    //Einfügen
    GtkWidget* item_paste = gtk_menu_item_new_with_label("Einfügen");

    GtkWidget* menu_paste = gtk_menu_new();

    GtkWidget* item_paste_ge = gtk_menu_item_new_with_label( "Gleiche Ebene");
    g_object_set_data( G_OBJECT(contextmenu), "item-paste-ge", item_paste_ge );
    g_signal_connect( G_OBJECT(item_paste_ge), "activate",
            G_CALLBACK(sond_treeviewfm_paste_activate), (gpointer) stvfm );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_paste), item_paste_ge );

    GtkWidget* item_paste_up = gtk_menu_item_new_with_label( "Unterebene");
    g_object_set_data( G_OBJECT(item_paste_up), "kind",
            GINT_TO_POINTER(1) );
    g_object_set_data( G_OBJECT(contextmenu), "item-paste-up", item_paste_up );
    g_signal_connect( G_OBJECT(item_paste_up), "activate",
            G_CALLBACK(sond_treeviewfm_paste_activate), (gpointer) stvfm );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_paste), item_paste_up );

    gtk_menu_item_set_submenu( GTK_MENU_ITEM(item_paste), menu_paste );

    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_paste );

    //Punkt(e) löschen
    GtkWidget* item_loeschen = gtk_menu_item_new_with_label("Löschen");
    g_object_set_data( G_OBJECT(contextmenu), "item-loeschen", item_loeschen );
    g_signal_connect( G_OBJECT(item_loeschen), "activate",
            G_CALLBACK(sond_treeviewfm_loeschen_activate), (gpointer) stvfm );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_loeschen );

    //Trennblatt
    GtkWidget* item_separator_1 = gtk_separator_menu_item_new();
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_separator_1 );

    //Datei Öffnen
    GtkWidget* item_datei_oeffnen = gtk_menu_item_new_with_label( "Öffnen" );
    g_object_set_data( G_OBJECT(contextmenu), "item-datei-oeffnen", item_datei_oeffnen );
    g_signal_connect( item_datei_oeffnen, "activate",
                G_CALLBACK(sond_treeviewfm_datei_oeffnen_activate), (gpointer) stvfm );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_datei_oeffnen );

    //Datei Öffnen mit
    GtkWidget* item_datei_oeffnen_mit = gtk_menu_item_new_with_label( "Öffnen mit" );
    g_object_set_data( G_OBJECT(contextmenu), "item-datei-oeffnen-mit",
            item_datei_oeffnen_mit );
    g_object_set_data( G_OBJECT(item_datei_oeffnen_mit), "open-with",
            GINT_TO_POINTER(1) );
    g_signal_connect( item_datei_oeffnen_mit, "activate",
                G_CALLBACK(sond_treeviewfm_datei_oeffnen_mit_activate), (gpointer) stvfm );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_datei_oeffnen_mit );

    //In Projektverzeichnis suchen
    GtkWidget* item_search = gtk_menu_item_new_with_label("Dateisuche");

    GtkWidget* menu_search = gtk_menu_new();

    GtkWidget* item_search_all = gtk_menu_item_new_with_label( "Gesamtes Verzeichnis");
    g_object_set_data( G_OBJECT(contextmenu), "item-search-all", item_search_all );
    g_signal_connect( G_OBJECT(item_search_all), "activate",
            G_CALLBACK(sond_treeviewfm_search_activate), (gpointer) stvfm );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_search), item_search_all );

    GtkWidget* item_search_sel = gtk_menu_item_new_with_label( "Nur markierte Punkte");
    g_object_set_data( G_OBJECT(item_search_sel), "sel",
            GINT_TO_POINTER(1) );
    g_object_set_data( G_OBJECT(contextmenu), "item-search-sel", item_search_sel );
    g_signal_connect( G_OBJECT(item_search_sel), "activate",
            G_CALLBACK(sond_treeviewfm_search_activate), (gpointer) stvfm );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_search), item_search_sel );

    gtk_menu_item_set_submenu( GTK_MENU_ITEM(item_search), menu_search );

    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_search );

    gtk_widget_show_all( contextmenu );

    return;
}


static void
sond_treeviewfm_row_activated( GtkTreeView* tree_view, GtkTreePath* tree_path,
        gpointer column, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    rc = sond_treeviewfm_open_path( tree_view, tree_path, FALSE, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(tree_view) ),
                "Öffnen nicht möglich\n\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
sond_treeviewfm_render_eingang( GtkTreeViewColumn* column, GtkCellRenderer* renderer,
        GtkTreeModel* model, GtkTreeIter* iter, gpointer data )
{
    gint rc = 0;
    gint eingang_id = 0;
    gchar* rel_path = NULL;
    gchar* errmsg = NULL;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;
    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );
    if ( !rel_path ) return;

//    rc = eingang_for_rel_path( stvfm_priv->zond_dbase, rel_path, &eingang_id, &eingang, NULL, &errmsg );
    g_free( rel_path );
    if ( rc == -1 )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
                "Warnung -\n\nBei Aufruf eingang_for_rel_path:\n",
                errmsg, NULL );
        g_free( errmsg );
    }
    else if ( rc == 1 )
    {
        if ( eingang_id ) g_object_set( G_OBJECT(renderer), "text",
            "Datum", NULL );
        else g_object_set( G_OBJECT(renderer), "text", "----", NULL );
    }
    else g_object_set( G_OBJECT(renderer), "text", "", NULL );

    return;
}


static void
sond_treeviewfm_render_file_modify( GtkTreeViewColumn* column, GtkCellRenderer* renderer,
        GtkTreeModel* model, GtkTreeIter* iter, gpointer data )
{
    GFileInfo* info = NULL;
    GDateTime* datetime = NULL;
    gchar* text = NULL;

    gtk_tree_model_get( model, iter, 0, &info, -1 );

    datetime = g_file_info_get_modification_date_time( info );
    g_object_unref( info );

    text = g_date_time_format( datetime, "%d.%m.%Y %T" );
    g_date_time_unref( datetime );
    g_object_set( G_OBJECT(renderer), "text", text, NULL );
    g_free( text );

    return;
}


static void
sond_treeviewfm_render_file_size( GtkTreeViewColumn* column, GtkCellRenderer* renderer,
        GtkTreeModel* model, GtkTreeIter* iter, gpointer data )
{
    GFileInfo* info = NULL;
    goffset size = 0;
    gchar* text = NULL;

    gtk_tree_model_get( model, iter, 0, &info, -1 );

    size = g_file_info_get_size( info );
    g_object_unref( info );

    text = g_strdup_printf( "%lld", size );

    g_object_set( G_OBJECT(renderer), "text", text, NULL );
    g_free( text );

    return;
}


static void
sond_treeviewfm_render_file_icon( GtkTreeViewColumn* column, GtkCellRenderer* renderer,
        GtkTreeModel* model, GtkTreeIter* iter, gpointer data )
{
    GFileInfo* info = NULL;

    gtk_tree_model_get( model, iter, 0, &info, -1 );

    g_object_set( G_OBJECT(renderer), "gicon", g_file_info_get_icon( info ), NULL );

    g_object_unref( info );

    return;
}


static void
sond_treeviewfm_init( SondTreeviewFM* stvfm )
{
    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    gtk_tree_view_column_set_cell_data_func( gtk_tree_view_get_column( GTK_TREE_VIEW(stvfm), 0 ),
            sond_treeview_get_cell_renderer_icon( SOND_TREEVIEW(stvfm) ),
            sond_treeviewfm_render_file_icon, NULL, NULL );

    //Größe
    GtkCellRenderer* renderer_size = gtk_cell_renderer_text_new( );

    GtkTreeViewColumn* fs_tree_column_size = gtk_tree_view_column_new( );
    gtk_tree_view_column_set_resizable( fs_tree_column_size, FALSE );
    gtk_tree_view_column_set_sizing(fs_tree_column_size, GTK_TREE_VIEW_COLUMN_FIXED );
    gtk_tree_view_column_pack_start( fs_tree_column_size, renderer_size, FALSE );
    gtk_tree_view_column_set_cell_data_func( fs_tree_column_size, renderer_size,
            sond_treeviewfm_render_file_size, NULL, NULL );

    //Änderungsdatum
    GtkCellRenderer* renderer_modify = gtk_cell_renderer_text_new( );

    GtkTreeViewColumn* fs_tree_column_modify = gtk_tree_view_column_new( );
    gtk_tree_view_column_set_resizable( fs_tree_column_modify, FALSE );
    gtk_tree_view_column_set_sizing(fs_tree_column_modify, GTK_TREE_VIEW_COLUMN_FIXED );
    gtk_tree_view_column_pack_start( fs_tree_column_modify, renderer_modify, FALSE );
    gtk_tree_view_column_set_cell_data_func( fs_tree_column_modify, renderer_modify,
            sond_treeviewfm_render_file_modify, NULL, NULL );

    //Eingang
    GtkCellRenderer* renderer_eingang = gtk_cell_renderer_text_new( );

    stvfm_priv->column_eingang = gtk_tree_view_column_new( );
    gtk_tree_view_column_set_resizable( stvfm_priv->column_eingang, FALSE );
    gtk_tree_view_column_set_sizing(stvfm_priv->column_eingang, GTK_TREE_VIEW_COLUMN_FIXED );
    gtk_tree_view_column_pack_start( stvfm_priv->column_eingang, renderer_eingang, FALSE );
    gtk_tree_view_column_set_cell_data_func( stvfm_priv->column_eingang, renderer_eingang,
            sond_treeviewfm_render_eingang, stvfm, NULL );
//    gtk_tree_view_column_set_visible( stvfm_priv->column_eingang, FALSE );

    gtk_tree_view_append_column( GTK_TREE_VIEW(stvfm), stvfm_priv->column_eingang );
    gtk_tree_view_append_column( GTK_TREE_VIEW(stvfm), fs_tree_column_size );
    gtk_tree_view_append_column( GTK_TREE_VIEW(stvfm), fs_tree_column_modify );

    gtk_tree_view_column_set_title( gtk_tree_view_get_column( GTK_TREE_VIEW(stvfm), 0 ), "Datei" );
    gtk_tree_view_column_set_title( fs_tree_column_size, "Größe" );
    gtk_tree_view_column_set_title( fs_tree_column_modify, "Änderungsdatum" );
    gtk_tree_view_column_set_title( stvfm_priv->column_eingang, "Eingang" );

    GtkTreeStore* tree_store = gtk_tree_store_new( 1, G_TYPE_OBJECT );
    gtk_tree_view_set_model( GTK_TREE_VIEW(stvfm), GTK_TREE_MODEL(tree_store) );
    g_object_unref( tree_store );

    //Zeile expandiert
    g_signal_connect( stvfm, "row-expanded", G_CALLBACK(sond_treeviewfm_row_expanded), NULL );
    //Zeile kollabiert
    g_signal_connect( stvfm, "row-collapsed", G_CALLBACK(sond_treeviewfm_row_collapsed), NULL );
    // Doppelklick = angebundene Datei anzeigen
    g_signal_connect( stvfm, "row-activated", G_CALLBACK(sond_treeviewfm_row_activated), NULL );

    sond_treeviewfm_init_contextmenu( stvfm );

    return;
}


gint
sond_treeviewfm_set_root( SondTreeviewFM* stvfm, const gchar* root, gchar** errmsg )
{
    gint rc = 0;

    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    g_free( stvfm_priv->root );

    if ( !root )
    {
        stvfm_priv->root = NULL;
        gtk_tree_store_clear( GTK_TREE_STORE(gtk_tree_view_get_model(
                GTK_TREE_VIEW(stvfm) )) );

        return 0;
    }

    stvfm_priv->root = g_strdup( root );

    rc = sond_treeviewfm_load_dir( stvfm, NULL, errmsg );
    if ( rc ) ERROR_S

    return 0;
}


const gchar*
sond_treeviewfm_get_root( SondTreeviewFM* stvfm )
{
    if ( !stvfm ) return NULL;

    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    return stvfm_priv->root;
}


void
sond_treeviewfm_set_dbase( SondTreeviewFM* stvfm, ZondDBase* zond_dbase )
{
    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    stvfm_priv->zond_dbase = zond_dbase;

    return;
}


ZondDBase*
sond_treeviewfm_get_dbase( SondTreeviewFM* stvfm )
{
    if ( !stvfm ) return NULL;

    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    return stvfm_priv->zond_dbase;
}


void
sond_treeviewfm_column_eingang_set_visible( SondTreeviewFM* stvfm, gboolean vis )
{
    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    gtk_tree_view_column_set_visible( stvfm_priv->column_eingang, vis );

    return;
}


gchar*
sond_treeviewfm_get_full_path( SondTreeviewFM* stvfm, GtkTreeIter* iter )
{
    gchar* full_path = NULL;

    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    full_path = add_string( g_strconcat( stvfm_priv->root, "/", NULL ),
            sond_treeviewfm_get_rel_path( stvfm, iter ) );

    return full_path;
}


gchar*
sond_treeviewfm_get_rel_path( SondTreeviewFM* stvfm, GtkTreeIter* iter )
{
    gchar* rel_path = NULL;
    GtkTreeIter iter_parent = { 0 };
    GtkTreeIter* iter_seg = NULL;

    if ( !iter ) return NULL;

    GtkTreeModel* model = gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) );

    iter_seg = gtk_tree_iter_copy( iter );

    rel_path = g_strdup( sond_treeviewfm_get_name( stvfm, iter_seg ) );

    while ( gtk_tree_model_iter_parent( model, &iter_parent, iter_seg ) )
    {
        gchar* path_segment = NULL;

        gtk_tree_iter_free( iter_seg );
        iter_seg = gtk_tree_iter_copy( &iter_parent );

        path_segment = g_strdup( sond_treeviewfm_get_name( stvfm, iter_seg ) );

        rel_path = add_string( g_strdup( "/" ), rel_path );
        rel_path = add_string( path_segment, rel_path );
    }

    gtk_tree_iter_free( iter_seg );

    return rel_path;
}


typedef struct _S_FM_Paste_Selection{
    GFile* file_parent;
    GFile* file_dest;
    GtkTreeIter* iter_cursor;
    gboolean kind;
    gboolean expanded;
    gboolean inserted;
} SFMPasteSelection;


static gint
sond_treeviewfm_move_or_copy_node( SondTreeviewFM* stvfm, GtkTreeIter* iter, GFile* file,
        GFileInfo* info_file, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gchar* basename_file = NULL;

    SFMPasteSelection* s_fm_paste_selection = (SFMPasteSelection*) data;
    Clipboard* clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;

    g_clear_object( &s_fm_paste_selection->file_dest );

    basename_file = g_file_get_basename( file );
    s_fm_paste_selection->file_dest = g_file_get_child( s_fm_paste_selection->file_parent, basename_file );
    g_free( basename_file );

    GFileType type = g_file_query_file_type( file, G_FILE_QUERY_INFO_NONE, NULL );
    if ( type == G_FILE_TYPE_DIRECTORY && !clipboard->ausschneiden )
    {
        SFMPasteSelection s_fm_paste_selection_loop = { 0 };

        rc = sond_treeviewfm_move_copy_create_delete( stvfm, NULL, &s_fm_paste_selection->file_dest, 0, errmsg );
        if ( rc )
        {
            g_clear_object( &s_fm_paste_selection->file_dest );
            if ( rc == -1 ) ERROR_S
            else return rc;
        }

        s_fm_paste_selection_loop.file_parent = s_fm_paste_selection->file_dest;

        rc = sond_treeviewfm_dir_foreach( stvfm, NULL, file, FALSE,
                sond_treeviewfm_move_or_copy_node, &s_fm_paste_selection_loop, errmsg );

        if ( rc )
        {
            g_clear_object( &s_fm_paste_selection->file_dest );
            if ( rc == -1 ) ERROR_S
            else return rc;
        }
    }
    else
    {
        gint mode = 0;

        if ( clipboard->ausschneiden ) mode = 2;
        else mode = 1;

        rc = sond_treeviewfm_move_copy_create_delete( stvfm, file,
                &s_fm_paste_selection->file_dest, mode,
                errmsg );
        if ( rc )
        {
            g_clear_object( &s_fm_paste_selection->file_dest );
            if ( rc == -1 ) ERROR_S
            else return rc;
        }
    }

    if ( iter && clipboard->ausschneiden )
            gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(clipboard->tree_view) )), iter );

    return 0;
}


static gint
sond_treeviewfm_paste_clipboard_foreach( SondTreeview* stv, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gint rc = 0;
    GFile* file_source = NULL;
    gchar* path_source = NULL;
    gboolean same_dir = FALSE;
    gboolean dir_with_children = FALSE;
    GFileType file_type = G_FILE_TYPE_UNKNOWN;

    SFMPasteSelection* s_fm_paste_selection = (SFMPasteSelection*) data;
    Clipboard* clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;

    //Namen der Datei holen
    path_source = sond_treeviewfm_get_full_path( SOND_TREEVIEWFM(stv), iter );

    //source-file
    file_source = g_file_new_for_path( path_source );
    g_free( path_source );

    //prüfen, ob innerhalb des gleichen Verzeichnisses verschoben/kopiert werden
    //soll - dann: same_dir = TRUE
    GFile* file_parent_source = g_file_get_parent( file_source );
    same_dir = g_file_equal( s_fm_paste_selection->file_parent, file_parent_source );
    g_object_unref( file_parent_source );

    //verschieben im gleichen Verzeichnis ist Quatsch
    if ( same_dir && clipboard->ausschneiden )
    {
        g_object_unref( file_source );
        return 0;
    }

    file_type = g_file_query_file_type( file_source, G_FILE_QUERY_INFO_NONE, NULL );

    //Falls Verzeichnis: Prüfen, ob Datei drinne, dann dir_with_children = TRUE
    if (  file_type == G_FILE_TYPE_DIRECTORY &&
            (!s_fm_paste_selection->kind || s_fm_paste_selection->expanded) )
    {
        GError* error = NULL;
        GFile* file_out = NULL;

        GFileEnumerator* enumer = g_file_enumerate_children( file_source,
                "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
        if ( !enumer )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( file_source );

            return -1;
        }
        if ( !g_file_enumerator_iterate( enumer, NULL, &file_out, NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( enumer );
            g_object_unref( file_source );

            return -1;
        }

        if ( file_out ) dir_with_children = TRUE;

        g_object_unref( enumer );
    }

    //Kopieren/verschieben
    rc = sond_treeviewfm_move_or_copy_node( SOND_TREEVIEWFM(stv), iter, file_source,
            NULL, s_fm_paste_selection, errmsg );
    g_object_unref( file_source );

    if ( rc == -1 ) ERROR_S
    else if ( rc == 1 ) return 0; //Ünerspringen gewählt - einfach weiter
    else if ( rc == 2 ) return 1; //nur bei selection_move_file/_dir möglich

    s_fm_paste_selection->inserted = TRUE;

    //Knoten müssen nur eingefügt werden, wenn Row expanded ist; sonst passiert das im callback beim Öffnen
    if ( (!s_fm_paste_selection->kind) || s_fm_paste_selection->expanded )
    {
        //Ziel-FS-tree eintragen
        GtkTreeIter* iter_new = NULL;
        GError* error = NULL;

        iter_new = sond_treeviewfm_insert_node( SOND_TREEVIEWFM(stv), s_fm_paste_selection->iter_cursor,
                s_fm_paste_selection->kind );

        *(s_fm_paste_selection->iter_cursor) = *iter_new;
        gtk_tree_iter_free( iter_new );
        s_fm_paste_selection->kind = FALSE;

        //Falls Verzeichnis mit Datei innendrin, Knoten in tree einfügen
        if ( dir_with_children )
        {
            GtkTreeIter iter_tmp;
            gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model(
                GTK_TREE_VIEW(stv) )), &iter_tmp, s_fm_paste_selection->iter_cursor, -1 );
        }

        //alte Daten holen
        GFileInfo* file_dest_info = g_file_query_info( s_fm_paste_selection->file_dest, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
        if ( !file_dest_info && error )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_query_info:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( s_fm_paste_selection->file_dest );

            return -1;
        }
        else if ( file_dest_info )
        {
            //in neu erzeugten node einsetzen
            gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stv) )),
                    s_fm_paste_selection->iter_cursor, 0, file_dest_info, -1 );

            g_object_unref( file_dest_info );
        }
    }

    g_clear_object( &s_fm_paste_selection->file_dest );

    return 0;
}


gint
sond_treeviewfm_paste_clipboard( SondTreeviewFM* stvfm, gboolean kind, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter_cursor = { 0 };
    gchar* path = NULL;
    GFile* file_cursor = NULL;
    GFile* file_parent = NULL;
    GFileType file_type = G_FILE_TYPE_UNKNOWN;
    gboolean expanded = FALSE;
    Clipboard* clipboard = NULL;

    clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;

    if ( clipboard->arr_ref->len == 0 ) return 0;

    //Datei unter cursor holen
    if ( !sond_treeview_get_cursor( SOND_TREEVIEW(stvfm), &iter_cursor ) ) return 0;

    path = sond_treeviewfm_get_full_path( stvfm, &iter_cursor );
    if ( !path ) ERROR_SOND( "sond_treeviewfm_get_full_path:\nKein Pfadname" )

    file_cursor = g_file_new_for_path( path );
    g_free( path );
    file_type = g_file_query_file_type( file_cursor, G_FILE_QUERY_INFO_NONE, NULL );

    //if kind && datei != dir: Fehler
    if ( (file_type != G_FILE_TYPE_DIRECTORY) && kind )
    {
        g_object_unref( file_cursor );
        if ( errmsg ) *errmsg = g_strdup( "Einfügen als Unterpunkt von Dateien "
                "nicht zulässig" );

        return -1;
    }
    else if ( kind ) //und Verzeichnis, ist aber ja schon die 1. Var.
    {
        //dann ist Datei unter cursor parent
        file_parent = file_cursor;

        //prüfen, ob geöffnet ist
        GtkTreePath* path = NULL;

        path = gtk_tree_model_get_path( gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) ),
                &iter_cursor );
        expanded = gtk_tree_view_row_expanded( GTK_TREE_VIEW(stvfm), path );
        gtk_tree_path_free( path );
    }
    else //unterhalb angefügt
    {
        //dann ist parent parent!
        file_parent = g_file_get_parent( file_cursor );
        g_object_unref( file_cursor );
    }

    SFMPasteSelection s_fm_paste_selection = { file_parent, NULL,
            &iter_cursor, kind, expanded, FALSE };

    rc = sond_treeview_clipboard_foreach( sond_treeviewfm_paste_clipboard_foreach,
            (gpointer) &s_fm_paste_selection, errmsg );
    g_object_unref( file_parent );
    if ( rc == -1 )
    {
        gtk_tree_iter_free( s_fm_paste_selection.iter_cursor );
        ERROR_S
    }

    //Wenn in nicht ausgeklapptes Verzeichnis etwas eingefügt wurde und
    //Verzeichnis leer ist:
    //Dummy einfügen
    if ( s_fm_paste_selection.inserted && kind && !expanded &&
            !gtk_tree_model_iter_has_child( gtk_tree_view_get_model(
            GTK_TREE_VIEW(stvfm) ), s_fm_paste_selection.iter_cursor ) )
    {
        GtkTreeIter iter_tmp;
        gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model(
                GTK_TREE_VIEW(stvfm) )), &iter_tmp, s_fm_paste_selection.iter_cursor, -1 );
    }

    //Alte Auswahl löschen - muß vor baum_setzen_cursor geschehen,
    //da in change_row-callback ref abgefragt wird
    if ( clipboard->ausschneiden && clipboard->arr_ref->len > 0 )
            g_ptr_array_remove_range( clipboard->arr_ref,
            0, clipboard->arr_ref->len );
    gtk_widget_queue_draw( GTK_WIDGET(clipboard->tree_view) );

    //Wenn neuer Knoten sichtbar: Cursor setzen
    if ( !kind || expanded ) sond_treeview_set_cursor( SOND_TREEVIEW(stvfm),
            s_fm_paste_selection.iter_cursor );

    return 0;
}

