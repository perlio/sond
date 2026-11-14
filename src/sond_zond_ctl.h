/*
 sond (sond_zond_ctl.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_zond_ctl.h
 * @brief Verständigung zwischen sond_client und zond über Steuerdateien
 *
 * Beide Programme laufen auf demselben Rechner und kennen die Projektdatei
 * (voller Pfad). Je Projekt gibt es zwei kleine Dateien in einem lokalen
 * Verzeichnis (nicht im synchronisierten Projektordner):
 *
 * - Modus (vom Client): der gewünschte Zugriff. "rw" lesen und schreiben,
 *   "ro" schreibgeschützt (zond speichert vorher), "lost" schreibgeschützt
 *   ohne Speichern (die Schreibberechtigung ging verloren, das Projekt auf dem
 *   Laufwerk darf nicht mehr beschrieben werden). Fehlt die Datei, gilt "rw".
 * - Zustand (von zond): alle paar Sekunden erneuert; zeigt, dass zond das
 *   Projekt offen hat, in welchem Modus es läuft und ob es ungespeicherte
 *   Änderungen hat.
 *
 * Frei von GTK, nur GLib.
 */

#ifndef SOND_ZOND_CTL_H
#define SOND_ZOND_CTL_H

#include <glib.h>

G_BEGIN_DECLS

#define SOND_ZOND_MODE_RW "rw"
#define SOND_ZOND_MODE_RO "ro"
#define SOND_ZOND_MODE_LOST "lost"

typedef struct {
    gboolean running;  //zond hat das Projekt offen (Zustand frisch)
    gchar *mode;       //"rw" oder "ro" (nullable)
    gboolean changed;  //ungespeicherte Änderungen
} SondZondCtlState;

/* Modus setzen (atomar). project_path: voller Pfad der Projektdatei. */
gboolean sond_zond_ctl_set_mode(const gchar *project_path, const gchar *mode,
        GError **error);

/* Gewünschter Modus ("rw", "ro", "lost") oder NULL, wenn keiner gesetzt ist
 * (g_free()) */
gchar* sond_zond_ctl_get_mode(const gchar *project_path);

/* Von zond: Zustand veröffentlichen bzw. beim Schließen entfernen */
void sond_zond_ctl_write_state(const gchar *project_path, const gchar *mode,
        gboolean changed);

void sond_zond_ctl_clear_state(const gchar *project_path);

/* Zustand lesen. running ist FALSE, wenn es keinen oder nur einen veralteten
 * Zustand gibt. Mit sond_zond_ctl_state_clear() aufräumen. */
void sond_zond_ctl_read_state(const gchar *project_path, SondZondCtlState *state);

void sond_zond_ctl_state_clear(SondZondCtlState *state);

G_END_DECLS

#endif /* SOND_ZOND_CTL_H */
