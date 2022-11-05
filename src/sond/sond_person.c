#include "sond_person.h"

void
person_kurz_free( PersonKurz* person_kurz )
{
    g_free( person_kurz->name );
    g_free( person_kurz->vorname );

    g_free( person_kurz );

    return;
}
