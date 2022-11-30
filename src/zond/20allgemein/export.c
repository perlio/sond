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

#include <gtk/gtk.h>
#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32

#include "../../misc.h"

#include "../global_types.h"
#include "../zond_dbase.h"

#include "project.h"



static gchar*
export_get_buffer( gint left_indent, gchar* node_text,
        gchar* rel_path, Anbindung* anbindung, gchar* text )
{
    gchar* buffer = NULL;
    gchar* node_text_locale = NULL;
    GError* error = NULL;

    node_text_locale = g_locale_from_utf8( node_text, -1, NULL, NULL, &error );
    if ( error )
    {
        g_free( node_text_locale );
        node_text_locale = g_strdup_printf( "Fehler: %s", error->message );
        g_error_free( error );
    }

    buffer = g_strdup_printf( "{\\li%i\\fs28\\b %s \\b0", left_indent, node_text_locale );
    g_free( node_text_locale );

    if ( rel_path ) buffer = add_string( buffer, g_strdup_printf( "\\par\\fs20 %s", rel_path ) );
    {
        if ( anbindung ) buffer = add_string( buffer,
                    g_strdup_printf(" - von Seite %i, Index %i, bis Seite %i, Index %i",
                    anbindung->von.seite, anbindung->von.index, anbindung->bis.seite,
                    anbindung->bis.index ) );
        buffer = add_string( buffer, g_strdup( "\\par" ) );
    }

    if ( text )
    {
        gchar** strv = NULL;
        gint zaehler = 0;
        gchar* text_locale = NULL;
        GError* error = NULL;

        text_locale = g_locale_from_utf8( text, -1, NULL, NULL, &error );
        if ( error )
        {
            g_free( text_locale );
            text_locale = g_strdup_printf( "Fehler: %s", error->message );
            g_error_free( error );
        }

        buffer = add_string( buffer, g_strdup_printf( "\\par\\fs24\\par\\ " ) );
        strv = g_strsplit( text_locale, "\n", -1 );

        while ( strv[zaehler] )
        {
            gchar* zeile = NULL;

            zeile = g_strconcat( strv[zaehler], "\\line ", NULL );
            buffer = add_string( buffer, zeile );

            zaehler++;
        }

        g_strfreev( strv );
        g_free( text_locale );

        buffer = add_string( buffer, g_strdup( "\\par" ) );
    }

    buffer = add_string( buffer, g_strdup( "}" ) );

    return buffer;
}


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
    gint left_indent = 0;

    GtkTreeIter iter;
    if ( !gtk_tree_model_get_iter( model, &iter, path ) )
    {
        if ( errmsg ) *errmsg = g_strdup( "Bei Aufruf gtk_tree_model_get_iter:\n"
                "iter konnte nicht gesetzt werden" );

        return -1;
    }

    gtk_tree_model_get( model, &iter, 1, &node_text, 2, &node_id, -1 );

    left_indent = 200 * depth;

    if ( !(model == gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) ) ||
            (GTK_IS_TREE_MODEL_FILTER(model) && (gtk_tree_model_filter_get_model( GTK_TREE_MODEL_FILTER(model) ) ==
                gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) )))) )
    {
        rc = zond_dbase_get_text( zond->dbase_zond->zond_dbase_work, node_id, &text, errmsg );
        if ( rc )
        {
            g_free( node_text );
            ERROR_S
        }
    }

    buffer = export_get_buffer( left_indent, node_text, NULL, NULL, text );
    g_free( node_text );
    g_free( text );
    rc = g_output_stream_write( G_OUTPUT_STREAM(stream), (const void*) buffer,
            strlen( buffer ), NULL, &error );
    g_free( buffer );
    if ( rc == -1 )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_output_stream_"
                "write (header):\n", error->message, NULL );
        g_error_free( error );

        return -1;
    }

    return 0;
}


static gint
export_selektierte_punkte( Projekt* zond, Baum baum, GFileOutputStream* stream,
        gchar** errmsg )
{
    GList* selected = NULL;
    GList* list = NULL;
    GtkTreeModel* model = NULL;

    model = gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[baum]) );

    selected = gtk_tree_selection_get_selected_rows( zond->selection[baum], NULL );
    if( !selected )
    {
        if ( errmsg ) *errmsg = g_strdup( "Keine Punkte ausgewählt" );

        return -1;
    }

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
            ERROR_SOND( "export_node" )
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

    GtkTreeModel* model = gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[baum]) );

    list = selected;
    do //alle rows aus der Liste
    {
        gint rc = 0;

        rc = export_node( zond, model, list->data, 1, stream, errmsg );
        if ( rc )
        {
            g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );
            ERROR_SOND( "export_node" )
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
            ERROR_SOND( "foreach: export_foreach" )
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
    GtkTreeModel* model = gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[baum]) );

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

    if ( zond->baum_prev == KEIN_BAUM )
    {
        if ( errmsg ) *errmsg = g_strdup( "Kein Baum ausgewählt" );

        return -1;
    }

    const gchar* buffer = g_strconcat( "{\\rtf1 "
            "{\\fs50\\b\\ul ", zond->dbase_zond->project_name, "\\par\\plain ",
            NULL );

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

    switch ( umfang )
    {
        case 1:
            rc = export_alles( zond, zond->baum_prev, stream, errmsg );
            break;
        case 2:
            rc = export_selektierte_zweige( zond, zond->baum_prev, stream, errmsg );
            break;
        case 3:
            rc = export_selektierte_punkte( zond, zond->baum_prev, stream, errmsg );
            break;
    }
    if ( rc ) ERROR_S

    //Hier htm-Datei in stream schreiben
    rc = g_output_stream_write( G_OUTPUT_STREAM(stream), "}}",
            strlen( "}}" ), NULL, &error );
    if ( rc == -1 )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_output_stream_write "
                "(end):\n", error->message, NULL );
        g_error_free( error );

        return -1;
    }
    return 0;
}


void
cb_menu_datei_export_activate( GtkMenuItem*item, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    gchar* filename = filename_speichern( GTK_WINDOW(zond->app_window), "Datei wählen", ".odt" );
    if ( !filename ) return;

    GError* error = NULL;

    GFile* file = g_file_new_for_path( "export_tmp.rtf" );
    GFileOutputStream* stream = g_file_replace( file, NULL, FALSE,
            G_FILE_CREATE_NONE, NULL, &error );
    if ( !stream )
    {
        display_message( zond->app_window, "Export nicht möglich\n\nBei Aufruf g_file_"
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
        display_message( zond->app_window, "Export nicht möglich\n\nFehler bei Aufruf "
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
        display_message( zond->app_window, "Export nicht möglich:\n\nFehler bei Aufruf "
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
    argv[3] = "export_tmp.rtf";
    argv[4] = "--headless";

    ret = g_spawn_sync( NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL,
            NULL, NULL, &error );
    if ( !ret )
    {
        display_message( zond->app_window, "Export nicht möglich:\n\nFehler bei Aufruf "
                "g_spawn_sync:\n", error->message, NULL );
        g_error_free( error );
        error = NULL;
    }

    if ( !g_file_delete( file, NULL, &error ) )
    {
        display_message( zond->app_window, "Löschen der bei Export im Arbeitsverzeichnis "
                "erzeugten Datei 'export_tmp.rtf' nicht möglich:\n\n",
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
        display_message( zond->app_window, "Exportierte Datei konnte nicht umbenannt "
                "werden:\n\n", error->message, "\nErzeugte Datei 'export_tmp.odt' "
                "von Hand umbenennen", NULL );
        g_error_free( error );
    }

    g_object_unref( source );
    g_object_unref( dest );

    return;
}



