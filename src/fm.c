#include "zond/99conv/db_write.h"

#include "zond/global_types.h"
#include "zond/error.h"

#include "misc.h"
#include "treeview.h"

#include <gtk/gtk.h>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32


static gchar*
fm_get_basename( GtkTreeView* tree_view, GtkTreeIter* iter )
{
    gchar* basename = NULL;

    gtk_tree_model_get( gtk_tree_view_get_model( tree_view ), iter, 1,
            &basename, -1 );

    return basename;
}


static gchar*
fm_get_rel_path( GtkTreeView* tree_view, GtkTreeIter* iter )
{
    GtkTreeIter iter_parent;
    gchar* rel_path = NULL;

    if ( iter )
    {
        GtkTreeIter* iter_child = gtk_tree_iter_copy( iter );
        gboolean parent = FALSE;

        do
        {
            parent = FALSE;
            gchar* path_segment = fm_get_basename( tree_view, iter_child );

            rel_path = add_string( path_segment, rel_path );

            if ( gtk_tree_model_iter_parent( gtk_tree_view_get_model( tree_view ), &iter_parent, iter_child ) )
            {
                parent = TRUE;
                gtk_tree_iter_free( iter_child );
                iter_child = gtk_tree_iter_copy( &iter_parent );

                rel_path = add_string( g_strdup( "/" ), rel_path );
            }
        }
        while ( parent );

        gtk_tree_iter_free( iter_child );
    }

    return rel_path;
}


static gchar*
fm_get_full_path( GtkTreeView* tree_view, GtkTreeIter* iter )
{
    gchar* path_dir = NULL;

    gchar* rel_path = fm_get_rel_path( tree_view, iter );
    path_dir = g_strconcat( g_object_get_data( G_OBJECT(tree_view), "root" ), "/", NULL );
    path_dir = add_string( path_dir, rel_path );

    return path_dir;
}


static gint
fm_move_file( GtkTreeView* tree_view, const gchar* path_old, const gchar* path_new, gchar** errmsg )
{
    gint ret = 0;
    gboolean success = FALSE;

    //file umbenennen
    GFile* old_file = g_file_new_for_path( path_old );
    GFile* new_file = g_file_new_for_path( path_new );

    do
    {
        GError* error = NULL;

        success = g_file_move( old_file, new_file, G_FILE_COPY_NONE,
                NULL, NULL, NULL, &error );
        if ( success )
        {
            ret = 0;
            break;
        }
        else if ( g_error_matches( error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED ) )
        {
            gint res = 0;

            g_clear_error( &error );

            res = abfrage_frage( gtk_widget_get_toplevel( GTK_WIDGET(tree_view) ),
                        "Umbenennen nicht möglich - Datei möglicherweise "
                        "geöffnet", "Erneut versuchen?", NULL );
            if ( res == GTK_RESPONSE_YES) continue;
            else
            {
                ret = 1;
                break;
            }
        }
        else
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_move:\n",
                    error->message, NULL );
            g_error_free( error );

            ret = -1;

            break;
        }
    } while ( 1 );

    g_object_unref( old_file );
    g_object_unref( new_file );

    return ret;
}


static gint
fm_edit_row( GtkTreeView* tree_view, GtkTreeIter* iter, const gchar* old_path,
        const gchar* new_path, Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    if ( zond )
    {
        rc = db_begin_both( zond, errmsg );
        if ( rc )
        {
            gchar* errmsg_ii = NULL;

            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf db_begin_both:\n", *errmsg, NULL );

            rc = db_rollback_both( zond, &errmsg_ii );
            if ( rc )
            {
                errmsg_ii = add_string( errmsg_ii, "Bei Aufruf db_rollback_both:\n" );
                if ( errmsg ) *errmsg = g_strconcat( *errmsg, errmsg_ii, NULL );
                g_free( errmsg_ii );
            }

            return -1;
        }

        gchar* old_rel_path = g_strdup( old_path + strlen( zond->project_dir ) + 1 );
        gchar* new_rel_path = g_strdup( new_path + strlen( zond->project_dir ) + 1 );

        rc = db_update_path( zond, old_rel_path, new_rel_path, errmsg );
        g_free( old_rel_path );
        g_free( new_rel_path );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf db_update_path:\n", *errmsg, NULL );

            gchar* errmsg_ii = NULL;
            rc = db_rollback_both( zond, &errmsg_ii );
            if ( rc )
            {
                errmsg_ii = add_string( errmsg_ii, "Bei Aufruf db_rollback_both:\n" );
                if ( errmsg ) *errmsg = g_strconcat( *errmsg, errmsg_ii, NULL );
                g_free( errmsg_ii );
            }

            return -1;
        }
    }

    rc = fm_move_file( tree_view, old_path, new_path, errmsg );
    if ( rc )
    {
        if ( zond )
        {
            gint ret = 0;
            gchar* errmsg_ii = NULL;
            ret = db_rollback_both( zond, &errmsg_ii );
            if ( ret )
            {
                errmsg_ii = add_string( "Bei Aufruf db_rollback_both:\n", errmsg_ii );
                if ( errmsg )
                {
                    if ( rc == -1 )
                    {
                        *errmsg = add_string( g_strdup( "Bei Aufruf fm_move_file:\n"),
                                *errmsg );
                        *errmsg = add_string( *errmsg, g_strdup( "\n\n" ) );
                        *errmsg = add_string( *errmsg, g_strdup( errmsg_ii ) );
                    }
                    else if ( rc == 1 ) *errmsg = g_strdup( errmsg_ii );
                }

                g_free( errmsg_ii );
            }

            return rc;
        }
        else if ( rc == -1 ) ERROR_PAO( "fm_move_file" )
        else if ( rc == 1 ) return 1;
    }

    if ( zond )
    {
        rc = db_commit_both( zond, errmsg );
        if ( rc )
        {
            if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf db_commit:\n" ),
                    *errmsg );

            gchar* errmsg_ii = NULL;
            rc = db_rollback_both( zond, &errmsg_ii );
            if ( rc )
            {
                errmsg_ii = prepend_string( errmsg_ii, "Bei Aufruf db_rollback_both:\n" );
                if ( errmsg ) *errmsg = g_strconcat( *errmsg, errmsg_ii, NULL );
                g_free( errmsg_ii );
            }

            return -1;
        }
    }

    return 0;
}


void
cb_fm_row_text_edited( GtkCellRenderer* cell, gchar* path_string, gchar* new_text,
        gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gchar* path_old = NULL;
    gchar* path_new = NULL;
    GtkTreeView* tree_view = NULL;
    GtkTreeModel* model = NULL;
    GtkTreeIter iter;
    gchar* root = NULL;
    gchar* old_text = NULL;
    GtkTreeIter iter_parent = { 0 };
    gboolean dif = FALSE;

    Projekt* zond = (Projekt*) data;

    tree_view = g_object_get_data( G_OBJECT(cell), "tree-view" );
    model = gtk_tree_view_get_model( tree_view );
    gtk_tree_model_get_iter_from_string( gtk_tree_view_get_model( tree_view ), &iter, path_string );

    if ( gtk_tree_model_iter_parent( model, &iter_parent, &iter ) ) root =
            fm_get_full_path( tree_view, &iter_parent );
    else root = g_strdup( g_object_get_data( G_OBJECT(tree_view), "root" ) );

    gtk_tree_model_get( model, &iter, 1, &old_text, -1 );

    path_old = g_strconcat( root, "/", old_text, NULL );
    g_free( old_text );

    path_new = g_strconcat( root, "/", new_text, NULL );
    g_free( root );

    dif = g_strcmp0( path_old, path_new );
    if ( dif )
    {
        rc = fm_edit_row( tree_view, &iter, path_old, path_new, zond, &errmsg );
        if ( rc == -1 )
        {
            display_message( gtk_widget_get_toplevel( GTK_WIDGET(tree_view) ),
                    "Umbenennen nicht möglich\n\nBei Aufruf fm_edit_row:\n",
                    errmsg, NULL );
            g_free( errmsg );
        }
        else if ( rc == 0 )
        {
            gtk_tree_store_set( GTK_TREE_STORE(model), &iter, 1, new_text, -1 );
            gtk_tree_view_columns_autosize( tree_view );
        }
    }

    g_free( path_old );
    g_free( path_new );

    return;
}


gint
fm_datei_oeffnen( const gchar* path, gchar** errmsg )
{
#ifdef _WIN32 //glib funktioniert nicht; daher Windows-Api verwenden
    HINSTANCE ret = 0;

    gchar* path_win32 = g_strdelimit( g_strdup( path ), "/", '\\' );

    //utf8 in filename konvertieren
    gsize written;
    gchar* charset = g_get_codeset();
    gchar* local_filename = g_convert( path_win32, -1, charset, "UTF-8", NULL, &written,
            NULL );
    g_free( charset );

    g_free( path_win32 );
    ret = ShellExecute( NULL, NULL, local_filename, NULL, NULL, SW_SHOWNORMAL );
    g_free( local_filename );
    if ( ret == (HINSTANCE) 31 ) //no app associated
    {
        if ( errmsg ) *errmsg = g_strdup( "Bei Aufruf ShellExecute:\n"
                "Keine Anwendung mit Datei verbunden" );

        return -1;
    }
    else if ( ret <= (HINSTANCE) 32 )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "Bei Aufruf "
                "ShellExecute:\nErrCode: %p", ret );

        return -1;
    }
#else //Linux/Mac
    gchar* exe = NULL;
    gchar* argv[3] = { NULL };

    //exe herausfinden, vielleicht mit xdgopen???!

    argv[0] = exe;
    argv[1] = path;

    gboolean rc = g_spawn_async( NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
            NULL, NULL, &g_pid, &error );
    if ( !rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_spawn_async:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    g_child_watch_add( g_pid, (GChildWatchFunc) close_pid, NULL );

#endif // _WIN32

    return 0;
}


static void
cb_fm_row_activated( GtkTreeView* tree_view, GtkTreePath* tree_path,
        GtkTreeViewColumn* column, gpointer data )
{
    GtkWidget* window_parent = (GtkWidget*) data;

    gchar* path = NULL;
    GtkTreeIter iter;
    gint rc = 0;
    gchar* errmsg = NULL;

    gtk_tree_model_get_iter( gtk_tree_view_get_model( tree_view ), &iter, tree_path );

    path = fm_get_full_path( tree_view, &iter );

    rc = fm_datei_oeffnen( path, &errmsg );
    g_free( path );
    if ( rc )
    {
        display_message( window_parent, "Öffnen nicht möglich\n\nBei Aufruf "
                "oeffnen_datei:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
cb_fm_row_collapsed( GtkTreeView* tree_view, GtkTreeIter* iter,
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

    return;
}


static gint
fm_dir_foreach( GtkTreeView* tree_view, GFile* dir,
        gint (*foreach) ( GtkTreeView*, GFile*, GFile*, GFileInfo*, gpointer, gchar** ),
        gpointer data, gchar** errmsg )
{
    GError* error = NULL;

    GFileEnumerator* enumer = g_file_enumerate_children( dir, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
    if ( !enumer )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    while ( 1 )
    {
        GFile* child = NULL;
        GFileInfo* info_child = NULL;

        if ( !g_file_enumerator_iterate( enumer, &info_child, &child, NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( enumer );

            return -1;
        }

        if ( child ) //es gibt noch Datei in Verzeichnis
        {
            gint rc = 0;

            rc = foreach( tree_view, dir, child, info_child, data, errmsg );
            if ( rc == -1 )
            {
                g_object_unref( enumer );
                if ( errmsg ) *errmsg = add_string( "Bei Aufruf foreach:\n",
                        *errmsg );

                return -1;
            }
            else if ( rc == 1 ) break;//Abbruch gewählt
        } //ende if ( child )
        else break;
    }

    g_object_unref( enumer );

    return 0;
}


/** iter zeigt auf Verzeichnis, was zu füllen ist
    Es wurde bereits getestet, ob das Verzeichnis bereits geladen wurde
**/
static gint
fm_load_dir_foreach( GtkTreeView* tree_view, GFile* dir_parent, GFile* file,
        GFileInfo* info, gpointer data, gchar** errmsg )
{
    GtkTreeIter iter_new = { 0 };

    GtkTreeIter* iter = (GtkTreeIter*) data;

    //child in tree einfügen
    gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )),
            &iter_new, iter, -1 );
    gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )),
            &iter_new, 0, g_file_info_get_icon( info ),
            1, g_file_info_get_display_name( info ), -1 );

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
                GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )),
                &newest_iter, &iter_new, -1 );
    }

    return 0;
}


gint
fm_load_dir( GtkTreeView* tree_view, GtkTreeIter* iter, gchar** errmsg )
{
    gchar* path_dir = NULL;
    gint rc = 0;

    //path des directory aus tree holen
    path_dir = fm_get_full_path( tree_view, iter ); //keine Fehlerrückgabe0

    GFile* dir = g_file_new_for_path( path_dir );
    g_free( path_dir );

    rc = fm_dir_foreach( tree_view, dir, fm_load_dir_foreach, iter, errmsg );
    if ( rc )
    {
        if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf fm_dir_foreach:\n" ),
                *errmsg );

        return -1;
    }

    return 0;
}


static void
cb_fm_row_expanded( GtkTreeView* tree_view, GtkTreeIter* iter,
        GtkTreePath* path, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    GtkTreeIter new_iter;
    gchar* string = NULL;

    GtkWidget* window = (GtkWidget*) data;

    gtk_tree_model_iter_nth_child( gtk_tree_view_get_model( tree_view ), &new_iter, iter, 0 );
    gtk_tree_model_get( gtk_tree_view_get_model( tree_view ), &new_iter, 1, &string, -1 );
    if ( !string )
    {
        rc = fm_load_dir( tree_view, iter, &errmsg );
        if ( rc )
        {
            display_message( window, "Directory konnte nicht geladen werden\n\n"
                    "Bei Aufruf fm_load_dir:\n", errmsg, NULL );
            g_free( errmsg );
        }
        gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )),
                &new_iter );
    }
    gtk_tree_view_columns_autosize( tree_view );

    return;
}


GtkTreeView*
fm_create_tree_view( GtkWidget* window_parent, Projekt* zond )
{
    //renderer
    GtkCellRenderer* renderer_icon = gtk_cell_renderer_pixbuf_new( );
    GtkCellRenderer* renderer_text = gtk_cell_renderer_text_new( );

    g_object_set( renderer_text, "editable", TRUE, NULL);
    g_object_set( renderer_text, "underline", PANGO_UNDERLINE_SINGLE, NULL );

    //die column
    GtkTreeViewColumn* fs_tree_column = gtk_tree_view_column_new( );
    gtk_tree_view_column_set_resizable( fs_tree_column, FALSE );
    gtk_tree_view_column_set_sizing(fs_tree_column, GTK_TREE_VIEW_COLUMN_FIXED );
    gtk_tree_view_column_pack_start( fs_tree_column, renderer_icon, FALSE );
    gtk_tree_view_column_pack_start( fs_tree_column, renderer_text, TRUE );

    gtk_tree_view_column_set_attributes( fs_tree_column,
            renderer_icon, "gicon", 0, NULL );
    gtk_tree_view_column_set_attributes( fs_tree_column,
            renderer_text, "text", 1, NULL );

    //treeview
    GtkTreeView* treeview_fs = (GtkTreeView*) gtk_tree_view_new( );
    gtk_tree_view_set_headers_visible( treeview_fs, FALSE );
    gtk_tree_view_set_fixed_height_mode( treeview_fs, TRUE );
    gtk_tree_view_set_enable_tree_lines( treeview_fs, TRUE );
    gtk_tree_view_set_enable_search( treeview_fs, FALSE );

    gtk_tree_view_append_column( treeview_fs, fs_tree_column );

    g_object_set_data( G_OBJECT(treeview_fs), "renderer-icon", renderer_icon );
    g_object_set_data( G_OBJECT(treeview_fs), "renderer-text", renderer_text );
    g_object_set_data( G_OBJECT(renderer_text), "tree-view", treeview_fs );

    GtkTreeStore* tree_store = gtk_tree_store_new( 2, G_TYPE_ICON, G_TYPE_STRING );
    gtk_tree_view_set_model( treeview_fs, GTK_TREE_MODEL(tree_store) );
    g_object_unref( tree_store );

    //die Selection
    GtkTreeSelection* selection = gtk_tree_view_get_selection( treeview_fs );
    gtk_tree_selection_set_mode( selection, GTK_SELECTION_MULTIPLE );
    gtk_tree_selection_set_select_function( selection,
            (GtkTreeSelectionFunc) treeview_selection_select_func, NULL, NULL );

    //Zeile expandiert
    g_signal_connect( treeview_fs, "row-expanded", G_CALLBACK(cb_fm_row_expanded), window_parent );
    //Zeile kollabiert
    g_signal_connect( treeview_fs, "row-collapsed", G_CALLBACK(cb_fm_row_collapsed), NULL );
    // Doppelklick = angebundene Datei anzeigen
    g_signal_connect( treeview_fs, "row-activated", G_CALLBACK(cb_fm_row_activated), window_parent );

    //Text-Spalte wird editiert
    g_signal_connect( renderer_text, "edited", G_CALLBACK(cb_fm_row_text_edited),
            zond ); //Klick in textzelle = Datei umbenennen

    return treeview_fs;
}



