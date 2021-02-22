#ifndef FILE_MANAGER_H_INCLUDED
#define FILE_MANAGER_H_INCLUDED

typedef struct _Open_FM
{
    GtkWidget* window;
    gint regnr;
    gint jahr;
}OpenFM;


void file_manager_entry_activate( GtkWidget*, gpointer );


#endif // FILE_MANAGER_H_INCLUDED
