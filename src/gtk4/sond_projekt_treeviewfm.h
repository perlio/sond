/*
 sond (gtk4/sond_projekt_treeviewfm.h) - Akten, Beweisstücke, Unterlagen
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
 * @file gtk4/sond_projekt_treeviewfm.h
 * @brief Dateibaum eines Projekts (GTK4)
 *
 * Der Dateibaum SondTreeviewFM kennt nur das Dateisystem. Dieser Baum führt außerdem die Daten des
 * Projekts nach, wenn Einträge gelöscht, verschoben, umbenannt oder kopiert werden: die Verweise
 * in der Projekt-DB, den Index und die Adressen in Containern (sond_projekt_pflege). Wurzel,
 * Datenbanken, Schreibberechtigung und Meldungen kommen von außen über SondProjektHooks, die
 * Klasse selbst hängt an keiner Anwendung. Eine Anwendung mit eigenen Zutaten (z.B. zond mit
 * Viewer) leitet davon ab.
 */

#ifndef SOND_GTK4_PROJEKT_TREEVIEWFM_H_INCLUDED
#define SOND_GTK4_PROJEKT_TREEVIEWFM_H_INCLUDED

#include "sond_treeviewfm.h"
#include "../znd/dbase_zond.h"
#include "../znd/sond_projekt_pflege.h"

G_BEGIN_DECLS

/* Was die Anwendung dem Baum liefert. data wird jedem Aufruf mitgegeben; alle Einträge außer
 * message sind Pflicht. */
typedef struct {
	/* Darf die Akte verändert werden (Schreibberechtigung, ggf. holen)? FALSE mit error: die
	 * Aktion unterbleibt. Gilt auch für "Dauerhaft entsperren". */
	gboolean (*permit)(gpointer data, GError **error);

	/* Beginnt eine ändernde Aktion: Berechtigung prüfen und die Datenbanken für die Dauer der
	 * Aktion bereitstellen (Index und, bei project, die Projektdatei). Rückgabe: -1 geht nicht
	 * (error), 0 geht, aber ohne Projektdatei (es gibt keine; nur der Index wird nachgeführt),
	 * 1 geht, die Projektdatei wird mit gepflegt. Mit project = FALSE wird nur der Index gebraucht
	 * (Rückgabe 0 oder -1). Jedem erfolgreichen write_begin folgt genau ein write_end. */
	gint (*write_begin)(gpointer data, gboolean project, GError **error);
	void (*write_end)(gpointer data);

	/* Die Datenbank des Projekts (zond: work und store, Client: store) und der Index-Kontext,
	 * wie in SondProjektKontext; zwischen write_begin und write_end schreibbar */
	DBaseZond* (*dbase)(gpointer data);
	SondIndexCtx* (*index_ctx)(gpointer data);

	//Meldung an den Nutzer (nullable: dann nur ins Log)
	void (*message)(gpointer data, gchar const *title, gchar const *text);

	/* Ein Commit ist gescheitert und nicht zurückzunehmen (Projektdatei und Dateien können
	 * inkonsistent sein, die Fehlerdatei ist geschrieben). Die Anwendung soll das Projekt schließen und
	 * sich beenden (zond). Nullable: dann nur eine Meldung über message. */
	void (*critical)(gpointer data, gchar const *text);

	/* Eine Aktion mit Transaktion ist abgeschlossen (Commit oder Rollback), vor write_end. dual: sie
	 * lief über store und work (zond: das Änderungskennzeichen des Projekts auf den Stand vor der
	 * Aktion zurücksetzen, weil store schon geschrieben ist). Nullable. */
	void (*tx_done)(gpointer data, gboolean dual);

	gpointer data;
} SondProjektHooks;

#define SOND_TYPE_PROJEKT_TREEVIEWFM sond_projekt_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondProjektTreeviewFM, sond_projekt_treeviewfm, SOND, PROJEKT_TREEVIEWFM,
		SondTreeviewFM)

struct _SondProjektTreeviewFMClass {
	SondTreeviewFMClass parent_class;
};

GtkWidget* sond_projekt_treeviewfm_new(void);

//Hooks der Anwendung setzen (kopiert); vor dem ersten Setzen der Wurzel
void sond_projekt_treeviewfm_set_hooks(SondProjektTreeviewFM*, SondProjektHooks const *hooks);

//Die Pflege des Baums (nicht freigeben)
SondProjektPflege* sond_projekt_treeviewfm_get_pflege(SondProjektTreeviewFM*);

G_END_DECLS

#endif /* SOND_GTK4_PROJEKT_TREEVIEWFM_H_INCLUDED */
