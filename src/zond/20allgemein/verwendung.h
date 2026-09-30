#ifndef VERWENDUNG_H_INCLUDED
#define VERWENDUNG_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct _GError GError;

typedef int gint;
typedef char gchar;

/* Zeigt "Herkunft und Verwendung" eines Knotens in einem eigenen Fenster:
 * Ursprung (Datei mit Sections oder Strukturpunkt), Anbindungen, Copies und
 * Links. node_id ist eine knoten-ID; ist sie 0, wird der Knoten über
 * file_part/section gesucht (Aufruf aus BAUM_FS). */
gint verwendung_anzeigen(Projekt*, gint, gchar const*, gchar const*, GError**);

#endif // VERWENDUNG_H_INCLUDED
