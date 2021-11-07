#ifndef APP_WINDOW_H_INCLUDED
#define APP_WINDOW_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct _GtkWidget GtkWidget;
typedef struct _GdkEventKey GdkEventKey;

gboolean cb_key_press( GtkWidget*, GdkEventKey*, gpointer );

void init_app_window( Projekt* );

#endif // APP_WINDOW_H_INCLUDED
