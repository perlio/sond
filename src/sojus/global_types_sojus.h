#ifndef GLOBAL_TYPES_SOJUS_H_INCLUDED
#define GLOBAL_TYPES_SOJUS_H_INCLUDED

typedef char gchar;
typedef int gint;
typedef int gboolean;
typedef struct _GtkWidget GtkWidget;
typedef struct st_mysql MYSQL;
typedef struct _GPtrArray GPtrArray;
typedef struct _GSocketService GSocketService;
typedef struct _GSettings GSettings;


struct _SB
{
    gchar* sachbearbeiter_id;
    gchar* name;
};

typedef struct _SB SB;

struct _Aktenbet
{
    gint ID;
    gint adressnr;
    gchar* betart;
    gchar* betreff1;
    gchar* betreff2;
    gchar* betreff3;
    gboolean geaendert;
};

typedef struct _Aktenbet Aktenbet;

struct _Akte
{
    gint ID;
    gint regnr;
    gint regjahr;
    gchar* bezeichnung;
    gchar* gegenstand;
    gchar* sachgebiet;
    gchar* sachbearbeiter_id;
    gchar* anlagedatum;
    gchar* ablagenr;
};

typedef struct _Akte Akte;

typedef struct _Subjekt
{
    gchar* adresszeile1;
    gchar* titel;
    gchar* vorname;
    gchar* name;
    gchar* adresszusatz;
    gchar* strasse;
    gchar* hausnr;
    gchar* plz;
    gchar* ort;
    gchar* land;
    gchar* telefon1;
    gchar* telefon2;
    gchar* telefon3;
    gchar* fax;
    gchar* email;
    gchar* homepage;
    gchar* iban;
    gchar* bic;
    gchar* anrede;
    gchar* bemerkungen;
} Subjekt;



#endif // GLOBAL_TYPES_SOJUS_H_INCLUDED
