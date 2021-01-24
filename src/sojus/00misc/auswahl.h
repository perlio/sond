#ifndef AUSWAHL_H_INCLUDED
#define AUSWAHL_H_INCLUDED

typedef struct _Sojus Sojus;

typedef struct _GtkDialog GtkDialog;
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkEntry GtkEntry;

typedef char gchar;
typedef int gint;
typedef int gboolean;


//void cb_row_activated( GtkListBox*, GtkListBoxRow*, gpointer );

GtkDialog* auswahl_dialog_oeffnen( GtkWidget*, const gchar*, const gchar* );

void auswahl_dialog_zeile_einfuegen( GtkDialog*, const gchar* );

gint auswahl_dialog_run( GtkDialog* );

void auswahl_parse_regnr( const gchar*, gint*, gint* );

gboolean auswahl_regnr_existiert( GtkWidget*, MYSQL*, gint, gint );

gboolean auswahl_regnr_ist_wohlgeformt( const gchar* );

gboolean auswahl_parse_entry( GtkWidget*, const gchar* );

gboolean auswahl_get_regnr_akt( Sojus*, GtkEntry*);

#endif // AUSWAHL_H_INCLUDED
