#ifndef SOND_TREEVIEWFM_H_INCLUDED
#define SOND_TREEVIEWFM_H_INCLUDED

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "sond_treeview.h"

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
void     sond_treeviewfm_seadrive_update_status(SondTreeviewFM*,
             gint delta_down, const gchar *path_up, gboolean up_pending);
void     sond_treeviewfm_seadrive_set_pending_down(SondTreeviewFM*, guint);
void     sond_treeviewfm_seadrive_item_hydrated(SondTreeviewFM*, const gchar *full_path);
gboolean sond_treeviewfm_seadrive_stop_requested(SondTreeviewFM*);
void     sond_treeviewfm_seadrive_start_watcher(SondTreeviewFM*);
void     sond_treeviewfm_seadrive_stop_watcher(SondTreeviewFM*);
#endif

G_END_DECLS

#endif // SOND_TREEVIEWFM_H_INCLUDED
