#ifndef PDF_TEXT_H_INCLUDED
#define PDF_TEXT_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct _GPtrArray GPtrArray;
typedef struct _GArray GArray;
typedef struct _Info_Window InfoWindow;
typedef struct _GtkWindow GtkWindow;
typedef struct fz_context fz_context;

typedef int gint;
typedef char gchar;


gint pdf_text_anzeigen_ergebnisse( Projekt*, gchar*, GPtrArray*, GArray*, gchar** );

gint pdf_textsuche( Projekt*, InfoWindow*, GPtrArray*, const gchar*, GArray**, gchar** );

#endif // PDF_TEXT_H_INCLUDED
