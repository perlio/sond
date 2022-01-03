#include <stdlib.h>

#include <gtk/gtk.h>
#include <ctype.h>

#include "../zond_pdf_document.h"
#include "../zond_tree_store.h"

#include "../global_types.h"
#include "../error.h"

#include "../../misc.h"
#include "../../dbase.h"

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

    while ( anchor_id != 0 )
    {
        gint rc = 0;

        rc = db_get_rel_path( zond, baum, anchor_id, NULL, errmsg );
        if ( rc == -1 ) ERROR_PAO( "db_get_rel_path" )
        else if ( rc == 0 ) return 1; //nicht mal datei!

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
    GtkTreeIter* iter = NULL;

    //kind verschieben
    rc = db_verschieben_knoten( zond, baum, node_id, new_parent,
            new_older_sibling, errmsg );
    if ( rc ) ERROR_PAO (" db_verschieben_knoten" )

    //Knoten im tree löschen
    //hierzu iter des verschobenen Kindknotens herausfinden
    iter = baum_abfragen_iter( zond->treeview[baum], node_id );

    zond_tree_store_remove( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(zond->treeview[baum]) ) ), iter );
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
    rc = db_baum_knoten_mit_kindern( zond, FALSE,
            baum, node_id, iter, kind, NULL, errmsg );
    gtk_tree_iter_free( iter );

    if ( rc ) ERROR_SOND( "db_baum_knoten_mit_kindern" )

    return 0;
}


static gint
general_get_page_num_from_dest_doc( fz_context* ctx, pdf_document* doc, const gchar* dest, gchar** errmsg )
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
general_get_page_num_from_dest( fz_context* ctx, const gchar* rel_path,
        const gchar* dest, gchar** errmsg )
{
    pdf_document* doc = NULL;
    gint page_num = 0;

    fz_try( ctx ) doc = pdf_open_document( ctx, rel_path );
    fz_catch( ctx ) ERROR_MUPDF( "fz_open_document" )

    page_num = general_get_page_num_from_dest_doc( ctx, doc, dest, errmsg );
	pdf_drop_document( ctx, doc );
    if ( page_num < 0 ) ERROR_PAO( "get_page_num_from_dest_doc" )

    return page_num;
}


/** Gibt nur bei Fehler NULL zurück, sonst immer Zeiger auf Anbindung **/
static Anbindung*
ziel_zu_anbindung( fz_context* ctx, const gchar* rel_path, Ziel* ziel, gchar** errmsg )
{
    gint page_num = 0;

    Anbindung* anbindung = g_malloc0( sizeof( Anbindung ) );

    page_num = general_get_page_num_from_dest( ctx, rel_path, ziel->ziel_id_von, errmsg );
    if ( page_num == -1 )
    {
        g_free( anbindung );
        ERROR_PAO_R( "general_get_page_num_from_dest", NULL )
    }
    else if ( page_num == -2 )
    {
        if ( errmsg ) *errmsg = g_strdup( "NamedDest nicht in Dokument vohanden" );
        g_free( anbindung );

        return NULL;
    }
    else anbindung->von.seite = page_num;

    page_num = general_get_page_num_from_dest( ctx, rel_path, ziel->ziel_id_bis,
            errmsg );
    if ( page_num == -1 )
    {
        g_free( anbindung );

        ERROR_PAO_R( "general_get_page_num_from_dest", NULL )
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
abfragen_rel_path_and_anbindung( Projekt* zond, Baum baum, gint node_id,
        gchar** rel_path, Anbindung** anbindung, gchar** errmsg )
{
    gint rc = 0;
    Ziel* ziel = NULL;
    gchar* rel_path_intern = NULL;
    Anbindung* anbindung_intern = NULL;

    rc = db_get_rel_path( zond, baum, node_id, &rel_path_intern, errmsg );
    if ( rc == -1 ) ERROR_PAO( "db_get_rel_path" )
    else if ( rc == 1 ) return 2;

    rc = db_get_ziel( zond, baum, node_id, &ziel, errmsg );
    if ( rc == -1 )
    {
        g_free( rel_path_intern );
        ERROR_PAO( "db_get_ziel" )
    }
    else if ( rc == 1 )
    {
        if ( rel_path ) *rel_path = rel_path_intern;
        else g_free( rel_path_intern );

        return 1;
    }

    const ZondPdfDocument* zond_pdf_document = zond_pdf_document_is_open( rel_path_intern );
    if ( zond_pdf_document ) zond_pdf_document_mutex_lock( zond_pdf_document );

    anbindung_intern = ziel_zu_anbindung( zond->ctx, rel_path_intern, ziel, errmsg );

    if ( zond_pdf_document ) zond_pdf_document_mutex_unlock( zond_pdf_document );

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

    return info_window;
}

