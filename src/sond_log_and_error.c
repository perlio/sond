/*
 sond (sond_log_and_error.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  peloamerica

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

#include "sond_log_and_error.h"

#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#include "sond_file_helper.h"

// ============================================================================
// Globale Variablen
// ============================================================================

const gchar *_log_domain = NULL;
FILE *_global_log_file = NULL;

static gchar *_log_file_path = NULL;
static gboolean _is_systemd = FALSE;
static gboolean _has_console = FALSE;

// ============================================================================
// Hilfsfunktionen
// ============================================================================

gboolean has_console(void)
{
#ifdef _WIN32
    // Windows: Prüfe ob Console-Fenster existiert oder stderr an TTY
    return GetConsoleWindow() != NULL || isatty(fileno(stderr));
#else
    // Linux/Unix: Prüfe ob stderr an Terminal gebunden ist
    return isatty(STDERR_FILENO);
#endif
}

static gboolean is_running_under_systemd(void)
{
#ifdef __linux__
    // systemd setzt JOURNAL_STREAM wenn unter systemd gestartet
    return (g_getenv("JOURNAL_STREAM") != NULL);
#else
    return FALSE;
#endif
}

static const gchar* log_level_to_string(GLogLevelFlags log_level)
{
    if (log_level & G_LOG_LEVEL_ERROR)         return "ERROR";
    if (log_level & G_LOG_LEVEL_CRITICAL)      return "CRITICAL";
    if (log_level & G_LOG_LEVEL_WARNING)       return "WARNING";
    if (log_level & G_LOG_LEVEL_MESSAGE)       return "MESSAGE";
    if (log_level & G_LOG_LEVEL_INFO)          return "INFO";
    if (log_level & G_LOG_LEVEL_DEBUG)         return "DEBUG";
    return "UNKNOWN";
}

static void rotate_log_if_needed(const gchar *log_path)
{
    struct stat st;
    if (stat(log_path, &st) == 0) {
        // Wenn größer als 10 MB, rotieren
        if (st.st_size > 10 * 1024 * 1024) {
            gchar *old_path = g_strdup_printf("%s.1", log_path);
            gchar *older_path = g_strdup_printf("%s.2", log_path);
            GError* error = NULL;

            // .2 löschen falls vorhanden
            if (!sond_remove(older_path, &error)) {
            	LOG_WARN("Alte Logdatei konnte nicht gelöscht werden: %s",
            			error->message);
            	g_clear_error(&error);
            }
            // .1 → .2
            if (!sond_rename(old_path, older_path, &error)) {
            	LOG_WARN("Logdatei konnte nicht umbenannt werden: %s",
            			error->message);
            	g_clear_error(&error);
            }
            // current → .1
            if (!sond_rename(log_path, old_path, &error)) {
            	LOG_WARN("Aktuelle Logdatei konnte nicht umbenannt werden: %s",
            			error->message);
            	g_error_free(error);
            }

            g_free(old_path);
            g_free(older_path);
        }
    }
}

// ============================================================================
// Initialisierung
// ============================================================================

void logging_init(const char *app_name)
{
    // Domain setzen
    _log_domain = g_strdup(app_name);

    // Umgebung erkennen
    _is_systemd = is_running_under_systemd();
    _has_console = has_console();

    // Wenn keine Console: Log-Datei einrichten
    if (!_has_console) {
        gchar *log_dir;

        // Windows: %APPDATA%\AppName
        // Linux: ~/.local/share/appname
        log_dir = g_build_filename(g_get_user_data_dir(), app_name, NULL);

        // Verzeichnis erstellen falls nicht vorhanden
        g_mkdir_with_parents(log_dir, 0755);

        // Log-Dateiname aus app_name erstellen
		gchar *log_filename = g_strdup_printf("%s.log", app_name);
		_log_file_path = g_build_filename(log_dir, log_filename, NULL);

		g_free(log_dir);
		g_free(log_filename);

        // Log rotieren falls nötig
        rotate_log_if_needed(_log_file_path);

        // Datei öffnen
        _global_log_file = fopen(_log_file_path, "a");

        if (_global_log_file) {
            // Start-Marker
            GDateTime *now = g_date_time_new_now_local();
            gchar *timestamp = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");
            fprintf(_global_log_file, "\n========== Started at %s ==========\n", timestamp);
            fprintf(_global_log_file, "Application: %s\n", app_name);
            fprintf(_global_log_file, "Log file: %s\n\n", _log_file_path);
            fflush(_global_log_file);
            g_free(timestamp);
            g_date_time_unref(now);
        }
    }

    // Initialisierungs-Info ausgeben
    if (_is_systemd) {
        LOG_INFO("Logging initialized (systemd)");
    } else if (_has_console) {
        fprintf(stderr, "[%s] Logging to console\n", app_name);
    } else if (_global_log_file) {
        fprintf(_global_log_file, "Logging initialized\n");
        fflush(_global_log_file);
    }
}

void logging_cleanup(void)
{
    // Log-Datei schließen
    if (_global_log_file) {
        GDateTime *now = g_date_time_new_now_local();
        gchar *timestamp = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");
        fprintf(_global_log_file, "\n========== Stopped at %s ==========\n\n", timestamp);
        fflush(_global_log_file);
        g_free(timestamp);
        g_date_time_unref(now);

        fclose(_global_log_file);
        _global_log_file = NULL;
    }

    // Pfad freigeben
    g_free(_log_file_path);
    _log_file_path = NULL;

    // Domain freigeben
    if (_log_domain) {
        g_free((gpointer)_log_domain);
        _log_domain = NULL;
    }
}

// ============================================================================
// Hauptfunktion: Log-Message verarbeiten
// ============================================================================

void log_message_internal(const gchar *log_domain,
                         GLogLevelFlags log_level,
                         const gchar *file,
                         int line,
                         const gchar *func,
                         const gchar *message)
{
    // Fallback wenn Domain nicht gesetzt
    if (!log_domain) {
        log_domain = "unknown";
    }

    // 1. Wenn systemd: Strukturiert loggen
    if (_is_systemd) {
        gchar line_str[32];
        g_snprintf(line_str, sizeof(line_str), "%d", line);

        g_log_structured(log_domain, log_level,
                        "MESSAGE", message,
                        "CODE_FILE", file,
                        "CODE_LINE", line_str,
                        "CODE_FUNC", func,
                        NULL);
    }

    // 2. Wenn Console: Formatiert ausgeben mit Farben
    if (_has_console) {
        GDateTime *now = g_date_time_new_now_local();
        gchar *timestamp = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");

        // ANSI-Farbcodes für Linux Terminal
        const char *color_start = "";
        const char *color_end = "";

#ifndef _WIN32
        if (isatty(STDERR_FILENO)) {
            if (log_level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL)) {
                color_start = "\033[1;31m";  // Rot, fett
            } else if (log_level & G_LOG_LEVEL_WARNING) {
                color_start = "\033[1;33m";  // Gelb, fett
            } else if (log_level & G_LOG_LEVEL_DEBUG) {
                color_start = "\033[0;36m";  // Cyan
            }
            if (*color_start) color_end = "\033[0m";
        }
#endif

        fprintf(stderr, "%s[%s] [%s] [%s] [%s:%d in %s()] %s%s\n",
                color_start,
                timestamp,
                log_domain,
                log_level_to_string(log_level),
                file, line, func,
                message,
                color_end);
        fflush(stderr);

        g_free(timestamp);
        g_date_time_unref(now);
    }

    // 3. Wenn Log-Datei: In Datei schreiben
    if (_global_log_file) {
        GDateTime *now = g_date_time_new_now_local();
        gchar *timestamp = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");

        fprintf(_global_log_file, "[%s] [%s] [%s] [%s:%d in %s()] %s\n",
                timestamp,
                log_domain,
                log_level_to_string(log_level),
                file, line, func,
                message);
        fflush(_global_log_file);

        g_free(timestamp);
        g_date_time_unref(now);
    }

    // 4. Fallback wenn nichts anderes greift
    if (!_is_systemd && !_has_console && !_global_log_file) {
        g_log(log_domain, log_level, "[%s:%d in %s()] %s",
              file, line, func, message);
    }
}
