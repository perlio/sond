/*
 sond (sond_client.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026 pelo america

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

#ifndef SOND_CLIENT_H
#define SOND_CLIENT_H

#include <gtk/gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _SondClient SondClient;

#define SOND_TYPE_CLIENT (sond_client_get_type())
G_DECLARE_FINAL_TYPE(SondClient, sond_client, SOND, CLIENT, GObject)

/* ========================================================================
 * Fehler beim Speichern/Loeschen von Nodes (generisch fuer alle Module)
 * ======================================================================== */

#define SOND_CLIENT_ERROR (sond_client_error_quark())

/**
 * SondClientErrorCode:
 * @SOND_CLIENT_ERROR_FAILED: sonstiger Fehler
 * @SOND_CLIENT_ERROR_LOCK_NOT_HELD: Dieser Client haelt den Lock auf den Knoten
 *   nicht (z.B. weil der eigene Lock gebrochen wurde). Ob und von wem der Knoten
 *   stattdessen gesperrt ist, steht in locked_by (leer = niemand)
 * @SOND_CLIENT_ERROR_GONE: Knoten (oder ein verknuepfter Knoten) wurde
 *   zwischenzeitlich geloescht
 *
 * Die Fehlermeldung (error->message) ist bereits fuer Benutzer formuliert.
 */
typedef enum {
    SOND_CLIENT_ERROR_FAILED,
    SOND_CLIENT_ERROR_LOCK_NOT_HELD,
    SOND_CLIENT_ERROR_GONE
} SondClientErrorCode;

GQuark sond_client_error_quark(void);

/**
 * SondClient::node-conflict:
 * @client: der #SondClient
 * @node_id: betroffener Knoten
 * @code: #SondClientErrorCode (LOCK_NOT_HELD oder GONE)
 * @locked_by: (nullable): bei LOCK_NOT_HELD der aktuelle Lock-Inhaber (NULL/leer: niemand haelt den Lock)
 * @locked_at: (nullable): bei LOCK_NOT_HELD Zeitpunkt, seit dem der Inhaber den Lock haelt
 *
 * Wird synchron ausgeloest, wenn der Server einen Schreibzugriff auf den
 * Knoten ablehnt (save/delete: LOCK_NOT_HELD, GONE). Jedes Modul, das den Knoten anzeigt, verbindet sich damit und
 * reagiert nur auf seinen eigenen @node_id. GONE wird nur gemeldet, wenn der
 * Knoten selbst (nicht nur ein verknuepfter Knoten) nicht mehr existiert.
 *
 * Signatur des Handlers:
 *   void handler(SondClient *client, gint64 node_id, guint code,
 *                const gchar *locked_by, const gchar *locked_at, gpointer user_data);
 */

/**
 * sond_client_error_is_conflict:
 * @error: (nullable): Fehler
 *
 * Returns: TRUE bei LOCK_NOT_HELD oder GONE (Modul sollte den Knoten neu laden)
 */
gboolean sond_client_error_is_conflict(const GError *error);

/**
 * sond_client_error_get_missing_ids:
 * @error: Fehler mit Code SOND_CLIENT_ERROR_GONE
 *
 * Returns: (transfer none) (nullable): #GArray mit gint64-IDs der nicht mehr
 *   vorhandenen Knoten (der Knoten selbst und/oder Ziele seiner Edges)
 */
const GArray* sond_client_error_get_missing_ids(const GError *error);

/**
 * sond_client_error_get_locked_by:
 * @error: Fehler mit Code SOND_CLIENT_ERROR_LOCK_NOT_HELD
 *
 * Returns: (transfer none) (nullable): Username des aktuellen Lock-Inhabers
 */
const gchar* sond_client_error_get_locked_by(const GError *error);

/**
 * sond_client_error_get_locked_at:
 * @error: Fehler mit Code SOND_CLIENT_ERROR_LOCK_NOT_HELD
 *
 * Returns: (transfer none) (nullable): Zeitpunkt, seit dem der Inhaber den Lock haelt
 */
const gchar* sond_client_error_get_locked_at(const GError *error);

/**
 * sond_client_new:
 * @config_file: Pfad zur Konfigurationsdatei
 * @error: (nullable): Fehler-Rückgabe
 *
 * Erstellt einen neuen SondClient.
 *
 * Returns: (transfer full) (nullable): Neuer Client oder NULL bei Fehler
 */
SondClient* sond_client_new(const gchar *config_file, GError **error);

/**
 * sond_client_get_server_url:
 * @client: SondClient
 *
 * Returns: Server-URL
 */
const gchar* sond_client_get_server_url(SondClient *client);

/**
 * sond_client_connect:
 * @client: SondClient
 * @error: (nullable): Fehler-Rückgabe
 *
 * Stellt Verbindung zum Server her.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_connect(SondClient *client, GError **error);

/**
 * sond_client_get_user_id:
 * @client: SondClient
 *
 * Gibt die User-ID zurück (Seafile-Username).
 *
 * Returns: (transfer none): User-ID String
 */
const gchar* sond_client_get_user_id(SondClient *client);

/**
 * sond_client_is_logged_in:
 * @client: SondClient
 *
 * Besteht eine Anmeldung am Server? Löst keinen Login aus und fragt den
 * Server nicht.
 *
 * Returns: TRUE, wenn Sitzung und Benutzer gesetzt sind
 */
gboolean sond_client_is_logged_in(SondClient *client);

const gchar* sond_client_get_user_token(SondClient *client);

/**
 * sond_client_set_auth:
 * @client: SondClient
 * @username: Seafile-Username
 * @session_token: Session-Token vom Server
 *
 * Setzt Authentifizierungsinformationen.
 * Wird nach erfolgreichem Login aufgerufen.
 */
void sond_client_set_auth(SondClient *client,
                          const gchar *username,
                          const gchar *session_token,
						  const gchar *seafile_token,
						  const gchar *seafile_url);

/**
 * sond_client_set_seafile_url:
 * @client: SondClient
 * @seafile_url: Seafile-Server URL (z.B. "https://seafile.example.com")
 *
 * Setzt die Seafile-Server URL.
 * Wird nach erfolgreichem Login aufgerufen.
 */
void sond_client_set_seafile_url(SondClient *client,
                                  const gchar *seafile_url);

/**
 * sond_client_get_seafile_url:
 * @client: SondClient
 *
 * Gibt die Seafile-Server URL zurück.
 *
 * Returns: (transfer none): Seafile URL oder NULL
 */
const gchar* sond_client_get_seafile_url(SondClient *client);

/**
 * sond_client_set_auth_failed_callback:
 * @client: SondClient
 * @callback: Callback-Funktion bei 401-Fehler
 * @user_data: User-Data für Callback
 *
 * Setzt Callback der bei 401-Fehler aufgerufen wird.
 * Wird für UI-Benachrichtigungen verwendet.
 */
void sond_client_set_auth_failed_callback(SondClient *client,
                                          void (*callback)(SondClient*, gpointer),
                                          gpointer user_data);

/**
 * sond_client_set_login_callback:
 * @client: SondClient
 * @callback: Callback-Funktion für Login (Lazy-Auth und Re-Login)
 * @user_data: User-Data für Callback
 *
 * Setzt Callback für Login/Re-Login.
 * Callback gibt TRUE bei Erfolg zurück, FALSE bei Abbruch.
 * Nach Login muss sond_client_set_auth() aufgerufen werden.
 */
void sond_client_set_login_callback(SondClient *client,
                                    gboolean (*callback)(SondClient*, gpointer),
                                    gpointer user_data);

/**
 * sond_client_create_and_lock_node:
 * @client: SondClient
 * @node: Node zum Anlegen
 * @lock_reason: (nullable): Grund für Lock
 * @error: (nullable): Fehler-Rückgabe
 *
 * Erstellt Node und setzt Lock atomar auf Server.
 * Node-ID wird aktualisiert.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei 401 wird automatisch Re-Login durchgeführt.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_create_and_lock_node(SondClient *client,
                                           gpointer node,
                                           const gchar *lock_reason,
                                           GError **error);

/**
 * sond_client_lock_node:
 * @client: SondClient
 * @node_id: Node-ID
 * @lock_reason: (nullable): Grund für Lock
 * @error: (nullable): Fehler-Rückgabe
 *
 * Setzt Lock auf existierenden Node.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei 401 wird automatisch Re-Login durchgeführt.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_lock_node(SondClient *client,
                                gint64 node_id,
                                const gchar *lock_reason,
                                GError **error);

/**
 * sond_client_lock_node_ex:
 * @client: SondClient
 * @node_id: Node-ID
 * @lock_reason: (nullable): Grund für Lock
 * @out_locked_by: (out) (nullable) (transfer full): bei Konflikt (409) der
 *   aktuelle Lock-Inhaber
 * @out_locked_at: (out) (nullable) (transfer full): bei Konflikt (409)
 *   Zeitpunkt der letzten Lock-Erneuerung durch den Inhaber
 * @error: (nullable): Fehler-Rückgabe
 *
 * Wie sond_client_lock_node(), liefert bei Konflikt aber zusätzlich
 * Inhaber/Zeitpunkt zurück, für die Anzeige im Client ("Gesperrt von X,
 * seit Y") und den "Lock brechen"-Button.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_lock_node_ex(SondClient *client,
                                   gint64 node_id,
                                   const gchar *lock_reason,
                                   gchar **out_locked_by,
                                   gchar **out_locked_at,
                                   GError **error);

/**
 * sond_client_unlock_node:
 * @client: SondClient
 * @node_id: Node-ID
 * @error: (nullable): Fehler-Rückgabe
 *
 * Entfernt Lock von Node.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei 401 wird automatisch Re-Login durchgeführt.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_unlock_node(SondClient *client,
                                  gint64 node_id,
                                  GError **error);

/**
 * sond_client_force_unlock_node:
 * @client: SondClient
 * @node_id: Node-ID
 * @error: (nullable): Fehler-Rückgabe
 *
 * "Lock brechen": entfernt den Lock unabhängig davon, wer ihn hält. Für den
 * Fall, dass ein anderer User den Node offensichtlich verwaist gesperrt hat
 * (Crash/lange Inaktivität) und jemand anderes ihn übernehmen will.
 * Wird serverseitig geloggt.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_force_unlock_node(SondClient *client,
                                        gint64 node_id,
                                        GError **error);

/**
 * sond_client_check_lock:
 * @client: SondClient
 * @node_id: Node-ID
 * @error: (nullable): Fehler-Rückgabe
 *
 * Prüft ob Node gelockt ist.
 *
 * Returns: (transfer full) (nullable): User-ID des Lockers oder NULL wenn nicht gelockt
 */
gchar* sond_client_check_lock(SondClient *client,
                               gint64 node_id,
                               GError **error);

/**
 * sond_client_search_nodes:
 * @client: SondClient
 * @criteria: Suchkriterien (SondGraphNodeSearchCriteria*)
 * @error: (nullable): Fehler-Rückgabe
 *
 * Sucht Nodes auf dem Server.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei 401 wird automatisch Re-Login durchgeführt.
 *
 * Returns: (transfer full) (element-type SondGraphNode) (nullable): Array von Nodes oder NULL bei Fehler
 */
GPtrArray* sond_client_search_nodes(SondClient *client,
                                     gpointer criteria,
                                     GError **error);

/**
 * sond_client_save_node:
 * @client: SondClient
 * @node: Node zum Speichern (SondGraphNode*)
 * @error: (nullable): Fehler-Rückgabe
 *
 * Speichert einen Node auf dem Server.
 * Die Node-ID wird aus der Serverantwort uebernommen.
 * Fehler (error->domain == SOND_CLIENT_ERROR): LOCK_NOT_HELD, GONE -
 * s. #SondClientErrorCode.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei 401 wird automatisch Re-Login durchgeführt.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_save_node(SondClient *client,
                                gpointer node,
                                GError **error);

/**
 * sond_client_delete_node:
 * @client: SondClient
 * @node: (type SondGraphNode): Zu loeschender Node (nur die ID wird mitgeschickt)
 * @error: (nullable): Fehler-Rückgabe
 *
 * Löscht einen Node vom Server (nur mit eigenem Lock).
 * Fehler (error->domain == SOND_CLIENT_ERROR): LOCK_NOT_HELD, GONE.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei 401 wird automatisch Re-Login durchgeführt.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_delete_node(SondClient *client,
                                  gpointer node,
                                  GError **error);

/**
 * sond_client_create_seafile_library:
 * @client: SondClient
 * @name: Library-Name
 * @description: (nullable): Beschreibung
 * @bezeichnung: (nullable): Aktenbezeichnung; der Server legt sie als
 *   .sond/akte/<Name> in der Library ab (Suche ohne Server)
 * @error: (nullable): Fehler-Rückgabe
 *
 * Erstellt eine Seafile Library.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei 401 wird automatisch Re-Login durchgeführt.
 *
 * Returns: (transfer full) (nullable): Library-ID oder NULL bei Fehler
 */
gchar* sond_client_create_seafile_library(SondClient *client,
                                          const gchar *name,
                                          const gchar *description,
                                          const gchar *bezeichnung,
                                          GError **error);

/**
 * sond_client_set_library_bezeichnung:
 * @client: SondClient
 * @library_name: Library-Name (z.B. "2026-1")
 * @bezeichnung: neue Aktenbezeichnung
 * @error: (nullable): Fehler-Rückgabe
 *
 * Setzt bzw. ersetzt die Aktenbezeichnung (.sond/akte) der Library.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_set_library_bezeichnung(SondClient *client,
                                             const gchar *library_name,
                                             const gchar *bezeichnung,
                                             GError **error);

/**
 * sond_client_get_seafile_library_id:
 * @client: SondClient
 * @library_name: Library-Name (z.B. "2026-1")
 * @error: (nullable): Fehler-Rückgabe
 *
 * Holt die Library-ID vom Server anhand des Namens.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei 401 wird automatisch Re-Login durchgeführt.
 *
 * Returns: (transfer full) (nullable): Library-ID oder NULL bei Fehler
 */
gchar* sond_client_get_seafile_library_id(SondClient *client,
                                           const gchar *library_name,
                                           GError **error);

/**
 * sond_client_set_current_akte:
 * @client: SondClient
 * @lfd_nr: Laufende Nummer
 * @year: Jahr (vollständig, z.B. 2026)
 *
 * Merkt die gerade im Aktenmodul angezeigte Akte, damit andere Module sie
 * vorschlagen können (nur Hinweis, kein Lock, keine Zusage).
 */
void sond_client_set_current_akte(SondClient *client, guint lfd_nr, guint year);

/**
 * sond_client_clear_current_akte:
 * @client: SondClient
 *
 * Es wird keine Akte mehr angezeigt.
 */
void sond_client_clear_current_akte(SondClient *client);

/**
 * sond_client_get_current_akte:
 * @client: SondClient
 * @lfd_nr: (out) (optional): Laufende Nummer
 * @year: (out) (optional): Jahr
 *
 * Returns: TRUE, wenn gerade eine Akte angezeigt wird
 */
gboolean sond_client_get_current_akte(SondClient *client, guint *lfd_nr, guint *year);

/**
 * sond_client_get_seadrive_root:
 * @client: SondClient
 *
 * Gibt die in der Konfiguration angegebene SeaDrive-Wurzel (Abschnitt
 * [seadrive], Schlüssel root) zurück, das Konto-Verzeichnis von SeaDrive.
 *
 * Returns: (transfer none) (nullable): Pfad oder NULL, wenn nicht konfiguriert
 */
const gchar* sond_client_get_seadrive_root(SondClient *client);

/**
 * sond_client_set_last_regnr:
 * @client: SondClient
 * @lfd_nr: Laufende Nummer
 * @year: Jahr (vollständig, z.B. 2026)
 *
 * Speichert die zuletzt verwendete RegNr (während Laufzeit).
 */
void sond_client_set_last_regnr(SondClient *client, guint lfd_nr, guint year);

/**
 * sond_client_get_last_regnr:
 * @client: SondClient
 * @lfd_nr: (out): Laufende Nummer
 * @year: (out): Jahr
 *
 * Gibt die zuletzt verwendete RegNr zurück.
 *
 * Returns: TRUE wenn eine RegNr gespeichert ist, FALSE sonst
 */
gboolean sond_client_get_last_regnr(SondClient *client, guint *lfd_nr, guint *year);

/**
 * OcrStatus:
 *
 * Status eines OCR-Jobs
 */
typedef struct {
    gboolean running;
    gint total_files;
    gint processed_files;
    gint processed_pdfs;
    gchar *current_file;
    GDateTime *start_time;
    GDateTime *end_time;
} OcrStatus;

/**
 * sond_client_ocr_start_job:
 * @client: SondClient
 * @library_name: Name der Library (z.B. "2026-23")
 * @force_reprocess: TRUE = alle Dateien neu verarbeiten
 * @error: (nullable): Fehler-Rückgabe
 *
 * Startet einen OCR-Job auf dem Server.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei 401 wird automatisch Re-Login durchgeführt.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_ocr_start_job(SondClient *client,
                                   const gchar *library_name,
                                   gboolean force_reprocess,
                                   GError **error);

/**
 * sond_client_ocr_get_status:
 * @client: SondClient
 * @library_name: Name der Library
 * @error: (nullable): Fehler-Rückgabe
 *
 * Holt OCR-Status für eine Library.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei 401 wird automatisch Re-Login durchgeführt.
 *
 * Returns: (transfer full) (nullable): OcrStatus oder NULL.
 *          NULL bedeutet: Kein Job gefunden (404) - das ist kein Fehler!
 *          Muss mit sond_client_ocr_status_free() freigegeben werden.
 */
OcrStatus* sond_client_ocr_get_status(SondClient *client,
                                      const gchar *library_name,
                                      GError **error);

/**
 * sond_client_ocr_status_free:
 * @status: OcrStatus Instanz
 *
 * Gibt OcrStatus frei.
 */
void sond_client_ocr_status_free(OcrStatus *status);

G_END_DECLS

#endif /* SOND_CLIENT_H */
