#include "../globals.h"

void
widgets_aktenbeteiligte_geaendert( GObject* window, gboolean geaendert )
{
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_uebernehmen" )), geaendert );

    return;
}


void
widgets_aktenbeteiligte_bearbeiten( GObject* window, gboolean bearbeiten,
        gboolean waehlen )
{
    //Widgets Aktenbeteiligte
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "entry_adressnr" )), bearbeiten && waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_neue_adresse" )), bearbeiten && waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "combo_beteiligtenart" )), bearbeiten && !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "entry_betreff1" )), bearbeiten && !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "entry_betreff2" )), bearbeiten && !waehlen );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "entry_betreff3" )), bearbeiten && !waehlen );

    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_verwerfen" )), bearbeiten );

    //Abbrechen
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_abbrechen" )), !bearbeiten );

    return;
}


void
widgets_akte_geaendert( GObject* window, gboolean geaendert )
{
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_speichern" )), geaendert );

    return;
}


void
widgets_aktenbeteiligte_vorhanden( GObject* window, gboolean vorhanden )
{
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_aendern" )), vorhanden );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_loeschen" )), vorhanden );

    return;
}


void
widgets_akte_bearbeiten( GObject* window, gboolean bearbeiten, gboolean aktiv )
{
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "entry_bezeichnung" )), bearbeiten && aktiv );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "entry_gegenstand" )), bearbeiten && aktiv  );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "combo_sachgebiete" )), bearbeiten && aktiv  );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "combo_sachbearbeiter" )), bearbeiten && aktiv  );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_ablegen" )), bearbeiten && aktiv  );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_reakt" )), bearbeiten && !aktiv  );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_hinzu" )), bearbeiten && aktiv  );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "listbox_aktenbet" )), bearbeiten && aktiv  );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_aendern" )), bearbeiten && aktiv  );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_loeschen" )), bearbeiten && aktiv  );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_ok" )), bearbeiten && aktiv  );

    return;
}


void
widgets_akte_waehlen( GObject* window, gboolean bearbeiten )
{
    //entry_regnr aktivieren
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "entry_regnr" )), bearbeiten );
    gtk_widget_set_sensitive( GTK_WIDGET(g_object_get_data( window,
            "button_neue_akte" )), bearbeiten );

    return;
}


