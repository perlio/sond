#include <stdlib.h>

#include <gtk/gtk.h>
#include <ctype.h>

#include "../global_types.h"
#include "../error.h"

#include "../../misc.h"
#include "../../fm.h"

#include "../99conv/baum.h"
#include "../99conv/db_read.h"
#include "../99conv/db_write.h"
#include "../99conv/db_zu_baum.h"
#include "../99conv/pdf.h"

#include "../40viewer/document.h"



/** String Utilities **/
gchar*
utf8_to_local_filename( const gchar* utf8 )
{
    //utf8 in filename konvertieren
    gsize written;
    gchar* charset = g_get_codeset();
    gchar* local_filename = g_convert( utf8, -1, charset, "UTF-8", NULL, &written,
            NULL );
    g_free( charset );

    return local_filename; //muß g_freed werden!
}


gint
string_to_guint( const gchar* string, guint* zahl )
{
    gboolean is_guint = TRUE;
    if ( !strlen( string ) ) is_guint = FALSE;
    gint i = 0;
    while ( i < (gint) strlen( string ) && is_guint )
    {
        if ( !isdigit( (int) *(string + i) ) ) is_guint = FALSE;
        i++;
    }

    if ( is_guint )
    {
        *zahl = atoi( string );
        return 0;
    }
    else return -1;
}


gchar*
prepend_string( gchar* old_string, gchar* prepend )
{
    gchar* new_string = g_strconcat( prepend, old_string, NULL );
    g_free( old_string );
    g_free( prepend );

    return new_string;
}



/** Andere Sachen **/
gchar*
filename_speichern( GtkWindow* window, const gchar* titel )
{
    GSList* list = choose_files( GTK_WIDGET(window), NULL, titel, "Speichern",
            GTK_FILE_CHOOSER_ACTION_SAVE, FALSE );

    if ( !list ) return NULL;

    gchar* uri_unescaped = g_uri_unescape_string( list->data, NULL );
    g_free( list->data );
    g_slist_free( list );

    gchar* abs_path = g_strdup( uri_unescaped + 8 );
    g_free( uri_unescaped );

    return abs_path; //muß g_freed werden
}


gchar*
filename_oeffnen( GtkWindow* window )
{
    GSList* list = choose_files( GTK_WIDGET(window), NULL, "Datei auswählen", "Öffnen",
            GTK_FILE_CHOOSER_ACTION_OPEN, FALSE );

    if ( !list ) return NULL;

    gchar* uri_unescaped = g_uri_unescape_string( list->data, NULL );
    g_free( list->data );
    g_slist_free( list );

    gchar* abs_path = g_strdup( uri_unescaped + 8);
    g_free( uri_unescaped );

    return abs_path; //muß g_freed werden
}


/*
**  Zeigt Fenster mit Liste übergebener Strings.
**  Muß mit NULL abgeschlossen werden.
**  text1 darf nicht NULL sein!  */
void
meldung( GtkWidget* window, const gchar* text1, ... )
{
    va_list ap;
    gchar* message = g_strdup( "" );
    gchar* str = NULL;

    str = (gchar*) text1;
    va_start( ap, text1 );
    while ( str )
    {
        message = add_string( message, g_strdup( str ) );
        str = va_arg( ap, gchar* );
    }

    GtkWidget* dialog = gtk_message_dialog_new( GTK_WINDOW(window),
            GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,
            GTK_BUTTONS_CLOSE, message );
    gtk_dialog_run ( GTK_DIALOG (dialog) );
    gtk_widget_destroy( dialog );

    g_free( message );

    return;
}


/** Gibt nur bei Fehler NULL zurück, sonst immer Zeiger auf Anbindung **/
static Anbindung*
ziel_zu_anbindung( fz_context* ctx, const gchar* rel_path, Ziel* ziel, gchar** errmsg )
{
    gint page_num = 0;

    Anbindung* anbindung = g_malloc0( sizeof( Anbindung ) );

    page_num = pdf_get_page_num_from_dest( ctx, rel_path, ziel->ziel_id_von, errmsg );
    if ( page_num == -1 )
    {
        g_free( anbindung );
        ERROR_PAO_R( "pdf_get_page_num_from_dest", NULL )
    }
    else if ( page_num == -2 )
    {
        if ( errmsg ) *errmsg = g_strdup( "NamedDest nicht in Dokument vohanden" );
        g_free( anbindung );

        return NULL;
    }
    else anbindung->von.seite = page_num;

    page_num = pdf_get_page_num_from_dest( ctx, rel_path, ziel->ziel_id_bis,
            errmsg );
    if ( page_num == -1 )
    {
        g_free( anbindung );

        ERROR_PAO_R( "pdf_get_page_num_from_dest", NULL )
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


void
ziele_free( Ziel* ziel )
{
    if ( !ziel ) return;

    g_free( ziel->ziel_id_von );
    g_free( ziel->ziel_id_bis );

    g_free( ziel );

    return;
}


#ifndef VIEWER
/** Wenn Fehler: -1
    Wenn Vorfahre Datei ist: 1
    ansonsten: 0 **/
gint
hat_vorfahre_datei( Projekt* zond, Baum baum, gint anchor_id, gboolean child, gchar** errmsg )
{
    if ( !child )
    {
        anchor_id = db_get_parent( zond, baum, anchor_id, errmsg );
        if ( anchor_id < 0 ) ERROR_PAO( "db_get_parent" )
    }

    gint rc = 0;
    while ( anchor_id != 0 )
    {
        rc = db_knotentyp_abfragen( zond, baum, anchor_id, errmsg );
        if ( rc == -1 ) ERROR_PAO( "db_knotentyp_abfragen" )

        if ( rc > 0 ) return 1;

        anchor_id = db_get_parent( zond, baum, anchor_id, errmsg );
        if ( anchor_id < 0 ) ERROR_PAO( "db_get_parent" )
    }

    return 0;
}


gint
knoten_verschieben( Projekt* zond, Baum baum, gint node_id, gint new_parent,
        gint new_older_sibling, gchar** errmsg )
{
    gint rc = 0;

    //kind verschieben
    rc = db_verschieben_knoten( zond, baum, node_id, new_parent,
            new_older_sibling, errmsg );
    if ( rc ) ERROR_PAO (" db_verschieben_knoten" )

    //Knoten im tree löschen
    //hierzu iter des verschobenen Kindknotens herausfinden
    GtkTreeIter* iter = NULL;
    iter = baum_abfragen_iter( zond->treeview[baum], node_id );

    gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model(
            zond->treeview[baum] ) ), iter );
    gtk_tree_iter_free( iter );

    //jetzt neuen kindknoten aus db erzeugen
    //hierzu zunächst iter des Anker-Knotens ermitteln
    gboolean kind = FALSE;
    if ( new_older_sibling )
    {
        iter = baum_abfragen_iter( zond->treeview[baum], new_older_sibling );
        kind = FALSE;
    }
    else
    {
        iter = baum_abfragen_iter( zond->treeview[baum], new_parent );
        kind = TRUE;
    }

    //Knoten erzeugen
    GtkTreeIter* iter_anker = db_baum_knoten_mit_kindern( zond, FALSE,
            baum, node_id, iter, kind, errmsg );
    gtk_tree_iter_free( iter );

    if ( !iter_anker ) ERROR_PAO( "db_baum_knoten_mit_kindern" )

    gtk_tree_iter_free( iter_anker );

    return 0;
}


/** Keine Datei mit node_id verknüpft: 2
    Kein ziel mit node_id verknüpft: 1
    Datei und ziel verknüpft: 0
    Fehler (inkl. node_id existiert nicht): -1

    Funktion sollte thread-safe sein! **/
gint
abfragen_rel_path_and_anbindung( Projekt* zond, Baum baum, gint node_id,
        gchar** rel_path, Anbindung** anbindung, gchar** errmsg )
{
    gint rc = 0;
    Ziel* ziel = NULL;
    gchar* rel_path_intern = NULL;
    Anbindung* anbindung_intern = NULL;

    rc = db_get_rel_path( zond, baum, node_id, &rel_path_intern, errmsg );
    if ( rc == -1 ) ERROR_PAO( "db_get_rel_path" )
    else if ( rc == -2 ) return 2;

    rc = db_get_ziel( zond, baum, node_id, &ziel, errmsg );
    if ( rc )
    {
        g_free( rel_path_intern );
        ERROR_PAO( "db_get_ziel" )
    }

    if ( !ziel )
    {
        if ( rel_path ) *rel_path = rel_path_intern;
        else g_free( rel_path_intern );

        return 1;
    }

    Document* document = document_geoeffnet( zond, rel_path_intern );
    if ( document ) g_mutex_lock( &document->mutex_doc );

    anbindung_intern = ziel_zu_anbindung( zond->ctx, rel_path_intern, ziel, errmsg );

    if ( document ) g_mutex_unlock( &document->mutex_doc );

    ziele_free( ziel );

    if ( !anbindung_intern )
    {
        g_free( rel_path_intern );
        ERROR_PAO( "ziel_zu_anbindung" )
    }

    if ( rel_path ) *rel_path = rel_path_intern;
    else g_free( rel_path_intern );

    if ( anbindung ) *anbindung = anbindung_intern;
    else g_free( anbindung_intern );

    return 0;
}


gint
test_rel_path( const GFile* file, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gchar* rel_path = NULL;

    Projekt* zond = (Projekt*) data;

    rel_path = fm_get_rel_path_from_file( zond->project_dir, file );

    rc = db_check_rel_path( zond, rel_path, errmsg );
    g_free( rel_path );

    if ( rc == -1 ) ERROR_PAO( "db_get_node_id_from_rel_path" )
    else if ( rc > 0 ) return 1;

    return 0;
}


gint
update_db_before_path_change( const GFile* file_source, const GFile* file_dest,
        gpointer data, gchar** errmsg )
{
    gint rc = 0;

    Projekt* zond = (Projekt*) data;

    rc = db_begin_both( zond, errmsg );
    if ( rc == -1 ) ERROR_PAO( "db_begin_both" )
    else if ( rc == -2 ) ERROR_PAO_ROLLBACK( "db_begin_both" )

    gchar* rel_path_source = fm_get_rel_path_from_file( zond->project_dir, file_source );
    gchar* rel_path_dest = fm_get_rel_path_from_file( zond->project_dir, file_dest );

    rc = db_update_path( zond, rel_path_source, rel_path_dest, errmsg );

    g_free( rel_path_source );
    g_free( rel_path_dest );

    if ( rc ) ERROR_PAO_ROLLBACK_BOTH( "db_update_path" );

    return 0;
}


gint
update_db_after_path_change( const gint rc_edit, gpointer data, gchar** errmsg )
{
    gint rc = 0;

    Projekt* zond = (Projekt*) data;

    if ( rc_edit == 1 )
    {
        rc = db_rollback_both( zond, errmsg );
        if ( rc ) ERROR_PAO( "db_rollback_both" )
    }
    else
    {
        rc = db_commit_both( zond, errmsg );
        if ( rc ) ERROR_PAO_ROLLBACK_BOTH( "db_commit_both" )
    }

    return 0;
}
#endif // VIEWER


gboolean
is_pdf( const gchar* path )
{
    gchar* content_type = NULL;
    gboolean res = FALSE;

    content_type = g_content_type_guess( path, NULL, 0, NULL );

    //Sonderbehandung, falls pdf-Datei
    if ( (!g_strcmp0( content_type, ".pdf" ) || !g_strcmp0( content_type,
            "application/pdf" )) ) res = TRUE;
    g_free( content_type );

    return res;
}


void
info_window_close( InfoWindow* info_window )
{
    GtkWidget* button =
            gtk_dialog_get_widget_for_response( GTK_DIALOG(info_window->dialog),
            GTK_RESPONSE_CANCEL );
    gtk_button_set_label( GTK_BUTTON(button), "Schließen" );
    gtk_widget_grab_focus( button );

    gtk_dialog_run( GTK_DIALOG(info_window->dialog) );

    gtk_widget_destroy( info_window->dialog );

    g_free( info_window );

    return;
}


void
info_window_scroll( InfoWindow* info_window )
{
    GtkWidget* viewport = NULL;
    GtkWidget* swindow = NULL;
    GtkAdjustment* adj = NULL;

    viewport = gtk_widget_get_parent( info_window->content);
    swindow = gtk_widget_get_parent( viewport );
    adj = gtk_scrolled_window_get_vadjustment( GTK_SCROLLED_WINDOW(swindow) );
    gtk_adjustment_set_value( adj, gtk_adjustment_get_upper( adj ) );

    return;
}


void
info_window_set_message( InfoWindow* info_window, const gchar* message )
{
    GtkWidget* label = NULL;

    label = gtk_label_new( message );
    gtk_widget_set_halign( label, GTK_ALIGN_START );
    gtk_box_pack_start( GTK_BOX(info_window->content), label, FALSE, FALSE, 0 );
    gtk_widget_show_all( label );

    while ( gtk_events_pending( ) ) gtk_main_iteration( );

    info_window_scroll( info_window );

    return;
}


static gboolean
cb_info_window_delete_event( GtkWidget* widget, gpointer data )
{
    gtk_dialog_response( GTK_DIALOG(widget), GTK_RESPONSE_CANCEL );

    return TRUE;
}


static void
cb_info_window_response( GtkDialog* dialog, gint id, gpointer data )
{
    InfoWindow* info_window = (InfoWindow*) data;

    if ( info_window->cancel ) return;

    info_window_set_message( info_window, "...abbrechen" );
    info_window->cancel = TRUE;

    return;
}


InfoWindow*
info_window_open( GtkWidget* window, const gchar* title )
{
    GtkWidget* content = NULL;
    GtkWidget* swindow = NULL;

    InfoWindow* info_window = g_malloc0( sizeof( InfoWindow ) );

    info_window->dialog = gtk_dialog_new_with_buttons( title, GTK_WINDOW(window),
            GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL, "Abbrechen",
            GTK_RESPONSE_CANCEL, NULL );

    gtk_window_set_default_size( GTK_WINDOW(info_window->dialog), 450, 110 );

    content = gtk_dialog_get_content_area( GTK_DIALOG(info_window->dialog) );
    swindow = gtk_scrolled_window_new( NULL, NULL );
    gtk_box_pack_start( GTK_BOX(content), swindow, TRUE, TRUE, 0 );

    info_window->content = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_container_add( GTK_CONTAINER(swindow), info_window->content );

    gtk_widget_show_all( info_window->dialog );

    g_signal_connect( GTK_DIALOG(info_window->dialog), "response",
            G_CALLBACK(cb_info_window_response), info_window );
    g_signal_connect( info_window->dialog, "delete-event",
            G_CALLBACK(cb_info_window_delete_event), NULL );

    return info_window;
}

