/*
 sond (sond_tvfm_unlock.h) - Akten, Beweisstücke, Unterlagen
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

/*
 * Passwortschutz von ZIP-Einträgen und PDFs dauerhaft entfernen (die Dateien werden dabei
 * verändert): GTK-frei, gemeinsam für den GTK3- und den GTK4-Dateibaum. Fragen nach
 * Passwörtern läuft über sond_password.h (Oberfläche meldet ihre Dialogfunktion an), alles,
 * was die Oberfläche betrifft (SeaDrive-Download, Zeilen auffrischen, Meldungen), über
 * SondUnlockHooks.
 */

#ifndef SOND_TVFM_UNLOCK_H_INCLUDED
#define SOND_TVFM_UNLOCK_H_INCLUDED

#include <glib.h>

#include "../sond_fileparts.h"
#include "../sond_process_file.h"
#include "sond_tvfm_item.h"

G_BEGIN_DECLS

typedef struct {
	/* Datei lokal verfügbar machen (SeaDrive-Platzhalter laden); FALSE: Abbruch oder Fehler. NULL =
	 * immer verfügbar. */
	gboolean (*ensure_local)(SondFilePart *sfp, gpointer data);
	/* Der Eintrag zu sfp hat sich geändert (jetzt ohne Schutz, ggf. mit Anhängen): Anzeige auffrischen. NULL erlaubt. */
	void (*refresh)(SondFilePart *sfp, gpointer data);
	/* Meldung an den Nutzer (Fehler). NULL erlaubt. */
	void (*report)(gchar const *message, gpointer data);
	/* Ist die Datei gerade geöffnet (Viewer) und darf nicht verändert werden? NULL = nie. */
	gboolean (*busy)(SondFilePart *sfp, gpointer data);
	gpointer data;
} SondUnlockHooks;

typedef struct {
	GPtrArray *zips; //ZIP-Archive mit verschlüsselten Einträgen (intern)
	GPtrArray *pdfs; //SondFilePart* (Ref), PDF mit Passwort
} SondUnlockJobs;

typedef struct {
	guint done;
	guint skipped;
	gboolean aborted;
	GPtrArray *decrypted;   //Fileparts der entsperrten Dateien (g_free)
	GPtrArray *skipped_fps; //Fileparts der übersprungenen Dateien (g_free)
} SondUnlockResult;

void sond_tvfm_unlock_jobs_init(SondUnlockJobs *jobs);
void sond_tvfm_unlock_jobs_clear(SondUnlockJobs *jobs);

/* Merkt vor, was an item zu entsperren ist (ZIP-Eintrag, ZIP-Archiv ganz oder ein Ordner darin,
 * PDF mit Passwort); Dateisystem-Verzeichnisse und Ungeschütztes ergeben nichts. */
void sond_tvfm_unlock_collect_item(SondUnlockJobs *jobs, SondTVFMItem *item);

typedef enum {
	SOND_UNLOCK_READY = 0,   //kann losgehen
	SOND_UNLOCK_NOTHING,     //nichts Geschütztes in der Auswahl
	SOND_UNLOCK_BUSY,        //eine Datei ist geöffnet (message gesetzt)
	SOND_UNLOCK_NOT_LOCAL    //eine Datei lässt sich nicht lokal bereitstellen
} SondUnlockPrepare;

/* Zählt die vorgemerkten Dateien (n_total), prüft geöffnete Dateien und die lokale
 * Verfügbarkeit. message (g_free) nur bei SOND_UNLOCK_BUSY. */
SondUnlockPrepare sond_tvfm_unlock_prepare(SondUnlockJobs *jobs, SondUnlockHooks const *hooks,
		guint *n_total, gchar **message);

/* Führt die vorgemerkten Arbeiten aus: erst die Einträge in ZIP-Archiven (Passwörter sicherstellen,
 * je Archiv einmal neu schreiben), dann PDFs mit Passwort, auch die aus entschlüsselten
 * Einträgen entstandenen. Läuft als Aktion im Sinn von sond_password_action_begin(). Nur im
 * Hauptthread. res wird von der Funktion gefüllt (vorher nicht initialisieren), mit
 * sond_tvfm_unlock_result_clear() freigeben. */
void sond_tvfm_unlock_run(SondUnlockJobs *jobs, SondUnlockHooks const *hooks,
		SondUnlockResult *res);

/* Entsperrt die in einem Index-Lauf übersprungenen Teile (SondLockedItem* aus wctx->locked).
 * Ergebnis wie sond_tvfm_unlock_run(). */
void sond_tvfm_unlock_locked_items(GPtrArray *items, SondUnlockHooks const *hooks,
		SondUnlockResult *res);

void sond_tvfm_unlock_result_clear(SondUnlockResult *res);

G_END_DECLS

#endif /* SOND_TVFM_UNLOCK_H_INCLUDED */
