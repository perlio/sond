#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include "sond_akte.h"


static void
akte_lebenszeit_free( AkteLebenszeit* akte_lebenszeit )
{
    g_date_time_unref( akte_lebenszeit->von );
    g_date_time_unref( akte_lebenszeit->bis );

    g_free( akte_lebenszeit );
}


static void
akte_beteiligter_free( AkteBeteiligter* akte_beteiligter )
{
    person_kurz_free( akte_beteiligter->person_kurz );
    g_ptr_array_unref( akte_beteiligter->arr_betreffs );

    g_free( akte_beteiligter );

    return;
}


static void
akte_sachbearbeiter_free( AkteSachbearbeiter* akte_sachbearbeiter )
{
    person_kurz_free( akte_sachbearbeiter->person_kurz );
    g_free( akte_sachbearbeiter->sb_kuerzel );
    g_date_time_unref( akte_sachbearbeiter->von );
    g_date_time_unref( akte_sachbearbeiter->bis );

    g_free( akte_sachbearbeiter );

    return;
}

static void
akte_kurz_free( AkteKurz* akte_kurz )
{
    g_free( akte_kurz->bezeichnung );
    g_free( akte_kurz->gegenstand );

    g_free( akte_kurz );

    return;
}


void
akte_free( Akte* akte )
{
    akte_kurz_free( akte->akte_kurz );
    akte_sachbearbeiter_free( akte->akte_sachbearbeiter );

    g_ptr_array_unref( akte->arr_beteiligte );
    g_ptr_array_unref( akte->arr_lebenszeiten );

    g_free( akte );

    return;
}


Akte*
akte_new( void )
{
    Akte* akte = g_malloc0( sizeof( Akte ) );

    akte->arr_beteiligte = g_ptr_array_new_with_free_func( (GDestroyNotify) akte_beteiligter_free );

    akte->arr_lebenszeiten = g_ptr_array_new_with_free_func( (GDestroyNotify) akte_lebenszeit_free );

    return akte;
}


