#include "../sojus_init.h"

#include "../00misc/auswahl.h"
#include "../../misc.h"
#include "../02Akten/akten.h"

#include "../../sond_treeviewfm.h"

#include <gtk/gtk.h>


#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32


static void
cb_bu_doc_erzeugen( GtkButton* button, gpointer data )
{
    Sojus* sojus = (Sojus*) data;

    //Pfad LibreOffice herausfinden
    gchar soffice_exe[270] = { 0 };
    GError* error = NULL;

#ifdef _WIN32
    HRESULT rc = 0;

    DWORD bufferlen = 270;

    rc = AssocQueryString( 0, ASSOCSTR_EXECUTABLE, ".odt", "open", soffice_exe,
            &bufferlen );
    if ( rc != S_OK )
    {
        display_message( sojus->app_window, "Fehler in doc erzeugen: assoc", NULL );
        return;
    }
#else
    //für Linux etc: Pfad von soffice suchen
#endif // _WIN32
    gboolean ret = FALSE;

    gchar* argv[6] = { NULL };
    argv[0] = soffice_exe;
    argv[1] = "C:/Users/pkrieger/AppData/Roaming/LibreOffice/4/user/template/vorlagen/Briefkopf.ott";
    argv[2] = "macro:///Standard.Module.Dokument_erzeugen(2,2020,3,0,0)";

    ret = g_spawn_async( NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL,
            &error );
    if ( !ret )
    {
        display_message( sojus->app_window, "Fehler bei Dokument erzeugen",
                error->message, NULL );
        g_error_free( error );
    }

    return;
}

