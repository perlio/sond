/*
 sond (gtk4/sond_treeviewfm.h) - Akten, Beweisstücke, Unterlagen
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
 * @file gtk4/sond_treeviewfm.h
 * @brief Dateibaum für GTK4 (Neufassung von src/sond_treeviewfm.h)
 *
 * Zeigt ein Verzeichnis (und die Container darin, soweit SondTVFMItem sie
 * öffnet) als Baum mit den Spalten Name, Offline, Geändert und Größe. Die
 * Zeilen sind SondTVFMItem; das Widget implementiert SondTVFMOwner.
 *
 * Stand Phase 1: Anzeigen, Sortieren, Mehrfachauswahl, Offline-Zustand
 * (SeaDrive) und Kontextmenü zum Anheften. Bearbeiten (Umbenennen, Anlegen,
 * Löschen, Kopieren, Verschieben), Suche und Öffnen folgen.
 */

#ifndef SOND_GTK4_TREEVIEWFM_H_INCLUDED
#define SOND_GTK4_TREEVIEWFM_H_INCLUDED

#include "sond_treeview.h"
#include "../sond_icon_util.h" /* SondSeadriveBadge, SondSeadriveDirStatus, SondIndexStatus */
#include "../tvfm/sond_tvfm_item.h"

G_BEGIN_DECLS

#define SOND_TYPE_TREEVIEWFM sond_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondTreeviewFM, sond_treeviewfm, SOND, TREEVIEWFM, SondTreeview)

struct _SondTreeviewFMClass {
	SondTreeviewClass parent_class;

	/* Signale vor und nach Änderungen (wie in der GTK3-Fassung); die Item-Schicht
	 * löst sie über SondTVFMOwner aus. Rückgabe der before-*-Signale: 0 = weiter,
	 * -1 = Fehler, 1 = (vor dem Löschen) nichts weiter tun. */
	guint signal_before_move;   //item, parent, base, index, GError**, ctx* -> int
	guint signal_before_insert; //item, parent, base, index, GError** -> int
	guint signal_before_delete; //item, GError**, ctx* -> int
	guint signal_after;         //gboolean success, ctx
	guint signal_after_insert;  //gboolean success

	/* Einhängepunkte für Unterklassen (Zond): Aufbau der Spalten und des
	 * Kontextmenüs. Die Basisklasse legt Name, Geändert und Größe sowie das
	 * Offline-Menü an; eine Unterklasse überschreibt, ruft die Elternfunktion auf
	 * und ergänzt. Aufruf in constructed(). */
	void (*setup_columns)(SondTreeviewFM*);
	void (*setup_menu)(SondTreeviewFM*);

	/* Quellen für die Overlay-Symbole (s. sond_tvfm_status.h). Standard: Zustand
	 * einer Datei aus den Dateiattributen, Ordner-Teilbaum und Index leer. Zond
	 * überschreibt sie mit dem Stand von Watcher und Index. */
	SondSeadriveBadge (*file_badge)(SondTreeviewFM*, gchar const *full_path);
	SondSeadriveDirStatus (*dir_status)(SondTreeviewFM*, gchar const *full_path);
	SondIndexStatus (*index_status)(SondTreeviewFM*, SondTVFMItem*);

	/* Status einer Anbindung (Section) in der Index-DB; die Basisklasse kennt die
	 * Bedeutung von Sections nicht. NULL = kein Symbol für Sections. */
	SondIndexStatus (*get_section_index_status)(SondTVFMItem*, SondIndexCtx*);

	/* Ein Eintrag des Dateisystems (rel_path relativ zur Wurzel, '/' als Trenner)
	 * wurde gelöscht bzw. umbenannt: die Unterklasse kann Zusatzangaben (Eingangs-
	 * stempel) nachführen. NULL = nichts zu tun. */
	void (*fs_entry_deleted)(SondTreeviewFM*, gchar const *rel_path, gboolean is_dir);
	void (*fs_entry_renamed)(SondTreeviewFM*, gchar const *old_rel,
			gchar const *new_rel, gboolean is_dir);

	/* Ein Eintrag des Dateisystems wurde verschoben (copy = FALSE) oder kopiert
	 * (copy = TRUE): old_rel/new_rel wie oben (new_rel NULL, wenn das Ziel kein
	 * Dateisystem-Ordner ist). TRUE, wenn Zusatzangaben geändert wurden und die
	 * Anzeige aufzufrischen ist. NULL = nichts zu tun. */
	gboolean (*fs_entry_moved)(SondTreeviewFM*, gchar const *old_rel,
			gchar const *new_rel, gboolean is_dir, gboolean copy);

	/* Öffnet einen Eintrag (Doppelklick, Menü). Standard: Datei des Dateisystems mit
	 * dem Standardprogramm bzw. "Öffnen mit" des Betriebssystems. Zond überschreibt
	 * das mit seinem Viewer. 0 bei Erfolg, -1 bei Fehler. */
	gint (*open_item)(SondTreeviewFM*, SondTVFMItem*, gboolean open_with, GError**);

	/* Soll das Löschen vorher bestätigt werden? (Standard: ja) */
	gboolean confirm_delete;

	/* Abschnitte (Anbindungen) von Einträgen; NULL = unterstützt der Baum nicht */
	gboolean (*has_sections)(SondTVFMItem*);
	gint (*load_sections)(SondTVFMItem*, GPtrArray**, GError**);
	gint (*delete_section)(SondTVFMItem*, GError**);
};

GtkWidget* sond_treeviewfm_new(void);

/* Quelle des Index-Kontexts für das Index-Symbol (unten links). Wird bei jedem Zeichnen
 * einer Zeile gefragt, deshalb schnell und ohne Ein-/Ausgabe; NULL liefert kein Symbol.
 * Den Kontext besitzt der Aufrufer. Nach Änderungen sond_treeviewfm_refresh_status(). */
typedef SondIndexCtx* (*SondTreeviewFMIndexCtxFunc)(gpointer user_data);

void sond_treeviewfm_set_index_ctx_func(SondTreeviewFM*, SondTreeviewFMIndexCtxFunc func,
		gpointer user_data);

/* Setzt die Wurzel (Projektverzeichnis) und baut die oberste Ebene auf. NULL
 * leert den Baum. 0 bei Erfolg, -1 bei Fehler. */
gint sond_treeviewfm_set_root(SondTreeviewFM*, const gchar *root, GError **error);

const gchar* sond_treeviewfm_get_root(SondTreeviewFM*);

/* Liegt die Wurzel im SeaDrive-Laufwerk (nur dann sind die Offline-Funktionen
 * wirksam)? */
gboolean sond_treeviewfm_is_seadrive_path(SondTreeviewFM*);

/* Die vollständigen Pfade der markierten Dateisystem-Einträge (g_free(),
 * g_ptr_array_unref()); Einträge in Containern fehlen. */
GPtrArray* sond_treeviewfm_get_selected_paths(SondTreeviewFM*);

/* Pin-Zustand (STVFM_PIN_STATE_*, s. sond_seadrive_core.h) der Markierten setzen;
 * Verzeichnisse rekursiv. Gescheiterte Einträge werden in einem Dialog gemeldet.
 * Ohne SeaDrive ohne Wirkung. */
void sond_treeviewfm_pin_selection(SondTreeviewFM*, guint pin_state);

/* Wie viele Dateien des Projekts gibt es, und wie viele davon sind angeheftet (lokal und
 * gepinnt oder gepinnt und noch im Laden)? FALSE, wenn das nicht bekannt ist (kein
 * SeaDrive-Laufwerk, Scan noch nicht fertig). */
gboolean sond_treeviewfm_get_pin_summary(SondTreeviewFM*, guint *total, guint *pinned);

/* Die Offline-Spalte aller sichtbaren Zeilen neu lesen */
void sond_treeviewfm_refresh_status(SondTreeviewFM*);

G_END_DECLS

#endif // SOND_GTK4_TREEVIEWFM_H_INCLUDED
