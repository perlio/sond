#ifndef GLOBAL_TYPES_SOJUS_H_INCLUDED
#define GLOBAL_TYPES_SOJUS_H_INCLUDED

#define JAHRHUNDERT_GRENZE 70

typedef char gchar;
typedef int gint;
typedef int gboolean;
typedef struct _GtkWidget GtkWidget;
typedef struct st_mysql MYSQL;
typedef struct _GPtrArray GPtrArray;
typedef struct _GSocketService GSocketService;
typedef struct _GSettings GSettings;

typedef struct _Clipboard Clipboard;


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
    gchar* bezeichnung;
    gchar* gegenstand;
    gchar* sachgebiet;
    gchar* sachbearbeiter_id;
    gchar* anlagedatum;
    gchar* ablagenr;
};

typedef struct _Akte Akte;

struct _Adresse
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
};

typedef struct _Adresse Adresse;


typedef struct
{
    struct
    {
        struct {
            GtkWidget* entry_regnr;
            GtkWidget* button_doc_erzeugen;
            GtkWidget* treeview_fm;
        } AktenSchnellansicht;
    } AppWindow;

    struct {
        GtkWidget* bu_dokument_dir;
    } Einstellungen;
} Widgets;


struct _Sojus
{
    GtkWidget* app_window;
    Widgets widgets;

    GSettings* settings;

    GSocketService* socket;

    Clipboard* clipboard;

    struct {
        MYSQL* con;
    } db;

    gint regnr_akt;
    gint jahr_akt;
    gint adressnr_akt;

    GPtrArray* arr_open_fm;

    GPtrArray* sachgebiete;
    GPtrArray* beteiligtenart;
    GPtrArray* sachbearbeiter;
};

typedef struct _Sojus Sojus;

#endif // GLOBAL_TYPES_SOJUS_H_INCLUDED
