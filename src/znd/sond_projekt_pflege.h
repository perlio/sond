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

/* ---- Transaktionsrahmen von Löschen und Verschieben ---- */

/* Beginnt die Transaktion: dual = store und work (bzw. nur store ohne work), sonst nur work.
 * with_index hängt vorher die Index-DB an (Schema idx), ihre Schreibzugriffe gehören dann zum selben
 * Commit. 0 bei Erfolg, -1 bei Fehler (nichts angehängt, nichts offen). */
gint sond_projekt_pflege_tx_begin(SondProjektPflege*, gboolean dual, gboolean with_index,
		GError **error);

/* Vormerken für den Abschluss, erst unmittelbar vor dem garantierten Erfolg der Vorbereitung:
 * der gelöschte Pfad (Coverage des Elternverzeichnisses nach der physischen Löschung neu
 * zusammenfassen; NULL: nichts) bzw. alter/neuer absoluter Pfad einer Verschiebung und ob sie
 * physisch im Dateisystem stattfand (NULL-Pfade: nichts). */
void sond_projekt_pflege_delete_merken(SondProjektPflege*, gchar const *path);
void sond_projekt_pflege_move_merken(SondProjektPflege*, gchar const *path_old,
		gchar const *path_new, gboolean physical);

typedef enum {
	SOND_PROJEKT_TX_OK, //Aktion gespeichert (suc) bzw. zurückgerollt (nicht suc)
	SOND_PROJEKT_TX_FEHLER, //Commit gescheitert, Zustand zurückgenommen (error: Grund); bitte erneut versuchen
	SOND_PROJEKT_TX_KRITISCH //Commit gescheitert und nicht zurücknehmbar: Fehlerdatei ist geschrieben
		//(error: Grund), die Anwendung soll das Projekt schließen und sich beenden
} SondProjektTxErgebnis;

/* Schließt die Transaktion ab: bei suc Coverage zusammenfassen, Commit, Index lösen, geöffnete Anhänge
 * auf ihre neue Adresse setzen, Struktur neu einlesen; sonst Rollback. Bei gescheitertem Commit einer
 * physischen Verschiebung wird diese per Umbenennen mit vertauschten Pfaden zurückgenommen. Alle
 * Vormerkungen sind danach verworfen. project_dir: Projektverzeichnis (Coverage, Fehlerdatei). */
SondProjektTxErgebnis sond_projekt_pflege_tx_ende(SondProjektPflege*, gboolean suc, gboolean dual,
		gchar const *project_dir, GError **error);

/* ---- Verschieben, Umbenennen ---- */

/* Eine Verschiebung oder Umbenennung in den Projektdaten. Die Pfade sind fertig gebildet (Filepart,
 * Container-Inhalte mit "//", Anhang einer PDF mit kodiertem Dateinamen, Mimepart mit Nummer). */
typedef struct {
	gchar const *prefix_old; //bisheriger Pfad (Präfix)
	gchar const *prefix_new; //neuer Pfad; bei einem Mimepart noch mit Platzhalter "alpha"
	gchar const *project_dir;
	gint index_to; //-1: Umbenennen an Ort und Stelle; sonst Einfügeposition (Mimepart)
	gboolean from_gmessage; //kommt aus einer Mail (index_from gültig)
	gint index_from;
	gboolean into_gmessage; //geht in eine Mail
	gboolean rename_in_gmessage; //nur Anzeigename eines Mimeparts: Pfad bleibt
	SondFilePart *sfp_emb; //Element, wenn es ein Anhang einer PDF ist, sonst NULL
	SondFilePart *sfp_target; //neuer Container (für "bleibt in derselben PDF")
	gchar const *base_new; //neuer Name im Ziel
} SondProjektMove;

/* Beginnt die Dual-Write-Transaktion (Index-DB angehängt, dbase_zond_begin) und schreibt Pfad,
 * Mimepart-Nummern und Anhang-Adressen in store/work und Index um; Commit oder Rollback macht der
 * Aufrufer. 0 bei Erfolg; bei -1 ist alles zurückgenommen (kein Rollback nötig). */
gint sond_projekt_pflege_move(SondProjektPflege*, SondProjektMove const*, GError **error);

G_END_DECLS

#endif // SOND_PROJEKT_PFLEGE_H_INCLUDED
