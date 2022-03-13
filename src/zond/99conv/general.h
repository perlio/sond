#ifndef GENERAL_H_INCLUDED
#define GENERAL_H_INCLUDED

#include "../global_types.h"

#include <stdio.h>

typedef struct _Info_Window
{
    GtkWidget* dialog;
    GtkWidget* content;
    gboolean cancel;
} InfoWindow;

typedef struct _GFile GFile;
typedef struct _GSList GSList;
typedef struct _GtkWindow GtkWindow;
typedef struct _GtkWidget GtkWidget;
typedef struct TessBaseAPI TessBaseAPI;
typedef struct TessResultRenderer TessResultRenderer;

typedef int gboolean;
typedef char gchar;
typedef int gint;
typedef unsigned int guint;
typedef const void* gconstpointer;


gboolean is_pdf( const gchar* );

/*  info_window  */
void info_window_scroll( InfoWindow* );

void info_window_close( InfoWindow* );

void info_window_set_message( InfoWindow*, const gchar* );

InfoWindow* info_window_open( GtkWidget*, const gchar* );

#endif // GENERAL_H_INCLUDED

