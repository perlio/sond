#ifndef SETTINGS_H_INCLUDED
#define SETTINGS_H_INCLUDED

GSettings* settings_open( void );

void settings_con_speichern( const gchar*, gint, const gchar*, const gchar* );

#endif // SETTINGS_H_INCLUDED
