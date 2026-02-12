#ifndef GENERAL_H_INCLUDED
#define GENERAL_H_INCLUDED

#include "../zond_init.h"

#include <stdio.h>

typedef struct _ZondPdfDocument ZondPdfDocument;
typedef struct _ZPDFD_Part ZPDFDPart;

typedef int gboolean;
typedef char gchar;

gboolean anbindung_1_gleich_2(const Anbindung, const Anbindung);

gboolean anbindung_is_pdf_punkt(Anbindung);

gboolean anbindung_1_vor_2(Anbindung, Anbindung);

gboolean anbindung_1_eltern_von_2(Anbindung, Anbindung);

void anbindung_parse_file_section(gchar const*, Anbindung*);

void anbindung_build_file_section(Anbindung, gchar**);

gchar* anbindung_to_human_readable(Anbindung*);

void anbindung_get_orig(ZondPdfDocument*, Anbindung*);

void anbindung_korrigieren(ZPDFDPart*, Anbindung*);

void anbindung_aktualisieren(ZondPdfDocument*, Anbindung*);

#endif // GENERAL_H_INCLUDED

