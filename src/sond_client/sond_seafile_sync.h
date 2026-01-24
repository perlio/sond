/*
 * sond (sond_seafile_sync.h) - Seafile RPC Integration
 * Copyright (C) 2026 pelo america
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file sond_seafile_sync.h
 * @brief Seafile RPC Integration für Library-Synchronisation
 *
 * Kommuniziert mit dem lokalen Seafile-Client via Named Pipe (Windows)
 * oder Unix Domain Socket (Linux) um Libraries zu synchronisieren.
 */

#ifndef SOND_SEAFILE_SYNC_H
#define SOND_SEAFILE_SYNC_H

typedef struct _SondClient SondClient;

#include <glib.h>

G_BEGIN_DECLS

/**
 * sond_seafile_find_library_by_name:
 * @library_name: Name der Library (z.B. "2026-1")
 * @error: Fehler-Rückgabe
 *
 * Sucht eine Seafile Library anhand des Namens.
 *
 * Returns: (transfer full) (nullable): Library ID oder NULL bei Fehler
 */
gchar* sond_seafile_find_library_by_name(const gchar *library_name,
                                          GError **error);

/**
 * sond_seafile_get_library_id_from_server:
 * @client: SondClient Instanz für HTTP-Requests
 * @library_name: Name der Library (z.B. "2026-1")
 * @error: Fehler-Rückgabe
 *
 * Holt die Library-ID vom sond_server (der fragt Seafile-Server).
 * Nutzen wenn Library noch nicht lokal synchronisiert ist.
 *
 * Returns: (transfer full) (nullable): Library ID oder NULL bei Fehler
 */
gchar* sond_seafile_get_library_id_from_server(SondClient *client,
                                                 const gchar *library_name,
                                                 GError **error);

/**
 * sond_seafile_sync_library:
 * @SondClient* client: SondClient Instanz für HTTP-Requests
 * @library_id: Seafile Library ID
 * @local_path: Lokaler Pfad für Synchronisation
 * @error: Fehler-Rückgabe
 *
 * Startet die Synchronisation einer Library mit einem lokalen Verzeichnis.
 * Der Seafile-Client muss laufen.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_seafile_sync_library(SondClient* client,
									const gchar *library_id,
									const gchar* library_name,
                                    const gchar *local_path,
                                    GError **error);

/**
 * sond_seafile_unsync_library:
 * @library_id: Seafile Library ID
 * @error: Fehler-Rückgabe
 *
 * Stoppt die Synchronisation einer Library.
 * Die lokalen Dateien bleiben erhalten.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_seafile_unsync_library(const gchar *library_id,
                                      GError **error);

/**
 * sond_seafile_get_sync_status:
 * @library_id: Seafile Library ID
 * @error: Fehler-Rückgabe
 *
 * Holt den aktuellen Sync-Status einer Library.
 *
 * Mögliche Status-Strings:
 * - "synchronized" - Vollständig synchronisiert
 * - "syncing" - Synchronisation läuft
 * - "error" - Fehler beim Synchronisieren
 * - NULL - Library wird nicht synchronisiert
 *
 * Returns: (transfer full) (nullable): Status-String oder NULL
 */
gchar* sond_seafile_get_sync_status(const gchar *library_id,
                                     GError **error);

/**
 * sond_seafile_test_connection:
 * @error: Fehler-Rückgabe
 *
 * Testet ob der Seafile-Client erreichbar ist.
 * Nützlich für Diagnose.
 *
 * Returns: TRUE wenn Verbindung erfolgreich
 */
gboolean sond_seafile_test_connection(GError **error);

/**
 * sond_seafile_is_repo_in_sync:
 * @library_id: Seafile Library ID
 * @error: Fehler-Rückgabe
 *
 * Prüft ob ein Repository vollständig synchronisiert ist.
 * Der seaf-daemon kümmert sich selbstständig um die Synchronisation,
 * diese Funktion fragt nur den aktuellen Status ab.
 *
 * Returns: TRUE wenn vollständig synchronisiert, FALSE sonst
 */
gboolean sond_seafile_is_repo_in_sync(const gchar *library_id,
                                       GError **error);

G_END_DECLS

#endif /* SOND_SEAFILE_SYNC_H */
