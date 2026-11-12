/*
 sond (sond_archiv_kandidaten.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026 pelo america

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
 * @file sond_archiv_kandidaten.h
 * @brief Auswertung der Akten-Nodes für die Archivierung abgelegter Akten
 *        (ohne GTK und ohne Netz)
 *
 * Eine Akte ist abgelegt, wenn es nicht mehr "von"- als "bis"-Properties gibt.
 * Jedes "bis" (Datum "JJJJ-MM-TT") trägt die Unter-Property "ablnr"
 * (Ablagenummer, z. B. "2026-5"). Nach der Archivierung erhält das letzte "bis"
 * die Unter-Property "archiv" mit dem Archivnamen.
 */

#ifndef SOND_ARCHIV_KANDIDATEN_H
#define SOND_ARCHIV_KANDIDATEN_H

#include <glib.h>

#include "../../sond_graph/sond_graph_node.h"
#include "../../sond_graph/sond_graph_db.h"

G_BEGIN_DECLS

/** Voreinstellung der Überliegedauer in Tagen */
#define SOND_ARCHIV_UEBERLIEGE_STANDARD 60

/**
 * SondArchivKandidat:
 * @node_id: ID des Akten-Nodes
 * @year: Jahr der RegNr
 * @lfd_nr: laufende Nummer der RegNr
 * @library_name: Name der Library ("Jahr-LfdNr")
 * @bezeichnung: (nullable): Kurzbezeichnung der Akte
 * @ablnr: letzte Ablagenummer
 * @bis: Datum der letzten Ablage ("JJJJ-MM-TT")
 * @alter_tage: Tage seit der letzten Ablage
 * @archiv_name: vorgesehener Archivname (ohne ".zip")
 */
typedef struct {
    gint64 node_id;
    guint year;
    guint lfd_nr;
    gchar *library_name;
    gchar *bezeichnung;
    gchar *ablnr;
    gchar *bis;
    gint alter_tage;
    gchar *archiv_name;
} SondArchivKandidat;

void sond_archiv_kandidat_free(SondArchivKandidat *kandidat);

/**
 * sond_archiv_suchkriterien:
 *
 * Suchkriterien für alle Akten, die jemals abgelegt wurden (Label "Akte" mit
 * mindestens einer "bis"-Property, ohne Mengenbegrenzung). Ob die Akte jetzt
 * abgelegt ist und ob sie Kandidat ist, entscheidet
 * sond_archiv_kandidat_pruefen().
 *
 * Returns: (transfer full): mit sond_graph_node_search_criteria_free() freigeben
 */
SondGraphNodeSearchCriteria* sond_archiv_suchkriterien(void);

/**
 * sond_archiv_ablnr_suchkriterien:
 * @year: Jahr
 *
 * Suchkriterien für alle Akten mit einer Ablage im Jahr @year (Label "Akte", ein
 * "bis" mit Datum "@year-*"). Die Ablagenummer eines "bis" trägt das Jahr der
 * Ablage; ihre Unter-Property "ablnr" lässt sich nicht direkt suchen, deshalb wird
 * über das Datum eingegrenzt und mit sond_archiv_hoechste_ablnr() ausgewertet.
 *
 * Returns: (transfer full): mit sond_graph_node_search_criteria_free() freigeben
 */
SondGraphNodeSearchCriteria* sond_archiv_ablnr_suchkriterien(guint year);

/**
 * sond_archiv_hoechste_ablnr:
 * @nodes: (element-type SondGraphNode): Akten-Nodes
 * @year: Jahr
 *
 * Returns: höchste laufende Nummer aller Ablagenummern "@year-N" in den "bis"-
 *   Properties der Nodes; 0, wenn es keine gibt. Ablagenummern anderer Jahre
 *   zählen nicht.
 */
guint sond_archiv_hoechste_ablnr(GPtrArray *nodes, guint year);

/**
 * sond_archiv_kandidat_pruefen:
 * @node: Akten-Node
 * @ueberliege_tage: Überliegedauer in Tagen
 * @heute: (nullable): Stichtag (NULL = heute)
 * @grund: (out) (nullable): bei NULL als Rückgabe eine kurze Begründung
 *   (statischer Text, nicht freigeben)
 *
 * Kandidat ist eine Akte, die jetzt abgelegt ist, deren letzte Ablage
 * mindestens @ueberliege_tage zurückliegt, die eine Ablagenummer hat und noch
 * nicht archiviert ist.
 *
 * Returns: (transfer full) (nullable): Kandidat oder NULL
 */
SondArchivKandidat* sond_archiv_kandidat_pruefen(SondGraphNode *node,
                                                 gint ueberliege_tage,
                                                 const GDate *heute,
                                                 const gchar **grund);

/**
 * sond_archiv_name_bilden:
 *
 * Archivname "<Library>_Abl<Ablagenr>_<bis-Datum>", z. B.
 * "2026-23_Abl2026-5_2026-11-12".
 *
 * Returns: (transfer full) (nullable): NULL bei fehlenden oder ungültigen Teilen
 */
gchar* sond_archiv_name_bilden(const gchar *library_name, const gchar *ablnr,
                               const gchar *bis);

/**
 * sond_archiv_ist_archiviert:
 * @node: Akten-Node
 * @archiv_name: (out) (nullable) (transfer full): Name des Archivs
 *
 * Returns: TRUE, wenn das letzte "bis" die Markierung "archiv" trägt
 */
gboolean sond_archiv_ist_archiviert(SondGraphNode *node, gchar **archiv_name);

/**
 * sond_archiv_markieren:
 * @node: Akten-Node (mit mindestens einem "bis")
 * @archiv_name: Archivname
 *
 * Setzt die Unter-Property "archiv" am letzten "bis" (ersetzt eine vorhandene).
 * Speichern ist Sache des Aufrufers.
 *
 * Returns: FALSE, wenn der Node kein "bis" hat
 */
gboolean sond_archiv_markieren(SondGraphNode *node, const gchar *archiv_name);

G_END_DECLS

#endif /* SOND_ARCHIV_KANDIDATEN_H */
