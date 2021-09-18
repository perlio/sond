#define MAIN_C

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <mariadb/mysql.h>

#include "../global_types_sojus.h"

#include "aktenschnellansicht.h"
#include "callbacks_akten.h"
#include "callbacks_adressen.h"
#include "callbacks_einstellungen.h"

#include "../00misc/settings.h"

#include "../20Einstellungen/db.h"
#include "../20Einstellungen/einstellungen.h"

#include"../06Dokumente/file_manager.h"

#include "../../misc.h"


static gboolean
cb_desktop_delete_event( GtkWidget* app_window, GdkEvent* event, gpointer data )
{
    Sojus* sojus = (Sojus*) data;

    gtk_widget_destroy( app_window );

    mysql_close( sojus->db.con );

    g_object_unref( sojus->socket );
    g_object_unref( sojus->settings );

    clipboard_free( sojus->clipboard );

    g_ptr_array_unref( sojus->sachgebiete );
    g_ptr_array_unref( sojus->beteiligtenart );
    g_ptr_array_unref( sojus->sachbearbeiter );
    g_ptr_array_unref( sojus->arr_open_fm );

    g_free( sojus );

    return TRUE;
}


