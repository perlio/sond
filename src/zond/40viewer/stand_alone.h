#ifndef STAND_ALONE_H_INCLUDED
#define STAND_ALONE_H_INCLUDED

typedef struct _GtkWidget GtkWidget;

typedef void* gpointer;


void cb_pv_sa_beenden( GtkWidget*, gpointer );

void cb_datei_schliessen( GtkWidget*, gpointer );

void cb_datei_oeffnen( GtkWidget*, gpointer );

#endif // STAND_ALONE_H_INCLUDED
