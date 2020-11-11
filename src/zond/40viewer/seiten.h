#ifndef SEITEN_H_INCLUDED
#define SEITEN_H_INCLUDED

typedef struct _GtkMenuItem GtkMenuItem;

typedef void* gpointer;


void cb_pv_seiten_ocr(GtkMenuItem*, gpointer );

void cb_pv_seiten_loeschen( GtkMenuItem*, gpointer );

void cb_pv_seiten_einfuegen( GtkMenuItem*, gpointer );

void cb_pv_seiten_drehen( GtkMenuItem*, gpointer );

void cb_seiten_ausschneiden( GtkMenuItem*, gpointer );

void cb_seiten_kopieren( GtkMenuItem*, gpointer );

#endif // SEITEN_H_INCLUDED
