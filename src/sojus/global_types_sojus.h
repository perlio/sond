#ifndef GLOBAL_TYPES_SOJUS_H_INCLUDED
#define GLOBAL_TYPES_SOJUS_H_INCLUDED

typedef struct _GtkWidget GtkWidget;
typedef struct _GFile GFile;
typedef struct _GPtrArray GPtrArray;
typedef struct _GFileMonitor GFileMonitor;


typedef struct _Sojus
{
    GtkWidget* app_window;

    GFileMonitor* monitor;

    GPtrArray* arr_dirs;
} Sojus;



#endif // GLOBAL_TYPES_SOJUS_H_INCLUDED
