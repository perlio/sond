#ifndef VERWENDUNG_H_INCLUDED
#define VERWENDUNG_H_INCLUDED

#include <gtk/gtk.h>

typedef struct _Projekt Projekt;

/* Zeigt "Herkunft und Verwendung" eines Knotens in einem eigenen Fenster:
 * Ursprung (Datei mit Sections oder Strukturpunkt), Anbindungen, Copies und
 * Links. node_id ist eine knoten-ID; ist sie 0, wird der Knoten über
 * file_part/section gesucht (Aufruf aus BAUM_FS). */
gint verwendung_anzeigen(Projekt*, gint, gchar const*, gchar const*, GError**);

/* Baumaufbau, auch für andere Fenster (Ergebnis der Baumsuche) */

/* Art eines Treffers - Datei-/Section-Zeile und Anbindungszeile tragen
 * dieselbe FILE_PART-ID und werden hierüber unterschieden */
typedef enum {
	VERWENDUNG_TREFFER_ALLE, //jede Zeile mit dieser ID
	VERWENDUNG_TREFFER_PFAD, //nur Datei-/Section-Zeile (Treffer im file_part)
	VERWENDUNG_TREFFER_TEXT //alle anderen Zeilen (node_text/text)
} VerwendungTrefferArt;

typedef struct {
	gint key_id;
	VerwendungTrefferArt art;
} VerwendungTreffer;

GtkTreeStore* verwendung_store_new(void);

/* Ursprung von node_id: *strukt_id (Strukturpunkt) oder *file_part_id
 * (Wurzel-FILE_PART), das andere 0. *key_id: ID, für die im Baum die Zeile
 * von node_id steht. */
gint verwendung_ursprung_ermitteln(Projekt*, gint, gint*, gint*, gint*,
		GError**);

//Ursprung samt allem Abgeleiteten als oberste Zeile(n) anfügen
gint verwendung_store_add_ursprung(Projekt*, GtkTreeStore*, gint, gint,
		GError**);

//Spalte "Enthält" für alle Zeilen mit Kindern füllen
void verwendung_store_zaehlen(GtkTreeStore*);

//Baumansicht mit Spalten, Tooltip und Sprung per Doppelklick
GtkWidget* verwendung_treeview_new(Projekt*, GtkTreeStore*);

/* Treffer fett, Pfade dorthin aufgeklappt (Treffer selbst bleiben zu).
 * cursor: Cursor auf den letzten Treffer setzen. */
void verwendung_hervorheben(GtkTreeView*, VerwendungTreffer const*, guint,
		gboolean);

/* Knoten, für den die Zeile in BAUM_INHALT/BAUM_AUSWERTUNG steht; FALSE bei
 * Verweisen (Links, indirekte Fundstellen) und Zeilen ohne Knoten */
gboolean verwendung_zeile_knoten(GtkTreeModel*, GtkTreeIter*, gint*, gint*);

#endif // VERWENDUNG_H_INCLUDED
