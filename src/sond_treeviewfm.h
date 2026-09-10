#ifndef SOND_TREEVIEWFM_H_INCLUDED
#define SOND_TREEVIEWFM_H_INCLUDED

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "sond_treeview.h"
#include "sond_icon_util.h" /* SondSeadriveDirStatus */

typedef struct _SondFilePart SondFilePart;
typedef struct _SondProcessFileCtx SondProcessFileCtx;
typedef struct _SondIndexCtx SondIndexCtx;

G_BEGIN_DECLS

typedef enum {
	SOND_TVFM_ITEM_TYPE_DIR, //Verzeichnis
	SOND_TVFM_ITEM_TYPE_LEAF, //SondFilePart
	SOND_TVFM_ITEM_TYPE_LEAF_SECTION //Teil von Datei
} SondTVFMItemType;

//Sond_TVFM_ITEM definieren - lokales GObject-Derivat
#define SOND_TYPE_TVFM_ITEM sond_tvfm_item_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondTVFMItem, sond_tvfm_item, SOND, TVFM_ITEM, GObject)

struct _SondTVFMItemClass {
	GObjectClass parent_class;

	gint (*load_sections)(SondTVFMItem*, GPtrArray**, GError**);
};

#define SOND_TYPE_TREEVIEWFM sond_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondTreeviewFM, sond_treeviewfm, SOND, TREEVIEWFM, SondTreeview)

struct _SondTreeviewFMClass {
	SondTreeviewClass parent_class;

	//Signale
	guint signal_before_move;
	guint signal_before_insert;
	guint signal_before_delete;
	guint signal_after;

	gint (*deter_background)(SondTVFMItem*, GError**);
	gint (*text_from_section)(SondTVFMItem*, gchar**, GError**);
	gint (*text_edited)(SondTreeviewFM*, GtkTreeIter*, SondTVFMItem*, const gchar*,
			GError**);
	void (*results_row_activated)(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer);
	gint (*open_stvfm_item) (GtkTreeIter*, SondTVFMItem*, gboolean, GError**);
	gint (*load_sections) (SondTVFMItem*, GPtrArray**, GError**);
	gboolean (*has_sections) (SondTVFMItem*);
	gint (*delete_section) (SondTVFMItem*, GError**);
	SondProcessFileCtx* (*get_wctx) (SondTreeviewFM*);

	/* Liefert für einen LEAF_SECTION-Knoten (Anbindung) den 1-basierten
	 * Seitenbereich, den diese Section abdeckt - für den
	 * Indizierungsstatus-Overlay. Die generische Basisklasse kennt die
	 * Section-Semantik nicht (s. has_sections/load_sections), deshalb
	 * optionales Vfunc statt fester Logik; NULL/Rückgabe FALSE = Section
	 * wird wie die ganze Datei behandelt. */
	gboolean (*get_section_page_range)(SondTVFMItem*, gint *von_seite,
			gint *bis_seite);

	/* Signal: SeaDrive-Status geändert (connected, pending_down, pending_up) */
	guint signal_seadrive_status;
};

/* Haengt die Basis-Section von SondTreeview plus alle FM-Sections an gmenu.
 * Wird von abgeleiteten Klassen in deren class_init aufgerufen,
 * nachdem sie ein eigenes GMenu angelegt haben. */
void sond_treeviewfm_add_base_menu(GMenu *gmenu);

SondTVFMItemType sond_tvfm_item_get_item_type(SondTVFMItem*);

gchar const* sond_tvfm_item_get_path_or_section(SondTVFMItem *);

gchar const* sond_tvfm_item_get_display_name(SondTVFMItem*);

SondFilePart* sond_tvfm_item_get_sond_file_part(SondTVFMItem*);

SondTreeviewFM* sond_tvfm_item_get_stvfm(SondTVFMItem *);

void sond_tvfm_item_set_icon_name(SondTVFMItem*, gchar const*);

gchar const* sond_tvfm_item_get_icon_name(SondTVFMItem*);

SondTVFMItem* sond_tvfm_item_create(SondTreeviewFM*,
		SondFilePart *, gchar const*);

gint sond_tvfm_item_load_children(SondTVFMItem*, GPtrArray**, GError**);

gint sond_treeviewfm_file_part_visible(SondTreeviewFM*, GtkTreeIter*,
		gchar const*, gboolean, GtkTreeIter*, GError**);

GHashTable* sond_treeviewfm_get_fileparts(SondTreeviewFM *stv, gboolean selected_only,
		GError **error);

gint sond_treeviewfm_set_root(SondTreeviewFM*, const gchar*, GError**);

const gchar* sond_treeviewfm_get_root(SondTreeviewFM*);

gboolean sond_treeviewfm_is_seadrive_path(SondTreeviewFM*);

/* Overlay-Icons (Indizierungsstatus): das generische SondTreeviewFM kennt
 * keinen Projekt-/SondIndexCtx-Bezug direkt (der entsteht/verschwindet mit
 * dem geöffneten Projekt), deshalb Dependency-Injection per Getter-
 * Funktion statt eines fest gespeicherten Zeigers - so wird bei jedem
 * Neuzeichnen der jeweils aktuelle SondIndexCtx erfragt (oder NULL, wenn
 * gerade kein Projekt offen ist - dann werden schlicht keine Overlay-Icons
 * gezeichnet). func/user_data dürfen NULL sein, um die Funktion wieder zu
 * deaktivieren. */
typedef SondIndexCtx* (*SondTreeviewFMIndexCtxFunc)(gpointer user_data);

void sond_treeviewfm_set_index_ctx_func(SondTreeviewFM*,
		SondTreeviewFMIndexCtxFunc func, gpointer user_data);

#ifdef _WIN32
/* Rekursive Ordner-Statistik für den SeaDrive-Coverage-Badge (Ordner-
 * Ebene), jeweils im GANZEN Teilbaum unter dem Ordner (rekursiv), nicht
 * nur direkte Kinder - Rendering bleibt dadurch O(1) pro Zeile. S.
 * sond_treeviewfm_seadrive_get_dir_status(). Bewusst UNABHÄNGIG von
 * seadrive_pending_down_paths/-pending_down (das bleibt die engere Frage
 * "wie viele gepinnte Dateien werden gerade heruntergeladen" für die
 * Projekt-weite Zähleranzeige) - hier geht es um die tatsächliche lokale
 * Verfügbarkeit (Redesign "SeaDrive-Badges Datei+Ordner", 09/2026,
 * nachdem die vorherige, auf "gepinnt+pending" basierende Definition
 * Ordner ohne jedes Pin fälschlich badge-los erscheinen ließ). */
typedef struct {
	guint not_hydrated;    /* Dateien im Teilbaum, die NICHT lokal vorhanden sind (unabhängig vom Pin-Status) */
	guint hydrated_pinned; /* Dateien im Teilbaum, die lokal vorhanden UND gepinnt sind */
	guint total;           /* Dateien insgesamt im Teilbaum */
} SondSeadriveDirCounts;

/* path_pending_down: Pfad, auf den sich delta_down bezieht (NULL, wenn
 * delta_down==0). delta_down>0: Pfad wird ins interne pending_down-Set
 * aufgenommen (Zähler nur erhöht, wenn er noch nicht drin war);
 * delta_down<0: Pfad wird aus dem Set entfernt (Zähler nur verringert,
 * wenn er tatsächlich drin war) - verhindert Drift bei doppelten/
 * verpassten Events (s. Untersuchung SeaDrive-Coverage, 09/2026). Betrifft
 * NUR den Projekt-weiten "wird gerade heruntergeladen"-Zähler
 * (seadrive_pending_down) - für den Ordner-Coverage-Badge s.
 * sond_treeviewfm_seadrive_update_dir_coverage(). */
void     sond_treeviewfm_seadrive_update_status(SondTreeviewFM*,
             const gchar *path_pending_down, gint delta_down,
             const gchar *path_up, gboolean up_pending);
/* Ersetzt das komplette pending_down-Set (Initialscan oder Resync nach
 * Buffer-Overflow) - transfer full, Ownership geht an stvfm über (String-
 * Set, Werte irrelevant). Zähler wird aus g_hash_table_size() abgeleitet. */
void     sond_treeviewfm_seadrive_set_pending_down_paths(SondTreeviewFM*,
             GHashTable *paths);
/* Ersetzt die komplette Ordner-Statistik (Initialscan/Resync) - transfer
 * full (Pfad -> SondSeadriveDirCounts*), Ownership geht an stvfm über. */
void     sond_treeviewfm_seadrive_set_dir_counts(SondTreeviewFM*,
             GHashTable *dir_counts);
/* Ersetzen die Ground-Truth-Sets für die Ordner-Coverage-Zähler
 * (Initialscan/Resync) - transfer full, Ownership geht an stvfm über
 * (String-Sets, Werte irrelevant), analog set_pending_down_paths(). */
void     sond_treeviewfm_seadrive_set_not_hydrated_paths(SondTreeviewFM*,
             GHashTable *paths);
void     sond_treeviewfm_seadrive_set_hydrated_pinned_paths(SondTreeviewFM*,
             GHashTable *paths);
/* Aktualisiert den Ordner-Coverage-Badge für EINE Datei anhand ihres
 * aktuellen Zustands (Ground-Truth-Sets seadrive_not_hydrated_paths/
 * seadrive_hydrated_pinned_paths verhindern Drift bei doppelten/
 * verpassten Events, analog seadrive_pending_down_paths). not_hydrated/
 * hydrated_pinned = FALSE/FALSE für eine gelöschte Datei (existiert nicht
 * mehr, wird also aus beiden Sets entfernt, falls enthalten). delta_total:
 * +1/-1/0 wie bei sond_treeviewfm_seadrive_update_dir_coverage(). Ruft
 * diese intern mit den TATSÄCHLICH angewandten Deltas auf (nur wenn ein
 * Set sich wirklich geändert hat). */
void     sond_treeviewfm_seadrive_update_coverage(SondTreeviewFM*,
             const gchar *file_full_path, gboolean not_hydrated,
             gboolean hydrated_pinned, gint delta_total);
/* Passt die Ordner-Statistik für GENAU dir_path an (kein Ancestor-Walk -
 * das macht sond_treeviewfm_seadrive_update_dir_coverage() für eine Datei
 * automatisch). Legt den Eintrag bei Bedarf an; negative Deltas werden bei
 * 0 gekappt (Schutz gegen Drift durch verpasste/doppelte Events). */
void     sond_treeviewfm_seadrive_dir_delta(SondTreeviewFM*,
             const gchar *dir_path, gint delta_not_hydrated,
             gint delta_hydrated_pinned, gint delta_total);
/* Wendet die Deltas einer einzelnen Datei-Zustandsänderung (Hydrierung/
 * Pin/Neuanlage/Löschung) auf ALLE Vorfahren-Verzeichnisse von
 * file_full_path bis root an (Ordner-Badge bleibt dadurch laufend
 * aktuell, nicht nur nach einem Rescan). Bewusst von
 * sond_treeviewfm_seadrive_update_status() getrennt - andere Fragestellung
 * (s. SondSeadriveDirCounts). */
void     sond_treeviewfm_seadrive_update_dir_coverage(SondTreeviewFM*,
             const gchar *file_full_path, gint delta_not_hydrated,
             gint delta_hydrated_pinned, gint delta_total);
/* Liefert den aggregierten Hydrierungsstatus für dir_path (voller Pfad,
 * wie von watcher_count_pending_down() als Key verwendet), oder NONE,
 * wenn kein Eintrag existiert (z.B. Ordner erst nach dem letzten
 * Rescan angelegt), der Teilbaum leer ist, oder alle Dateien hydriert,
 * aber nicht alle gepinnt sind (dieselbe "kein Icon nötig"-Bedeutung wie
 * beim Datei-Badge, s. SondSeadriveDirStatus). */
SondSeadriveDirStatus sond_treeviewfm_seadrive_get_dir_status(SondTreeviewFM*,
             const gchar *dir_path);
void     sond_treeviewfm_seadrive_item_hydrated(SondTreeviewFM*, const gchar *full_path);
gboolean sond_treeviewfm_seadrive_stop_requested(SondTreeviewFM*);
void     sond_treeviewfm_seadrive_start_watcher(SondTreeviewFM*);
void     sond_treeviewfm_seadrive_stop_watcher(SondTreeviewFM*);
#endif

G_END_DECLS

#endif // SOND_TREEVIEWFM_H_INCLUDED
