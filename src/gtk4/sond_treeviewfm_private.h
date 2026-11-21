/*
 sond (gtk4/sond_treeviewfm_private.h) - Akten, Beweisstücke, Unterlagen
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

/* Gemeinsamer, nur innerhalb von gtk4/ benutzter Teil der Dateien sond_treeviewfm*.c
 * (SondTreeviewFM ist auf mehrere Dateien verteilt):
 *   sond_treeviewfm.c          Klasse, Eigentümer-Schnittstelle, Wurzel, Auswahl, Menü
 *   sond_treeviewfm_lazy.c     träges Kindermodell
 *   sond_treeviewfm_cells.c    Sortierung, Symbole, Zellen, Spalten
 *   sond_treeviewfm_watch.c    Root-Monitor und SeaDrive-Watcher
 *   sond_treeviewfm_edit.c     Neues Verzeichnis, Umbenennen, Löschen, Öffnen
 *   sond_treeviewfm_transfer.c Kopieren, Ausschneiden, Einfügen, Ziehen und Ablegen
 *   sond_treeviewfm_search.c   Dateisuche (Dialog, Fortschritt, Ergebnisse)
 *   sond_treeviewfm_unlock.c   Dauerhaft entsperren
 */

#ifndef SOND_GTK4_TREEVIEWFM_PRIVATE_H_INCLUDED
#define SOND_GTK4_TREEVIEWFM_PRIVATE_H_INCLUDED

#include "sond_treeviewfm.h"
#ifdef G_OS_WIN32
#include "../seadrive/sond_seadrive_watcher.h"
#endif

G_BEGIN_DECLS

typedef struct {
	gchar *root;
	gboolean is_seadrive;
	SondTreeviewFMIndexCtxFunc index_ctx_func;
	gpointer index_ctx_func_data;
	GFileMonitor *root_monitor; //Zugänge von außen auf Wurzel-Ebene
	guint root_monitor_timer;   //Entprellung
	gchar *search_text;         //Einstellungen der letzten Dateisuche
	SondTVFMSearchOpts search_opts;
	gboolean search_settings_valid;
	GtkColumnViewColumn *column_eingang; //nur mit Vfunc get_eingang
	SondTreeviewFMWriteCheckFunc write_check;
	gpointer write_check_data;
	gchar *read_only;           //Grund, solange der Baum nichts ändern darf; sonst NULL
#ifdef G_OS_WIN32
	SondSeadriveWatcher *watcher; //nur bei einer Wurzel im SeaDrive-Laufwerk
	guint refresh_idle;           //gebündelte Auffrischung der Symbole
#endif
} SondTreeviewFMPrivate;

enum {
	SIGNAL_PINS_CHANGED, SIGNAL_SEADRIVE_STATUS, SIGNAL_EINGANG_CHANGED, N_SIGNALS
};

extern guint fm_signals[N_SIGNALS];

/* Träges Kindermodell: lädt die Kinder eines Verzeichnisses beim ersten Zugriff */
#define SOND_TYPE_LAZY_CHILDREN sond_lazy_children_get_type( )
G_DECLARE_FINAL_TYPE(SondLazyChildren, sond_lazy_children, SOND, LAZY_CHILDREN, GObject)

struct _SondLazyChildren {
	GObject parent_instance;
	SondTVFMItem *item;
	GPtrArray *children;
	gboolean loaded;
};

SondLazyChildren* fm_lazy_from_model(GListModel *model);
SondLazyChildren* fm_lazy_new_loaded(GPtrArray *items);
void fm_lazy_insert(SondLazyChildren *self, SondTVFMItem *item);
void fm_lazy_remove(SondLazyChildren *self, SondTVFMItem *item);
gint fm_lazy_index_of(SondLazyChildren *self, SondTVFMItem *item);
void fm_lazy_insert_at(SondLazyChildren *self, gint index, SondTVFMItem *item);
void fm_lazy_replace(SondLazyChildren *self, SondTVFMItem *old_item,
		SondTVFMItem *new_item);
void fm_lazy_touch(SondLazyChildren *self, SondTVFMItem *item);
gchar* fm_item_full_path(SondTreeviewFM *self, SondTVFMItem *item,
		gboolean *is_dir);
gboolean fm_item_is_dir(SondTVFMItem *item);
GtkWindow* fm_parent_window(SondTreeviewFM *self);
void fm_show_error(SondTreeviewFM *self, gchar const *title, gchar const *message);
SondTVFMItem* fm_parent_item_of_row(SondTreeviewFM *self, GtkTreeListRow *row);
gboolean fm_match_same(gpointer item, gpointer user_data);
gboolean fm_match_path(gpointer item, gpointer user_data);
void fm_dnd_attach(GtkWidget *cell, GtkListItem *listitem);
SondSeadriveBadge fm_default_file_badge(SondTreeviewFM *self, gchar const *full_path);
SondSeadriveDirStatus fm_default_dir_status(SondTreeviewFM *self,
		gchar const *full_path);
SondIndexStatus fm_default_index_status(SondTreeviewFM *self, SondTVFMItem *item);
void fm_default_setup_columns(SondTreeviewFM *self);
void fm_register_icons(void);
void fm_action_mkdir_same(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_action_mkdir_child(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_action_rename(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_action_delete(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_action_open(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_action_open_with(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_action_copy(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_action_cut(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_action_paste(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_action_paste_same(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_action_paste_child(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_on_activate(GtkColumnView *column_view, guint position, gpointer data);
gint fm_default_text_edited(SondTreeviewFM *self, SondTVFMItem *item, SondTVFMItem *parent,
		gchar const *text_new, GError **error);
gint fm_default_open_item(SondTreeviewFM *self, SondTVFMItem *item,
		gboolean open_with, GError **error);
void fm_root_monitor_start(SondTreeviewFM *self);
void fm_root_monitor_stop(SondTreeviewFMPrivate *priv);
void fm_watcher_start(SondTreeviewFM *self);
void fm_watcher_stop(SondTreeviewFM *self);
GListModel* fm_create_children(SondTreeview *base, gpointer object);
SondTreeviewFMPrivate* fm_get_priv(SondTreeviewFM *self);
void fm_replace_item(SondTreeviewFM *self, SondTVFMItem *old_item, SondTVFMItem *new_item);
gboolean fm_reveal_filepart(SondTreeviewFM *self, gchar const *filepart);
void fm_action_search(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_action_unlock(GSimpleAction *action, GVariant *parameter, gpointer data);
void fm_search_register_signals(GObjectClass *klass);

G_END_DECLS

#endif /* SOND_GTK4_TREEVIEWFM_PRIVATE_H_INCLUDED */
