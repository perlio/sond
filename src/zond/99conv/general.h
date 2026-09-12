#ifndef GENERAL_H_INCLUDED
#define GENERAL_H_INCLUDED

#include "../40viewer/viewer.h"

#include <stdio.h>

typedef struct _ZondPdfDocument ZondPdfDocument;
typedef struct _ZPDFD_Part ZPDFDPart;

typedef int gboolean;
typedef char gchar;

gboolean anbindung_1_gleich_2(const Anbindung, const Anbindung);

gboolean anbindung_is_pdf_punkt(Anbindung);

/* "unterseitig" (etablierter Begriff, s. 40viewer/seiten.c) - beginnt
 * oder endet die Anbindung an einer Position INNERHALB einer Seite,
 * statt an deren Anfang/Ende? Ein Punkt (anbindung_is_pdf_punkt) ist
 * per Definition immer unterseitig - er markiert nie eine (oder
 * mehrere) ganze Seite(n). Relevant für Indizierung/Index löschen:
 * beides arbeitet nur seitenweise (chunks/pages-Tabelle kennt keine
 * Position innerhalb einer Seite), eine unterseitige Anbindung würde
 * also entweder redundant die ganze Seite mit indizieren, oder beim
 * Löschen versehentlich den Index einer ggf. von einer ANDEREN
 * Anbindung noch benötigten ganzen Seite mit entfernen - deshalb dort
 * bewusst nicht zugelassen (11.09.2026, Nutzerentscheidung, s. ToDo.c). */
gboolean anbindung_ist_unterseitig(Anbindung);

gboolean anbindung_1_vor_2(Anbindung, Anbindung);

gboolean anbindung_1_eltern_von_2(Anbindung, Anbindung);

void anbindung_parse_file_section(gchar const*, Anbindung*);

void anbindung_build_file_section(Anbindung, gchar**);

gchar* anbindung_to_human_readable(Anbindung*);

void anbindung_get_orig(ZondPdfDocument*, Anbindung*);

void anbindung_korrigieren(ZPDFDPart*, Anbindung*);

gboolean anbindung_is_empty(Anbindung* anbindung);

void anbindung_aktualisieren(ZondPdfDocument*, Anbindung*);

#endif // GENERAL_H_INCLUDED

