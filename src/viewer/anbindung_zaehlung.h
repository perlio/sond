#ifndef ANBINDUNG_ZAEHLUNG_H_INCLUDED
#define ANBINDUNG_ZAEHLUNG_H_INCLUDED

#include "zond_pdf_document.h"

//Live-Numerierung in die Zählung der Datei auf der Platte (vor dem Speichern)
void anbindung_get_orig(ZondPdfDocument*, Anbindung*);

/* Live-Numerierung in die Zählung, die der Viewer anzeigt (ohne gelöschte Seiten) */
void anbindung_get_sichtbar(ZondPdfDocument*, Anbindung*);

//aus aktueller Anbindung diejenige errechnen, die sich nach Speichern von ZPDFDPart ergibt
void anbindung_korrigieren(ZPDFDPart*, Anbindung*);

//Zählung der Platte (wie in der Datenbank) in die Live-Zählung; Gegenstück zu anbindung_get_orig
void anbindung_aktualisieren(ZondPdfDocument*, Anbindung*);

#endif /* ANBINDUNG_ZAEHLUNG_H_INCLUDED */
