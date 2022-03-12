#include "../global_types.h"

#include <gtk/gtk.h>

#include "../../sond_treeview.h"


Baum
baum_abfragen_aktiver_treeview( Projekt* zond )
{
    Baum baum = KEIN_BAUM;

    if ( gtk_widget_is_focus( GTK_WIDGET(zond->treeview[BAUM_FS]) ) ) baum =
            BAUM_FS;
    if ( gtk_widget_is_focus( GTK_WIDGET(zond->treeview[BAUM_INHALT]) ) ) baum =
            BAUM_INHALT;
    if ( gtk_widget_is_focus( GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]) ) ) baum =
            BAUM_AUSWERTUNG;

    return baum;
}


