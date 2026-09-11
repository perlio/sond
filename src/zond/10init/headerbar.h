#ifndef HEADERBAR_H_INCLUDED
#define HEADERBAR_H_INCLUDED

#include <glib.h>

#include "../zond_init.h" /* fuer Baum-Enum (zond_index_erstellen_activate_fuer_baum) */

typedef struct _Projekt Projekt;
typedef struct _SondTreeviewFM SondTreeviewFM;

void init_headerbar(Projekt*);

/**
 * zond_index_erstellen_ht:
 * @zond:     Projekt
 * @ht_index: (transfer full) Map SondFilePart* -> SondPageRange* (NULL-Wert
 *            = ganze Datei), wie von sond_treeviewfm_get_fileparts()/
 *            zond_treeview_get_selected_fileparts() geliefert. Wird von
 *            dieser Funktion übernommen und am Ende freigegeben.
 *
 * Fragt den OCR-Modus ab und indiziert dann @ht_index (blockiert den
 * Aufrufer bis zum Abschluss, pumpt dabei aber die GTK-Ereignisschleife
 * weiter). Aus do_index_erstellen() herausgelöst, damit auch andere Stellen
 * (z.B. Nachindizieren fehlender Seiten vor einer Suche über eine Auswahl,
 * siehe zond_indexsuche.c) dieselbe Indizierungslogik nutzen können, ohne
 * sie zu duplizieren.
 *
 * Returns: TRUE, wenn tatsächlich indiziert wurde, FALSE bei Abbruch
 *          (OCR-Modus-Dialog abgebrochen oder Thread konnte nicht erzeugt
 *          werden).
 */
gboolean zond_index_erstellen_ht(Projekt *zond, GHashTable *ht_index);

/**
 * zond_index_erstellen_activate_fuer_baum:
 * @zond: Projekt
 * @baum: welcher der drei Bäume die Auswahl liefert (KEIN_BAUM -> Meldung
 *        "Keine Punkte ausgewählt")
 *
 * Gemeinsame Logik für "Index erstellen (Auswahl)" - Analogon zu
 * zond_indexsuche_activate_fuer_baum() (zond_indexsuche.c), s. dort für die
 * Begründung des baum-Parameters (Kontextmenüs kennen ihn synchron über
 * zond->baum_active, das globale Fenstermenü ermittelt ihn per Scan über
 * zond_baum_mit_auswahl()).
 */
void zond_index_erstellen_activate_fuer_baum(Projekt *zond, Baum baum);

/**
 * headerbar_set_seadrive_sensitive:
 * @zond:      Projekt
 * @sensitive: TRUE, wenn die SeaDrive-Hauptmenüpunkte ("Projekt >
 *             Immer offline verfügbar/Offline verfügbar aufheben/
 *             Cache leeren", je Gesamtes-Projekt- und Auswahl-Variante)
 *             anwählbar sein sollen.
 *
 * Von project_set_widgets_sensitive() (project.c) aufgerufen, wenn ein
 * Projekt geöffnet/geschlossen wird - @sensitive sollte dabei bereits
 * "aktives Projekt UND BAUM_FS liegt auf einem SeaDrive-Pfad" sein.
 */
void headerbar_set_seadrive_sensitive(Projekt *zond, gboolean sensitive);

#endif // HEADERBAR_H_INCLUDED
