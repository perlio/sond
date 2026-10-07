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

/* Obergrenze für die Größe, die ein ZIP-Eintrag laut Header haben darf, wenn
 * er in den Speicher gelesen wird. Die Größe steht im Archiv und kann
 * beschädigt oder gefälscht sein - g_malloc() beendet bei einer solchen
 * Anforderung das Programm. */
#define SOND_ZIP_ENTRY_MAX_SIZE ((guint64) 1 << 30)

#ifdef G_OS_WIN32
#include <wchar.h>
/**
 * Konvertiert einen UTF-8-Pfad in einen Wide-String mit \\?\\ Präfix
 * für Windows Long-Path-Support. Rückgabe mit g_free() freigeben.
 */
wchar_t* prepare_long_path(const gchar *path, GError **error);
#endif

/**
 * Macht aus einem einzelnen Namensteil (kein Pfad) einen unter Windows/SeaDrive
 * zulässigen Dateinamen: < > : " / \ | ? * und Steuerzeichen werden zu '_',
 * ein führender Punkt wird zu '_', Punkte/Leerzeichen am Ende entfallen,
 * ein leerer Name wird zu "_", ein reservierter Gerätename (CON, PRN, AUX,
 * NUL, COM1-9, LPT1-9, auch mit Endung) bekommt '_' hinter dem Stamm.
 * Ein bereits zulässiger Name bleibt unverändert. Rückgabe mit g_free().
 */
gchar* sond_sanitize_filename(const gchar *name);

/**
 * Hängt " (counter)" an den Namen an, bei split_ext vor der letzten Endung
 * (ein Punkt am Namensanfang zählt nicht als Endung). Rückgabe mit g_free().
 */
gchar* sond_filename_add_counter(const gchar *name, guint counter,
		gboolean split_ext);

/**
 * Erstellt ein Verzeichnis (Windows: mit Long-Path-Support)
 */
gboolean sond_mkdir(const gchar *path, GError **error);

/**
 * Prüft ob Datei/Verzeichnis existiert (Windows: mit Long-Path-Support)
 */
gboolean sond_exists(const gchar *path);

/**
 * Erstellt Verzeichnisse rekursiv (Windows: mit Long-Path-Support)
 */
gboolean sond_mkdir_with_parents(const gchar *path, GError **error);

/**
 * Löscht eine Datei (Windows: mit Long-Path-Support)
 */
gboolean sond_remove(const gchar *path, GError **error);

/**
 * Löscht ein leeres Verzeichnis (Windows: mit Long-Path-Support)
 */
gboolean sond_rmdir(const gchar *path, GError **error);

/**
 * Löscht ein Verzeichnis rekursiv mit allen Inhalten (Windows: mit Long-Path-Support)
 */
gboolean sond_rmdir_r(const gchar *path, GError **error);

/**
 * Öffnet eine Datei (Windows: mit Long-Path-Support)
 *
 * @param mode Modus wie bei fopen ("r", "w", "rb", etc.)
 */
FILE* sond_fopen(const gchar *path, const gchar *mode, GError **error);

/**
 * Holt Datei-Statistiken (Windows: mit Long-Path-Support)
 */
gint sond_stat(const gchar *path, GStatBuf *buf, GError **error);

/**
 * Verschiebt/umbenennt eine Datei (Windows: mit Long-Path-Support)
 */
gboolean sond_rename(const gchar *oldpath, const gchar *newpath, GError **error);

/**
 * Kopiert eine Datei (Windows: mit Long-Path-Support)
 */
gboolean sond_copy(const gchar *source, const gchar *dest, gboolean overwrite, GError **error);

/**
 * Kopiert ein Verzeichnis rekursiv mit allen Inhalten (Windows: mit Long-Path-Support)
 */
gboolean sond_copy_r(const gchar *source, const gchar *dest, gboolean overwrite, GError **error);

/**
 * Öffnet eine Datei mit angegebenen Flags als File-Descriptor (Windows: mit Long-Path-Support).
 * Plattformübergreifendes Äquivalent zu g_open() / open().
 *
 * @param flags  Flags wie O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_BINARY etc.
 * @param mode   Berechtigungen (z.B. 0644), nur relevant bei O_CREAT auf POSIX
 * @return File-Descriptor oder -1 bei Fehler
 */
gint sond_open_fd(const gchar *path, gint flags, gint mode, GError **error);

/**
 * Öffnet eine Datei zum Lesen als File-Descriptor (Windows: mit Long-Path-Support).
 * Wrapper um sond_open_fd() mit O_RDONLY | O_BINARY.
 */
gint sond_open_read(const gchar *path, GError **error);

/* Opaque Typ für Verzeichnis-Iteration */
typedef struct _SondDir SondDir;

/**
 * Öffnet ein Verzeichnis für Iteration (Windows: mit Long-Path-Support)
 */
SondDir* sond_dir_open(const gchar *path, GError **error);

/**
 * Liest den nächsten Eintrag aus dem Verzeichnis.
 * Der zurückgegebene String gehört SondDir und darf nicht freigegeben werden.
 */
const gchar* sond_dir_read_name(SondDir *dir);

/**
 * Schließt das Verzeichnis-Handle
 */
void sond_dir_close(SondDir *dir);

/**
 * Öffnet eine Datei/Verzeichnis mit der Standard-Anwendung des Systems
 *
 * @param open_with TRUE für "Öffnen mit"-Dialog, FALSE für direkt öffnen
 */
gboolean sond_open(const gchar *path, gboolean open_with, GError **error);

/**
 * Liest den gesamten Inhalt einer Datei in einen neu allozierten Puffer.
 * Analog zu g_file_get_contents, mit Long-Path-Support unter Windows.
 */
gboolean sond_file_get_contents(const gchar *path, gchar **contents, gsize *length, GError **error);

/**
 * Wie sond_file_get_contents(), liest aber in Blöcken und prüft nach jedem
 * Block *cancel (darf NULL sein). Bei Abbruch: FALSE mit
 * G_IO_ERROR_CANCELLED. Reicht der Speicher nicht, gibt es einen Fehler
 * statt eines Programmendes.
 */
gboolean sond_file_get_contents_cancellable(const gchar *path, gchar **contents,
		gsize *length, gint const *cancel, GError **error);

#endif /* SRC_SOND_FILE_HELPER_H_ */
