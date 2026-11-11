/*
 sond (sond_import.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_import.h
 * @brief Import von Dokumenten in das SeaDrive-Verzeichnis einer Akte
 *
 * Reiner Kern ohne GUI und ohne SondClient: ermittelt das SeaDrive-Verzeichnis
 * einer Library und kopiert Dateien dorthin. Hochgeladen wird anschließend von
 * SeaDrive selbst.
 */

#ifndef SOND_IMPORT_H
#define SOND_IMPORT_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * sond_import_detect_seadrive_root:
 * @server_hint: (nullable): Seafile-Server-URL; bei mehreren SeaDrive-Konten
 *               wird das Konto bevorzugt, dessen Registrierung die URL enthält
 * @error: Fehler-Rückgabe
 *
 * Ermittelt das Konto-Verzeichnis von SeaDrive (Windows: aus den
 * registrierten Cloud-Sync-Wurzeln des angemeldeten Benutzers). Darunter
 * liegen die Kategorien ("Meine Bibliotheken", "Für meine Gruppen", ...).
 *
 * Returns: (transfer full) (nullable): Pfad oder NULL
 */
gchar* sond_import_detect_seadrive_root(const gchar *server_hint,
                                         GError **error);

/**
 * sond_import_find_library_dir:
 * @seadrive_root: Konto-Verzeichnis von SeaDrive (Konfiguration oder
 *                 sond_import_detect_seadrive_root())
 * @library_name: Name der Library (z.B. "2026-23")
 * @error: Fehler-Rückgabe
 *
 * Sucht das Verzeichnis der Library direkt unter den Kategorien. Ein frisch
 * angelegte Library erscheint in SeaDrive erst nach dessen nächster
 * Aktualisierung (G_IO_ERROR_NOT_FOUND). Mehrfach vorhanden: G_IO_ERROR_EXISTS.
 *
 * Returns: (transfer full) (nullable): Pfad oder NULL
 */
gchar* sond_import_find_library_dir(const gchar *seadrive_root,
                                     const gchar *library_name,
                                     GError **error);

/**
 * SondImportProgress:
 *
 * Fortschritt, wird aus dem Kopier-Thread gemeldet (nicht GTK-Thread).
 */
typedef struct {
    guint  files_total;
    guint  files_done;      /* verarbeitet (kopiert oder fehlgeschlagen) */
    gint64 bytes_total;
    gint64 bytes_done;
    const gchar *current;   /* relativer Pfad der aktuellen Datei */
} SondImportProgress;

typedef void (*SondImportProgressFunc)(const SondImportProgress *progress,
                                        gpointer user_data);

/**
 * SondImportResult:
 *
 * Ergebnis eines Imports. skipped und failed enthalten Zeilen der Form
 * "<relativer Pfad> - <Grund>".
 */
typedef struct {
    guint      copied;      /* kopierte Dateien */
    guint      renamed;     /* davon wegen Namensgleichheit mit Zähler */
    GPtrArray *skipped;     /* übersprungen (Name beginnt mit Punkt, ...) */
    GPtrArray *failed;      /* fehlgeschlagen */
    gboolean   cancelled;
} SondImportResult;

void sond_import_result_free(SondImportResult *result);

/**
 * sond_import_copy:
 * @sources: (element-type utf8): Verzeichnisse und/oder Dateien. Bei einem
 *           Verzeichnis kommt sein INHALT nach target_dir, der relative Aufbau
 *           bleibt erhalten; Dateien kommen direkt nach target_dir.
 * @target_dir: vorhandenes Zielverzeichnis (Library-Wurzel)
 * @progress_func: (nullable): Fortschrittsmeldung, höchstens alle ~100 ms
 * @user_data: Zeiger für progress_func
 * @cancel: (nullable): bei Wert != 0 wird abgebrochen (angefangene Datei
 *          wird wieder entfernt)
 * @error: nur bei unbrauchbaren Argumenten gesetzt
 *
 * Kopiert synchron (für einen Worker-Thread gedacht). Namen mit führendem
 * Punkt werden übersprungen, unzulässige Zeichen ersetzt, die Änderungszeit
 * der Quelle wird auf die Kopie übertragen, bei bereits
 * vorhandenem Namen ein Zähler " (n)" angehängt. Fehler einzelner Dateien
 * brechen den Lauf nicht ab, sondern landen in failed.
 *
 * Returns: (transfer full) (nullable): Ergebnis, NULL nur bei Argumentfehler
 */
SondImportResult* sond_import_copy(GPtrArray *sources,
                                    const gchar *target_dir,
                                    SondImportProgressFunc progress_func,
                                    gpointer user_data,
                                    const gint *cancel,
                                    GError **error);

G_END_DECLS

#endif /* SOND_IMPORT_H */
