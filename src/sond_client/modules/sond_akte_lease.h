/*
 sond (sond_akte_lease.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_akte_lease.h
 * @brief Schreibberechtigung (Lease) für das Projekt einer Akte
 *
 * Genau ein Gerät darf die Projektdaten einer Akte schreiben. Die Berechtigung
 * liegt als generischer Knoten (Label "ProjektLease") in der Graph-DB und wird
 * ausschließlich über die normalen Node-Endpunkte gesteuert; der Server kennt
 * ihre Bedeutung nicht. Gegenseitiger Ausschluss beim Ändern: kurzer Node-Lock
 * auf dem Lease-Knoten. Alle Zeitvergleiche beruhen auf der Serverzeit.
 *
 * Eine Lease läuft ab, wenn der Halter länger als SOND_AKTE_LEASE_SECONDS
 * nichts hat von sich hören lassen. Offline darf ein Gerät bis dahin weiter
 * schreiben (may_write(), nur mit lokaler Uhr).
 *
 * Alle Funktionen blockieren (synchrone Client-Aufrufe), nur im Hauptthread
 * verwenden. Frei von GTK.
 */

#ifndef SOND_AKTE_LEASE_H
#define SOND_AKTE_LEASE_H

#include <glib.h>
#include "../sond_client.h"

G_BEGIN_DECLS

/* Gültigkeitsdauer ohne Lebenszeichen des Halters */
#define SOND_AKTE_LEASE_SECONDS (48 * 3600)

typedef struct _SondAkteLease SondAkteLease;

typedef enum {
    SOND_LEASE_ERROR,     //Kommunikationsfehler, error ist gesetzt; der lokale
                          //Zustand bleibt unverändert (z. B. offline)
    SOND_LEASE_OK,        //erteilt/erneuert/freigegeben
    SOND_LEASE_DENIED,    //gehalten von einem anderen Gerät oder Nutzer
    SOND_LEASE_REQUESTED, //Übergabe angefordert, Halter muss noch freigeben
    SOND_LEASE_LOST,      //Lease gehört nicht mehr diesem Gerät
    SOND_LEASE_NOT_ALLOWED //Übernahme einer Lease eines anderen Nutzers
} SondLeaseResult;

/* Stand einer Lease zum Zeitpunkt der Abfrage; mit sond_akte_lease_info_clear()
 * aufräumen (auch wenn die Funktion fehlschlug) */
typedef struct {
    gboolean found;           //Lease-Knoten vorhanden
    gboolean held;            //von jemandem gehalten (auch abgelaufen)
    gboolean mine;            //von diesem Gerät gehalten
    gboolean same_user;       //Halter ist der angemeldete Nutzer
    gboolean expired;         //Lease abgelaufen (dann frei vergebbar)
    gboolean takeover_requested; //Halter soll freigeben
    gboolean takeover_by_me;  //die Anforderung stammt von diesem Gerät
    gboolean zond_open;       //Halter meldete: zond ist offen
    gint pending_up;          //Halter meldete: noch nicht hochgeladen; -1 unbekannt
    gint64 idle_seconds;      //Sekunden seit dem letzten Lebenszeichen; -1 unbekannt
    gint64 epoch;
    gchar *user;
    gchar *device_name;       //Rechnername des Halters
} SondLeaseInfo;

void sond_akte_lease_info_clear(SondLeaseInfo *info);

/* Lädt/erzeugt Geräte-ID und den lokalen Lease-Zustand. Der Zustand liegt in
 * einer eigenen Datei im Benutzerverzeichnis (Umgebungsvariable
 * SOND_CLIENT_STATE überschreibt den Pfad, z. B. für Tests mit zwei Clients
 * auf einem Rechner). */
SondAkteLease* sond_akte_lease_new(SondClient *client);

void sond_akte_lease_free(SondAkteLease *lease);

const gchar* sond_akte_lease_get_device_id(SondAkteLease *lease);

const gchar* sond_akte_lease_get_device_name(SondAkteLease *lease);

/* Stand der Lease der Akte lesen, ohne etwas zu ändern. FALSE nur bei
 * Kommunikationsfehler (error gesetzt). Ohne Lease-Knoten: TRUE, found FALSE. */
gboolean sond_akte_lease_get_info(SondAkteLease *lease, gint64 akte_id,
        SondLeaseInfo *info, GError **error);

/* Lease holen. OK: erteilt bzw. erneuert (gleiches Gerät). DENIED: info zeigt
 * den Halter. Legt den Lease-Knoten bei Bedarf an. info optional. */
SondLeaseResult sond_akte_lease_acquire(SondAkteLease *lease, gint64 akte_id,
        SondLeaseInfo *info, GError **error);

/* Lebenszeichen: erneuert die Lease und meldet Zustand. OK: weiter gültig,
 * *takeover_requested (optional) sagt, ob ein anderes Gerät die Übergabe
 * wünscht. LOST: Lease gehört nicht mehr diesem Gerät (lokaler Zustand ist
 * dann gelöscht). pending_up: Zahl noch nicht hochgeladener Dateien, -1
 * unbekannt. */
SondLeaseResult sond_akte_lease_heartbeat(SondAkteLease *lease, gint64 akte_id,
        gboolean zond_open, gint pending_up, gboolean *takeover_requested,
        GError **error);

/* Lease zurückgeben. OK: freigegeben. LOST: gehörte schon nicht mehr uns
 * (lokaler Zustand ist gelöscht). */
SondLeaseResult sond_akte_lease_release(SondAkteLease *lease, gint64 akte_id,
        GError **error);

/* Übernahme durch denselben Nutzer von einem anderen Gerät. force = FALSE:
 * Übergabe anfordern (REQUESTED; der Halter gibt bei seinem nächsten
 * Lebenszeichen frei, danach acquire aufrufen). force = TRUE: Lease sofort an
 * dieses Gerät, neue Epoche (OK). Ist die Lease frei oder abgelaufen, wird
 * sie einfach erteilt (OK). Andere Nutzer: NOT_ALLOWED. info optional. */
SondLeaseResult sond_akte_lease_takeover(SondAkteLease *lease, gint64 akte_id,
        gboolean force, SondLeaseInfo *info, GError **error);

/* Merkt sich den Library-Namen der Akte beim lokalen Lease-Zustand (nur, wenn
 * dieses Gerät die Lease hält), damit die Akte auch offline gefunden wird. */
void sond_akte_lease_set_name(SondAkteLease *lease, gint64 akte_id,
        const gchar *name);

/* Akte-ID zum Library-Namen laut lokalem Zustand; 0, wenn dieses Gerät keine
 * Lease für diesen Namen hält. */
gint64 sond_akte_lease_find_by_name(SondAkteLease *lease, const gchar *name);

/* Library-Name der Akte laut lokalem Zustand (nullable), nicht freigeben */
const gchar* sond_akte_lease_get_name(SondAkteLease *lease, gint64 akte_id);

/* ------------------------------------------------------------------ */
/*  Lebenszeichen im Hintergrund                                       */
/* ------------------------------------------------------------------ */

typedef enum {
    SOND_LEASE_EVENT_LOST,               //Berechtigung nicht mehr bei diesem Gerät
    SOND_LEASE_EVENT_TAKEOVER_REQUESTED, //ein anderes Gerät wünscht die Übergabe
                                         //(die Übergabe läuft nun selbständig)
    SOND_LEASE_EVENT_HANDED_OVER,        //Übergabe abgeschlossen: Berechtigung
                                         //freigegeben (zond war gespeichert,
                                         //SeaDrive hat alles hochgeladen)
    SOND_LEASE_EVENT_HANDOVER_STUCK      //nicht selbständig zu klären (zu lange
                                         //oder Upload-Stand unbekannt): der
                                         //Nutzer muss entscheiden
} SondLeaseEvent;

/* name: Library-Name der Akte laut lokalem Zustand (nullable, nur während des
 * Aufrufs gültig). Wird aus der Hauptschleife aufgerufen. TAKEOVER_REQUESTED
 * kommt einmal je Anforderung. */
typedef void (*SondLeaseEventFunc)(SondAkteLease *lease, gint64 akte_id,
        const gchar *name, SondLeaseEvent event, gpointer user_data);

void sond_akte_lease_set_event_handler(SondAkteLease *lease,
        SondLeaseEventFunc func, gpointer user_data);

/* Startet den Takt (alle 30 s ein Lebenszeichen je gehaltener Akte, nur bei
 * angemeldetem Client; bei Verbindungsfehlern mit wachsender Pause bis etwa
 * 5 Minuten). Mehrfaches Aufrufen ist harmlos; sond_akte_lease_free() beendet
 * ihn. Blockiert höchstens kurz (kurze Zeitgrenze je Lauf). */
void sond_akte_lease_start_heartbeat(SondAkteLease *lease);

/* Meldet einen gestarteten zond-Prozess für die Akte. Solange er läuft, melden
 * die Lebenszeichen "zond offen". Die Kennung wird hier abgeräumt. Best effort:
 * zond ist eine Einzelinstanz-Anwendung, ein zweiter Start endet sofort. */
void sond_akte_lease_note_zond_started(SondAkteLease *lease, gint64 akte_id,
        GPid pid);

gboolean sond_akte_lease_zond_open(SondAkteLease *lease, gint64 akte_id);

/* Merkt sich den Pfad der Projektdatei der Akte (nur, wenn dieses Gerät die Lease
 * hält), für die Verständigung mit zond (s. sond_zond_ctl.h). */
void sond_akte_lease_note_project(SondAkteLease *lease, gint64 akte_id,
        const gchar *project_path);

/* Gewünschten Modus für zond setzen (SOND_ZOND_MODE_*). Ohne bekanntes Projekt
 * oder ohne laufendes zond ohne Wirkung. */
/* Wie viele seit Beginn der Berechtigung geänderte Dateien der Akte sind noch
 * nicht hochgeladen? -1: nicht zu ermitteln (kein Windows/SeaDrive, kein
 * Projekt bekannt). Durchsucht das Verzeichnis der Akte, deshalb nicht in jedem
 * Takt aufrufen. */
gint sond_akte_lease_unsynced(SondAkteLease *lease, gint64 akte_id);

gboolean sond_akte_lease_set_zond_mode(SondAkteLease *lease, gint64 akte_id,
        const gchar *mode, GError **error);

/* Schaltet zond (falls es das Projekt offen hat) schreibgeschützt und wartet,
 * bis es gespeichert hat (höchstens timeout_ms; die Oberfläche läuft dabei
 * weiter). TRUE: zond ist nicht offen oder hat gespeichert und ist
 * schreibgeschützt. */
gboolean sond_akte_lease_zond_settle(SondAkteLease *lease, gint64 akte_id,
        guint timeout_ms);

/* Darf dieses Gerät die Projektdaten der Akte schreiben? Rein lokal (kein
 * Server nötig): Lease laut lokalem Zustand und noch nicht abgelaufen. */
gboolean sond_akte_lease_may_write(SondAkteLease *lease, gint64 akte_id);

G_END_DECLS

#endif /* SOND_AKTE_LEASE_H */
