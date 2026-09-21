#ifndef SOND_TREEVIEWFM_PRIVATE_H_INCLUDED
#define SOND_TREEVIEWFM_PRIVATE_H_INCLUDED

/*
 * "Freund"-Header: G_DEFINE_TYPE_WITH_PRIVATE() erzeugt einen STATISCHEN
 * inline-Accessor, der nur innerhalb derselben Übersetzungseinheit
 * sichtbar ist. Dieser Header macht die beiden privaten Structs UND je
 * einen normalen (nicht-statischen) "Freund"-Accessor bekannt, damit
 * sond_seadrive.c direkt auf SondTreeviewFMPrivate/SondTVFMItemPrivate
 * zugreifen kann (Ground-Truth-Hashtables für Badges/Coverage, Watcher),
 * ohne die öffentliche API von sond_treeviewfm.h zu vergrößern - nicht
 * für Aufrufer außerhalb von sond_treeviewfm.c/sond_seadrive.c gedacht
 * (deshalb kein Include in sond_treeviewfm.h). Die
 * G_DEFINE_TYPE_WITH_PRIVATE()-Makroaufrufe und die GObject-
 * Typregistrierung bleiben in sond_treeviewfm.c - hier nur die Struct-
 * Definitionen und die Freund-Funktionsdeklarationen, deren
 * Implementierung den jeweiligen Makro-Accessor 1:1 durchreicht.
 */

#include "sond_treeviewfm.h"

G_BEGIN_DECLS

//SOND_TREEVIEWFM
typedef struct {
	gchar *root;
	GtkTreeViewColumn *column_eingang;
	gboolean is_seadrive_path;
	SondTreeviewFMIndexCtxFunc index_ctx_func;
	gpointer index_ctx_func_data;
#ifdef _WIN32
	GThread *seadrive_watcher_thread;
	gint     seadrive_watcher_stop;   /* atomares Flag: 0=laufen, 1=stoppen */
	guint    seadrive_pending_down;   /* PINNED + RECALL_ON_DATA_ACCESS */
	guint    seadrive_pending_up;     /* NOT_IN_SYNC */
	/* Pfade, die aktuell als "nicht synchronisiert" gezählt sind (dedupliziert
	 * seadrive_pending_up-Erhöhungen bei mehrfachen LAST_WRITE-Events für
	 * denselben Pfad). Pro Instanz statt static/global, und beim Root-Wechsel
	 * geleert - sonst bleiben Einträge einer vorigen Projekt-Session stehen
	 * und der Zähler zählt beim nächsten Öffnen falsch (bleibt zu niedrig). */
	GHashTable *seadrive_not_in_sync;
	/* Pfade, die aktuell als "pending_down" (PINNED+offline) gezählt sind -
	 * Ground Truth für seadrive_pending_down, analog seadrive_not_in_sync.
	 * Ohne dieses Set wäre bei einem REMOVED-Event (Datei gelöscht, während
	 * sie noch heruntergeladen wurde) nicht feststellbar, ob sie gerade
	 * mitgezählt wurde - der Zähler würde langfristig auseinanderlaufen
	 * (Untersuchung SeaDrive-Coverage, 09/2026). Wird beim Initialscan und
	 * bei einem Resync (Buffer-Overflow von ReadDirectoryChangesW) komplett
	 * neu aufgebaut/ersetzt. */
	GHashTable *seadrive_pending_down_paths;
	/* Rekursive Ordner-Statistik für den Ordner-Coverage-Badge: Pfad (voller
	 * Pfad wie bei seadrive_pending_down_paths) -> SondSeadriveDirCounts*.
	 * Komplett neu aufgebaut bei Initialscan/Resync (s.
	 * sond_treeviewfm_seadrive_set_dir_counts()), inkrementell nachgezogen
	 * bei jedem Einzel-Event (s. sond_treeviewfm_seadrive_dir_delta(),
	 * Untersuchung SeaDrive-Coverage, 09/2026). */
	GHashTable *seadrive_dir_counts;
	/* Ground-Truth-Map für den Datei-eigenen SeaDrive-Badge: voller Pfad ->
	 * GINT_TO_POINTER(SondSeadriveBadge), Einträge mit Wert NONE werden
	 * nicht gespeichert. ERSETZT ab 09/2026 den früheren LIVEN
	 * GetFileAttributesW-Aufruf pro Renderzeile in
	 * sond_treeviewfm_render_file_icon() (Konsistenz mit seadrive_dir_
	 * counts, das schon vorher aus der Hashtable statt live gelesen wurde -
	 * Untersuchung "Ordner-Badges", 09/2026) UND liefert gleichzeitig die
	 * Grundlage für die Ordner-Coverage-Zähler (not_hydrated/
	 * hydrated_pinned in seadrive_dir_counts werden aus Änderungen dieser
	 * Map abgeleitet, s. sond_treeviewfm_seadrive_update_file_badge()) -
	 * ANDERE, weitere Fragestellung als seadrive_pending_down_paths (das
	 * bleibt die engere PINNED+offline-Definition für den Projekt-weiten
	 * Zähler). Komplett neu aufgebaut bei Initialscan/Resync, inkrementell
	 * gepflegt bei jedem Einzel-Event. */
	GHashTable *seadrive_file_badges;
#endif
} SondTreeviewFMPrivate;

/* Freund-Accessor für sond_seadrive.c - reicht 1:1 an den von
 * G_DEFINE_TYPE_WITH_PRIVATE() erzeugten (statischen, nur in
 * sond_treeviewfm.c sichtbaren) sond_treeviewfm_get_instance_private()
 * durch. Definiert in sond_treeviewfm.c. */
SondTreeviewFMPrivate *sond_treeviewfm_get_priv(SondTreeviewFM *stvfm);

//SOND_TVFM_ITEM
typedef struct {
	SondTreeviewFM* stvfm;
	gchar*display_name;
	gchar const* icon_name;
	gboolean has_children;
	SondTVFMItemType type;
	SondFilePart* sond_file_part;
	gchar* path_or_section;
	/* Markiert die synthetischen Marker-Kindknoten (PDF-PageTree bzw.
	 * GMESSAGE-Message), deren display_name bewusst NICHT der echte
	 * Dateiname ist - s. sond_tvfm_item_get_anbinden_label() (sond_tvfm_item.c). */
	gboolean is_content_root_marker;
} SondTVFMItemPrivate;

/* Freund-Accessor für sond_seadrive.c, analog sond_treeviewfm_get_priv().
 * Definiert (18.09.2026: jetzt in sond_tvfm_item.c, s. dortigen Kommentar
 * zum Refactoring "stvfm_item aus sond_treeviewfm herausnehmen"). */
SondTVFMItemPrivate *sond_tvfm_item_get_priv(SondTVFMItem *item);

/* Modul-interne "Freund"-API zwischen sond_treeviewfm.c und
 * sond_tvfm_item.c (18.09.2026, Refactoring "stvfm_item aus
 * sond_treeviewfm herausnehmen"): diese sechs Funktionen waren vor dem
 * Refactoring STATISCH innerhalb von sond_treeviewfm.c (bzw. hießen
 * "delete_item") und wurden sowohl von der (jetzt in sond_tvfm_item.c
 * lebenden) Item-Logik selbst als auch von im sond_treeviewfm.c
 * verbliebenem Baum-Code (Rename/Kontextmenü-Löschen/Umbenennen-Handler/
 * Fileparts-Sammlung) aufgerufen. Bewusst NICHT in sond_tvfm_item.h (also
 * nicht Teil der öffentlichen SondTVFMItem-API für den Rest der
 * Anwendung) - das entspricht genau der vorherigen Sichtbarkeit
 * (file-static), nur jetzt auf zwei Übersetzungseinheiten verteilt statt
 * einer. Implementiert in sond_tvfm_item.c. */
gchar const *sond_tvfm_item_get_basename(SondTVFMItem *stvfm_item);

gint sond_tvfm_item_rename(SondTVFMItem *stvfm_item,
		SondTVFMItem *stvfm_item_parent, gchar const *base_new,
		GError **error);

gint sond_tvfm_item_copy(SondTVFMItem *stvfm_item,
		SondTVFMItem *stvfm_item_parent, gchar const *base,
		gint index_to, GError **error);

gint sond_tvfm_item_move(SondTVFMItem *stvfm_item,
		SondTVFMItem *stvfm_item_parent, gchar const *base,
		gint index_to, GError **error);

/* Vormals "delete_item()" (umbenannt, da jetzt nicht mehr file-static -
 * ein unpräfigierter Name wäre in einem gemeinsam genutzten Header
 * unpassend). */
gint sond_tvfm_item_delete(SondTVFMItem *stvfm_item, GError **error);

gint sond_tvfm_item_get_fileparts(SondTVFMItem *stvfm_item,
		GHashTable *ht, GError **error);

G_END_DECLS

#endif // SOND_TREEVIEWFM_PRIVATE_H_INCLUDED
