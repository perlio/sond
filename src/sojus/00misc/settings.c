#include "../globals.h"


GSettings* settings_open( void )
{
    GSettingsSchemaSource* schema_source = NULL;
    GSettingsSchema* schema = NULL;
    GSettings* settings = NULL;
    GError* error = NULL;


    schema_source = g_settings_schema_source_new_from_directory(
            "99Settings/", NULL, FALSE, &error );
    if ( error )
    {
        printf( "%s\n", error->message );
        g_error_free( error );
        return NULL;
    }

    schema = g_settings_schema_source_lookup( schema_source,
            "de.rubarth-krieger.Sojus", FALSE );
    g_settings_schema_source_unref( schema_source );

    settings = g_settings_new_full( schema, NULL, NULL );
    g_settings_schema_unref( schema );

    return settings;
}


void
settings_con_speichern( const gchar* host, gint port, const gchar* user, const
        gchar* password )
{
    GSettings* settings = settings_open( );

    g_settings_set_string( settings, "host", host );
    g_settings_set_int( settings, "port", port );
    g_settings_set_string( settings, "user", user );
    g_settings_set_string( settings, "password", password );

    g_object_unref( settings );

    return;
}
