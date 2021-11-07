#include "misc.h"
#include "dbase.h"
#include "eingang.h"

#include "sond_treeviewfm.h"

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32



typedef struct
{
    gchar* root;
    GtkTreeViewColumn* column_eingang;
    DBase* dbase;
} SondTreeviewFMPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondTreeviewFM, sond_treeviewfm, SOND_TYPE_TREEVIEW)


static gint
sond_treeviewfm_dbase_begin( SondTreeviewFM* stvfm, gchar** errmsg )
{
    gint rc = 0;
    SondTreeviewFMPrivate* priv = sond_treeviewfm_get_instance_private( stvfm );

    rc = dbase_begin( priv->dbase, errmsg );
    if ( rc ) ERROR_SOND( "dbase_begin" )

    return 0;
}


static gint
sond_treeviewfm_dbase_test( SondTreeviewFM* stvfm, const gchar* source, gchar** errmsg )
{
    gint rc = 0;

    SondTreeviewFMPrivate* priv = sond_treeviewfm_get_instance_private( stvfm );

    rc = dbase_test_path( priv->dbase, source, errmsg );
    if ( rc == -1 ) ERROR_SOND( "dbase_test_path" )

    return rc;
}


static gint
sond_treeviewfm_dbase_update_path( SondTreeviewFM* stvfm, const gchar* source,
        const gchar* dest, gchar** errmsg )
{
    gint rc = 0;

    SondTreeviewFMPrivate* priv = sond_treeviewfm_get_instance_private( stvfm );

    rc = dbase_update_path( priv->dbase, source, dest, errmsg );
    if ( rc ) ERROR_ROLLBACK( priv->dbase, "dbase_update_path" )

    return 0;
}


static gint
sond_treeviewfm_dbase_update_eingang( SondTreeviewFM* stvfm, const gchar* source,
        const gchar* dest, gboolean del, gchar** errmsg )
{
    gint rc = 0;

    Clipboard* clipboard = sond_treeview_get_clipboard( SOND_TREEVIEW(stvfm) );
    SondTreeviewFMPrivate* priv = sond_treeviewfm_get_instance_private( stvfm );

    rc = eingang_update_rel_path( sond_treeviewfm_get_dbase( SOND_TREEVIEWFM(clipboard->tree_view) ), source, priv->dbase, dest, del, errmsg );
    if ( rc ) ERROR_ROLLBACK( priv->dbase, "eingang_update_rel_path" )

    return 0;
}


static gint
sond_treeviewfm_dbase_end( SondTreeviewFM* stvfm, gboolean suc, gchar** errmsg )
{
    SondTreeviewFMPrivate* priv = sond_treeviewfm_get_instance_private( stvfm );

    if ( suc )
    {
        gint rc = 0;

        rc = dbase_commit( priv->dbase, errmsg );
        if ( rc ) ERROR_ROLLBACK( priv->dbase, "dbase_commit" )
    }
    else ROLLBACK(priv->dbase)

    return 0;
}


static void
sond_treeviewfm_finalize( GObject* g_object )
{
    Clipboard* clipboard = NULL;

    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( SOND_TREEVIEWFM(g_object) );

    g_free( stvfm_priv->root );

    clipboard = sond_treeview_get_clipboard(SOND_TREEVIEW(g_object) );
    if ( G_OBJECT(clipboard->tree_view) == g_object )
            g_ptr_array_remove_range( clipboard->arr_ref, 0, clipboard->arr_ref->len );

    G_OBJECT_CLASS (sond_treeviewfm_parent_class)->finalize (g_object);

    return;
}


static gboolean
sond_treeviewfm_other_fm( SondTreeviewFM* stvfm )
{
    Clipboard* clipboard = NULL;
    DBase* dbase_source = NULL;
    DBase* dbase_dest = NULL;

    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    clipboard = sond_treeview_get_clipboard( SOND_TREEVIEW(stvfm) );
    dbase_source = sond_treeviewfm_get_dbase( SOND_TREEVIEWFM(clipboard->tree_view) );

    dbase_dest = stvfm_priv->dbase;

    if ( dbase_dest == dbase_source ) return FALSE;

    return TRUE;
}


static gint
sond_treeviewfm_dbase( SondTreeviewFM* stvfm, gint mode, const gchar* rel_path_source,
        const gchar* rel_path_dest, gchar** errmsg )
{
    gint rc = 0;
    Clipboard* clipboard = NULL;

    clipboard = sond_treeview_get_clipboard( SOND_TREEVIEW(stvfm) );

    if ( mode == 2 && sond_treeviewfm_other_fm( stvfm ) )
    {
        rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_test( SOND_TREEVIEWFM(clipboard->tree_view), rel_path_source, errmsg );
        if ( rc ) //aufräumen...
        {
            if ( rc == -1 ) ERROR_SOND( "dbase_test" )
            else if ( rc == 1 ) return 1;
        }
    }

    if ( mode == 4 )
    {
        gint rc = 0;

        rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_test( stvfm, rel_path_dest, errmsg );
        if ( rc )
        {
            if ( rc == -1 ) ERROR_SOND( "dbase_test" )
            else if ( rc == 1 ) return 1;
        }
    }

    rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_begin( stvfm, errmsg );
    if ( rc ) ERROR_SOND( "dbase_begin" )

    if ( mode == 2 || mode == 3 ) //mode == 2: beyond wurde schon ausgeschlossen - mode == 3: ausgeschlossen
    {
        rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_update_path( stvfm,
                rel_path_source, rel_path_dest, errmsg );
        if ( rc ) ERROR_SOND( "dbase_update_path" )
    }

    if ( mode == 1 || mode == 2 )
    {
        rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_update_eingang( stvfm,
                rel_path_source, rel_path_dest, (gboolean) mode - 1, errmsg );
        if ( rc ) ERROR_SOND( "dbase_update_eingang" )
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

            clipboard = sond_treeview_get_clipboard( SOND_TREEVIEW(stvfm) );

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
                ERROR_SOND( "sond_treeviewfm_dbase" )
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
sond_treeviewfm_row_text_edited( GtkCellRenderer* cell, gchar* path_string, gchar* new_text,
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


static gint
sond_treeviewfm_datei_oeffnen( const gchar* path, gchar** errmsg )
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
sond_treeviewfm_row_activated( GtkTreeView* tree_view, GtkTreePath* tree_path,
        GtkTreeViewColumn* column, gpointer data )
{
    GtkTreeIter iter;
    gchar* path = NULL;
    gint rc = 0;
    gchar* errmsg = NULL;

    gtk_tree_model_get_iter( gtk_tree_view_get_model( tree_view ), &iter, tree_path );

    path = sond_treeviewfm_get_full_path( SOND_TREEVIEWFM(tree_view), &iter );
    rc = sond_treeviewfm_datei_oeffnen( path, &errmsg );
    g_free( path );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(tree_view) ),
                "Öffnen nicht möglich\n\nBei Aufruf oeffnen_datei:\n", errmsg, NULL );
        g_free( errmsg );
    }

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
    rc == 2: nicht alles ausgeführt, Callback hat 2 zurückgegeben -> Abbruch
    **/
static gint
sond_treeviewfm_dir_foreach( SondTreeviewFM* stvfm, GtkTreeIter* iter_dir, GFile* file,
        gint (*foreach) ( SondTreeviewFM*, GtkTreeIter*, GFile*, GFileInfo*, gpointer, gchar** ),
        gpointer data, gchar** errmsg )
{
    GError* error = NULL;
    gboolean flag = FALSE;
    GFileEnumerator* enumer = NULL;

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

            if ( iter_dir && gtk_tree_model_iter_children(
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
            else if ( rc == 1 ) flag = TRUE;//Abbruch gewählt
            else if ( rc == 2 )
            {
                g_object_unref( enumer );
                return 2;
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

    //fm_load_dir_foreach gibt 0 oder -1 zurück
    rc = sond_treeviewfm_dir_foreach( stvfm, iter, file,
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
    if ( !info )
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


static void
sond_treeviewfm_render_eingang( GtkTreeViewColumn* column, GtkCellRenderer* renderer,
        GtkTreeModel* model, GtkTreeIter* iter, gpointer data )
{
    gint rc = 0;
    Eingang eingang = { 0 };
    gint eingang_id = 0;
    gchar* rel_path = NULL;
    gchar* errmsg = NULL;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;
    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );
    if ( !rel_path ) return;

    rc = eingang_for_rel_path( stvfm_priv->dbase, rel_path, &eingang_id, &eingang, NULL, &errmsg );
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
            eingang.eingangsdatum, NULL );
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
sond_treeviewfm_render_file_name( SondTreeview* stv, GtkTreeIter* iter, gpointer data )
{
    g_object_set( G_OBJECT(sond_treeview_get_cell_renderer_text( stv )), "text",
            sond_treeviewfm_get_name( SOND_TREEVIEWFM(stv), iter ), NULL );

    gchar* rel_path = NULL;

    rel_path = sond_treeviewfm_get_rel_path( SOND_TREEVIEWFM(stv), iter );
    if ( rel_path )
    {
        gint rc = 0;
        gchar* errmsg = NULL;

        rc = SOND_TREEVIEWFM_GET_CLASS(SOND_TREEVIEWFM(stv))->dbase_test( SOND_TREEVIEWFM(stv), rel_path, &errmsg );
        g_free( rel_path );
        if ( rc == -1 )
        {
            display_message( gtk_widget_get_toplevel( GTK_WIDGET(stv) ),
                    "Warnung -\n\nBei Aufruf dbase_test:\n",
                    errmsg, NULL );
            g_free( errmsg );
        }
        else if ( rc == 0 ) g_object_set(
                G_OBJECT(sond_treeview_get_cell_renderer_text( stv )),
                "background-set", FALSE, NULL );
        else g_object_set( G_OBJECT(sond_treeview_get_cell_renderer_text(
                stv )), "background-set", TRUE, NULL );
    }

    return;
}


static void
sond_treeviewfm_class_init( SondTreeviewFMClass* klass )
{
    klass->dbase_begin = sond_treeviewfm_dbase_begin;
    klass->dbase_test = sond_treeviewfm_dbase_test;
    klass->dbase_update_path = sond_treeviewfm_dbase_update_path;
    klass->dbase_update_eingang = sond_treeviewfm_dbase_update_eingang;
    klass->dbase_end= sond_treeviewfm_dbase_end;
    klass->row_text_edited = sond_treeviewfm_row_text_edited;

    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = sond_treeviewfm_finalize;

    return;
}


static void
sond_treeviewfm_init( SondTreeviewFM* stvfm )
{
    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    gtk_tree_view_column_set_cell_data_func( sond_treeview_get_column(
            SOND_TREEVIEW(stvfm) ),
            sond_treeview_get_cell_renderer_icon( SOND_TREEVIEW(stvfm) ),
            sond_treeviewfm_render_file_icon, NULL, NULL );

    sond_treeview_set_render_text_cell_func( SOND_TREEVIEW(stvfm),
            sond_treeviewfm_render_file_name, stvfm );

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

    gtk_tree_view_column_set_title( sond_treeview_get_column( SOND_TREEVIEW(stvfm) ), "Datei" );
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

    //Text-Spalte wird editiert
    g_signal_connect( sond_treeview_get_cell_renderer_text( SOND_TREEVIEW(stvfm) ),
            "edited", G_CALLBACK(sond_treeviewfm_row_text_edited), stvfm ); //Klick in textzelle = Datei umbenennen

    return;
}


SondTreeviewFM*
sond_treeviewfm_new( void )
{
    SondTreeviewFM* stvfm = g_object_new( SOND_TYPE_TREEVIEWFM, NULL );

    return stvfm;
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
    if ( rc ) ERROR_SOND( "sond_treeviewfm_load_dir" )

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
sond_treeviewfm_set_dbase( SondTreeviewFM* stvfm, DBase* dbase )
{
    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    stvfm_priv->dbase = dbase;

    return;
}


DBase*
sond_treeviewfm_get_dbase( SondTreeviewFM* stvfm )
{
    if ( ! stvfm ) return NULL;

    SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

    return stvfm_priv->dbase;
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


static GtkTreeIter*
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

    GtkTreeIter* iter = sond_treeview_get_cursor( SOND_TREEVIEW(stvfm) );

    if ( !iter ) return 0;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) ), iter, 0, &info, -1 );

    type = g_file_info_get_file_type( info );
    g_object_unref( info );
    if ( !(type == G_FILE_TYPE_DIRECTORY) && child ) //kein Verzeichnis in Datei
    {
        gtk_tree_iter_free( iter );

        return 0;
    }

    full_path = sond_treeviewfm_get_full_path( stvfm, iter );
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
    if ( rc == -1 ) //anderer Fall tritt nicht ein
    {
        gtk_tree_iter_free( iter );
        ERROR_SOND( "sond_treeviewfm_move_copy_create_delete" )
    }

    //In Baum tun
    GtkTreeIter* iter_new = NULL;
    GFileInfo* info_new = NULL;
    GError* error = NULL;

    iter_new = sond_treeviewfm_insert_node( stvfm, iter, child );
    gtk_tree_iter_free( iter );

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
    Clipboard* clipboard = sond_treeview_get_clipboard( SOND_TREEVIEW(stvfm) );

    g_clear_object( &s_fm_paste_selection->file_dest );

    basename_file = g_file_get_basename( file );
    s_fm_paste_selection->file_dest = g_file_get_child( s_fm_paste_selection->file_parent, basename_file );
    g_free( basename_file );

    GFileType type = g_file_query_file_type( file, G_FILE_QUERY_INFO_NONE, NULL );
    if ( type == G_FILE_TYPE_DIRECTORY && !clipboard->ausschneiden )
    {
        GFile* file_parent_tmp = NULL;

        rc = sond_treeviewfm_move_copy_create_delete( stvfm, NULL, &s_fm_paste_selection->file_dest, 0, errmsg );
        if ( rc )
        {
            g_object_unref( s_fm_paste_selection->file_dest );
            s_fm_paste_selection->file_dest = NULL;
            if ( rc == -1 ) ERROR_SOND( "sond_treeviewfm_move_copy_create_delete" )
            else return rc;
        }

        file_parent_tmp = s_fm_paste_selection->file_parent;
        s_fm_paste_selection->file_parent = g_object_ref( s_fm_paste_selection->file_dest );

        rc = sond_treeviewfm_dir_foreach( stvfm, iter, s_fm_paste_selection->file_dest,
                sond_treeviewfm_move_or_copy_node, s_fm_paste_selection, errmsg );

        g_object_unref( s_fm_paste_selection->file_parent );
        s_fm_paste_selection->file_parent = file_parent_tmp;

        if ( rc )
        {
            g_object_unref( s_fm_paste_selection->file_dest );
            s_fm_paste_selection->file_dest = NULL;
            if ( rc == -1 ) ERROR_SOND( "sond_treeviewfm_dir_foreach" )
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
            g_object_unref( s_fm_paste_selection->file_dest );
            s_fm_paste_selection->file_dest = NULL;
            if ( rc == -1 ) ERROR_SOND( "sond_treeviewfm_move_copy_create_delete" )
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
    Clipboard* clipboard = sond_treeview_get_clipboard( stv );

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

    if ( rc == -1 ) ERROR_SOND( "selection_move_file" )
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

        gtk_tree_iter_free( s_fm_paste_selection->iter_cursor );
        s_fm_paste_selection->iter_cursor = iter_new;
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
    GtkTreeIter* iter_cursor = NULL;
    gchar* path = NULL;
    GFile* file_cursor = NULL;
    GFile* file_parent = NULL;
    GFileType file_type = G_FILE_TYPE_UNKNOWN;
    gboolean expanded = FALSE;
    Clipboard* clipboard = NULL;

    clipboard = sond_treeview_get_clipboard( SOND_TREEVIEW(stvfm) );

    if ( clipboard->arr_ref->len == 0 ) return 0;

    //Datei unter cursor holen
    iter_cursor = sond_treeview_get_cursor( SOND_TREEVIEW(stvfm) );
    if ( !iter_cursor ) return 0;

    path = sond_treeviewfm_get_full_path( stvfm, iter_cursor );
    if ( !path )
    {
        gtk_tree_iter_free( iter_cursor );
        ERROR_SOND( "sond_treeviewfm_get_full_path:\nKein Pfadname" )
    }

    file_cursor = g_file_new_for_path( path );
    g_free( path );
    file_type = g_file_query_file_type( file_cursor, G_FILE_QUERY_INFO_NONE, NULL );

    //if kind && datei != dir: Fehler
    if ( (file_type != G_FILE_TYPE_DIRECTORY) && kind )
    {
        g_object_unref( file_cursor );
        gtk_tree_iter_free( iter_cursor );
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
                iter_cursor );
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
            iter_cursor, kind, expanded, FALSE };

    rc = sond_treeview_clipboard_foreach( SOND_TREEVIEW(stvfm),
            sond_treeviewfm_paste_clipboard_foreach, (gpointer) &s_fm_paste_selection, errmsg );
    g_object_unref( file_parent );
    if ( rc == -1 )
    {
        gtk_tree_iter_free( s_fm_paste_selection.iter_cursor );
        ERROR_SOND( "sond_treeview_clipboard_foreach" )
    }

    //Wenn in nicht ausgeklapptes Verzeichnis etwas eingefügt wurde:
    //Dummy einfügen
    if ( s_fm_paste_selection.inserted && kind && !expanded )
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

    gtk_tree_iter_free( s_fm_paste_selection.iter_cursor );

    return 0;
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
        rc = sond_treeviewfm_dir_foreach( stvfm, iter_file, file,
                sond_treeviewfm_remove_node, data, errmsg );
        if ( rc == -1 ) ERROR_SOND( "sond_treeviewfm_dir_foreach" )
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


gint
sond_treeviewfm_selection_loeschen( SondTreeviewFM* stvfm, gchar** errmsg )
{
    gint rc = 0;

    rc = sond_treeview_selection_foreach( SOND_TREEVIEW(stvfm),
            sond_treeviewfm_foreach_loeschen, NULL, errmsg );
    if ( rc == -1 ) ERROR_SOND( "sond_treeview_clipboard_foreach" )

    return 0;
}


