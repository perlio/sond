/*
 zond (sond_projekt_pflege.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_projekt_pflege.h
 * @brief Pflege der Projektdaten bei Änderungen am Dateibaum (GTK-frei)
 *
 * Wird eine Datei oder ein Teil davon gelöscht, verschoben oder umbenannt, müssen die Verweise
 * in der ZND-Datenbank (store und, in zond, work), der Index und die Adressen von Anhängen
 * nachgeführt werden, in einer Transaktion. Diese Logik lag in zond_treeviewfm.c; hier liegt
 * sie ohne Oberfläche, damit der Client (nur store) und zond (store und work) sie gemeinsam
 * nutzen. Die Datenbanken und der Index-Kontext kommen über SondProjektKontext, wer gerade
 * welche hat, entscheidet der Aufrufer (ein Projekt kann sich während der Lebensdauer des
 * Baums ändern).
 *
 * Diese Fassung enthält die Bausteine (Prüfen, Index-Transaktion, Renummerierung von Mimeparts,
 * Anhang-Adressen, Neueinlesen der Struktur); die Abläufe vor und nach Löschen, Verschieben und
 * Einfügen folgen schrittweise.
 */

#ifndef SOND_PROJEKT_PFLEGE_H_INCLUDED
#define SOND_PROJEKT_PFLEGE_H_INCLUDED

#include <glib.h>

#include "dbase_zond.h"
#include "../sond_fileparts.h"
#include "../sond_index.h"

G_BEGIN_DECLS

typedef struct _SondProjektPflege SondProjektPflege;

/* Woher die Pflege Datenbank und Index nimmt. Gelesen wird bei jedem Aufruf, nicht beim Anlegen. */
typedef struct {
	DBaseZond* (*dbase)(gpointer data);
	SondIndexCtx* (*index_ctx)(gpointer data); //NULL ohne Index
	gpointer data;
} SondProjektKontext;

SondProjektPflege* sond_projekt_pflege_new(SondProjektKontext const *kontext);

void sond_projekt_pflege_free(SondProjektPflege*);

/* ---- Prüfen ---- */

/* Verhindert Löschen: ist der Eintrag (oder, bei einem Verzeichnis, ein Abkömmling) angebunden?
 * Ein Anker (type 2, link zeigt auf den zu löschenden Knoten) würde sonst per ON DELETE CASCADE
 * zur toten Referenz. Bei einem Verzeichnis (is_dir) wird der Präfix path plus "//" (container_root:
 * ein Container, der als Verzeichnis dargestellt wird) bzw. "/" geprüft, sonst path samt Abschnitt.
 * 0 mit *ref, -1 bei Fehler. */
gint sond_projekt_pflege_check_delete(SondProjektPflege*, gchar const *path, gboolean is_dir,
		gboolean container_root, gchar const *section, DBaseZondRef *ref, GError **error);

/* ---- Index in der Transaktion ---- */

/* Hängt die Index-DB als Schema idx an (VOR dbase_zond_begin()). Ohne Index-Kontext ist nichts zu
 * tun. */
gint sond_projekt_pflege_index_tx_begin(SondProjektPflege*, GError **error);

//Nach Commit oder Rollback (auch auf allen Fehlerwegen)
void sond_projekt_pflege_index_tx_end(SondProjektPflege*);

//Index-Kontext für Schreibzugriffe in der Transaktion, NULL ohne Index
SondIndexCtx* sond_projekt_pflege_index_tx(SondProjektPflege*);

//Transaktion verwerfen: Rollback von store (und work) und Index lösen
void sond_projekt_pflege_tx_abort(SondProjektPflege*);

/* Index-Gegenstück zu dbase_zond_update_gmessage_index(): Mimepart-Nummern hinter prefix ("x.eml//",
 * "x.eml//0/") umzählen, die Strukturangaben der Mail (Teilezahl, Inline-Teile) verwerfen. */
gint sond_projekt_pflege_gmessage_index_renumber(SondProjektPflege*, gchar const *prefix,
		gint index, gboolean into, GError **error);

/* ---- Anhänge einer PDF ---- */

/* Ist sfp eine eingebettete Datei einer PDF, ändert ihr Löschen, Herausverschieben (filename_new
 * NULL) oder Umbenennen ggf. die Adressen der übrigen Anhänge (ein doppelter Dateiname wird
 * eindeutig, die Adresse des anderen wechselt vom Schlüssel zum Dateinamen). Ermittelt das vorab
 * (PDF nur lesend) und merkt es vor; nichts zu tun: nichts vorgemerkt. Vorher gemerkte Änderungen
 * werden verworfen. */
gint sond_projekt_pflege_emb_sibling_changes(SondProjektPflege*, SondFilePart *sfp,
		gchar const *filename_new, GError **error);

//Die vorgemerkten Adressen in store/work und im Index umschreiben (innerhalb der offenen Transaktion)
gint sond_projekt_pflege_emb_sibling_apply(SondProjektPflege*, GError **error);

//Nach erfolgreichem Commit: geöffnete Fileparts der Anhänge auf die neue Adresse setzen
void sond_projekt_pflege_emb_sibling_adjust_opened(SondProjektPflege*);

void sond_projekt_pflege_emb_sibling_clear(SondProjektPflege*);

/* ---- Struktur von PDF und Mail im Index ---- */

//Merkt die PDF bzw. Mail vor, deren Struktur nach der Aktion neu einzulesen ist (container NULL: nichts)
void sond_projekt_pflege_structure_add(SondProjektPflege*, SondFilePart *container);

void sond_projekt_pflege_structure_clear(SondProjektPflege*);

//Nach erfolgreicher Aktion: Struktur der vorgemerkten Container neu in den Index einlesen
void sond_projekt_pflege_structure_refresh(SondProjektPflege*);

G_END_DECLS

#endif // SOND_PROJEKT_PFLEGE_H_INCLUDED
