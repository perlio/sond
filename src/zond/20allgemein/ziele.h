#ifndef ZIELE_H_INCLUDED
#define ZIELE_H_INCLUDED

#include "../global_types.h"

typedef int gint;
typedef char gchar;
typedef int gboolean;
typedef struct _Pdf_Viewer PdfViewer;



gboolean ziele_1_gleich_2( const Anbindung, const Anbindung );

gint ziele_abfragen_anker_rek( ZondDBase*, Anbindung, gint, gint*, gboolean*, GError** );

gint zond_anbindung_erzeugen( PdfViewer* pv, GError** );

#endif // ZIELE_H_INCLUDED
