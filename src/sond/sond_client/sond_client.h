#ifndef SOND_CLIENT_H_INCLUDED
#define SOND_CLIENT_H_INCLUDED

typedef struct _GtkWidget GtkWidget;
typedef char gchar;

typedef struct _Sond_Client
{
    gchar* user;
    gchar* password;
    GtkWidget* app_window;
} SondClient;

void sond_client_quit( SondClient* );

#endif // SOND_CLIENT_H_INCLUDED
