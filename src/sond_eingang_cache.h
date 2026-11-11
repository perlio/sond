/*
 sond (sond_eingang_cache.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_eingang_cache.h
 * @brief Lokale Nachschlagetabelle für die Eingangsangaben (Stempel) einer Akte
 *
 * Die Zettel im Verzeichnis ".sond/eingang" der Library sind die Quelle der
 * Wahrheit. Dieser Cache (SQLite, lokal, nie auf dem Cloud-Laufwerk) liest sie
 * einmal ein und beantwortet die Frage "welches Eingangsdatum gilt für diesen
 * Pfad?" schnell; er lässt sich jederzeit aus den Zetteln neu aufbauen.
 *
 * Regeln:
 * - Ein eigener Stempel eines Pfads hat Vorrang.
 * - Sonst erbt ein Eintrag den Stempel des nächsten Ordners über ihm.
 * - Stempel von Einträgen darunter beeinflussen einen Ordner nicht.
 * - Gibt es nichts, ist das Ergebnis 0 (der Aufrufer fällt auf die
 *   Änderungszeit zurück).
 *
 * Frei von GTK; nur glib und sqlite3.
 */

#ifndef SOND_EINGANG_CACHE_H
#define SOND_EINGANG_CACHE_H

#include <glib.h>

#include "sond_eingang_zettel.h"

G_BEGIN_DECLS

typedef struct _SondEingangCache SondEingangCache;

/**
 * sond_eingang_cache_open:
 * @db_path: Pfad der lokalen Cache-Datei (wird angelegt)
 * @library_dir: Library-Wurzel (Projektverzeichnis), enthält .sond/eingang
 *
 * Returns: (transfer full) (nullable): Cache oder NULL bei Fehler
 */
SondEingangCache* sond_eingang_cache_open(const gchar *db_path,
        const gchar *library_dir, GError **error);

void sond_eingang_cache_free(SondEingangCache *cache);

/**
 * sond_eingang_cache_reload:
 * @n_zettel: (out) (optional): Anzahl neu eingelesener Zettel
 *
 * Liest die noch nicht bekannten Zettel aus .sond/eingang ein. Ein nicht
 * lesbarer Zettel wird übersprungen (und beim nächsten Mal erneut versucht).
 *
 * Returns: 0 bei Erfolg, -1 bei einem Fehler am Cache selbst
 */
gint sond_eingang_cache_reload(SondEingangCache *cache, guint *n_zettel,
        GError **error);

/**
 * sond_eingang_cache_apply:
 * @zettel_name: Dateiname des Zettels (bestimmt die Reihenfolge)
 *
 * Wendet die Einträge eines Zettels an. Ein älterer Zettel überschreibt nie
 * die Angabe eines jüngeren (der Dateiname sortiert chronologisch).
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_eingang_cache_apply(SondEingangCache *cache,
        SondEingangZettel *zettel, const gchar *zettel_name, GError **error);

/**
 * sond_eingang_cache_lookup:
 * @rel_path: Pfad relativ zur Library-Wurzel ('/' als Trenner)
 * @is_dir: ist ein Verzeichnis? (ohne Einfluss auf das Ergebnis)
 *
 * Returns: das wirksame Eingangsdatum als JJJJMMTT, 0 = keine Angabe
 */
gint sond_eingang_cache_lookup(SondEingangCache *cache, const gchar *rel_path,
        gboolean is_dir);

/**
 * sond_eingang_cache_has_own:
 * @rel_path: Pfad relativ zur Library-Wurzel
 *
 * Returns: TRUE, wenn für diesen Pfad selbst ein Stempel gesetzt ist (nicht nur
 *          geerbt)
 */
gboolean sond_eingang_cache_has_own(SondEingangCache *cache,
        const gchar *rel_path);

/**
 * sond_eingang_cache_uebertragen_zettel:
 * @zettel: Zettel, dem die Aktionen hinzugefügt werden
 * @old_rel: bisheriger Pfad des umbenannten, verschobenen oder kopierten Eintrags
 * @new_rel: neuer Pfad
 * @alte_entfernen: TRUE beim Umbenennen und Verschieben (die Stempel ziehen
 *                  um), FALSE beim Kopieren (das Original behält sie)
 *
 * Für jeden Stempel an old_rel und darunter: "setzen" für den entsprechenden
 * neuen Pfad mit gleichem Datum und gleicher Art, bei alte_entfernen davor ein
 * "entfernen" für den alten.
 *
 * Returns: Anzahl der übertragenen Stempel (0 = nichts zu tun)
 */
guint sond_eingang_cache_uebertragen_zettel(SondEingangCache *cache,
        SondEingangZettel *zettel, const gchar *old_rel, const gchar *new_rel,
        gboolean alte_entfernen);

/**
 * sond_eingang_cache_wirksam:
 * @rel_path: Pfad relativ zur Library-Wurzel
 * @art: (out) (optional) (nullable): Art des bestimmenden Stempels (g_free())
 *
 * Eigener Stempel des Pfads oder, wenn es keinen gibt, der des nächsten
 * Ordners darüber. Das ist, was ein Eintrag ohne eigenen Stempel erbt.
 *
 * Returns: Datum JJJJMMTT, 0 = keine Angabe
 */
gint sond_eingang_cache_wirksam(SondEingangCache *cache, const gchar *rel_path,
        gchar **art);

/**
 * sond_eingang_cache_stempel_unter:
 * @rel_path: Pfad relativ zur Library-Wurzel
 *
 * Alle gesetzten Stempel für rel_path selbst und für alles darunter (Pfade wie
 * im Zettel angegeben). Für das Aufräumen, wenn ein Eintrag gelöscht wird.
 *
 * Returns: (transfer container) (element-type utf8): Pfade; mit
 *          g_ptr_array_unref() freigeben (die Strings gehören dem Array)
 */
GPtrArray* sond_eingang_cache_stempel_unter(SondEingangCache *cache,
        const gchar *rel_path);

G_END_DECLS

#endif /* SOND_EINGANG_CACHE_H */
