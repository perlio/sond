#include "../globals.h"


void
cb_button_adresse_fenster_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* adressen_window = adressenfenster_oeffnen( (Sojus*) user_data );

    widgets_adresse_waehlen( G_OBJECT(adressen_window), TRUE );

    gtk_widget_grab_focus( GTK_WIDGET(g_object_get_data( G_OBJECT(adressen_window),
            "entry_adressnr" )) );

    gtk_widget_show_all( adressen_window );

    return;
}


void
cb_bu_adresse_suchen_clicked( GtkButton* button, gpointer user_data )
{

    return;
}
