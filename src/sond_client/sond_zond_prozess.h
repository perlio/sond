/*
 sond (sond_zond_prozess.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_zond_prozess.h
 * @brief Die vom Client gestarteten zond-Prozesse, je Akte
 *
 * zond und das Dokumentenfenster einer Akte laufen nie gleichzeitig. Dafür muss der Client wissen,
 * ob zond für eine Akte läuft: Er merkt sich jeden von ihm gestarteten Prozess (Schlüssel: der
 * Library-Name der Akte, z.B. "2026-1") und prüft in kurzem Takt, ob er noch lebt. Prozess-ID und
 * Startzeit stehen in einer lokalen Datei, damit ein neu gestarteter Client einen noch laufenden
 * zond wiederfindet.
 *
 * Frei von GTK, nur GLib (Windows: Win32-Prozess-API).
 */

#ifndef SOND_ZOND_PROZESS_H
#define SOND_ZOND_PROZESS_H

#include <glib.h>

G_BEGIN_DECLS

typedef void (*SondZondProzessFunc)(gpointer data);

/* Ein soeben gestarteter zond für die Akte lib_name. pid wie von g_spawn_async() geliefert (Windows:
 * das Handle; es bleibt Eigentum des Aufrufers, hier wird eine eigene Kopie gehalten). */
void sond_zond_prozess_merken(const gchar *lib_name, GPid pid);

//Läuft für die Akte ein zond?
gboolean sond_zond_prozess_laeuft(const gchar *lib_name);

/* Beim Start des Clients: Prozesse aus der lokalen Datei wiederfinden, die noch laufen (gleiche
 * Prozess-ID und Startzeit). Mehrfaches Aufrufen ist harmlos. */
void sond_zond_prozess_wiederfinden(void);

/* Rückruf (Hauptschleife), wenn sich ändert, ob für eine Akte ein zond läuft (Start und Ende).
 * func NULL entfernt ihn. */
void sond_zond_prozess_set_func(SondZondProzessFunc func, gpointer data);

G_END_DECLS

#endif /* SOND_ZOND_PROZESS_H */
