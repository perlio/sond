#ifndef SOND_TREEVIEWFM_H_INCLUDED
#define SOND_TREEVIEWFM_H_INCLUDED

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "sond_treeview.h"
/* SondIndexCtx, SondIndexStatus - für SondTreeviewFMClass::
 * get_section_index_status(): die vfunc liefert direkt einen SondIndexStatus
 * statt zweier Ints, die die Basisklasse dann selbst als Seitenbereich
 * interpretieren müsste - s. dortigen Doc-Kommentar. Keine zirkuläre
 * Abhängigkeit: sond_index.h inkludiert nur glib/sqlite3/mupdf. */
#include "sond_index.h"
/* SondTVFMItem - MUSS vor der SondTreeviewFMClass-Deklaration stehen,
 * deren vfuncs SondTVFMItem* verwenden. */
#include "sond_tvfm_item.h"

typedef struct _SondProcessFileCtx SondProcessFileCtx;

G_BEGIN_DECLS

#define SOND_TYPE_TREEVIEWFM sond_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondTreeviewFM, sond_treeviewfm, SOND, TREEVIEWFM, SondTreeview)

struct _SondTreeviewFMClass {
	SondTreeviewClass parent_class;

	//Signale
	guint signal_before_move;
	guint signal_before_insert;
	guint signal_before_delete;
	guint signal_after;
	guint signal_after_insert; //nach Kopieren (zu before-insert), Erfolg als gboolean

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

	/* Liefert den Indizierungsstatus für einen LEAF_SECTION-Knoten
	 * (Anbindung), für das Indizierungsstatus-Overlay.
	 *
	 * Die Unterklasse liefert den fertigen SondIndexStatus direkt statt
	 * zweier Ints (von_seite/bis_seite), die die BASISKLASSE dann selbst als
	 * PDF-artigen Seitenbereich an sond_index_ctx_get_file_status()
	 * weiterreichen müsste: "Section = Seitenbereich" ist eine
	 * zond/PDF-spezifische Annahme (bei zond immer zutreffend, weil
	 * path_or_section bei einem LEAF_SECTION-Knoten in BAUM_FS immer eine
	 * Anbindung mit Seitenbereich ist), die die generische Basisklasse
	 * nicht voraussetzen darf - eine andere Unterklasse könnte "Section"
	 * z.B. als Zeitausschnitt (Audio/Video) verstehen, für den ein Aufruf
	 * mit von_seite/bis_seite keinen Sinn ergäbe. Die Unterklasse bekommt
	 * dafür den index_ctx (den die Basisklasse ohnehin schon über
	 * sond_treeviewfm_set_index_ctx_func() hat) übergeben und ist komplett
	 * selbst dafür verantwortlich, wie sie "ihre" Section (und deren
	 * MIME-Typ - s. zond_treeviewfm_get_section_index_status() für die
	 * dortige Leaf/PDF/GMessage-bewusste Behandlung) in einen Status
	 * übersetzt. Die generische Basisklasse kennt die Section-Semantik
	 * nicht (s. has_sections/load_sections) - deshalb optionales Vfunc;
	 * NULL/kein Vfunc = kein Overlay für Sections
	 * (SOND_INDEX_STATUS_NONE). */
	SondIndexStatus (*get_section_index_status)(SondTVFMItem*, SondIndexCtx*);

	/* Signal: SeaDrive-Status geändert (connected, pending_down, pending_up) */
	guint signal_seadrive_status;
};

/* Haengt die Basis-Section von SondTreeview plus alle FM-Sections an gmenu.
 * Wird von abgeleiteten Klassen in deren class_init aufgerufen,
 * nachdem sie ein eigenes GMenu angelegt haben. */
void sond_treeviewfm_add_base_menu(GMenu *gmenu);

gint sond_treeviewfm_file_part_visible(SondTreeviewFM*, GtkTreeIter*,
		gchar const*, gboolean, GtkTreeIter*, GError**);

typedef enum {
	SOND_TVFM_SEARCH_CONTAINS, //Name enthält Text
	SOND_TVFM_SEARCH_WHOLE_NAME, //Name ist gleich Text
	SOND_TVFM_SEARCH_WILDCARD //Text ist Muster mit * und ?
} SondTVFMSearchMode;

typedef struct {
	gchar const *text;
	SondTVFMSearchMode mode;
	gboolean case_sensitive;
	gboolean match_files; //Dateien (auch Container) als Treffer
	gboolean match_dirs; //Verzeichnisse als Treffer
	gboolean in_containers; //ZIP, PDF-Anhänge, E-Mails durchsuchen
} SondTVFMSearchOpts;

/* Sucht im Dateisystembaum nach Namen. Liefert die Fileparts der Treffer
 * (GPtrArray mit g_free) in der Form, die sond_treeviewfm_file_part_visible()
 * erwartet, oder NULL und error. Läuft mit Fortschrittsfenster und
 * Abbrechen; bei Abbruch kommen die bis dahin gefundenen Treffer zurück.
 * selected_only: nur unterhalb der markierten Punkte, sonst ab Wurzel.
 * n_skipped (optional): Zahl nicht lesbarer, übersprungener Verzeichnisse. */
GPtrArray* sond_treeviewfm_search(SondTreeviewFM*, SondTVFMSearchOpts const*,
		gboolean selected_only, guint *n_skipped, GError**);

/* Dateisuche mit Dialog und Ergebnisfenster, wie im Kontextmenü. */
void sond_treeviewfm_search_activate(SondTreeviewFM*);

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

G_END_DECLS

#endif // SOND_TREEVIEWFM_H_INCLUDED
