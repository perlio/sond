#ifndef AKTENFENSTER_H_INCLUDED
#define AKTENFENSTER_H_INCLUDED

GtkWidget* aktenfenster_oeffnen(Sojus*);

void aktenfenster_fuellen(GtkWidget *window, gint regnr, gint jahr);

void listboxlabel_fuellen(GtkWidget*, GtkWidget*, Aktenbet*);

#endif // AKTENFENSTER_H_INCLUDED
