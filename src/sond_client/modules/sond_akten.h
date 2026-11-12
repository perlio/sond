/*
 sond (sond_akten.h) - Akten, Beweisstücke, Unterlagen
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
 * Akten-Liste des Clients, ohne Server: Die Akten ergeben sich aus den
 * Libraries im SeaDrive-Laufwerk (Verzeichnisnamen der Form "Jahr-Nr"); die
 * Bezeichnung steht als Dateiname in <Library>/.sond/akte (s. sond_akte_name.h).
 * Ist sie offline nicht lesbar, greift eine lokale Merkliste (Library-Name ->
 * Bezeichnung), die bei jedem erfolgreichen Lesen nachgeführt wird.
 *
 * Frei von GTK; nur glib und json-glib.
 */

#ifndef SOND_AKTEN_H
#define SOND_AKTEN_H

#include <glib.h>

typedef struct {
    gchar   *library_name;        /* "2026-23" */
    guint    lfd_nr;              /* 23 */
    guint    year;                /* 2026 */
    gchar   *library_dir;         /* Verzeichnis der Library im SeaDrive */
    gchar   *bezeichnung;         /* nullable: Aktenbezeichnung */
    gboolean bezeichnung_gemerkt; /* aus der Merkliste, nicht aus der Library */
} SondAkteEintrag;

void sond_akte_eintrag_free(SondAkteEintrag *eintrag);

/**
 * sond_akten_merkliste_pfad:
 *
 * Returns: (transfer full): Standardpfad der Merkliste
 *          (<Konfigurationsverzeichnis>/sond/akten.json)
 */
gchar* sond_akten_merkliste_pfad(void);

/**
 * sond_akten_lese_bezeichnung:
 * @library_dir: Verzeichnis einer Library
 *
 * Liest die Aktenbezeichnung aus dem einzigen Eintrag von .sond/akte. Bei
 * mehreren Einträgen gilt der in der Sortierung erste.
 *
 * Returns: (transfer full) (nullable): Bezeichnung oder NULL (kein Eintrag,
 *          Verzeichnis nicht lesbar)
 */
gchar* sond_akten_lese_bezeichnung(const gchar *library_dir);

/**
 * sond_akten_scan:
 * @seadrive_root: Wurzel des SeaDrive-Laufwerks
 * @merkliste: (nullable): Pfad der Merkliste; NULL = keine
 * @error: (nullable)
 *
 * Alle Akten-Libraries im SeaDrive, neueste zuerst (Jahr, dann Nummer
 * absteigend). Eine Library, die in mehreren Kategorien auftaucht, zählt
 * einmal. Gelesene Bezeichnungen werden in die Merkliste übernommen.
 *
 * Returns: (transfer full) (element-type SondAkteEintrag) (nullable): Liste
 *          (g_ptr_array_unref()); NULL, wenn die Wurzel nicht lesbar ist
 */
GPtrArray* sond_akten_scan(const gchar *seadrive_root, const gchar *merkliste,
        GError **error);

/**
 * sond_akten_suchen:
 * @alle: Liste aus sond_akten_scan()
 * @text: Suchtext
 *
 * Erkennt RegNr ("23/26", "23/2026", "2026-23", nur "23" = jedes Jahr) und
 * sonst Wörter: jedes Wort muss in Bezeichnung oder Library-Name vorkommen
 * (ohne Beachtung der Groß-/Kleinschreibung). Leerer Text liefert alle.
 *
 * Returns: (transfer container) (element-type SondAkteEintrag): Treffer in der
 *          Reihenfolge von @alle; die Einträge gehören @alle
 */
GPtrArray* sond_akten_suchen(GPtrArray *alle, const gchar *text);

/**
 * sond_akten_merken:
 * @merkliste: Pfad der Merkliste
 *
 * Trägt eine Bezeichnung in die Merkliste ein (z. B. nach einer Serverabfrage).
 */
void sond_akten_merken(const gchar *merkliste, const gchar *library_name,
        const gchar *bezeichnung);

#endif /* SOND_AKTEN_H */
