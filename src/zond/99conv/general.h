#ifndef GENERAL_H_INCLUDED
#define GENERAL_H_INCLUDED

#include "../global_types.h"

#include <stdio.h>

typedef struct _GFile GFile;
typedef struct _GSList GSList;
typedef struct _GtkWindow GtkWindow;
typedef struct _GtkWidget GtkWidget;
typedef struct TessBaseAPI TessBaseAPI;
typedef struct TessResultRenderer TessResultRenderer;

typedef int gboolean;
typedef char gchar;
typedef int gint;
typedef unsigned int guint;
typedef const void *gconstpointer;

gboolean is_pdf(const gchar*);

gchar* get_rel_path_from_file_part(gchar const*);

gboolean anbindung_1_gleich_2(const Anbindung, const Anbindung);

gboolean anbindung_is_pdf_punkt(Anbindung);

gboolean anbindung_1_vor_2(Anbindung, Anbindung);

gboolean anbindung_1_eltern_von_2(Anbindung, Anbindung);

void anbindung_parse_file_section(gchar const*, Anbindung*);

void anbindung_build_file_section(Anbindung, gchar**);

void anbindung_aktualisieren_insert_pages(GArray*, Anbindung*);

void anbindung_aktualisieren(GArray*, Anbindung*);

gchar* anbindung_to_human_readable(Anbindung*);

#endif // GENERAL_H_INCLUDED

