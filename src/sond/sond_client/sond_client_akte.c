/*
sond (sond_client_akte.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2023  pelo america

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
#include <json-glib/json-glib.h>
#include "../../misc.h"
#include "../sond_akte.h"

#include "sond_client.h"
#include "sond_client_misc.h"
#include "sond_client_connection.h"


typedef struct _SondClientAkte
{
    SondClientType type;
    GtkWidget* window;
    SondClient* sond_client;

    SondAkte* sond_akte;

    //Widgets
    GtkWidget* entry_reg_nr;
    GtkWidget* button_neu;
    GtkWidget* button_ok;
    GtkWidget* button_unlock;

    GtkWidget* entry_aktenrubrum;
    GtkWidget* entry_aktenkurzbez;
} SondClientAkte;


static void
sond_client_akte_close( SondClientAkte* sond_client_akte )
{
    GtkWidget* window = NULL;

    g_ptr_array_remove_fast( sond_client_akte->sond_client->arr_children_windows, sond_client_akte );

    //wenn lock geholt wurde, dann freigeben
    if ( gtk_widget_get_sensitive( sond_client_akte->button_ok ) &&
            sond_client_akte->sond_akte->ID_entity ) //also keine neue, sondern geholte Akte
    {
        gint rc = 0;
        GError* error = NULL;

        rc = sond_client_unlock( sond_client_akte->sond_client,
                sond_client_akte->sond_akte->ID_entity, &error );
        if ( rc )
        {
            display_message( sond_client_akte->window,
                    "Aktenlock konnte nicht gelöst werden\n\n%s", error->message, NULL );
            g_error_free( error );
        }
    }

    sond_akte_free( sond_client_akte->sond_akte );

    window = sond_client_akte->window;

    g_free( sond_client_akte );

    gtk_widget_destroy( window );

    return;
}


static void
sond_client_akte_button_abbrechen_clicked( GtkButton* button, gpointer data )
{
    sond_client_akte_close( (SondClientAkte*) data );

    return;
}


static gboolean
sond_client_akte_delete_event( GtkWindow* window, GdkEvent* event, gpointer data )
{
    sond_client_akte_close( (SondClientAkte*) data );

    return TRUE;
}


static void
sond_client_akte_button_ok_clicked( GtkButton* button, gpointer data )
{
    gchar* params = NULL;
    gchar* resp = NULL;
    gboolean changed = FALSE;

    SondClientAkte* sond_client_akte = (SondClientAkte*) data;

    if ( strlen( gtk_entry_get_text( GTK_ENTRY(sond_client_akte->entry_aktenrubrum) ) ) < 3 )
    {
        display_message( sond_client_akte->window,
                "Aktenrubrum muß mindestens drei Zeichen umfassen", NULL );

        return;
    }

    if ( g_strcmp0( sond_client_akte->sond_akte->aktenrubrum,
            gtk_entry_get_text( GTK_ENTRY(sond_client_akte->entry_aktenrubrum) ) ) )
    { //daß ins entry nichts eingetragen ist (""), wurde bereits zuvor abgefragt
        changed = TRUE;
        g_free( sond_client_akte->sond_akte->aktenrubrum );
        sond_client_akte->sond_akte->aktenrubrum =
                g_strdup( gtk_entry_get_text( GTK_ENTRY(sond_client_akte->entry_aktenrubrum) ) );

    }
    if ( g_strcmp0( sond_client_akte->sond_akte->aktenkurzbez,
            gtk_entry_get_text( GTK_ENTRY(sond_client_akte->entry_aktenkurzbez) ) ) )
    {
        changed = TRUE;
        g_free( sond_client_akte->sond_akte->aktenkurzbez );
        sond_client_akte->sond_akte->aktenkurzbez =
                g_strdup( gtk_entry_get_text( GTK_ENTRY(sond_client_akte->entry_aktenkurzbez) ) );
    }

    if ( changed ) do
    {
        GError* error = NULL;

        params = sond_akte_to_json_string( sond_client_akte->sond_akte );

        resp = sond_client_send_and_read( sond_client_akte->sond_client,
                "AKTE_SCHREIBEN", params, &error );
        g_free( params );
        if ( !resp )
        {
            display_message( sond_client_akte->window,
                    "Akte konnte nicht geschrieben werden\n\n", error->message, NULL );
            g_error_free( error );

            return; //Fenster bleibt geöffnet; je nach Fehler kann man es nochmal versuchen
        }
        else if ( g_str_has_prefix( resp, "ERROR ***" ) )
        {
            display_message( sond_client_akte->window,
                    "Akte konnte nicht geschrieben werden\n\n"
                    "Server antwortet:", resp, NULL );
            g_free( resp );

            return; //Fenster bleibt geöffnet; je nach Fehler kann man es nochmal versuchen
        }
        else if ( g_str_has_prefix( resp, "NEU" ) )
        {
            gchar** v_resp = NULL;

            v_resp = g_strsplit( resp + 3, "-", 2 );

            sond_client_akte->sond_client->reg_nr_akt = atoi( v_resp[0] );
            sond_client_akte->sond_client->reg_jahr_akt = atoi( v_resp[1] );

            g_strfreev( v_resp );

            g_free( resp );

            break;
        }
        else if ( g_str_has_prefix( resp, "NO_LOCK" ) )
        {
            gint rc = 0;
            gchar* title = NULL;
            gchar* second = NULL;

            if ( strlen( resp ) > 7 )
            {
                title = g_strconcat( "Schreibschutz für ", resp + 7, " besteht", NULL );
                second = g_strdup( "Trotzdem speichern?" );
            }
            else
            {
                title = g_strdup( "Zwischenzeitliche Änderungen durch anderen user" );
                second = g_strdup( "Änderungen überschreiben?" );
            }

            rc = abfrage_frage( sond_client_akte->window, title, second, NULL );
            g_free( title );
            g_free( second );
            if ( rc == GTK_RESPONSE_NO )
            {
                g_free( resp );
                break;
            }
            else
            {
                gint rc = 0;
                GError* error = NULL;

                rc = sond_client_get_lock( sond_client_akte->sond_client, sond_client_akte->sond_akte->ID_entity, &error );
                if ( rc )
                {
                    display_message( sond_client_akte->window,
                            "Lock konnte nicht geholt werden\n\n", error->message, NULL );
                    g_error_free( error );
                    g_free( resp );

                    return; //Fenster bleibt geöffnet; je nach Fehler kann man es nochmal versuchen
                }
            }
        }
        else
        {
            g_free( resp );
            break;
        }
    } while ( 1 );

    sond_client_akte_close( sond_client_akte );

    return;
}


static void
sond_client_akte_loaded( SondClientAkte* sond_client_akte, gboolean editable )
{
    gtk_widget_set_sensitive( sond_client_akte->button_neu, FALSE );
    gtk_widget_set_sensitive( sond_client_akte->entry_reg_nr, FALSE );

    gtk_widget_set_sensitive( sond_client_akte->button_unlock, !editable );

    gtk_widget_set_sensitive( gtk_widget_get_parent( sond_client_akte->entry_aktenrubrum ), editable );
    gtk_widget_set_sensitive( gtk_widget_get_parent( sond_client_akte->entry_aktenkurzbez ), editable );
    gtk_widget_set_sensitive( sond_client_akte->button_ok, editable );

    return;
}


static void
sond_client_akte_button_unlock_clicked( GtkButton* button, gpointer data )
{
    gint rc = 0;
    GError* error = NULL;

    SondClientAkte* sond_client_akte = (SondClientAkte*) data;

    rc = sond_client_get_lock( sond_client_akte->sond_client, sond_client_akte->sond_akte->ID_entity, &error );
    if ( rc )
    {
        display_message( sond_client_akte->window, "Akte konnte nicht entsperrt werden\n\n",
                error->message, NULL );
        g_error_free( error );

        return;
    }

    gtk_widget_set_sensitive( sond_client_akte->button_unlock, FALSE );

    sond_client_akte_loaded( sond_client_akte, TRUE );

    return;
}


static gint
sond_client_akte_holen( SondClientAkte* sond_client_akte, gint reg_nr, gint reg_jahr,
        gchar** user, GError** error )
{
    gchar* resp = NULL;
    gchar* json_string = NULL;
    gchar* params = NULL;

    params = g_strdup_printf( "%i-%i", reg_nr, reg_jahr );
    resp = sond_client_send_and_read( sond_client_akte->sond_client,
            "AKTE_HOLEN", params, error );
    g_free( params );
    if ( !resp )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }
    else if ( g_str_has_prefix( resp, "ERROR ***" ) )
    {
        *error = g_error_new( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_INVALRESP,
                "%s\nServer antwortet: %s", __func__, resp );
        g_free( resp );
        return -1;
    }

    if ( g_str_has_prefix( resp, "LOCKED" ) )
    {
        //delta resp + x herausfinden
        json_string = strstr( resp, "&" ) + 1;

        //user herausfinden (LOCKED...&)
        if ( user ) *user = g_strndup( resp + 6, strlen( resp ) - 6 - strlen( json_string ) - 1 );
    }
    else json_string = resp;

    sond_client_akte->sond_akte = sond_akte_new_from_json( json_string, error );
    g_free( resp );
    if ( !sond_client_akte->sond_akte )
    {
        g_prefix_error( error, "%s\n", __func__ );
        if ( user )
        {
            g_free( *user );
            *user = NULL;
        }

        return -1;
    }

    return 0;
}


static void
sond_client_akte_load( SondClientAkte* sond_client_akte, gint reg_nr, gint reg_jahr )
{
    gchar* user = FALSE;
    GError* error = NULL;
    gint rc = 0;

    //test auf schon geöffnetes Aktenfenster
    for ( gint i = 0; i < sond_client_akte->sond_client->arr_children_windows->len; i++ )
    {
        SondClientAny* sond_client_any = NULL;

        sond_client_any =
                g_ptr_array_index( sond_client_akte->sond_client->arr_children_windows, i );

        if ( sond_client_any->type == SOND_CLIENT_TYPE_AKTE &&
                ((SondClientAkte*) sond_client_any)->sond_akte &&
                ((SondClientAkte*) sond_client_any)->sond_akte->reg_nr == reg_nr &&
                ((SondClientAkte*) sond_client_any)->sond_akte->reg_jahr == reg_jahr )
        {
            sond_client_akte_close( sond_client_akte );
            gtk_window_present( GTK_WINDOW(sond_client_any->window) );

            return;
        }
    }

    rc = sond_client_akte_holen( sond_client_akte, reg_nr, reg_jahr, &user, &error );
    if ( rc )
    {
        display_message( sond_client_akte->window, "Akte kann nicht geladen werden\n\n",
                error->message, NULL );
        g_error_free( error );

        return; //Fenster bleibt geöffnet; je nach Fehler kann man es nochmal versuchen
    }

    sond_client_akte->sond_client->reg_nr_akt = reg_nr;
    sond_client_akte->sond_client->reg_jahr_akt= reg_jahr;

    gtk_entry_set_text( GTK_ENTRY(sond_client_akte->entry_aktenrubrum),
            sond_client_akte->sond_akte->aktenrubrum );
    gtk_entry_set_text( GTK_ENTRY(sond_client_akte->entry_aktenkurzbez),
            sond_client_akte->sond_akte->aktenkurzbez );

    if ( user ) display_message( sond_client_akte->window, "Akte ist zur "
            "Bearbeitung durch Benutzer \n", user, " gesperrt", NULL );

    sond_client_akte_loaded( sond_client_akte, (user) ? FALSE : TRUE );
    g_free( user );
}


static void
sond_client_akte_auswahlfenster_row_activated( GtkListBox* listbox,
        GtkListBoxRow* row, gpointer data )
{
    gint reg_jahr = 0;
    gint reg_nr = 0;

    reg_jahr = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(row), "reg_jahr" ));
    reg_nr = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(row), "reg_nr" ));

    sond_client_akte_load( (SondClientAkte*) data, reg_nr, reg_jahr );

    gtk_widget_destroy( gtk_widget_get_toplevel( GTK_WIDGET(listbox) ) );

    return;
}


static gint
sond_client_akte_auswahlfenster( SondClientAkte* sond_client_akte,
        const gchar* resp, GError** error )
{
    GtkWidget* window = NULL;
    GtkWidget* listbox = NULL;
    JsonParser* jparser = NULL;
    JsonNode* jnode = NULL;
    JsonArray* jarray = NULL;

    window = result_listbox_new( GTK_WINDOW(sond_client_akte->window), "Akten", GTK_SELECTION_BROWSE );

    listbox = g_object_get_data( G_OBJECT(window), "listbox" );
    g_signal_connect( listbox, "row-activated", G_CALLBACK(sond_client_akte_auswahlfenster_row_activated),
            sond_client_akte );

    jparser = json_parser_new( );
    if ( !json_parser_load_from_data( jparser, resp, -1, error ) )
    {
        g_prefix_error( error, "%s\n", __func__ );
        g_object_unref( jparser );
        gtk_widget_destroy( window );

        return -1;
    }

    jnode = json_parser_get_root( jparser );

    if ( !JSON_NODE_HOLDS_ARRAY(jnode) )
    {
        if ( error ) *error = g_error_new( SOND_CLIENT_ERROR, 0,
                "%s\nJson-Knoten ist kein Array", __func__ );
        g_object_unref( jparser );
        gtk_widget_destroy( window );

        return -1;
    }

    jarray = json_node_get_array( jnode );

    for ( guint i = 0; i < json_array_get_length( jarray ); i++ )
    {
        JsonObject* jobject = NULL;
//        gint ID_entity = 0;
        gint reg_jahr = 0;
        gint reg_nr = 0;
        const gchar* aktenrubrum = NULL;
        const gchar* aktenkurzbez = NULL;
        gchar* label_text = NULL;
        GtkWidget* label = NULL;
        GtkListBoxRow* row = NULL;

        jobject = json_array_get_object_element( jarray, i );

//        ID_entity = json_object_get_int_member( jobject, "ID_entity" );
        reg_jahr = json_object_get_int_member( jobject, "reg_jahr" );
        reg_nr= json_object_get_int_member( jobject, "reg_nr" );
        aktenrubrum = json_object_get_string_member( jobject, "aktenrubrum" );
        aktenkurzbez = json_object_get_string_member( jobject, "aktenkurzbez" );

        label_text = g_strdup_printf( " %4i/%i  %s  (%s)",
                reg_nr, reg_jahr % 100, aktenrubrum, aktenkurzbez );
        label = gtk_label_new( label_text );
        gtk_widget_set_halign( label, GTK_ALIGN_START );
        g_free( label_text );

        gtk_list_box_insert( GTK_LIST_BOX(listbox), label, -1 );
        row = gtk_list_box_get_row_at_index( GTK_LIST_BOX(listbox), i );
        g_object_set_data( G_OBJECT(row), "reg_jahr", GINT_TO_POINTER(reg_jahr) );
        g_object_set_data( G_OBJECT(row), "reg_nr", GINT_TO_POINTER(reg_nr) );
    }

    gtk_widget_show_all( window );

    return 0;
}


static void
sond_client_akte_entry_reg_nr_activate( GtkEntry* entry, gpointer data )
{
    SondClientAkte* sond_client_akte = (SondClientAkte*) data;

    if ( strlen( gtk_entry_get_text( entry ) ) < 3 )
    {
        display_message( sond_client_akte->window, "Mindestens drei Zeichen", NULL );

        return;
    }
    else if ( !g_strcmp0( gtk_entry_get_text( entry ), "./." ) ||
            !g_strcmp0( gtk_entry_get_text( entry ), " ./" ) ||
            !g_strcmp0( gtk_entry_get_text( entry ), "/. " ) ||
            !g_strcmp0( gtk_entry_get_text( entry ), " ./. " ) ) return; //wäre Quatsch

    if ( sond_client_misc_regnr_wohlgeformt( gtk_entry_get_text( entry ) ) )
    {
        gint reg_nr = 0;
        gint reg_jahr = 0;

        sond_client_misc_parse_regnr( gtk_entry_get_text( entry ), &reg_nr, &reg_jahr );

        sond_client_akte_load( sond_client_akte, reg_nr, reg_jahr );

    }
    else //text in aktenrubrum suchen
    {
        gchar* resp = NULL;
        GError* error = NULL;
        gint rc = 0;

        resp = sond_client_send_and_read( sond_client_akte->sond_client,
                "AKTE_SUCHEN", gtk_entry_get_text( entry ), &error );
        if ( !resp )
        {
            display_message( sond_client_akte->window, "Fehler\n\n", error->message, NULL );
            g_error_free( error );

            return; //Fenster bleibt geöffnet; je nach Fehler kann man es nochmal versuchen
        }
        else if ( g_str_has_prefix( resp, "ERROR ***" ) )
        {
            display_message( sond_client_akte->window, "Fehler\n\nServer antwortet:\n", resp, NULL );
            g_free( resp );

            return; //Fenster bleibt geöffnet; je nach Fehler kann man es nochmal versuchen
        }
        else if ( g_str_has_prefix( resp, "NOT_FOUND" ) )
        {
            display_message( sond_client_akte->window, "Kein Treffer", NULL );

            return;
        }

        rc = sond_client_akte_auswahlfenster( sond_client_akte, resp, &error );
        if ( rc )
        {
            display_message( sond_client_akte->window,
                    "Auswahlfenster konnte nicht erstellt werden\n\n", error->message, NULL );
            g_error_free( error );

            return;
        }
    }

    return;
}


static void
sond_client_akte_button_neu_clicked( GtkButton* button, gpointer data )
{
    SondClientAkte* sond_client_akte = (SondClientAkte*) data;

    gtk_entry_set_text( GTK_ENTRY(sond_client_akte->entry_reg_nr), "-- Neu --" );

    sond_client_akte->sond_akte = sond_akte_new( );

    sond_client_akte_loaded( sond_client_akte, TRUE );

    return;
}


void
sond_client_akte_init( GtkButton* button, gpointer data )
{
    SondClientAkte* sond_client_akte = NULL;

    sond_client_akte = g_malloc0( sizeof( SondClientAkte ) );
    sond_client_akte->type = SOND_CLIENT_TYPE_AKTE;
    sond_client_akte->sond_client = (SondClient*) data;

    sond_client_akte->window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(sond_client_akte->window), 800, 300 );

    GtkWidget* akten_headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_show_close_button( GTK_HEADER_BAR(akten_headerbar), TRUE );
    gtk_header_bar_set_title( GTK_HEADER_BAR(akten_headerbar), "Aktenfenster" );
    gtk_window_set_titlebar( GTK_WINDOW(sond_client_akte->window), akten_headerbar );

    GtkWidget* grid = gtk_grid_new( );
    gtk_container_add( GTK_CONTAINER(sond_client_akte->window), grid );

    //RegNr
    GtkWidget* frame_regnr = gtk_frame_new( "Register-Nr." );
    sond_client_akte->entry_reg_nr = gtk_entry_new( );
    if ( (sond_client_akte->sond_client->reg_jahr_akt) && (sond_client_akte->sond_client->reg_nr_akt) )
    {
        gchar* text_regnr = g_strdup_printf( "%i/%i", sond_client_akte->sond_client->reg_nr_akt,
                sond_client_akte->sond_client->reg_jahr_akt % 100 );
        gtk_entry_set_text( GTK_ENTRY(sond_client_akte->entry_reg_nr), text_regnr );
        g_free( text_regnr );
    }
    gtk_container_add( GTK_CONTAINER(frame_regnr), sond_client_akte->entry_reg_nr );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(akten_headerbar), frame_regnr );

    //Unlock-Button
    sond_client_akte->button_unlock = gtk_button_new_with_label( "Unlock" );
    gtk_header_bar_pack_end( GTK_HEADER_BAR(akten_headerbar), sond_client_akte->button_unlock );
    gtk_widget_set_sensitive( sond_client_akte->button_unlock, FALSE );

    //Button Neue Akte
    sond_client_akte->button_neu = gtk_button_new_with_label( "Neue Akte" );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(akten_headerbar), sond_client_akte->button_neu );

    //Entry "Aktenrubrum"
    GtkWidget* frame_aktenrubrum = gtk_frame_new( "Rubrum" );
    sond_client_akte->entry_aktenrubrum = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_aktenrubrum), sond_client_akte->entry_aktenrubrum );
    gtk_grid_attach( GTK_GRID(grid), frame_aktenrubrum, 1, 2, 1, 1 );
    gtk_widget_set_sensitive( frame_aktenrubrum, FALSE );

    //Entry "Aktenkurzbez"
    GtkWidget* frame_aktenkurzbez = gtk_frame_new( "Kurzbezeichnung" );
    sond_client_akte->entry_aktenkurzbez = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_aktenkurzbez), sond_client_akte->entry_aktenkurzbez );
    gtk_grid_attach( GTK_GRID(grid), frame_aktenkurzbez, 1, 3, 1, 1 );
    gtk_widget_set_sensitive( frame_aktenkurzbez, FALSE );


    sond_client_akte->button_ok = gtk_button_new_with_label(
            "OK" );
    gtk_widget_set_sensitive( sond_client_akte->button_ok, FALSE );
    gtk_grid_attach( GTK_GRID(grid), sond_client_akte->button_ok, 1, 12, 1, 1 );

    GtkWidget* button_abbrechen = gtk_button_new_with_label(
            "Abbrechen" );
    gtk_grid_attach( GTK_GRID(grid), button_abbrechen, 3, 12, 1, 1 );

    //Signale
    g_signal_connect( sond_client_akte->button_neu, "clicked",
            G_CALLBACK(sond_client_akte_button_neu_clicked), sond_client_akte );

    g_signal_connect( sond_client_akte->entry_reg_nr, "activate",
            G_CALLBACK(sond_client_akte_entry_reg_nr_activate), sond_client_akte );

    g_signal_connect( sond_client_akte->button_unlock, "clicked",
            G_CALLBACK(sond_client_akte_button_unlock_clicked), sond_client_akte );

    g_signal_connect( sond_client_akte->button_ok, "clicked",
            G_CALLBACK(sond_client_akte_button_ok_clicked), sond_client_akte );

    g_signal_connect( button_abbrechen, "clicked",
            G_CALLBACK(sond_client_akte_button_abbrechen_clicked),
            sond_client_akte );

    g_signal_connect( sond_client_akte->window, "delete-event",
            G_CALLBACK(sond_client_akte_delete_event), sond_client_akte );

    gtk_widget_show_all( sond_client_akte->window );

    gtk_widget_grab_focus( sond_client_akte->entry_reg_nr );

    g_ptr_array_add( sond_client_akte->sond_client->arr_children_windows, sond_client_akte );

    return;
}
