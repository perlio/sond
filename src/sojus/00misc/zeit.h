#ifndef UMWANDLUNGEN_H_INCLUDED
#define UMWANDLUNGEN_H_INCLUDED

gchar* get_datetime( void );

GDate* sql_date_to_gdate( gchar* );

gchar* gdate_to_string( GDate* );


#endif // UMWANDLUNGEN_H_INCLUDED
