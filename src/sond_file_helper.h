/*
 sond (sond_file_helper.h) - Akten, Beweisstücke, Unterlagen
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



#ifndef SRC_SOND_FILE_HELPER_H_
#define SRC_SOND_FILE_HELPER_H_

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
/**
 * Erstellt ein Verzeichnis (Windows: mit Long-Path-Support)
 *
 * @param path UTF-8-kodierter Pfad (kann relativ oder absolut sein)
 * @param error Optionaler GError für Fehlermeldungen
 * @return TRUE bei Erfolg, FALSE bei Fehler
 */
gboolean sond_mkdir(const gchar *path, GError **error);

/**
 * Prüft ob Datei/Verzeichnis existiert (Windows: mit Long-Path-Support)
 *
 * @param path UTF-8-kodierter Pfad
 * @return TRUE wenn existiert, FALSE sonst
 */
gboolean sond_exists(const gchar *path);

/**
 * Erstellt Verzeichnisse rekursiv (Windows: mit Long-Path-Support)
 *
 * @param path UTF-8-kodierter Pfad
 * @param error Optionaler GError für Fehlermeldungen
 * @return TRUE bei Erfolg, FALSE bei Fehler
 */
gboolean sond_mkdir_with_parents(const gchar *path, GError **error);

/**
 * Löscht eine Datei (Windows: mit Long-Path-Support)
 *
 * @param path UTF-8-kodierter Pfad
 * @param error Optionaler GError für Fehlermeldungen
 * @return TRUE bei Erfolg, FALSE bei Fehler
 */
gboolean sond_remove(const gchar *path, GError **error);

/**
 * Löscht ein leeres Verzeichnis (Windows: mit Long-Path-Support)
 *
 * @param path UTF-8-kodierter Pfad
 * @param error Optionaler GError für Fehlermeldungen
 * @return TRUE bei Erfolg, FALSE bei Fehler
 */
gboolean sond_rmdir(const gchar *path, GError **error);

/**
 * Löscht ein Verzeichnis rekursiv mit allen Inhalten (Windows: mit Long-Path-Support)
 *
 * @param path UTF-8-kodierter Pfad
 * @param error Optionaler GError für Fehlermeldungen
 * @return TRUE bei Erfolg, FALSE bei Fehler
 */
gboolean sond_rmdir_r(const gchar *path, GError **error);

/**
 * Öffnet eine Datei (Windows: mit Long-Path-Support)
 *
 * @param path UTF-8-kodierter Pfad
 * @param mode Modus wie bei fopen ("r", "w", "rb", etc.)
 * @param error Optionaler GError für Fehlermeldungen
 * @return FILE* bei Erfolg, NULL bei Fehler
 */
FILE* sond_fopen(const gchar *path, const gchar *mode, GError **error);

/**
 * Holt Datei-Statistiken (Windows: mit Long-Path-Support)
 *
 * @param path UTF-8-kodierter Pfad
 * @param buf Pointer auf GStatBuf-Struktur
 * @param error Optionaler GError für Fehlermeldungen
 * @return 0 bei Erfolg, -1 bei Fehler
 */
gint sond_stat(const gchar *path, GStatBuf *buf, GError **error);

/**
 * Verschiebt/umbenennt eine Datei (Windows: mit Long-Path-Support)
 *
 * @param oldpath UTF-8-kodierter alter Pfad
 * @param newpath UTF-8-kodierter neuer Pfad
 * @param error Optionaler GError für Fehlermeldungen
 * @return TRUE bei Erfolg, FALSE bei Fehler
 */
gboolean sond_rename(const gchar *oldpath, const gchar *newpath, GError **error);

/**
 * Kopiert eine Datei (Windows: mit Long-Path-Support)
 *
 * @param source UTF-8-kodierter Quellpfad
 * @param dest UTF-8-kodierter Zielpfad
 * @param overwrite TRUE um Zieldatei zu überschreiben, FALSE sonst
 * @param error Optionaler GError für Fehlermeldungen
 * @return TRUE bei Erfolg, FALSE bei Fehler
 */
gboolean sond_copy(const gchar *source, const gchar *dest, gboolean overwrite, GError **error);

/**
 * Kopiert ein Verzeichnis rekursiv mit allen Inhalten (Windows: mit Long-Path-Support)
 *
 * @param source UTF-8-kodierter Quellpfad
 * @param dest UTF-8-kodierter Zielpfad
 * @param overwrite TRUE um existierende Dateien zu überschreiben, FALSE sonst
 * @param error Optionaler GError für Fehlermeldungen
 * @return TRUE bei Erfolg, FALSE bei Fehler
 */
gboolean sond_copy_r(const gchar *source, const gchar *dest, gboolean overwrite, GError **error);

/**
 * Öffnet File-Handle für MuPDF etc. (Windows: mit Long-Path-Support)
 *
 * @param path UTF-8-kodierter Pfad
 * @param error Optionaler GError für Fehlermeldungen
 * @return File-Descriptor oder -1 bei Fehler
 */
gint sond_open_read(const gchar *path, GError **error);

/* Opaque Typ für Verzeichnis-Iteration */
typedef struct _SondDir SondDir;

/**
 * Öffnet ein Verzeichnis für Iteration (Windows: mit Long-Path-Support)
 *
 * @param path UTF-8-kodierter Pfad zum Verzeichnis
 * @param error Optionaler GError für Fehlermeldungen
 * @return SondDir* bei Erfolg, NULL bei Fehler
 */
SondDir* sond_dir_open(const gchar *path, GError **error);

/**
 * Liest den nächsten Eintrag aus dem Verzeichnis
 *
 * @param dir Verzeichnis-Handle von sond_dir_open
 * @return Dateiname (UTF-8) oder NULL wenn keine weiteren Einträge
 *         Der zurückgegebene String gehört SondDir und darf nicht freigegeben werden
 */
const gchar* sond_dir_read_name(SondDir *dir);

/**
 * Schließt das Verzeichnis-Handle
 *
 * @param dir Verzeichnis-Handle von sond_dir_open
 */
void sond_dir_close(SondDir *dir);

/**
 * Öffnet eine Datei/Verzeichnis mit der Standard-Anwendung des Systems
 *
 * @param path UTF-8-kodierter Pfad
 * @param open_with TRUE für "Öffnen mit"-Dialog, FALSE für direkt öffnen
 * @param error Optionaler GError für Fehlermeldungen
 * @return TRUE bei Erfolg, FALSE bei Fehler
 */
gboolean sond_open(const gchar *path, gboolean open_with, GError **error);

#endif /* SRC_SOND_FILE_HELPER_H_ */
