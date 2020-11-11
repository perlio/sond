#include "../globals.h"


void
widgets_desktop_db_con( GObject* window, gboolean aktiv )
{
    GtkWidget* bu_db_waehlen = g_object_get_data( window, "bu_db_waehlen" );
    GtkWidget* bu_db_erstellen = g_object_get_data( window, "bu_db_erstellen" );

    gtk_widget_set_sensitive( bu_db_erstellen, aktiv );
    gtk_widget_set_sensitive( bu_db_waehlen, aktiv );

    return;
}


void
widgets_desktop_db_name( Sojus* sojus, gboolean aktiv )
{
    GtkWidget* bu_akte_fenster = g_object_get_data( G_OBJECT(sojus->app_window), "bu_akte_fenster" );
    GtkWidget* bu_akte_suchen = g_object_get_data( G_OBJECT(sojus->app_window), "bu_akte_suchen" );

    GtkWidget* bu_adresse_fenster = g_object_get_data( G_OBJECT(sojus->app_window), "bu_adresse_fenster" );
    GtkWidget* bu_adresse_suchen = g_object_get_data( G_OBJECT(sojus->app_window), "bu_adresse_suchen" );

    GtkWidget* bu_kalender = g_object_get_data( G_OBJECT(sojus->app_window), "bu_kalender" );
    GtkWidget* bu_termine_zur_akte = g_object_get_data( G_OBJECT(sojus->app_window), "bu_termine_zur_akte" );
    GtkWidget* bu_fristen = g_object_get_data( G_OBJECT(sojus->app_window), "bu_fristen" );
    GtkWidget* bu_wiedervorlagen = g_object_get_data( G_OBJECT(sojus->app_window), "bu_wiedervorlagen" );

    GtkWidget* bu_sachbearbeiterverwaltung = g_object_get_data( G_OBJECT(sojus->app_window),
            "bu_sachbearbeiterverwaltung" );

    gtk_widget_set_sensitive( bu_akte_fenster, aktiv );
    gtk_widget_set_sensitive( bu_akte_suchen, aktiv );

    gtk_widget_set_sensitive( bu_adresse_fenster, aktiv );
    gtk_widget_set_sensitive( bu_adresse_suchen, aktiv );

    gtk_widget_set_sensitive( bu_kalender, aktiv );
    gtk_widget_set_sensitive( bu_termine_zur_akte, aktiv );
    gtk_widget_set_sensitive( bu_fristen, aktiv );
    gtk_widget_set_sensitive( bu_wiedervorlagen, aktiv );

    gtk_widget_set_sensitive( bu_sachbearbeiterverwaltung, aktiv );

    gtk_widget_set_sensitive( sojus->widgets.AppWindow.AktenSchnellansicht.button_doc_erzeugen, aktiv );

    return;
}


void
widgets_desktop_label_con( GObject* window, const gchar* host, gint port, const
        gchar* user )
{
    GtkWidget* label_con = g_object_get_data( window, "label_con" );

    gchar* text_con = g_strdup_printf( "Host: %s\nPort: %i\nUser: %s", host,
            port, user );

    gtk_label_set_text( GTK_LABEL(label_con), text_con );
    g_free( text_con );

    return;
}


void
widgets_desktop_label_db( GObject* window, const gchar* db_name )
{
    GtkWidget* label_db = g_object_get_data( window, "label_db" );

    gtk_label_set_text( GTK_LABEL(label_db), db_name );

    return;
}

