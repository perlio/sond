#ifndef HEADERBAR_H_INCLUDED
#define HEADERBAR_H_INCLUDED

#include <glib.h>

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

#endif // HEADERBAR_H_INCLUDED
