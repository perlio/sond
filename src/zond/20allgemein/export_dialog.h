#ifndef EXPORT_DIALOG_H_INCLUDED
#define EXPORT_DIALOG_H_INCLUDED

#include <glib.h>

#include "export_selection.h"

/* Fragt die Export-Optionen ab. baum ist der Baum, auf den sich der Export
 * bezieht (BAUM_INHALT oder BAUM_AUSWERTUNG). Die zuletzt gewählten Werte
 * bleiben bis zum Programmende erhalten. Rückgabe TRUE bei OK, FALSE bei
 * Abbruch. */
gboolean export_dialog_run(Projekt *zond, Baum baum, ExportOptionen *opt);

#endif // EXPORT_DIALOG_H_INCLUDED
