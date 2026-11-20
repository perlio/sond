/*
 sond (sond_eingang_pflege.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_eingang_pflege.h
 * @brief Nachführen der Eingangsangaben (Stempel) bei Änderungen im Dateisystem
 *
 * GTK-frei, ohne Meldungen: die Funktionen liefern Ergebnisse und Fehler, die Anwendung entscheidet,
 * was sie dem Nutzer sagt. Grundlage sind Cache (sond_eingang_cache.h) und Zettel
 * (sond_eingang_zettel.h) der Akte.
 */

#ifndef SOND_EINGANG_PFLEGE_H_INCLUDED
#define SOND_EINGANG_PFLEGE_H_INCLUDED

#include <glib.h>

#include "sond_eingang_cache.h"
#include "sond_eingang_zettel.h"

G_BEGIN_DECLS

typedef enum {
	SOND_EINGANG_ABLEGEN_OK,
	SOND_EINGANG_ABLEGEN_NICHT_GESPEICHERT, //der Zettel konnte nicht in die Akte geschrieben werden
	SOND_EINGANG_ABLEGEN_NICHT_UEBERNOMMEN //geschrieben, aber nicht in die lokale Tabelle übernommen
} SondEingangAblegen;

/* Zettel in die Akte (.sond/eingang unter project_dir) schreiben und in die lokale Tabelle eintragen.
 * Bei einem Fehler enthält *error den Grund. */
SondEingangAblegen sond_eingang_pflege_ablegen(SondEingangCache *cache, gchar const *project_dir,
		SondEingangZettel *zettel, GError **error);

/* Ein Eintrag des Dateisystems wurde umbenannt: seine Stempel und die darunter ziehen auf den neuen
 * Pfad um (Zettel mit Quelle "umbenennen"; Datum und Art bleiben). 0 bei Erfolg oder wenn nichts
 * gestempelt war, -1 bei Fehler (die Angaben wären sonst verloren, die Anwendung soll es melden). */
gint sond_eingang_pflege_umbenannt(SondEingangCache *cache, gchar const *project_dir,
		gchar const *alt, gchar const *neu, GError **error);

/* Ein Eintrag des Dateisystems wurde gelöscht: seine Stempel und die darunter werden aufgehoben
 * (Zettel mit Quelle "loeschen"). 0 bei Erfolg oder wenn nichts gestempelt war, -1 bei Fehler; die
 * Löschung selbst bleibt gültig, zurückbleibende Stempel sind harmlos. */
gint sond_eingang_pflege_geloescht(SondEingangCache *cache, gchar const *project_dir,
		gchar const *rel_path);

//Was die Anwendung nach einer Verschiebung oder Kopie dem Nutzer mitteilen soll
typedef enum {
	SOND_EINGANG_VERSCHOBEN_KEIN_HINWEIS,
	SOND_EINGANG_VERSCHOBEN_KOPIE_IN_CONTAINER, //die Kopie liegt in einem Container, das Original behält den Eingang
	SOND_EINGANG_VERSCHOBEN_IN_CONTAINER, //der Eintrag liegt jetzt in einem Container, seine Stempel entfielen
	SOND_EINGANG_VERSCHOBEN_FEHLER_AUFHEBEN, //Stempel konnten nicht aufgehoben werden (error)
	SOND_EINGANG_VERSCHOBEN_FEHLER_UEBERTRAGEN //Stempel konnten nicht auf den neuen Ort übertragen werden (error)
} SondEingangVerschoben;

/* Ein Eintrag des Dateisystems wurde verschoben oder kopiert (neu = Pfad am Ziel, NULL: das Ziel liegt
 * in einem Container). Die Stempel des Eintrags und der Einträge darunter ziehen mit (Verschieben)
 * bzw. werden mitkopiert (Kopieren). Hat der Eintrag keinen eigenen Stempel und würde am Ziel etwas
 * anderes erben, wird der bisher wirksame Stempel am Ziel festgeschrieben. Wandert er in einen
 * Container, entfallen seine Stempel. Rückgabe: TRUE, wenn sich Eingangsangaben geändert haben.
 * *ergebnis sagt, was zu melden ist; bei den Fehlerwerten steht der Grund in *error. */
gboolean sond_eingang_pflege_verschoben(SondEingangCache *cache, gchar const *project_dir,
		gchar const *alt, gchar const *neu, gboolean ist_ordner, gboolean kopiert,
		SondEingangVerschoben *ergebnis, GError **error);

G_END_DECLS

#endif // SOND_EINGANG_PFLEGE_H_INCLUDED
