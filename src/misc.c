#include "misc.h"

#include <gtk/gtk.h>

#ifdef _WIN32
#include <windows.h>
//#include <shellapi.h>
#include <shlwapi.h>
#endif // _WIN32


/** Zeigt Fenster, in dem Liste übergebener strings angezeigt wird.
*   parent-window kann NULL sein, dann Warnung
*   text1 darf nicht NULL sein
*   Abschluß der Liste mit NULL
*/
void display_message( GtkWidget* window, const gchar* text1, ... )
{
    va_list ap;
    gchar* message = g_strdup( "" );
    gchar* str = NULL;

    str = (gchar*) text1;
    va_start( ap, text1 );
    while ( str )
    {
        gchar* tmp_str = g_strdup( message );
        g_free( message );
        message = g_strconcat( tmp_str, str, NULL );
        str = va_arg( ap, gchar* );
    }

    GtkWidget* dialog = gtk_message_dialog_new( GTK_WINDOW(window),
            GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,
            GTK_BUTTONS_CLOSE, message );
    g_free( message );

    gtk_widget_show_all( dialog );
    gtk_dialog_run ( GTK_DIALOG (dialog) );

    gtk_widget_destroy( dialog );

    return;
}


static void
cb_entry_text( GtkEntry* entry, gpointer data )
{
    gtk_widget_grab_focus( (GtkWidget*) data );

    return;
}


/** ... response_id, next_button_text, next_response_id, ..., NULL
**/
gint
dialog_with_buttons( GtkWidget* window, const gchar* message,
        const gchar* secondary, gchar** text, gchar* first_button_text, ... )
{
    gint res;
    GtkWidget* entry = NULL;
    va_list arg_pointer;
    gchar* button_text = NULL;
    GtkWidget* button = NULL;
    gboolean first_button = TRUE;

    va_start( arg_pointer, first_button_text );

    GtkWidget* dialog = gtk_message_dialog_new( GTK_WINDOW(window),
            GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_NONE, message, NULL );
    gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG(dialog), "%s",
            secondary );

    //buttons einfügen
    button_text = first_button_text;
    while ( button_text )
    {
        GtkWidget* tmp = NULL;
        gint response_id = 0;

        response_id = va_arg( arg_pointer, gint );

        tmp = gtk_dialog_add_button( GTK_DIALOG(dialog), button_text, response_id );
        if ( first_button )
        {
            button = tmp;
            first_button = FALSE;
        }

        button_text = va_arg( arg_pointer, gchar* );
    }

    if ( text )
    {
        GtkWidget* content = gtk_message_dialog_get_message_area( GTK_MESSAGE_DIALOG(dialog) );
        entry = gtk_entry_new( );
        gtk_container_add( GTK_CONTAINER(content), entry);
        if ( *text ) gtk_entry_set_text( GTK_ENTRY(entry), *text );
        g_free( *text );
        *text = NULL;

        gtk_widget_show_all( content );

        g_signal_connect( entry, "activate", G_CALLBACK(cb_entry_text),
                (gpointer) button );
    }

    res = gtk_dialog_run( GTK_DIALOG(dialog) );

    if ( text ) *text = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry) ) );

    gtk_widget_destroy( dialog );

    return res;
}


/** wrapper
**/
gint
abfrage_frage( GtkWidget* window, const gchar* message, const gchar* secondary, gchar** text )
{
    gint res;

    res = dialog_with_buttons( window, message, secondary, text, "Ja",
            GTK_RESPONSE_YES, "Nein", GTK_RESPONSE_NO, NULL );

    return res;
}


gint ask_question(GtkWidget* window, const gchar* title, const gchar* ja, const
        gchar* nein)
{
    gint res;
    GtkWidget* dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(window),
            GTK_DIALOG_DESTROY_WITH_PARENT, ja, GTK_RESPONSE_YES, nein,
            GTK_RESPONSE_NO, "_Abbrechen", GTK_RESPONSE_CANCEL, NULL);

    res = gtk_dialog_run( GTK_DIALOG(dialog) );
    gtk_widget_destroy(dialog);

    return res;
}


gint
allg_string_array_index_holen( GPtrArray* array, gchar* element )
{
    for ( gint i = 0; i < array->len; i++ ) if ( !g_strcmp0( g_ptr_array_index(
            array, i ), element ) ) return i;

    return -1;
}


gchar*
add_string( gchar* old_string, gchar* add_string )
{
    gchar* new_string = NULL;
    if ( old_string ) new_string = g_strconcat( old_string, add_string, NULL );
    else new_string = g_strdup( add_string );
    g_free( old_string );
    g_free( add_string );

    return new_string;
}


GSList*
choose_files( const GtkWidget* window, const gchar* path, const gchar* title_text, gchar* accept_text,
        gint action, gboolean multiple )
{
    GtkWidget *dialog = NULL;
    gint rc = 0;
    gchar* dir_start = NULL;
    GSList* list = NULL;

    dialog = gtk_file_chooser_dialog_new( title_text,
            GTK_WINDOW(window), action, "_Abbrechen",
            GTK_RESPONSE_CANCEL, accept_text, GTK_RESPONSE_ACCEPT, NULL);

    if ( !path || !g_strcmp0( path, "" ) ) dir_start = g_get_current_dir( );
    else dir_start = g_strdup( path );

    gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER(dialog), dir_start );
    g_free( dir_start );
    gtk_file_chooser_set_select_multiple( GTK_FILE_CHOOSER(dialog), multiple );
    gtk_file_chooser_set_do_overwrite_confirmation( GTK_FILE_CHOOSER(dialog),
            TRUE );
    if ( action == GTK_FILE_CHOOSER_ACTION_SAVE )
            gtk_file_chooser_set_current_name( GTK_FILE_CHOOSER(dialog),
            ".ZND" );

    rc = gtk_dialog_run( GTK_DIALOG(dialog) );
    if ( rc == GTK_RESPONSE_ACCEPT ) list = gtk_file_chooser_get_uris( GTK_FILE_CHOOSER(dialog) );

    //Dialog schließen
    gtk_widget_destroy(dialog);

    return list;
}


/*
 *  Gibt "...xxx\" zurück
 */
gchar*
get_path_from_base( const gchar* path, gchar** errmsg )
{
    gchar* base_dir = NULL;
    gchar* path_from_base = NULL;

#ifdef _WIN32
    DWORD ret = 0;
    TCHAR buff[MAX_PATH] = { 0 };

    ret = GetModuleFileName(NULL, buff, _countof(buff));
    if ( !ret && errmsg )
    {
        DWORD error_code = 0;

        error_code = GetLastError( );

        *errmsg = add_string( *errmsg, g_strdup_printf( "Bei Aufruf GetModuleFileName:\n"
                "Error Code: %li", error_code ) );
        return NULL;
    }

    base_dir = g_strndup( (const gchar*) buff, strlen( buff ) -
            strlen( g_strrstr( (const gchar*) buff, "\\" ) ) - 3 );
#elif defined( __linux__ )
    gchar buff[PATH_MAX] = { 0 };
    gchar proc[PATH_MAX] = { 0 };
    pid_t pid = getpid();
    sprintf( proc, "/proc/%d/exe", pid );

    if ( readlink( proc, buff, PATH_MAX ) == -1 )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf readling:\n",
                strerror( errno ), NULL );

        return NULL;
    }

    base_dir = g_strndup( (const gchar*) buff, strlen( buff ) -
            strlen( g_strrstr( (const gchar*) buff, "\\" ) ) - 3 );
#endif // _WIN32

    path_from_base = add_string( base_dir, g_strdup( path ) );

    return path_from_base;
}


gchar*
get_rel_path_from_file( const gchar* root, const GFile* file )
{
    if ( !file ) return NULL;

    //Überprüfung, ob schon angebunden
    gchar* rel_path = NULL;
    gchar* abs_path = g_file_get_path( (GFile*) file );

#ifdef _WIN32
    abs_path = g_strdelimit( abs_path, "\\", '/' );
#endif // _WIN32

    rel_path = g_strdup( abs_path + ((root) ? strlen( root ) + 1 : 0) );
    g_free( abs_path );

    return rel_path; //muß freed werden
}


void
misc_set_calendar( GtkCalendar* calendar, const gchar* date )
{
    if ( !date ) return;

    gchar* year_text = g_strndup( date, 4 );
    gint year = g_ascii_strtoll( year_text, NULL, 10 );
    g_free( year_text );

    gchar* month_text = g_strndup( date + 5, 2 );
    gint month = g_ascii_strtoll( month_text, NULL, 10 );
    g_free( month_text );

    gchar* day_text = g_strdup( date + 8 );
    gint day = g_ascii_strtoll( day_text, NULL, 10 );
    g_free( day_text );

    gtk_calendar_select_month( calendar, month - 1, year );
    gtk_calendar_select_day( calendar, 0 );
    gtk_calendar_mark_day( calendar, day );

    return;
}


gchar*
misc_get_calendar( GtkCalendar* calendar )
{
    guint year = 0;
    guint month = 0;
    guint day = 0;
    gchar* string = NULL;

    gtk_calendar_get_date( calendar, &year, &month, &day );

    string = g_strdup_printf( "%04d-%02d-%02d", year, month + 1, day );

    return string;
}





