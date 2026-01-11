/*
 sond (sond_akte.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_akte.h
 * @brief High-Level API für Akte-Nodes
 *
 * Wrapper um Graph-Nodes speziell für Akten.
 */

#ifndef SOND_AKTE_H
#define SOND_AKTE_H

#include <glib-object.h>
#include <mysql/mysql.h>
#include "sond_graph_node.h"

G_BEGIN_DECLS

/**
 * SondAkte:
 *
 * Ein Akte ist ein spezialisierter Graph-Node mit Label "Akte".
 *
 * Properties:
 * - regnr: [Jahr, lfdnr] - Registriernummer
 * - name: [Name] - Kurzname
 * - langbezeichnung: [Text] - Ausführliche Bezeichnung
 * - angelegt_am: [Datum] - Anlagedatum (ISO 8601: YYYY-MM-DD)
 * - abgelegt: [Datum, ablagenummer] - Ablagedatum und Nummer (optional)
 */
typedef SondGraphNode SondAkte;

/* ========================================================================
 * Konstruktoren
 * ======================================================================== */

/**
 * sond_akte_new:
 * @jahr: Jahr der Registriernummer (z.B. "2024")
 * @lfdnr: Laufende Nummer (z.B. "00123")
 *
 * Erstellt eine neue Akte mit Registriernummer.
 * Label wird automatisch auf "Akte" gesetzt.
 *
 * Returns: (transfer full): Neue Akte
 */
SondAkte* sond_akte_new(const gchar *jahr, const gchar *lfdnr);

/**
 * sond_akte_new_full:
 * @jahr: Jahr der Registriernummer
 * @lfdnr: Laufende Nummer
 * @name: Kurzname der Akte
 * @langbezeichnung: Ausführliche Bezeichnung
 *
 * Erstellt eine neue Akte mit allen Pflichtfeldern.
 *
 * Returns: (transfer full): Neue Akte
 */
SondAkte* sond_akte_new_full(const gchar *jahr,
                              const gchar *lfdnr,
                              const gchar *name,
                              const gchar *langbezeichnung);

/* ========================================================================
 * Registriernummer
 * ======================================================================== */

/**
 * sond_akte_get_jahr:
 * @akte: Akte
 *
 * Returns: (transfer full) (nullable): Jahr oder %NULL
 */
gchar* sond_akte_get_jahr(SondAkte *akte);

/**
 * sond_akte_get_lfdnr:
 * @akte: Akte
 *
 * Returns: (transfer full) (nullable): Laufende Nummer oder %NULL
 */
gchar* sond_akte_get_lfdnr(SondAkte *akte);

/**
 * sond_akte_get_regnr:
 * @akte: Akte
 * @jahr: (out) (transfer full) (nullable): Jahr
 * @lfdnr: (out) (transfer full) (nullable): Laufende Nummer
 *
 * Gibt beide Teile der Registriernummer zurück.
 *
 * Returns: %TRUE wenn Registriernummer vorhanden
 */
gboolean sond_akte_get_regnr(SondAkte *akte, gchar **jahr, gchar **lfdnr);

/**
 * sond_akte_set_regnr:
 * @akte: Akte
 * @jahr: Jahr
 * @lfdnr: Laufende Nummer
 *
 * Setzt die Registriernummer.
 */
void sond_akte_set_regnr(SondAkte *akte, const gchar *jahr, const gchar *lfdnr);

/**
 * sond_akte_get_regnr_string:
 * @akte: Akte
 *
 * Gibt Registriernummer als formatierten String zurück.
 * Format: "Jahr/lfdnr" (z.B. "2024/00123")
 *
 * Returns: (transfer full) (nullable): Formatierte Registriernummer oder %NULL
 */
gchar* sond_akte_get_regnr_string(SondAkte *akte);

/* ========================================================================
 * Name und Bezeichnung
 * ======================================================================== */

/**
 * sond_akte_get_name:
 * @akte: Akte
 *
 * Returns: (transfer full) (nullable): Kurzname oder %NULL
 */
gchar* sond_akte_get_name(SondAkte *akte);

/**
 * sond_akte_set_name:
 * @akte: Akte
 * @name: Neuer Kurzname
 *
 * Setzt den Kurznamen der Akte.
 */
void sond_akte_set_name(SondAkte *akte, const gchar *name);

/**
 * sond_akte_get_langbezeichnung:
 * @akte: Akte
 *
 * Returns: (transfer full) (nullable): Langbezeichnung oder %NULL
 */
gchar* sond_akte_get_langbezeichnung(SondAkte *akte);

/**
 * sond_akte_set_langbezeichnung:
 * @akte: Akte
 * @langbezeichnung: Neue Langbezeichnung
 *
 * Setzt die ausführliche Bezeichnung.
 */
void sond_akte_set_langbezeichnung(SondAkte *akte, const gchar *langbezeichnung);

/* ========================================================================
 * Datum
 * ======================================================================== */

/**
 * sond_akte_get_angelegt_am:
 * @akte: Akte
 *
 * Gibt Anlagedatum als String zurück (ISO 8601: YYYY-MM-DD).
 *
 * Returns: (transfer full) (nullable): Datum oder %NULL
 */
gchar* sond_akte_get_angelegt_am(SondAkte *akte);

/**
 * sond_akte_set_angelegt_am:
 * @akte: Akte
 * @datum: Datum im Format YYYY-MM-DD
 *
 * Setzt das Anlagedatum.
 */
void sond_akte_set_angelegt_am(SondAkte *akte, const gchar *datum);

/**
 * sond_akte_get_angelegt_am_datetime:
 * @akte: Akte
 *
 * Gibt Anlagedatum als GDateTime zurück.
 *
 * Returns: (transfer full) (nullable): GDateTime oder %NULL
 */
GDateTime* sond_akte_get_angelegt_am_datetime(SondAkte *akte);

/**
 * sond_akte_set_angelegt_am_datetime:
 * @akte: Akte
 * @datetime: Datum/Zeit
 *
 * Setzt das Anlagedatum aus GDateTime.
 */
void sond_akte_set_angelegt_am_datetime(SondAkte *akte, GDateTime *datetime);

/* ========================================================================
 * Ablage
 * ======================================================================== */

/**
 * sond_akte_is_abgelegt:
 * @akte: Akte
 *
 * Prüft ob Akte abgelegt ist.
 *
 * Returns: %TRUE wenn abgelegt
 */
gboolean sond_akte_is_abgelegt(SondAkte *akte);

/**
 * sond_akte_get_ablage:
 * @akte: Akte
 * @datum: (out) (transfer full) (nullable): Ablagedatum
 * @nummer: (out) (transfer full) (nullable): Ablagenummer
 *
 * Gibt Ablageinformationen zurück.
 *
 * Returns: %TRUE wenn Akte abgelegt ist
 */
gboolean sond_akte_get_ablage(SondAkte *akte, gchar **datum, gchar **nummer);

/**
 * sond_akte_set_ablage:
 * @akte: Akte
 * @datum: Ablagedatum (YYYY-MM-DD)
 * @nummer: Ablagenummer
 *
 * Markiert Akte als abgelegt.
 */
void sond_akte_set_ablage(SondAkte *akte, const gchar *datum, const gchar *nummer);

/**
 * sond_akte_clear_ablage:
 * @akte: Akte
 *
 * Entfernt Ablage-Markierung (reaktiviert Akte).
 */
void sond_akte_clear_ablage(SondAkte *akte);

/* ========================================================================
 * Validierung
 * ======================================================================== */

/**
 * sond_akte_is_valid:
 * @akte: Zu prüfende Akte
 *
 * Prüft ob eine Akte gültig ist (alle Pflichtfelder gesetzt).
 *
 * Pflichtfelder:
 * - Label muss "Akte" sein
 * - regnr (Jahr und lfdnr)
 * - name
 * - langbezeichnung
 *
 * Returns: %TRUE wenn gültig
 */
gboolean sond_akte_is_valid(SondAkte *akte);

/**
 * sond_akte_validate:
 * @akte: Zu prüfende Akte
 * @error: (nullable): Fehler-Rückgabe mit Details
 *
 * Validiert eine Akte mit detaillierter Fehlermeldung.
 *
 * Returns: %TRUE wenn gültig
 */
gboolean sond_akte_validate(SondAkte *akte, GError **error);

/* ========================================================================
 * Datenbank-Operationen
 * ======================================================================== */

/**
 * sond_akte_save:
 * @conn: MySQL-Verbindung
 * @akte: Zu speichernde Akte
 * @error: (nullable): Fehler-Rückgabe
 *
 * Speichert eine Akte in der Datenbank.
 * Validiert vor dem Speichern.
 *
 * Returns: %TRUE bei Erfolg
 */
gboolean sond_akte_save(MYSQL *conn, SondAkte *akte, GError **error);

/**
 * sond_akte_load:
 * @conn: MySQL-Verbindung
 * @akte_id: ID der zu ladenden Akte
 * @error: (nullable): Fehler-Rückgabe
 *
 * Lädt eine Akte aus der Datenbank.
 * Prüft ob der geladene Node wirklich eine Akte ist.
 *
 * Returns: (transfer full) (nullable): Geladene Akte oder %NULL
 */
SondAkte* sond_akte_load(MYSQL *conn, gint64 akte_id, GError **error);

/**
 * sond_akte_delete:
 * @conn: MySQL-Verbindung
 * @akte_id: ID der zu löschenden Akte
 * @error: (nullable): Fehler-Rückgabe
 *
 * Löscht eine Akte aus der Datenbank.
 *
 * Returns: %TRUE bei Erfolg
 */
gboolean sond_akte_delete(MYSQL *conn, gint64 akte_id, GError **error);

/* ========================================================================
 * Such-Funktionen
 * ======================================================================== */

/**
 * sond_akte_search_by_regnr:
 * @conn: MySQL-Verbindung
 * @jahr: Jahr
 * @lfdnr: Laufende Nummer
 * @error: (nullable): Fehler-Rückgabe
 *
 * Sucht eine Akte anhand der Registriernummer.
 *
 * Returns: (transfer full) (nullable): Gefundene Akte oder %NULL
 */
SondAkte* sond_akte_search_by_regnr(MYSQL *conn,
                                     const gchar *jahr,
                                     const gchar *lfdnr,
                                     GError **error);

/**
 * sond_akte_search_by_year:
 * @conn: MySQL-Verbindung
 * @jahr: Jahr
 * @error: (nullable): Fehler-Rückgabe
 *
 * Sucht alle Akten eines Jahres.
 *
 * Returns: (transfer full) (element-type SondAkte) (nullable): Array mit Akten oder %NULL
 */
GPtrArray* sond_akte_search_by_year(MYSQL *conn,
                                     const gchar *jahr,
                                     GError **error);

/**
 * sond_akte_search_by_name:
 * @conn: MySQL-Verbindung
 * @name: Name oder Name-Pattern (mit * und ?)
 * @error: (nullable): Fehler-Rückgabe
 *
 * Sucht Akten nach Name (unterstützt Wildcards).
 *
 * Returns: (transfer full) (element-type SondAkte) (nullable): Array mit Akten oder %NULL
 */
GPtrArray* sond_akte_search_by_name(MYSQL *conn,
                                     const gchar *name,
                                     GError **error);

/**
 * sond_akte_get_all_abgelegt:
 * @conn: MySQL-Verbindung
 * @error: (nullable): Fehler-Rückgabe
 *
 * Gibt alle abgelegten Akten zurück.
 *
 * Returns: (transfer full) (element-type SondAkte) (nullable): Array mit Akten oder %NULL
 */
GPtrArray* sond_akte_get_all_abgelegt(MYSQL *conn, GError **error);

/**
 * sond_akte_get_all_active:
 * @conn: MySQL-Verbindung
 * @error: (nullable): Fehler-Rückgabe
 *
 * Gibt alle aktiven (nicht abgelegten) Akten zurück.
 *
 * Returns: (transfer full) (element-type SondAkte) (nullable): Array mit Akten oder %NULL
 */
GPtrArray* sond_akte_get_all_active(MYSQL *conn, GError **error);

G_END_DECLS

#endif /* SOND_AKTE_H */
