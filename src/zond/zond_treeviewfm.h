#ifndef ZOND_TREEVIEWFM_H_INCLUDED
#define ZOND_TREEVIEWFM_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include "../sond_treeviewfm.h"

#include "zond_init.h"

G_BEGIN_DECLS

//ZOND_TYPE_TREEVIEWFM definieren
#define ZOND_TYPE_TREEVIEWFM zond_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE(ZondTreeviewFM, zond_treeviewfm, ZOND, TREEVIEWFM, SondTreeviewFM)

struct _ZondTreeviewFMClass {
	SondTreeviewFMClass parent_class;
};

ZondTreeviewFM* zond_treeviewfm_new(Projekt* zond);

gint zond_treeviewfm_section_visible(ZondTreeviewFM*, gchar const*,
		gchar const*, gboolean, gboolean*, GtkTreeIter*, gboolean*, gboolean*,
		GError**);

gint zond_treeviewfm_set_cursor_on_section(ZondTreeviewFM*, gchar const*,
		gchar const*, GError**);

void zond_treeviewfm_kill_parent(ZondTreeviewFM*, GtkTreeIter*);

/* Wie sond_treeviewfm_get_fileparts() (sond_treeviewfm.h), aber
 * Anbindungs-bewusst: ein markierter SOND_TVFM_ITEM_TYPE_LEAF_SECTION-
 * Knoten in BAUM_FS (= eine Anbindung, s. ziele.c) liefert den
 * tatsächlichen Seitenbereich statt (wie in der Basisklasse) immer NULL
 * (= ganze Datei).
 *
 * reject_unterseitig: TRUE, wenn eine unterseitige Anbindung (beginnt/
 * endet nicht an einer Seitengrenze, oder reiner Punkt - s.
 * anbindung_ist_unterseitig() in 99conv/general.h) in der Auswahl die
 * ganze Abfrage mit einem GError scheitern lassen soll. Für Index
 * erstellen/löschen (Auswahl) TRUE, für alle anderen Aufrufer
 * (Indexsuche) FALSE. Nur bei selected_only == TRUE relevant - bei
 * "Gesamtes Projekt" (selected_only == FALSE) wird nie geprüft, s.
 * ToDo.c (11.09.2026, Nutzerentscheidung).
 *
 * skip_fully_covered: Nutzer-Wunsch 16.09.2026, Verzeichnis-Kurzschluss
 * analog scan_coverage_gaps_fs() (zond_indexsuche.c) jetzt auch für "Index
 * erstellen (Gesamtes Projekt)". Nur bei selected_only == FALSE wirksam
 * (nur dort steigt zond_treeviewfm_item_get_fileparts_readdir() rekursiv
 * über "wirkliche" Verzeichnis-Äste ab): ein Ast, der laut
 * sond_index_ctx_get_dir_status() bereits VOLLSTÄNDIG indiziert ist, wird
 * gar nicht erst per readdir aufgeschlüsselt - erneutes Indizieren wäre
 * per Definition von "Coverage" ein No-Op. Der Aufrufer muss dafür sorgen,
 * dass dies NICHT gesetzt wird, wenn der OCR-Modus "erzwingen" ist (dort
 * darf kein Ast übersprungen werden) - s. do_index_erstellen_gesamt()
 * (headerbar.c), wo der OCR-Modus deshalb VOR diesem Aufruf abgefragt
 * wird (vorher danach). Für alle anderen Aufrufer (Auswahl, Indexsuche,
 * Lücken-Aufschlüsselung) bleibt es FALSE - unverändertes Verhalten. */
GHashTable* zond_treeviewfm_get_fileparts(ZondTreeviewFM*, gboolean,
		gboolean, gboolean, GError**);

/* Reiner readdir-Scanner für einen "wirklichen" (nicht in einem Container
 * liegenden) Dateisystem-Ast - Kernstück von
 * zond_treeviewfm_item_get_fileparts() (s. dortigen Kommentar), hier
 * zusätzlich exponiert für die verzögerte Aufschlüsselung eines als
 * Verzeichnis-Lücke gemeldeten Asts bei "Index durchsuchen" -> "jetzt
 * nachindizieren" (s. zond_indexsuche.c, scan_coverage_gaps_fs()/
 * handle_coverage_gaps()). rel_dir NULL = Projektwurzel. Trägt die
 * gefundenen Dateien (als SOND_TYPE_FILE_PART_LEAF, rein endungsbasierter
 * MIME-Typ, kein Dateizugriff) in ht ein (Value jeweils NULL = ganze
 * Datei) - ht muss vom Aufrufer mit passenden Destroy-Funktionen für
 * SondFilePart*-Keys angelegt sein. ToDo.c (12.-15.09.2026).
 *
 * skip_fully_covered: s. Kommentar an zond_treeviewfm_get_fileparts()
 * oben - bei rel_dir == NULL (Projektwurzel) ohne Wirkung (Coverage wird
 * nie über die oberste Ebene hinaus zusammengefasst, s.
 * sond_index_ctx_coverage_try_collapse()). Von handle_coverage_gaps()
 * bewusst mit FALSE aufgerufen: der dortige Ast ist per Definition schon
 * als Lücke bekannt, ein erneuter Coverage-Check wäre sinnlos. */
gint zond_treeviewfm_item_get_fileparts_readdir(SondTreeviewFM*,
		gchar const*, GHashTable*, gboolean, GError**);

G_END_DECLS

#endif // ZOND_TREEVIEWFM_H_INCLUDED
