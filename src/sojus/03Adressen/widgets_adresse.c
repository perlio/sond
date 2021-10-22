#include "../globals.h"


void
widgets_adresse_geaendert( GObject* window, gboolean geaendert )
{
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_speichern" )), geaendert );

    return;
}


void
widgets_adresse_waehlen( GObject* window, gboolean waehlen )
{
    gtk_widget_activate_get_sensitive( GTK_WIDGET(g_object_get_data(G_OBJECT(window), "grid" )), waehlen );
        /*
    //alles ausgrauen außer entry_adressnr
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_adressnr" )), waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "button_neue_adresse" )), waehlen );

    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_adresszeile1" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_titel" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_vorname" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_name" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_adresszusatz" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_strasse" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_hausnr" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_plz" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_ort" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_land" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_telefon1" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_telefon2" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_telefon3" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_fax" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_email" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_homepage" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_iban" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_bic" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "entry_anrede" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "textview_bemerkungen" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "button_ok" )), !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( G_OBJECT(window),
            "button_speichern" )), !waehlen );
*/
    return;
}
