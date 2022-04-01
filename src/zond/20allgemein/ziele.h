#ifndef ZIELE_H_INCLUDED
#define ZIELE_H_INCLUDED

#include "../global_types.h"

typedef int gint;
typedef char gchar;
typedef int gboolean;
typedef struct _Pdf_Viewer PdfViewer;


gboolean ziele_1_gleich_2( const Anbindung, const Anbindung );

gint ziele_abfragen_anker_rek( Projekt*, gint, Anbindung, gboolean*, gchar** );

gint ziele_erzeugen_anbindung( PdfViewer* pv, gint* ptr_new_node, gchar** );

#endif // ZIELE_H_INCLUDED
