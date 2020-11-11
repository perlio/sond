#include "../globals.h"


gchar*
get_datetime( void )
{
    GDateTime* dt = g_date_time_new_now_local( );
    gchar* datetime = g_date_time_format( dt, "%d.%m.%C %T" );

    return datetime;
}


GDate*
sql_date_to_gdate( gchar* date )
{
    gchar* year_text = g_strndup( date, 4 );
    gint year = g_ascii_strtoll( year_text, NULL, 10 );
    if ( year < 1000 || year > 9999 ) return NULL;

    gchar* month_text = g_strndup( date + 5, 2 );
    gint month = g_ascii_strtoll( month_text, NULL, 10 );
    if ( month < 1 || month > 12 ) return NULL;

    gchar* day_text = g_strdup( date + 8 );
    gint day = g_ascii_strtoll( day_text, NULL, 10 );
    if ( day < 1 || day > 31 ) return NULL;

    GDate* gdate = g_date_new_dmy( (GDateDay) day, (GDateMonth) month,
            (GDateYear) year );

    return gdate; //muß g_date_freed werden
}


gchar*
gdate_to_string( GDate* g_date )
{
    gint day = g_date_get_day( g_date );
    gint month = g_date_get_month( g_date );
    gint year = g_date_get_year( g_date );

    gchar* string = g_strdup_printf("%2i.%2i.%i", day, month, year );

    return string; //muß g_freed werden!
}
