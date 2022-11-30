#include "sond_client_dok.h"
#include "sond_client.h"
#include "sond_client_misc.h"

#include <gtk/gtk.h>


void
sond_client_dok_entry_activate( GtkEntry* entry, gpointer data )
{
    gint regnr = 0;
    gint jahr = 0;

    SondClient* sond_client = (SondClient*) data;

    if ( sond_client_misc_regnr_wohlgeformt( gtk_entry_get_text( entry ) ) )
    {
        gint rc = 0;

        sond_client_misc_parse_regnr( gtk_entry_get_text( entry ), &regnr, &jahr );

        if ( sond_client_akte_synchro( sond_client, regnr, jahr ) )
        {
            //zond im Synchro-Verzeichnis öffnen

            return;
        }
        else
        {
            //prüfen, ob online
                //Falls ja: prfen, ob Akte existiert
                    //Falls ja:
                        //ZND auf seadrive öffnen
                        //Nachfrage, ob synchronisiert werden soll
                    //Falls nein: Akte anlegen
                //Falls nein: Fehlermeldung
        }
    }
    else //Text
    {
        //Prüfen, ob online
            //Falls ja:
                //auf Server alle passenden Akten suchen, auswählen,
                //Auf Synchro?
                    //Falls ja: dort öffnen
                    //Falls nein: auf seadrive öffnen
            //Falls nein: Auswahl in syncho suchen u ggf. öffnen
    }

    return;
}
