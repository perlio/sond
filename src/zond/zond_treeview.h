#ifndef ZOND_TREEVIEW_H_INCLUDED
#define ZOND_TREEVIEW_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include "../sond_treeview.h"

typedef struct _Projekt Projekt;
typedef struct _Anbindung Anbindung;
typedef struct _Pdf_Pos PdfPos;
typedef struct _SondFilePartPDF SondFilePartPDF;
typedef struct _Displayed_Document DisplayedDocument;
typedef struct _ZondPdfDocument ZondPdfDocument;
typedef int gint;

G_BEGIN_DECLS

#define ZOND_TYPE_TREEVIEW zond_treeview_get_type( )
G_DECLARE_DERIVABLE_TYPE(ZondTreeview, zond_treeview, ZOND, TREEVIEW, SondTreeview)

struct _ZondTreeviewClass {
	SondTreeviewClass parent_class;
	GMenu *gmenu_icons; /* Icon-Submenu - wird in init_contextmenu befuellt */
};

void zond_treeview_load_textview(Projekt* zond);

void zond_treeview_cursor_changed(ZondTreeview*, gpointer);

gint zond_treeview_get_anchor(Projekt*, gboolean*, GtkTreeIter*,
		GtkTreeIter*, gint*, gboolean*, GError**);

gint zond_treeview_walk_tree(ZondTreeview*, gboolean, gint, GtkTreeIter*,
		gboolean, GtkTreeIter*, gint, gint*,
		gint (*)(ZondTreeview*, gint, GtkTreeIter*, gboolean, GtkTreeIter*,
				gint, gint*, GError**), GError**);

gint zond_treeview_insert_file_part_in_db(Projekt*, gchar const*, gchar const*,
		gchar const*, gint*, GError**);

gint zond_treeview_copy_node_to_baum_auswertung(ZondTreeview*, gint,
		GtkTreeIter*, gboolean, GtkTreeIter*, gint, gint*, GError**);

gint zond_treeview_jump_to_node_id(Projekt*, gint);

ZondTreeview* zond_treeview_new(Projekt*, gint);

gint zond_treeview_oeffnen_internal_viewer(Projekt*, DisplayedDocument*,
		PdfPos*, GError**);

gint zond_treeview_load_baum(ZondTreeview*, GError**);

GtkTreePath* zond_treeview_get_path(SondTreeview*, gint);

/**
 * zond_treeview_get_selected_fileparts:
 * @ztv:                ZondTreeview (BAUM_INHALT oder BAUM_AUSWERTUNG)
 * @reject_unterseitig: TRUE, wenn eine unterseitige Anbindung in der
 *                       Auswahl (beginnt/endet nicht an einer
 *                       Seitengrenze, oder reiner Punkt - s.
 *                       anbindung_ist_unterseitig() in
 *                       99conv/general.h) die ganze Abfrage mit einem
 *                       GError scheitern lassen soll, statt sie
 *                       stillschweigend wie eine ganzseitige Anbindung
 *                       zu behandeln. Für Index erstellen/löschen
 *                       (Auswahl) TRUE, für alle anderen Aufrufer
 *                       (Indexsuche, SeaDrive-Pinnen) FALSE - dort ist
 *                       eine unterseitige Anbindung unproblematisch.
 * @error:               GError
 */
GHashTable* zond_treeview_get_selected_fileparts(ZondTreeview *ztv,
		gboolean reject_unterseitig, GError **error);

/*
 * Wendet pin_state (STVFM_PIN_STATE_*, s. sond_treeviewfm_seadrive.h) auf
 * alle real referenzierten Dateien der aktuellen Auswahl in ztv an. Vom
 * eigenen Kontextmenü von ztv genutzt UND seit 11.09.2026 vom globalen
 * Hauptmenü ("Projekt > SeaDrive > .../Auswahl", win.sd-*-sel in
 * headerbar.c), wenn dieser Baum gerade der Baum mit einer Selektion ist.
 */
void zond_treeview_seadrive_apply_to_selection(ZondTreeview *ztv,
		guint pin_state);

/*
 * Graut die "Auswahl"-SeaDrive-Menüpunkte im Kontextmenü von ztv ein/aus.
 * Aufgerufen, wenn ein Projekt geöffnet/geschlossen wird (project.c,
 * project_set_widgets_sensitive()) - sensitive sollte dabei bereits
 * "aktives Projekt UND BAUM_FS liegt auf einem SeaDrive-Pfad" sein.
 */
void zond_treeview_seadrive_set_contextmenu_sensitive(ZondTreeview *ztv,
		gboolean sensitive);

G_END_DECLS

#endif // SOND_TREEVIEW_H_INCLUDED
