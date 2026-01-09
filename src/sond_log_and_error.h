/*
 sond (sond_log_and_error.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef LOGGING_H
#define LOGGING_H

#include <glib.h>
#include <stdio.h>

// ============================================================================
// Globale Variablen (extern)
// ============================================================================

extern const gchar *_log_domain;
extern FILE *_global_log_file;

// ============================================================================
// Initialisierung und Cleanup
// ============================================================================

/**
 * logging_init:
 * @app_name: Name der Anwendung (wird als Log-Domain verwendet)
 *
 * Initialisiert das Logging-System.
 * Erkennt automatisch: systemd, Console oder Log-Datei
 */
void logging_init(const char *app_name);

/**
 * logging_cleanup:
 *
 * Schließt Log-Dateien und gibt Ressourcen frei
 */
void logging_cleanup(void);

/**
 * has_console:
 *
 * Returns: TRUE wenn eine Console verfügbar ist
 */
gboolean has_console(void);

// ============================================================================
// Logging-Makros
// ============================================================================

/**
 * LOG_MSG:
 * @log_level: GLogLevelFlags (G_LOG_LEVEL_DEBUG, MESSAGE, WARNING, CRITICAL)
 * @...: printf-style Format-String und Argumente
 *
 * Haupt-Logging-Makro mit der globalen Domain
 */
#define LOG_MSG(log_level, fmt, ...) \
    do { \
        gchar *_formatted_msg = g_strdup_printf(fmt, ##__VA_ARGS__); \
        log_message_internal(_log_domain, log_level, \
                            __FILE__, __LINE__, __func__, \
                            _formatted_msg); \
        g_free(_formatted_msg); \
    } while(0)

/**
 * LOG_MSG_DOMAIN:
 * @domain: Spezifische Log-Domain (String)
 * @log_level: GLogLevelFlags
 * @...: printf-style Format-String und Argumente
 *
 * Wie LOG_MSG, aber mit expliziter Domain
 */
#define LOG_MSG_DOMAIN(domain, log_level, ...) \
    do { \
        gchar *_formatted_msg = g_strdup_printf(__VA_ARGS__); \
        log_message_internal(domain, log_level, \
                            __FILE__, __LINE__, __func__, \
                            _formatted_msg); \
        g_free(_formatted_msg); \
    } while(0)

// ============================================================================
// Convenience-Makros
// ============================================================================

/**
 * LOG_DEBUG:
 * Für Debug-Informationen (nur wenn G_MESSAGES_DEBUG gesetzt)
 */
#define LOG_DEBUG(...)   LOG_MSG(G_LOG_LEVEL_DEBUG, __VA_ARGS__)

/**
 * LOG_INFO:
 * Für normale Informationsmeldungen
 */
#define LOG_INFO(...)    LOG_MSG(G_LOG_LEVEL_MESSAGE, __VA_ARGS__)

/**
 * LOG_WARN:
 * Für Warnungen
 */
#define LOG_WARN(...)    LOG_MSG(G_LOG_LEVEL_WARNING, __VA_ARGS__)

/**
 * LOG_ERROR:
 * Für kritische Fehler (aber nicht fatal)
 */
#define LOG_ERROR(...)   LOG_MSG(G_LOG_LEVEL_CRITICAL, __VA_ARGS__)

// ============================================================================
// Interne Funktion (nicht direkt aufrufen)
// ============================================================================

void log_message_internal(const gchar *log_domain,
                         GLogLevelFlags log_level,
                         const gchar *file,
                         int line,
                         const gchar *func,
                         const gchar *message);

#endif // LOGGING_H
