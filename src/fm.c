#include <gtk/gtk.h>

#include "misc.h"
#include "treeview.h"
#include "fm.h"

#include "zond/global_types.h"
#include "zond/error.h"

#include "zond/99conv/db_write.h"

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


gchar*
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


gchar*
fm_get_full_path( GtkTreeView* tree_view, GtkTreeIter* iter )
{
    gchar* path_dir = NULL;

    gchar* rel_path = fm_get_rel_path( tree_view, iter );
    path_dir = g_strconcat( g_object_get_data( G_OBJECT(tree_view), "root" ), "/", NULL );
    path_dir = add_string( path_dir, rel_path );

    return path_dir;
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
fm_move_copy_create_delete( GtkTreeView* tree_view, GFile* file_source,
        GFile** file_dest, gint mode, gchar** errmsg )
{
    gint zaehler = 0;
    gchar* basename = NULL;

    basename = g_file_get_basename( *file_dest );

    while ( 1 )
    {
        gboolean suc = FALSE;
        GError* error = NULL;

        if ( mode == 0 ) suc = g_file_make_directory( *file_dest, NULL, &error );
        else if ( mode == 1 ) suc = g_file_copy ( file_source, *file_dest,
                G_FILE_COPY_NONE, NULL, NULL, NULL, &error );
        else if ( mode == 2 || mode == 3 ) suc = g_file_move ( file_source,
                *file_dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &error );
        else if ( mode == 4 ) suc = g_file_delete( *file_dest, NULL, &error );

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
                        GTK_WIDGET(tree_view) ), "Zugriff nicht erlaubt",
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


gint
fm_create_dir( GtkTreeView* tree_view, gboolean child, gchar** errmsg )
{
    gint rc = 0;
    gchar* path = NULL;
    GFile* file = NULL;
    GFile* parent = NULL;
    GFileType type = G_FILE_TYPE_UNKNOWN;

    GtkTreeIter* iter = treeview_get_cursor( tree_view );

    if ( !iter ) return 0;

    path = fm_get_full_path( tree_view, iter );
    file = g_file_new_for_path( path );
    g_free( path );
    type = g_file_query_file_type( file, G_FILE_QUERY_INFO_NONE, NULL );

    if ( !(type == G_FILE_TYPE_DIRECTORY) && child ) //kein Verzeichnis in Datei
    {
        g_object_unref( file );
        gtk_tree_iter_free( iter );

        return 0;
    }

    if ( child ) parent = file;
    else
    {
        parent = g_file_get_parent( file );
        g_object_unref( file );
    } //nur noch parent muß unrefed werden - file wurde übernommen

    GFile* file_dir = g_file_get_child( parent, "Neues Verzeichnis" );
    g_object_unref( parent );

    rc = fm_move_copy_create_delete( tree_view, NULL, &file_dir, 0, errmsg );
    if ( rc == -1 ) //anderer Fall tritt nicht ein
    {
        gtk_tree_iter_free( iter );
        ERROR_PAO( "fm_move_copy_create_delete" )
    }

    //In Baum tun
    GtkTreeIter* iter_new = NULL;

    iter_new = treeview_insert_node( tree_view, iter, child );

    gtk_tree_iter_free( iter );

    GIcon* icon = g_icon_new_for_string( "folder", NULL );
    gchar* node_text = g_file_get_basename( file_dir );
    gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model(
            tree_view )), iter_new, 0, icon, 1, node_text, -1 );

    g_object_unref( file_dir );
    g_object_unref( icon );

    treeview_set_cursor( tree_view, iter_new );

    gtk_tree_iter_free( iter_new );

    return 0;
}


/** rc == -1: Fähler
    rc == 0: alles ausgeführt, sämtliche Callbacks haben 0 zurückgegeben
    rc == 1: alles ausgeführt, mindestens ein Callback hat 1 zurückgegeben
    rc == 2: nicht alles ausgeführt, Callback hat 2 zurückgegeben -> Abbruch
    **/
static gint
fm_dir_foreach( GtkTreeView* tree_view, GtkTreeIter* iter_dir, GFile* file_dir,
        gint (*foreach) ( GtkTreeView*, GtkTreeIter*, GFile*, GFileInfo*, gpointer, gchar** ),
        gpointer data, gchar** errmsg )
{
    GError* error = NULL;
    gboolean flag = FALSE;

    GFileEnumerator* enumer = g_file_enumerate_children( file_dir, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
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
            GtkTreeIter iter_file = { 0 };
            gboolean found = FALSE;

            if ( iter_dir && gtk_tree_model_iter_children(
                    gtk_tree_view_get_model( tree_view ), &iter_file, iter_dir ) )
            {
                gchar* basename = g_file_get_basename( child );

                do
                {
                    //den Namen des aktuellen Kindes holen
                    gchar* text_row = NULL;

                    gtk_tree_model_get( gtk_tree_view_get_model( tree_view ), &iter_file, 1, &text_row, -1 );
                    if ( !g_strcmp0( text_row, basename ) ) found = TRUE;//paßt?
                    g_free( text_row );

                    if ( found ) break;
                } while ( gtk_tree_model_iter_next( gtk_tree_view_get_model( tree_view ), &iter_file ) );

                g_free( basename );
            }

            rc = foreach( tree_view, (found) ? &iter_file : NULL, child, info_child, data, errmsg );
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
        } //ende if ( child )
        else break;
    }

    g_object_unref( enumer );

    return (flag) ? 1 : 0;
}


typedef struct _S_FM_Paste_Selection{
    GFile* file_parent;
    GFile* file_dest;
    GtkTreeIter* iter_cursor;
    gboolean kind;
    gboolean ausschneiden;
    gboolean expanded;
    gboolean inserted;
    SFMChangePath* s_fm_change_path;
} SFMPasteSelection;


static gint
fm_move_or_copy_node( GtkTreeView* tree_view, GtkTreeIter* iter, GFile* file,
        GFileInfo* info_file, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gchar* basename_file = NULL;

    SFMPasteSelection* s_fm_paste_selection = (SFMPasteSelection*) data;

    g_clear_object( &s_fm_paste_selection->file_dest );

    basename_file = g_file_get_basename( file );
    s_fm_paste_selection->file_dest = g_file_get_child( s_fm_paste_selection->file_parent, basename_file );
    g_free( basename_file );

    GFileType type = g_file_query_file_type( file, G_FILE_QUERY_INFO_NONE, NULL );
    if ( type == G_FILE_TYPE_DIRECTORY && !s_fm_paste_selection->ausschneiden )
    {
        GFile* file_parent_tmp = NULL;

        rc = fm_move_copy_create_delete( tree_view, NULL, &s_fm_paste_selection->file_dest, 0, errmsg );
        if ( rc )
        {
            g_object_unref( s_fm_paste_selection->file_dest );
            s_fm_paste_selection->file_dest = NULL;
            if ( rc == -1 ) ERROR_PAO( "fm_move_copy_create_delete" )
            else return rc;
        }

        file_parent_tmp = s_fm_paste_selection->file_parent;
        s_fm_paste_selection->file_parent = g_object_ref( s_fm_paste_selection->file_dest );

        rc = fm_dir_foreach( tree_view, iter, s_fm_paste_selection->file_dest,
                fm_move_or_copy_node, s_fm_paste_selection, errmsg );

        g_object_unref( s_fm_paste_selection->file_parent );
        s_fm_paste_selection->file_parent = file_parent_tmp;

        if ( rc )
        {
            g_object_unref( s_fm_paste_selection->file_dest );
            s_fm_paste_selection->file_dest = NULL;
            if ( rc == -1 ) ERROR_PAO( "fm_dir_foreach" )
            else return rc;
        }
    }
    else
    {
        gint mode = 0;

        if ( s_fm_paste_selection->ausschneiden ) mode = 2;
        else mode = 1;

        rc = fm_move_copy_create_delete( tree_view, file, &s_fm_paste_selection->file_dest, mode, errmsg );
        if ( rc )
        {
            g_object_unref( s_fm_paste_selection->file_dest );
            s_fm_paste_selection->file_dest = NULL;
            if ( rc == -1 ) ERROR_PAO( "fm_move_copy_create_delete" )
            else return rc;
        }
    }

    if ( iter && s_fm_paste_selection->ausschneiden )
            gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model(
            tree_view )), iter );

    return 0;
}


static gint
fm_paste_selection_foreach( GtkTreeView* tree_view, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gint rc = 0;
    GFile* file_source = NULL;
    gchar* path_source = NULL;
    gboolean same_dir = FALSE;
    gboolean dir_with_children = FALSE;
    GFileType file_type = G_FILE_TYPE_UNKNOWN;

    SFMPasteSelection* s_fm_paste_selection = (SFMPasteSelection*) data;

    //Namen der Datei holen
    path_source = fm_get_full_path( tree_view, iter );

    //source-file
    file_source = g_file_new_for_path( path_source );
    g_free( path_source );

    //prüfen, ob innerhalb des gleichen Verzeichnisses verschoben/kopiert werden
    //soll - dann: same_dir = TRUE
    GFile* file_parent_source = g_file_get_parent( file_source );
    same_dir = g_file_equal( s_fm_paste_selection->file_parent, file_parent_source );
    g_object_unref( file_parent_source );

    //verschieben im gleichen Verzeichnis ist Quatsch
    if ( same_dir && s_fm_paste_selection->ausschneiden )
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
    rc = fm_move_or_copy_node( tree_view, iter, file_source, NULL, s_fm_paste_selection, errmsg );
    g_object_unref( file_source );

    if ( rc == -1 ) ERROR_PAO( "selection_move_file" )
    else if ( rc == 1 ) return 0; //Ünerspringen gewählt - einfach weiter
    else if ( rc == 2 ) return 1; //nur bei selection_move_file/_dir möglich

    s_fm_paste_selection->inserted = TRUE;

    //Knoten müssen nur eingefügt werden, wenn Row expanded ist; sonst passiert das im callback beim Öffnen
    if ( (!s_fm_paste_selection->kind) || s_fm_paste_selection->expanded )
    {
        //Ziel-FS-tree eintragen
        GtkTreeIter* iter_new = NULL;
        GError* error = NULL;

        iter_new = treeview_insert_node( tree_view, s_fm_paste_selection->iter_cursor,
                s_fm_paste_selection->kind );

        gtk_tree_iter_free( s_fm_paste_selection->iter_cursor );
        s_fm_paste_selection->iter_cursor = iter_new;
        s_fm_paste_selection->kind = FALSE;

        //Falls Verzeichnis mit Datei innendrin, Knoten in tree einfügen
        if ( dir_with_children )
        {
            GtkTreeIter iter_tmp;
            gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model(
                tree_view )), &iter_tmp, s_fm_paste_selection->iter_cursor, -1 );
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
            gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )),
                    s_fm_paste_selection->iter_cursor, 0, g_file_info_get_icon( file_dest_info ), 1, g_file_get_basename( s_fm_paste_selection->file_dest ), -1 );

            g_object_unref( file_dest_info );
        }
    }

    g_clear_object( &s_fm_paste_selection->file_dest );

    return 0;
}


gint
fm_paste_selection( GtkTreeView* tree_view, GPtrArray* refs,
        gboolean ausschneiden, gboolean kind, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter* iter_cursor = NULL;
    gchar* path = NULL;
    GFile* file_cursor = NULL;
    GFile* file_parent = NULL;
    GFileType file_type = G_FILE_TYPE_UNKNOWN;
    gboolean expanded = FALSE;

    //Datei unter cursor holen
    iter_cursor = treeview_get_cursor( tree_view );
    if ( !iter_cursor ) return 0;

    path = fm_get_full_path( tree_view, iter_cursor );
    if ( !path )
    {
        gtk_tree_iter_free( iter_cursor );
        ERROR_PAO( "fm_get_full_path:\nKein Pfadname" )
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

        path = gtk_tree_model_get_path( gtk_tree_view_get_model( tree_view ),
                iter_cursor );
        expanded = gtk_tree_view_row_expanded( tree_view, path );
        gtk_tree_path_free( path );
    }
    else //unterhalb angefügt
    {
        //dann ist parent parent!
        file_parent = g_file_get_parent( file_cursor );
        g_object_unref( file_cursor );
    }

    SFMPasteSelection s_fm_paste_selection = { file_parent, NULL, iter_cursor, kind,
            ausschneiden, expanded, FALSE, (SFMChangePath*) data };

    rc = treeview_selection_foreach( tree_view, refs,
            fm_paste_selection_foreach, (gpointer) &s_fm_paste_selection, errmsg );
    g_object_unref( file_parent );
    if ( rc == -1 )
    {
        gtk_tree_iter_free( s_fm_paste_selection.iter_cursor );
        ERROR_PAO( "treeview_selection_foreach" )
    }

    //Wenn in nicht ausgeklapptes Verzeichnis etwas eingefügt wurde:
    //Dummy einfügen
    if ( s_fm_paste_selection.inserted && kind && !expanded )
    {
        GtkTreeIter iter_tmp;
        gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model(
                tree_view )), &iter_tmp, s_fm_paste_selection.iter_cursor, -1 );
    }

    //Alte Auswahl löschen - muß vor baum_setzen_cursor geschehen,
    //da in change_row-callback ref abgefragt wird
    if ( ausschneiden && refs->len > 0 )
            g_ptr_array_remove_range( refs,
            0, refs->len );
    gtk_widget_queue_draw( GTK_WIDGET(tree_view) );

    //Wenn neuer Knoten sichtbar: Cursor setzen
    if ( !kind || expanded ) treeview_set_cursor( tree_view,
            s_fm_paste_selection.iter_cursor );

    gtk_tree_iter_free( s_fm_paste_selection.iter_cursor );

    return 0;
}


static gint
fm_remove_node( GtkTreeView* tree_view, GtkTreeIter* iter_file, GFile* file,
        GFileInfo* info_file, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    GFileType type = G_FILE_TYPE_UNKNOWN;

    SFMRemove* s_fm_remove = (SFMRemove*) data;

    type = g_file_query_file_type( file, G_FILE_QUERY_INFO_NONE, NULL );

    if ( type != G_FILE_TYPE_DIRECTORY )
    {
        if ( s_fm_remove && s_fm_remove->fm_test )
        {
            rc = s_fm_remove->fm_test( file, s_fm_remove->data, errmsg );
            if ( rc == -1 ) ERROR_PAO( "fm_test" )
            if ( rc == 1 ) return 1; //flag bei fm_dir_foreach wird gesetzt
        }
    }
    else //Verzeichnis - muß erst geleert werden
    {
        rc = fm_dir_foreach( tree_view, iter_file, file, fm_remove_node, data, errmsg );
        if ( rc == -1 ) ERROR_PAO( "fm_dir_foreach" )
        else if ( rc ) return rc; //Verzeichnis nicht leer, weil
                                    //Datei angebunden war, übersprungen wurde oder Abbruch gewählt
    }

    rc = fm_move_copy_create_delete( tree_view, NULL, &file, 4, errmsg );
    if ( rc == -1 ) ERROR_PAO( "fm_move_copy_create_delete" )

    if ( iter_file ) gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )),
            iter_file );

    return 0;
}


gint
fm_foreach_loeschen( GtkTreeView* tree_view, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gchar* full_path = NULL;
    GFile* file = NULL;

    full_path = fm_get_full_path( tree_view, iter );
    file = g_file_new_for_path( full_path );
    g_free( full_path );

    gint rc = fm_remove_node( tree_view, iter, file, NULL, data, errmsg );
    g_object_unref( file );
    if ( rc == -1 ) ERROR_PAO( "fm_remove_node" )
    else if ( rc == 2 ) return 1;

    return 0;
}


/** iter zeigt auf Verzeichnis, was zu füllen ist
    Es wurde bereits getestet, ob das Verzeichnis bereits geladen wurde
**/
static gint
fm_load_dir_foreach( GtkTreeView* tree_view, GtkTreeIter* iter, GFile* file,
        GFileInfo* info, gpointer data, gchar** errmsg )
{
    GtkTreeIter iter_new = { 0 };

    GtkTreeIter* iter_parent = (GtkTreeIter*) data;

    //child in tree einfügen
    gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model( tree_view )),
            &iter_new, iter_parent, -1 );
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

    //fm_load_dir_foreach gibt 0 oder -1 zurück
    rc = fm_dir_foreach( tree_view, NULL, dir, fm_load_dir_foreach, iter, errmsg );
    if ( rc == -1 )
    {
        if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf fm_dir_foreach:\n" ),
                *errmsg );

        return -1;
    }

    return 0;
}


void
cb_fm_row_text_edited( GtkCellRenderer* cell, gchar* path_string, gchar* new_text,
        gpointer data )
{
    gchar* errmsg = NULL;
    gchar* path_old = NULL;
    gchar* path_new = NULL;
    GtkTreeView* tree_view = NULL;
    GtkTreeModel* model = NULL;
    GtkTreeIter iter;
    gchar* root = NULL;
    gchar* old_text = NULL;
    GtkTreeIter iter_parent = { 0 };
    GFile* file_source = NULL;
    GFile* file_dest = NULL;

    SFMChangePath* s_fm_change_path = (SFMChangePath*) data;

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

    file_source = g_file_new_for_path( path_old );
    file_dest = g_file_new_for_path( path_new );

    g_free( path_old );
    g_free( path_new );

    if ( !g_file_equal( file_source, file_dest ) )
    {
        gint rc = 0;
        gint rc_edit = 0;

        if ( s_fm_change_path && s_fm_change_path->before )
        {
            rc = s_fm_change_path->before( s_fm_change_path->data, &errmsg );
            if ( rc )
            {
                display_message( gtk_widget_get_toplevel( GTK_WIDGET(tree_view) ),
                        "Umbenennen nicht möglich\n\nBei Aufruf before:\n",
                        errmsg, NULL );
                g_free( errmsg );

                g_object_unref( file_source );
                g_object_unref( file_dest );

                return;
            }
        }

        rc_edit = fm_move_copy_create_delete( tree_view, file_source, &file_dest, 3, &errmsg );
        if ( s_fm_change_path && s_fm_change_path->after )
                rc = s_fm_change_path->after( file_source, file_dest, rc_edit,
                s_fm_change_path->data, &errmsg );

        if ( rc_edit == -1 || rc )
                display_message( gtk_widget_get_toplevel( GTK_WIDGET(tree_view) ),
                "Umbenennen nicht möglich\n\nBei Aufruf fm_move_copy_create_delete:\n",
                errmsg, NULL );
        else if ( rc_edit == 0 && rc == 0 )
        {
            gtk_tree_store_set( GTK_TREE_STORE(model), &iter, 1, new_text, -1 );
            gtk_tree_view_columns_autosize( tree_view );
        } //wenn rc_edit == 1 oder 2: einfach zurück
    }

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
fm_create_tree_view( GtkWidget* window_parent, SFMChangePath* s_fm_change_path )
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
            s_fm_change_path ); //Klick in textzelle = Datei umbenennen

    return treeview_fs;
}



