#ifndef SOND_CLIENT_MISC_H_INCLUDED
#define SOND_CLIENT_MISC_H_INCLUDED

typedef int gint;
typedef int gboolean;
typedef char gchar;

void sond_client_misc_parse_regnr( const gchar*, gint*, gint* );

gboolean sond_client_misc_regnr_wohlgeformt( const gchar* );

void sond_client_seadrive_test_seafile_server( SondClient* );

#endif // SOND_CLIENT_MISC_H_INCLUDED
