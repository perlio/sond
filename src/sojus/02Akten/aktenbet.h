#ifndef AKTENBET_H_INCLUDED
#define AKTENBET_H_INCLUDED

GPtrArray* aktenbet_oeffnen( GtkWidget*, gint, gint );

void aktenbet_free( gpointer );

void aktenbet_speichern( GtkWidget* );

Aktenbet* aktenbet_einlesen( GtkWidget* );

Aktenbet* aktenbet_neu( void );

#endif // AKTENBET_H_INCLUDED
