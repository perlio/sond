/*
 sond (sond_password.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2020  pelo america

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

#ifndef SRC_SOND_PASSWORD_H_
#define SRC_SOND_PASSWORD_H_

#include <glib.h>

/* Sitzungsweiter Speicher für Passwörter passwortgeschützter ZIP-Einträge und
 * PDFs. Nur im Arbeitsspeicher: nichts davon gelangt in Datenbank, Index oder
 * Log. Beim Schließen des Projekts wird alles überschrieben und freigegeben
 * (sond_password_clear()). Threadsicher.
 *
 * Gespeichert werden nur Passwörter, die sich bewährt haben. Schlüssel ist der
 * Filepart-String ("a.zip//ordner/x.pdf") und eine Rolle:
 * - SOND_PASSWORD_ENTRY:   Passwort, mit dem DIESES Element als Eintrag seines
 *                          ZIP-Archivs entschlüsselt wird.
 * - SOND_PASSWORD_CONTENT: Passwort für den Inhalt des Elements selbst - bei
 *                          einer PDF ihr Dokumentpasswort, bei einem ZIP das
 *                          Standardpasswort seiner Einträge.
 * Ein Eintrag mit abweichendem Passwort wird mit ENTRY an seinem Pfad
 * abgelegt, alle übrigen laufen über das CONTENT-Standardpasswort des
 * Archivs (sond_password_candidates_entry()). */
typedef enum {
	SOND_PASSWORD_ENTRY = 0,
	SOND_PASSWORD_CONTENT
} SondPasswordRole;

/* Legt password für filepart/role ab (Kopie, ersetzt ein vorhandenes). */
void sond_password_set(gchar const *filepart, SondPasswordRole role,
		gchar const *password);

/* Liefert eine Kopie des gespeicherten Passworts oder NULL. Freigabe mit
 * sond_password_free(). */
gchar* sond_password_get(gchar const *filepart, SondPasswordRole role);

/* Passwörter, die für den ZIP-Eintrag filepart zu probieren sind, in dieser
 * Reihenfolge, ohne Doppelte:
 * 1. das für den Eintrag selbst gespeicherte,
 * 2. das Standardpasswort des umgebenden Archivs,
 * 3. die Passwörter, die bei anderen Einträgen desselben Archivs gebraucht
 *    wurden.
 * GPtrArray von gchar*; Freigabe mit sond_password_array_free(). Leer, wenn
 * nichts bekannt ist. */
GPtrArray* sond_password_candidates_entry(gchar const *filepart);

void sond_password_array_free(GPtrArray *arr);

/* Alles, was unter prefix liegt (prefix selbst, prefix + "/" und prefix +
 * "//"), unter new_prefix weiterführen - beim Umbenennen oder Verschieben im
 * selben Archiv. */
void sond_password_rename_prefix(gchar const *prefix, gchar const *new_prefix);

/* Alles, was unter prefix liegt, vergessen - beim Löschen. */
void sond_password_forget_prefix(gchar const *prefix);

/* Alles vergessen und überschreiben - beim Schließen des Projekts. */
void sond_password_clear(void);

/* Abfrage beim Nutzer. Die Oberfläche (zond) meldet beim Start eine Funktion
 * an, die einen Passwortdialog zeigt: what beschreibt, wofür das Passwort
 * gebraucht wird ("Passwort für den Eintrag x.pdf in a.zip"), wrong ist TRUE,
 * wenn das zuvor eingegebene Passwort nicht passte, allow_skip ist TRUE bei
 * einer Aktion über mehrere Dateien (dann bietet der Dialog zusätzlich
 * Überspringen und Alle überspringen an). Bei SOND_PROMPT_OK liefert die
 * Funktion das Passwort in *password (Freigabe mit sond_password_free()). */
typedef enum {
	SOND_PROMPT_OK = 0,
	SOND_PROMPT_SKIP,      //dieses Element überspringen
	SOND_PROMPT_SKIP_ALL,  //dieses und alle weiteren der Aktion überspringen
	SOND_PROMPT_CANCEL     //Dialog bzw. ganze Aktion abbrechen
} SondPromptResult;

typedef SondPromptResult (*SondPasswordPromptFunc)(gchar const *what,
		gboolean wrong, gboolean allow_skip, gchar **password,
		gpointer user_data);

/* Meldet die Dialogfunktion an. Muss im Hauptthread aufgerufen werden; dieser
 * gilt danach als der einzige Thread, der fragen darf. */
void sond_password_set_prompt_func(SondPasswordPromptFunc func,
		gpointer user_data);

/* TRUE, wenn gefragt werden darf: Dialogfunktion angemeldet und Aufruf aus
 * dem Hauptthread. Hintergrund-Threads (Indexlauf) fragen nie. */
gboolean sond_password_can_prompt(void);

/* Fragt den Nutzer; NULL bei Überspringen, Abbruch oder wenn nicht gefragt
 * werden darf (die Wahl des Nutzers liefert sond_password_last_result()).
 * Innerhalb einer Aktion (s.u.) wird nach "Alle überspringen" bzw. "Abbrechen"
 * nicht mehr gefragt. */
gchar* sond_password_prompt(gchar const *what, gboolean wrong);

/* Wahl des Nutzers im letzten Dialog; SOND_PROMPT_OK, wenn seit
 * sond_password_action_begin() nicht gefragt oder zuletzt ein Passwort
 * eingegeben wurde. */
SondPromptResult sond_password_last_result(void);

/* Eine Aktion über mehrere Dateien (Anbinden, Kopieren, Export, ...) klammern:
 * begin setzt "Alle überspringen" und "Abbrechen" und die Liste der
 * übersprungenen Teile zurück; allow_skip lässt im Dialog die Knöpfe
 * Überspringen/Alle überspringen zu (sonst nur OK und Abbrechen). end hebt das
 * wieder auf. Schachtelbar, nur der äußerste Aufruf zählt. Nur im Hauptthread. */
void sond_password_action_begin(gboolean allow_skip);
void sond_password_action_end(void);

/* TRUE, wenn der Nutzer die laufende Aktion im Passwortdialog abgebrochen hat. */
gboolean sond_password_action_cancelled(void);

/* Teile (Filepart-Strings), die in der laufenden Aktion wegen fehlenden
 * Passworts übersprungen werden: vormerken, abfragen, alle holen (Kopie, der
 * Aufrufer gibt sie mit g_ptr_array_unref() frei; Elemente g_free). */
void sond_password_action_skip_add(gchar const *filepart);
gboolean sond_password_action_is_skipped(gchar const *filepart);
GPtrArray* sond_password_action_skipped(void);

/* Überschreibt den String mit Nullen und gibt ihn frei (NULL erlaubt). Für
 * jede Kopie, die sond_password_get() oder der Dialog geliefert hat. */
void sond_password_free(gchar *password);

#endif /* SRC_SOND_PASSWORD_H_ */
