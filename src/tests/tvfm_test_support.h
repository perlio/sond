/*
 sond (tvfm_test_support.h) - Akten, Beweisstücke, Unterlagen
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
 * Gemeinsamer Rahmen der Tests von tvfm: ein temporäres Projektverzeichnis mit eingestelltem
 * path_root, ein nachgebauter Eigentümer (SondTVFMOwner) und Hilfen zum Anlegen von Dateien,
 * ZIP-Archiven, E-Mails und PDF-Dateien mit Anhängen.
 */

#ifndef TVFM_TEST_SUPPORT_H_INCLUDED
#define TVFM_TEST_SUPPORT_H_INCLUDED

#include <glib.h>
#include <glib-object.h>

#include "../sond_fileparts.h"
#include "../tvfm/sond_tvfm_item.h"
#include "../tvfm/sond_tvfm_owner.h"

G_BEGIN_DECLS

typedef struct {
	GObject parent;
	gchar *root;
	gint n_before_move;
	gint n_after;
	gboolean last_ok;
	gboolean seadrive; //die Wurzel gilt als SeaDrive-Pfad (Standard: nein)
} TvfmTestOwner;

typedef struct {
	gchar *dir; //Projektverzeichnis (temporär)
	TvfmTestOwner *owner;
} Fix;

GType tvfm_test_owner_get_type(void);

//legt das Projektverzeichnis an und stellt path_root darauf ein
Fix* fix_new(void);

//räumt auf: Eigentümer freigeben, path_root zurücksetzen, Verzeichnis löschen
void fix_free(Fix*);

//Datei rel (relativ zum Projektverzeichnis, Unterverzeichnisse werden angelegt)
void schreibe(Fix*, gchar const *rel, gchar const *inhalt);

//Inhalt der Datei rel oder NULL
gchar* lese(Fix*, gchar const *rel);

gboolean existiert(Fix*, gchar const *rel);

/* ZIP-Archiv rel mit Einträgen: Paare (Pfad, Inhalt), abgeschlossen mit NULL; ein Pfad mit "/"
 * am Ende ist ein Verzeichniseintrag (Inhalt NULL). */
void schreibe_zip(Fix*, gchar const *rel, ...) G_GNUC_NULL_TERMINATED;

//E-Mail rel (multipart/mixed) mit Text und einem Anhang
void schreibe_eml(Fix*, gchar const *rel, gchar const *betreff, gchar const *text,
		gchar const *anhang_name, gchar const *anhang_inhalt);

/* PDF-Datei rel mit einer Seite und eingebetteten Dateien: Paare (Name, Inhalt), abgeschlossen mit
 * NULL (gleiche Namen bekommen im Namensbaum einen Zähler). */
void schreibe_pdf(Fix*, gchar const *rel, ...) G_GNUC_NULL_TERMINATED;

/* ZIP-Archiv rel mit Einträgen als Tripel (Pfad, Inhalt, Passwort), abgeschlossen mit NULL; ein
 * Passwort NULL lässt den Eintrag unverschlüsselt, sonst AES-256. Ein Pfad mit "/" am Ende ist
 * ein Verzeichniseintrag. */
void schreibe_zip_passwort(Fix*, gchar const *rel, ...) G_GNUC_NULL_TERMINATED;

//PDF-Datei rel mit einer leeren Seite, die ein Passwort zum Öffnen verlangt (AES-256)
void schreibe_pdf_passwort(Fix*, gchar const *rel, gchar const *passwort);

//PDF-Datei rel mit n leeren Seiten, ohne Anhänge
void schreibe_pdf_seiten(Fix*, gchar const *rel, gint n);

//Item der Wurzel, eines Verzeichnisses (nur Pfad) und einer Datei (SondFilePart)
SondTVFMItem* wurzel(Fix*);
SondTVFMItem* verzeichnis(Fix*, gchar const *rel);
SondTVFMItem* datei(Fix*, gchar const *rel);

//Kind mit dem Anzeigenamen name oder NULL
SondTVFMItem* kind(GPtrArray *arr, gchar const *name);

G_END_DECLS

#endif /* TVFM_TEST_SUPPORT_H_INCLUDED */
