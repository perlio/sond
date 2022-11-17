

static void
activate_app( GtkApplication* app, gpointer data )
{
    return;
}


static void
startup_app( GtkApplication* app, gpointer data )
{
    Sojus** sojus = (Sojus**) data;

    *sojus = sojus_init( app );
    if ( !(*sojus) ) return;

    return;
}


gint
main( int argc, char **argv)
{
    GtkApplication* app = NULL;
    Sojus* sojus = NULL;
    gint status = 0;

    //ApplicationApp erzeugen
    app = gtk_application_new ( "de.rubarth-krieger.sond_client", G_APPLICATION_FLAGS_NONE );

    //und starten
    g_signal_connect( app, "startup", G_CALLBACK(startup_app), &sojus );
    g_signal_connect( app, "activate", G_CALLBACK (activate_app), &sojus );

    status = g_application_run( G_APPLICATION (app), argc, argv );

    g_object_unref( app );

    return status;
}
