/*
 sond (sond_zond_lease.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_zond_lease.h
 * @brief zond für eine Akte starten, mit der Schreibberechtigung (Lease) der Akte
 *
 * Holt die Berechtigung (acquire) oder fragt den Nutzer, wenn ein anderer Rechner sie hält
 * (Übernahme anfordern, erzwingen, schreibgeschützt öffnen), wartet auf die Freigabe, geht
 * mit einem Index-Job auf dem Server um und startet zond im passenden Modus. Die Akte wird
 * jeweils übergeben (Registernummer), unabhängig von jeder Auswahl in einem Modul.
 *
 * Dazu gehören die Texte zum Stand einer Lease, die auch das Akte-Modul anzeigt.
 */

#ifndef SOND_ZOND_LEASE_H
#define SOND_ZOND_LEASE_H

#include <gtk/gtk.h>

#include "../../sond_client.h"
#include "sond_akte_lease.h"

G_BEGIN_DECLS

typedef struct _SondZondStarter SondZondStarter;

/* Der Zustand der Lease oder des Wartens hat sich geändert (Anzeige auffrischen) */
typedef void (*SondZondStarterChanged)(gpointer user_data);

/* parent: Widget, dessen Fenster die Dialoge als Elternfenster bekommen (muss den Starter
 * überdauern). changed darf NULL sein. lease gehört dem Aufrufer. */
SondZondStarter* sond_zond_starter_new(SondClient *client, SondAkteLease *lease,
        GtkWidget *parent, SondZondStarterChanged changed, gpointer user_data);

/* Beendet das Warten und gibt den Starter frei; offene Dialoge laufen ohne Wirkung aus. */
void sond_zond_starter_free(SondZondStarter *s);

/* zond mit der Projektdatei der Akte starten (Library 'lfd_nr'/'year'). akte_id = 0:
 * unbekannt, der Starter fragt den Server bzw. nimmt den lokalen Lease-Zustand. */
void sond_zond_starter_start(SondZondStarter *s, guint lfd_nr, guint year, gint64 akte_id);

/* Bezeichnung und Gegenstand der gezeigten Akte für die Titelzeile von zond (beide dürfen NULL
 * sein; NULL für kurz und ggs löscht die Angaben). Gelten für die folgenden Starts. */
void sond_zond_starter_set_info(SondZondStarter *s, guint lfd_nr, guint year,
        const gchar *kurz, const gchar *gegenstand);

/* Wartet der Starter gerade auf die Freigabe durch einen anderen Rechner? */
gboolean sond_zond_starter_waiting(SondZondStarter *s);

/* Das Warten abbrechen (z. B. wenn eine andere Akte gewählt wird) */
void sond_zond_starter_cancel_wait(SondZondStarter *s);

/* Rückfrage "Projekt freigeben?" für die Akte, dann Freigabe (wartet auf zond und auf SeaDrive) */
void sond_zond_starter_confirm_release(SondZondStarter *s, gint64 akte_id, const gchar *name);

/* Ereignisse des Hintergrundtakts der Lease (Berechtigung verloren, Übergabe angefordert,
 * abgeschlossen oder festgefahren). Als Handler mit dem Starter als user_data anmelden:
 * sond_akte_lease_set_event_handler(lease, sond_zond_starter_lease_event, starter). */
void sond_zond_starter_lease_event(SondAkteLease *lease, gint64 akte_id, const gchar *name,
        SondLeaseEvent event, gpointer user_data);

/* Hinweis zum Stand der Lease der Akte (z. B. "Übergabe läuft") oder NULL; gilt, bis das Ereignis
 * abgeschlossen ist. Der Zeiger gilt bis zur nächsten Änderung. */
const gchar* sond_zond_starter_message(SondZondStarter *s, gint64 akte_id);

/* "gerade eben", "vor 5 Min.", ... zu einer Zahl Sekunden (g_free) */
gchar* sond_lease_idle_text(gint64 seconds);

/* Anzeigetext "Projekt: ..." zum Stand einer Lease (g_free) */
gchar* sond_lease_info_text(const SondLeaseInfo *info);

G_END_DECLS

#endif /* SOND_ZOND_LEASE_H */
