#include <gtk/gtk.h>

#include "../global_types_sojus.h"

#include "../../misc.h"

#include "../20Einstellungen/db.h"
#include "../20Einstellungen/sachbearbeiterverwaltung.h"


void
cb_button_db_erstellen_clicked( GtkButton* button, gpointer user_data )
{
//    db_create( (Sojus*) user_data );

    return;
}


void
cb_button_db_waehlen_clicked( GtkButton* button, gpointer user_data )
{
 //   db_select( (Sojus*) user_data );

    return;
}


void
cb_button_db_con_clicked( GtkButton* button, gpointer user_data )
{
//    db_connection_window( (Sojus*) user_data );

    return;
}


void
cb_button_sachbearbeiterverwaltung( GtkButton* button, gpointer user_data )
{
    sachbearbeiterfenster_oeffnen( (Sojus*) user_data );
    sachbearbeiterfenster_fuellen( (Sojus*) user_data );

    return;

}


void
cb_button_dokument_dir( GtkButton* button, gpointer data )
{
    gchar* path = NULL;
    GSList* list = NULL;

    Sojus* sojus = (Sojus*) data;

    path = g_settings_get_string( sojus->settings, "dokument-dir" );

    list = choose_files( sojus->app_window, path, "Dokumentenverzeichnis auswählen", "Ok", GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER, FALSE );
    if ( list )
    {
        gchar* uri_unescaped = g_uri_unescape_string( list->data, NULL );

        g_slist_free_full( list, g_free );

        g_settings_set_string( sojus->settings, "dokument-dir", uri_unescaped + 8 );
    }

    return;

}
