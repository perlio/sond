/*
 zond (zond_knoten.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  pelo america

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file zond_knoten.h
 * @brief Inhalts- und Auswertungsbaum aus der Projektdatenbank lesen (GTK-frei)
 *
 * Die Logik des Ladens lag in zond_treeview.c, verwoben mit GtkTreeIter und dem Baummodell.
 * Hier liegt sie ohne Oberfläche, damit das GTK3-zond und ein späteres GTK4-zond sie gemeinsam
 * nutzen. Die Bäume werden ebenenweise gelesen (Kinder eines Knotens), nicht auf einmal. Der
 * Client zeigt diese Bäume nicht; dafür wird zond geöffnet.
 *
 * Anzeige-ID: Was ein Baum zeigt, ist nicht immer die Datenbankzeile. Eine Anbindung
 * (BAUM_INHALT_FILE) zeigt die Wurzel des FILE_PART-Baums, auf den sie verweist, ein Link
 * (BAUM_AUSWERTUNG_LINK) den Knoten, auf den er zeigt. Die Kinder eines angezeigten Knotens sind
 * die Datenbank-Kinder seiner Anzeige-ID.
 */

#ifndef ZOND_KNOTEN_H_INCLUDED
#define ZOND_KNOTEN_H_INCLUDED

#include <glib.h>

#include "../znd/zond_dbase.h"

G_BEGIN_DECLS

typedef struct {
	gint id;          //Anzeige-ID (bei Anbindung: FILE_PART-Wurzel, bei Link: Ziel)
	gint db_id;       //Zeile in der Datenbank (bei Anbindung und Link: deren eigene Zeile)
	gint type;        //ZOND_DBASE_TYPE_* der Zeile db_id
	gint head_nr;     //bei Link: db_id der Link-Zeile, sonst 0
	gchar *icon_name; //bei Link NULL: Icon und Text kommen vom Ziel
	gchar *node_text;
	gboolean has_children; //bei Link: das Ziel hat Kinder
} ZondKnoten;

void zond_knoten_free(ZondKnoten *node);

/* Die Kinder von parent_id (Anzeige-ID oder Wurzel des Baums) in Reihenfolge, als GPtrArray
 * von ZondKnoten (mit zond_knoten_free als Freigabefunktion; g_ptr_array_unref()). Eine
 * Anbindung, deren Datei es in der Datenbank nicht gibt, wird nicht aufgeführt, das Projekt
 * lässt sich trotzdem laden und die Prüfung kann den Fehler zeigen und beheben. */
gint zond_knoten_get_children(ZondDBase *dbase, gint parent_id, GPtrArray **nodes, GError **error);

/* In welchem Baum (BAUM_INHALT oder BAUM_AUSWERTUNG, Wurzel-ID) steht der Knoten? Für einen
 * FILE_PART der Baum seiner Anbindung. */
gint zond_knoten_get_root(ZondDBase *dbase, gint node_id, gint *root, GError **error);

/* Prüft den Einfügepunkt anchor_id (Anzeige-ID) für einen neuen Knoten. Ein angebundener
 * FILE_PART gilt als Anbindungszeile (anchor_id wird ersetzt). Ein Punkt unterhalb eines
 * angebundenen FILE_PART oder neben einem Teil davon ist nicht zulässig: 1, anchor_id ist 0.
 * 0: in Ordnung, -1: Fehler. */
gint zond_knoten_anker_pruefen(ZondDBase *dbase, gint *anchor_id, gboolean child,
		GError **error);

/* Legt einen neuen Strukturpunkt (BAUM_STRUKT) an: unter anchor_id (child) oder dahinter. Die
 * neue ID kommt in *id. 0: in Ordnung, -1: Fehler. */
gint zond_knoten_neuer_punkt(ZondDBase *dbase, gint anchor_id, gboolean child,
		gchar const *icon_name, gchar const *node_text, gint *id, GError **error);

/* Verschiebt den Knoten node_id (Anzeige-ID; bei Links die Link-Nummer) unter anchor_id (child)
 * oder dahinter. Im Inhaltsbaum (in_inhalt) wird ein angebundener FILE_PART als seine Anbindung
 * verschoben, ein nicht angebundener bleibt, wo er ist. Die ID der tatsächlich verschobenen
 * Zeile kommt in *moved_id. 0: verschoben, 1: nicht verschoben (nicht angebundener FILE_PART),
 * -1: Fehler. */
gint zond_knoten_verschieben(ZondDBase *dbase, gint node_id, gboolean in_inhalt,
		gint anchor_id, gboolean child, gint *moved_id, GError **error);

/* Zeigt von außerhalb ein Link auf den Knoten node_id oder einen Nachkommen? Dann würde das
 * Löschen per Kaskade den Link und dessen jüngere Geschwister mitreißen. 2: ja, 0: nein,
 * -1: Fehler. Muss vor jeder Änderung an Baum oder Datenbank stehen. */
gint zond_knoten_extern_verlinkt(ZondDBase *dbase, gint node_id, GError **error);

/* Prüft, ob der Knoten node_id (kein Strukturpunkt, im Inhaltsbaum) gelöscht werden darf, und
 * welche Zeile dabei tatsächlich gelöscht wird: für einen angebundenen FILE_PART seine Anbindung
 * (*baum_inhalt_file, sonst 0). 0: löschen, 1: nicht löschen (als Copy angebunden oder kopiert),
 * 2: extern verlinkt, -1: Fehler. */
gint zond_knoten_loeschpruefung(ZondDBase *dbase, gint node_id, gint type,
		gint *baum_inhalt_file, GError **error);

/* Löst den Knoten node_id auf, in einer Transaktion: die Kinder rücken an seine Stelle, bei einem
 * angebundenen FILE_PART werden sie zu eigenen Anbindungen, dann wird er (bzw. seine Anbindung)
 * entfernt. *baum_inhalt_file: die Anbindungszeile, falls er angebunden war, sonst 0. 0: erledigt,
 * 1: nicht aufgelöst (als Copy angebunden), -1: Fehler. */
gint zond_knoten_aufloesen(ZondDBase *dbase, gint node_id, gint type, gint *baum_inhalt_file,
		GError **error);

/* Eine zu kopierende Zeile der Auswertung: was die Kopie in der Datenbank wird und was die
 * Anzeige dafür braucht. */
typedef struct {
	gint type;        //neuer Typ: BAUM_STRUKT, BAUM_AUSWERTUNG_COPY oder BAUM_AUSWERTUNG_LINK
	gint link;        //Copy: Quelle (Anzeige-ID), Link: Ziel
	gint link_root;   //nur Link: Wurzel-ID des Baums, in dem das Ziel steht
	gchar *icon_name; //nur Strukturpunkt und Copy
	gchar *node_text;
	gchar *text;
	gint erstes_kind; //nur Copy einer Anbindung: erstes Kind der Datei (ihre Abschnitte werden
	                  //mitkopiert), sonst 0
} ZondKopie;

void zond_knoten_kopie_clear(ZondKopie *kopie);

/* Liest die Quelle node_id und bestimmt, was daraus wird (ändert nichts): ein Strukturpunkt bleibt
 * einer, ein Link bleibt Link auf dasselbe Ziel (ein verschachtelter Link würde als Copy
 * unbeschriftet und ohne Kinder), alles andere wird eine Copy. Bei einer Anbindung kommen Icon und
 * Text von der verlinkten Datei, nicht vom Anker. 0 oder -1. */
gint zond_knoten_kopie_vorbereiten(ZondDBase *dbase, gint node_id, ZondKopie *kopie,
		GError **error);

/* Legt die vorbereitete Kopie unter anchor_id (child) oder dahinter an. Die neue ID kommt in *id.
 * 0 oder -1. */
gint zond_knoten_kopie_anlegen(ZondDBase *dbase, ZondKopie const *kopie, gint anchor_id,
		gboolean child, gint *id, GError **error);

G_END_DECLS

#endif /* ZOND_KNOTEN_H_INCLUDED */
