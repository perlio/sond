/*
 sond (sond_eingang_zettel.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_eingang_zettel.h
 * @brief Eingangsangaben ("Stempel") einer Akte, abgelegt als Zettel
 *
 * Ein Zettel ist eine einmal geschriebene, nie geänderte JSON-Datei im
 * Verzeichnis ".sond/eingang" der Library. Er enthält die Stempel eines
 * Vorgangs (Import, manuelles Stempeln): je Eintrag ein Pfad (relativ zur
 * Library-Wurzel, '/' als Trenner) mit Eingangsdatum. Jeder Schreiber legt
 * eigene Dateien an, deshalb gibt es auch bei gleichzeitigem Arbeiten auf
 * mehreren Rechnern keine Synchronisationskonflikte.
 *
 * Frei von GTK, nur glib und json-glib; von Client und zond nutzbar.
 */

#ifndef SOND_EINGANG_ZETTEL_H
#define SOND_EINGANG_ZETTEL_H

#include <glib.h>

G_BEGIN_DECLS

/* Verzeichnis der Zettel unterhalb der Library-Wurzel */
#define SOND_EINGANG_DIR ".sond/eingang"

/**
 * SondEingangEintrag:
 *
 * Ein Stempel (oder, mit entfernen, die Rücknahme eines Stempels).
 */
typedef struct {
    gchar   *pfad;        /* relativ zur Library-Wurzel */
    gboolean ist_ordner;
    gboolean entfernen;   /* Stempel für pfad aufheben (eingang ist dann NULL) */
    gchar   *eingang;     /* "JJJJ-MM-TT" */
    gchar   *art;         /* (nullable) z.B. "CD-ROM", "E-Mail" */
    gchar   *bemerkung;   /* (nullable) */
    gint64   original;    /* Änderungszeit der Quelle (Dateien), 0 = unbekannt */
} SondEingangEintrag;

/**
 * SondEingangZettel:
 */
typedef struct {
    gchar     *id;        /* UUID */
    gchar     *angelegt;  /* "JJJJ-MM-TTThh:mm:ssZ" (UTC) */
    gchar     *von;       /* Benutzer */
    gchar     *rechner;
    gchar     *quelle;    /* "import", "manuell", "kopie", "loeschen" */
    GPtrArray *eintraege; /* (element-type SondEingangEintrag) */
} SondEingangZettel;

/**
 * sond_eingang_zettel_new:
 * @von: Benutzer (nullable)
 * @quelle: "import", "manuell", "kopie" oder "loeschen"
 *
 * Neuer, leerer Zettel mit Kennung, Zeitpunkt (jetzt, UTC) und Rechnername.
 *
 * Returns: (transfer full): neuer Zettel
 */
SondEingangZettel* sond_eingang_zettel_new(const gchar *von, const gchar *quelle);

/**
 * sond_eingang_zettel_add:
 * @eingang: "JJJJ-MM-TT"
 * @art: (nullable), @bemerkung: (nullable)
 * @original: Änderungszeit der Quelle in Sekunden seit 1970, 0 = unbekannt
 */
void sond_eingang_zettel_add(SondEingangZettel *zettel, const gchar *pfad,
        gboolean ist_ordner, const gchar *eingang, const gchar *art,
        const gchar *bemerkung, gint64 original);

/**
 * sond_eingang_zettel_add_entfernen:
 *
 * Nimmt einen früheren Stempel für pfad zurück.
 */
void sond_eingang_zettel_add_entfernen(SondEingangZettel *zettel,
        const gchar *pfad);

/**
 * sond_eingang_zettel_write:
 * @library_dir: Library-Wurzel (SeaDrive-Verzeichnis der Akte)
 *
 * Schreibt den Zettel nach <library_dir>/.sond/eingang/<Zeit>_<Benutzer>_<Kennung>.json.
 * Erst unter einem Hilfsnamen, dann umbenannt, damit kein halber Zettel
 * sichtbar wird. Ein Zettel ohne Einträge wird nicht geschrieben.
 *
 * Returns: (transfer full) (nullable): Dateiname des Zettels (ohne Pfad)
 *          oder NULL bei Fehler (error gesetzt)
 */
gchar* sond_eingang_zettel_write(SondEingangZettel *zettel,
        const gchar *library_dir, GError **error);

/**
 * sond_eingang_zettel_read:
 * @file_path: Pfad der Zettel-Datei
 *
 * Returns: (transfer full) (nullable): Zettel oder NULL bei Fehler
 */
SondEingangZettel* sond_eingang_zettel_read(const gchar *file_path,
        GError **error);

void sond_eingang_zettel_free(SondEingangZettel *zettel);

/**
 * sond_eingang_datum_parse:
 * @text: "TT.MM.JJJJ", "T.M.JJ" oder "JJJJ-MM-TT"
 * @iso: (out) (optional): bei Erfolg "JJJJ-MM-TT" (g_free())
 *
 * Returns: TRUE, wenn text ein gültiges Datum ist
 */
gboolean sond_eingang_datum_parse(const gchar *text, gchar **iso);

/**
 * sond_eingang_datum_heute:
 *
 * Returns: (transfer full): heutiges Datum (lokal) als "JJJJ-MM-TT"
 */
gchar* sond_eingang_datum_heute(void);

/**
 * sond_eingang_datum_anzeige:
 * @iso: "JJJJ-MM-TT"
 *
 * Returns: (transfer full): "TT.MM.JJJJ" (bei ungültiger Eingabe eine Kopie)
 */
gchar* sond_eingang_datum_anzeige(const gchar *iso);

/**
 * sond_eingang_arten:
 *
 * Auswahl für die Art des Eingangs (Post, E-Mail, ...); Eintrag 0 steht für
 * "keine Angabe". Von Client und zond gemeinsam genutzt.
 *
 * Returns: (transfer none): NULL-terminierte Liste, nicht freigeben
 */
const gchar* const* sond_eingang_arten(void);

/**
 * sond_eingang_datum_zu_int:
 * @iso: "JJJJ-MM-TT"
 *
 * Returns: Datum als Zahl JJJJMMTT (z.B. 20261105), 0 bei ungültiger Eingabe
 */
gint sond_eingang_datum_zu_int(const gchar *iso);

/**
 * sond_eingang_int_zu_unix:
 * @datum: JJJJMMTT
 *
 * Für Sortierung und Vergleich mit Änderungszeiten: Sekunden seit 1970 für
 * Mitternacht (UTC) des Tages. 0 bei ungültigem Datum.
 */
gint64 sond_eingang_int_zu_unix(gint datum);

/**
 * sond_eingang_int_zu_anzeige:
 * @datum: JJJJMMTT
 *
 * Returns: (transfer full): "TT.MM.JJJJ"
 */
gchar* sond_eingang_int_zu_anzeige(gint datum);

G_END_DECLS

#endif /* SOND_EINGANG_ZETTEL_H */
