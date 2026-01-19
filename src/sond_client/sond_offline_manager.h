/*
 sond (sond_offline_manager.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_offline_manager.h
 * @brief Verwaltung von Offline-Akten (JSON-Persistierung)
 */

#ifndef SOND_OFFLINE_MANAGER_H
#define SOND_OFFLINE_MANAGER_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define SOND_TYPE_OFFLINE_MANAGER (sond_offline_manager_get_type())
G_DECLARE_FINAL_TYPE(SondOfflineManager, sond_offline_manager, SOND, OFFLINE_MANAGER, GObject)

/**
 * SondOfflineAkte:
 * 
 * Repräsentiert eine Offline-Akte mit Metadaten
 */
typedef struct {
    gchar *regnr;                   /* z.B. "2026-1" */
    gchar *kurzb;                   /* Kurzbezeichnung */
    gchar *ggstd;                   /* Gegenstand */
    gchar *seafile_library_id;      /* Seafile Library ID */
    gboolean sync_enabled;          /* TRUE wenn Sync aktiv */
    gchar *local_path;              /* Lokaler Pfad zum Sync-Verzeichnis */
    GDateTime *last_synced;         /* Letzter Sync-Zeitpunkt */
} SondOfflineAkte;

/**
 * sond_offline_akte_new:
 * 
 * Erstellt eine neue SondOfflineAkte
 */
SondOfflineAkte* sond_offline_akte_new(const gchar *regnr,
                                        const gchar *kurzb,
                                        const gchar *ggstd,
                                        const gchar *seafile_library_id,
                                        const gchar *local_path);

/**
 * sond_offline_akte_free:
 * 
 * Gibt SondOfflineAkte frei
 */
void sond_offline_akte_free(SondOfflineAkte *akte);

/**
 * sond_offline_manager_new:
 * @sync_directory: Basisverzeichnis für Offline-Akten
 * 
 * Erstellt einen neuen Offline Manager
 * 
 * Returns: (transfer full): Neuer SondOfflineManager
 */
SondOfflineManager* sond_offline_manager_new(const gchar *sync_directory);

/**
 * sond_offline_manager_add_akte:
 * @manager: SondOfflineManager
 * @akte: Hinzuzufügende Akte
 * @error: Fehler
 * 
 * Fügt eine Akte zur Offline-Liste hinzu und speichert
 * 
 * Returns: TRUE bei Erfolg
 */
gboolean sond_offline_manager_add_akte(SondOfflineManager *manager,
                                        SondOfflineAkte *akte,
                                        GError **error);

/**
 * sond_offline_manager_remove_akte:
 * @manager: SondOfflineManager
 * @regnr: RegNr der zu entfernenden Akte
 * @error: Fehler
 * 
 * Entfernt eine Akte aus der Offline-Liste
 * 
 * Returns: TRUE bei Erfolg
 */
gboolean sond_offline_manager_remove_akte(SondOfflineManager *manager,
                                           const gchar *regnr,
                                           GError **error);

/**
 * sond_offline_manager_get_akte:
 * @manager: SondOfflineManager
 * @regnr: RegNr der gesuchten Akte
 * 
 * Sucht eine Akte in der Offline-Liste
 * 
 * Returns: (transfer none) (nullable): Gefundene Akte oder NULL
 */
SondOfflineAkte* sond_offline_manager_get_akte(SondOfflineManager *manager,
                                                const gchar *regnr);

/**
 * sond_offline_manager_get_all_akten:
 * @manager: SondOfflineManager
 * 
 * Gibt alle Offline-Akten zurück
 * 
 * Returns: (transfer none) (element-type SondOfflineAkte): Liste aller Akten
 */
GList* sond_offline_manager_get_all_akten(SondOfflineManager *manager);

/**
 * sond_offline_manager_set_sync_enabled:
 * @manager: SondOfflineManager
 * @regnr: RegNr der Akte
 * @enabled: TRUE = Sync aktivieren, FALSE = pausieren
 * @error: Fehler
 * 
 * Aktiviert/deaktiviert Sync für eine Akte
 * 
 * Returns: TRUE bei Erfolg
 */
gboolean sond_offline_manager_set_sync_enabled(SondOfflineManager *manager,
                                                const gchar *regnr,
                                                gboolean enabled,
                                                GError **error);

/**
 * sond_offline_manager_update_last_synced:
 * @manager: SondOfflineManager
 * @regnr: RegNr der Akte
 * @error: Fehler
 * 
 * Aktualisiert den Last-Synced Zeitstempel auf jetzt
 * 
 * Returns: TRUE bei Erfolg
 */
gboolean sond_offline_manager_update_last_synced(SondOfflineManager *manager,
                                                  const gchar *regnr,
                                                  GError **error);

/**
 * sond_offline_manager_is_offline:
 * @manager: SondOfflineManager
 * @regnr: RegNr der Akte
 * 
 * Prüft ob eine Akte offline verfügbar ist
 * 
 * Returns: TRUE wenn Akte in Offline-Liste
 */
gboolean sond_offline_manager_is_offline(SondOfflineManager *manager,
                                          const gchar *regnr);

G_END_DECLS

#endif /* SOND_OFFLINE_MANAGER_H */
