#ifndef EXPORT_SELECTION_H_INCLUDED
#define EXPORT_SELECTION_H_INCLUDED

#include <glib.h>

#include "../zond_init.h"

/* Gemeinsame Auswahl-Schicht der Export-Funktionen: macht aus der
 * Markierung (oder dem ganzen Baum) eine geordnete Liste von Knoten mit
 * allem, was die Zielformate brauchen. Die Schicht kennt kein Zielformat. */

#define EXPORT_TIEFE_ALLE (-1)

typedef enum {
	EXPORT_FORMAT_ODT = 0,
	EXPORT_FORMAT_PDF,
	EXPORT_FORMAT_DOCX,
	NUM_EXPORT_FORMAT
} ExportFormat;

typedef struct _ExportOptionen {
	ExportFormat format;
	gboolean ganzer_baum;   //FALSE: markierte Punkte
	gint tiefe;             //Ebenen unter jedem Punkt, 0 = nur der Punkt
	gboolean nodetext;
	gboolean text;          //Notiz des Knotens
	gboolean anbindung;     //Datei und Seitenbereich
	gboolean pfad;          //Vorfahren oberhalb eines markierten Punkts
	gboolean nummern;       //Gliederungsnummern, relativ zur Markierung
	gboolean dokumente;     //angebundene Dokumente ausgeben
} ExportOptionen;

typedef struct _ExportEintrag {
	gint node_id;
	gint ebene;     //1 = markierter Punkt, darunter 2, 3, ...
	gchar *nummer;  //"1.2.1"; NULL, wenn Nummern abgeschaltet
	gchar *titel;
	gchar *pfad;    //"A > B > C", nur bei ebene 1 und wenn gewünscht
	gchar *notiz;
	gchar *datei;       //angebundene Datei (relativ zum Projekt)
	gchar *anbindung;   //Seitenbereich, lesbar
	Anbindung anb;      //Seitenbereich, roh; leer = ganze Datei
} ExportEintrag;

void export_eintrag_free(gpointer);

/* Baum, auf den sich ein Export bezieht (zond_baum_aktuell()), oder
 * KEIN_BAUM, wenn keiner der beiden Auswahlbäume fokussiert ist */
Baum export_selection_baum(Projekt *zond);

/* Zahl der markierten Punkte in baum */
gint export_selection_anzahl_markiert(Projekt *zond, Baum baum);

/* Liefert GPtrArray von ExportEintrag* (mit export_eintrag_free) in
 * Baumreihenfolge, oder NULL bei Fehler */
GPtrArray* export_selection_build(Projekt *zond, Baum baum,
		const ExportOptionen *opt, GError **error);

#endif // EXPORT_SELECTION_H_INCLUDED
