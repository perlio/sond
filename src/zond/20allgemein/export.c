/*
zond (export.c) - Akten, Beweisstücke, Unterlagen
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

#include "../enums.h"
#include "../global_types.h"
#include "../error.h"

#include "../99conv/db_read.h"
#include "../99conv/baum.h"
#include "../99conv/general.h"

#include <gtk/gtk.h>
#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32


static gint
export_node( Projekt* zond, GtkTreeModel* model, GtkTreePath* path, gint depth,
        GFileOutputStream* stream, gchar** errmsg )
{
    GError* error = NULL;
    gint rc = 0;

    gchar* node_text = NULL;
    gint node_id = 0;
    gchar* text = NULL;
    gchar* buffer = NULL;

    GtkTreeIter iter;
    if ( !gtk_tree_model_get_iter( model, &iter, path ) )
    {
        if ( errmsg ) *errmsg = g_strdup( "Bei Aufruf gtk_tree_model_get_iter:\n"
                "iter konnte nicht gesetzt werden" );

        return -1;
    }

    gtk_tree_model_get( model, &iter, 1, &node_text, 2, &node_id, -1 );

    buffer = g_strdup_printf( "<h%i>%s</h%i>", depth, node_text, depth );
    g_free( node_text );

    rc = g_output_stream_write( G_OUTPUT_STREAM(stream), (const void*) buffer,
            strlen( buffer ), NULL, &error );
    if ( rc == -1 )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_output_stream_write "
                "(header):\n", error->message, NULL );
        g_error_free( error );
        g_free( buffer );

        return -1;
    }

    g_free( buffer );

    if ( model == gtk_tree_view_get_model( zond->treeview[BAUM_INHALT] ) )
            return 0;
    if ( GTK_IS_TREE_MODEL_FILTER(model) )
    {
        if ( gtk_tree_model_filter_get_model( GTK_TREE_MODEL_FILTER(model) ) ==
                gtk_tree_view_get_model( zond->treeview[BAUM_INHALT] ) ) return 0;
    }

    rc = db_get_text( zond, node_id, &text, errmsg );
    if ( rc ) ERROR_PAO( "db_get_text" )

    if ( !text || !g_strcmp0( text, "" ) )
    {
        g_free( text );

        return 0;
    }

    buffer = g_strdup_printf( "<p>%s</p>", text );
    g_free( text );
    rc = g_output_stream_write( G_OUTPUT_STREAM(stream), (const void*) buffer,
            strlen( buffer ), NULL, &error );
    if ( rc == -1 )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_output_stream_"
                "write (header):\n", error->message, NULL );
        g_error_free( error );
        g_free( buffer );

        return -1;
    }

    g_free( buffer );

    return 0;
}


static gint
export_selektierte_punkte( Projekt* zond, Baum baum, GFileOutputStream* stream,
        gchar** errmsg )
{
    GList* selected = NULL;
    GList* list = NULL;

    selected = gtk_tree_selection_get_selected_rows(
            zond->selection[baum], NULL );
    if( !selected )
    {
        if ( errmsg ) *errmsg = g_strdup( "Keine Punkte ausgewählt" );

        return -1;
    }

    GtkTreeModel* model = gtk_tree_view_get_model( zond->treeview[baum] );

    g_object_set_data( G_OBJECT(model), "stream", (gpointer) stream );
    g_object_set_data( G_OBJECT(model), "errmsg", (gpointer) errmsg );

    list = selected;

    do //alle rows aus der Liste
    {
        gint rc = 0;

        rc = export_node( zond, model, list->data, 1, stream, errmsg );
        if ( rc )
        {
            g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );
            ERROR_PAO( "export_node" )
        }
    }
    while ( (list = list->next) );

    g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

    return 0;
}


static gboolean
export_foreach( GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter,
        gpointer user_data )
{
    gchar* errmsg_ii = NULL;

    Projekt* zond = (Projekt*) user_data;

    GFileOutputStream* stream = (GFileOutputStream*) g_object_get_data(
            G_OBJECT(model), "stream" );
    gchar** errmsg = (gchar**) g_object_get_data( G_OBJECT(model), "errmsg" );
    gint offset = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(model), "offset" ));

    gint depth = gtk_tree_path_get_depth( path );
    if ( depth > 6 ) depth = 6;
    if ( (depth + offset) > 6 ) offset = 0;

    gint rc = 0;
    rc = export_node( zond, model, path, depth + offset, stream, &errmsg_ii );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf export_node:\n",
                errmsg_ii, NULL );
        g_free( errmsg_ii );

        return TRUE;
    }

    return FALSE;
}


static gint
export_selektierte_zweige( Projekt* zond, Baum baum, GFileOutputStream* stream,
        gchar** errmsg )
{
    GList* selected = NULL;
    GList* list = NULL;

    selected = gtk_tree_selection_get_selected_rows( zond->selection[baum], NULL );
    if( !selected )
    {
        if ( errmsg ) *errmsg = g_strdup( "Keine Punkte ausgewählt" );

        return -1;
    }

    GtkTreeModel* model = gtk_tree_view_get_model( zond->treeview[baum] );

    list = selected;
    do //alle rows aus der Liste
    {
        gint rc = 0;

        rc = export_node( zond, model, list->data, 1, stream, errmsg );
        if ( rc )
        {
            g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );
            ERROR_PAO( "export_node" )
        }

        //neuen treeview mit root_node
        GtkTreeModel* new_model = gtk_tree_model_filter_new( model,
                list->data );

        g_object_set_data( G_OBJECT(new_model), "stream", (gpointer) stream );
        g_object_set_data( G_OBJECT(new_model), "errmsg", (gpointer) errmsg );
        g_object_set_data( G_OBJECT(new_model), "offset", GINT_TO_POINTER(1) );

        gtk_tree_model_foreach( new_model, (GtkTreeModelForeachFunc) export_foreach,
                (gpointer) zond );
        g_object_unref( new_model );
        if ( errmsg )
        {
            g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );
            ERROR_PAO( "foreach: export_foreach" )
        }
    }
    while ( (list = list->next) );

    g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

    return 0;
}


static gint
export_alles( Projekt* zond, Baum baum, GFileOutputStream* stream, gchar** errmsg )
{
    gchar* errmsg_ii = NULL;
    GtkTreeModel* model = gtk_tree_view_get_model( zond->treeview[baum] );

    g_object_set_data( G_OBJECT(model), "stream", (gpointer) stream );
    g_object_set_data( G_OBJECT(model), "errmsg", (gpointer) &errmsg_ii );

    gtk_tree_model_foreach( model, (GtkTreeModelForeachFunc) export_foreach,
            (gpointer) zond );

    if ( errmsg_ii )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf gtk_tree_model_foreach:\n",
                errmsg_ii, NULL );
        g_free( errmsg_ii );

        return -1;
    }

    return 0;
}


static gint
export_html( Projekt* zond, GFileOutputStream* stream, gint umfang, gchar** errmsg )
{
    GError* error = NULL;
    gint rc = 0;
    Baum baum = 0;

    baum = baum_abfragen_aktiver_treeview( zond );
    if ( baum == KEIN_BAUM )
    {
        if ( errmsg ) *errmsg = g_strdup( "Kein Baum aufgewählt" );

        return -1;
    }

    const gchar* buffer = g_strconcat( "<!DOCTYPE html>\n"
            "<html>"
            "<head><title>Export</title></head>"
            "<body>"
            "<h1>Projekt: ", zond->project_name, "\nBaum: ", (baum = BAUM_INHALT) ? "Inhalt" : "Auswertung",
            "</h1>", NULL );

    //Hier htm-Datei in stream schreiben
    rc = g_output_stream_write( G_OUTPUT_STREAM(stream), (const void*) buffer,
            strlen( buffer ), NULL, &error );
    if ( rc == -1 )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_output_stream_write "
                "(header):\n", error->message, NULL );
        g_error_free( error );

        return -1;
    }

    gchar* errmsg_ii = NULL;

    gchar* funktion = NULL;

    switch ( umfang )
    {
        case 1:
            rc = export_alles( zond, baum, stream, &errmsg_ii );
            funktion = "export_alles";
            break;
        case 2:
            rc = export_selektierte_zweige( zond, baum, stream, &errmsg_ii );
            funktion = "export_selektierte_zweige";
            break;
        case 3:
            rc = export_selektierte_punkte( zond, baum, stream, &errmsg_ii );
            funktion = "export_selektierte_punkte";
            break;
        default: funktion = "";
    }

    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", funktion, ":\n",
                errmsg_ii, NULL );
        g_free( errmsg_ii );

        return -1;
    }

    return 0;
}


void
cb_menu_datei_export_activate( GtkMenuItem*item, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    gchar* filename = filename_speichern( GTK_WINDOW(zond->app_window), "Datei wählen" );
    if ( !filename ) return;

    GError* error = NULL;

    GFile* file = g_file_new_for_path( "export_tmp.htm" );
    GFileOutputStream* stream = g_file_replace( file, NULL, FALSE,
            G_FILE_CREATE_NONE, NULL, &error );
    if ( !stream )
    {
        meldung( zond->app_window, "Export nicht möglich\n\nBei Aufruf g_file_"
                "replace:\n", error->message, NULL );
        g_error_free( error );
        g_object_unref( file );

        return;
    }

    //html-Datei füllen
    gint umfang = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item), "umfang" ));

    gchar* errmsg = NULL;
    gint res = export_html( zond, stream, umfang, &errmsg );
    if ( res )
    {
        meldung( zond->app_window, "Export nicht möglich\n\nFehler bei Aufruf "
                "export_html:\n", errmsg, NULL );
        g_free( errmsg );
        g_object_unref( file );
        g_object_unref( stream );

        return;
    }

    //stream schließen
    g_object_unref( stream );

    //Nun in .odt umwandeln

    //Pfad LibreOffice herausfinden
    gchar soffice_exe[270] = { 0 };

#ifdef _WIN32
    HRESULT rc = 0;

    DWORD bufferlen = 270;

    rc = AssocQueryString( 0, ASSOCSTR_EXECUTABLE, ".odt", "open", soffice_exe,
            &bufferlen );
    if ( rc != S_OK )
    {
        meldung( zond->app_window, "Export nicht möglich:\n\nFehler bei Aufruf "
                "AssocQueryString", NULL );

        g_object_unref( file );

        return;
    }
#else
    //für Linux etc: Pfad von soffice suchen
#endif // _WIN32

    //htm-Datei umwandeln
    gboolean ret = FALSE;

    gchar* argv[6] = { NULL };
    argv[0] = soffice_exe;
    argv[1] = "--convert-to";
    argv[2] = "odt:writer8";
    argv[3] = "export_tmp.htm";
    argv[4] = "--headless";

    ret = g_spawn_sync( NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL,
            NULL, NULL, &error );
    if ( !ret )
    {
        meldung( zond->app_window, "Export nicht möglich:\n\nFehler bei Aufruf "
                "g_spawn_sync:\n", error->message, NULL );
        g_error_free( error );
        error = NULL;
    }

    if ( !g_file_delete( file, NULL, &error ) )
    {
        meldung( zond->app_window, "Löschen der bei Export im Arbeitsverzeichnis "
                "erzeugten Datei 'export_tmp.htm' nicht möglich:\n\n",
                error->message, NULL );
        g_error_free( error );
        error = NULL;
    }

    g_object_unref( file );

    GFile* source = g_file_new_for_path( "export_tmp.odt" );
    GFile* dest = g_file_new_for_path( (const gchar*) filename );
    g_free( filename );

    if ( !g_file_move( source, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL,
            &error ) )
    {
        meldung( zond->app_window, "Exportierte Datei konnte nicht umbenannt "
                "werden\n\n", error->message, "\nErzeugte Datei 'export_tmp.odt' "
                "von Hand umbenennen", NULL );
        g_error_free( error );
    }

    g_object_unref( source );
    g_object_unref( dest );

    return;
}



