#ifndef SOND_TVFM_ITEM_PRIVATE_H_INCLUDED
#define SOND_TVFM_ITEM_PRIVATE_H_INCLUDED

/*
 * Private Struktur von SondTVFMItem und die modulinterne "Freund"-API des
 * Items. GTK-frei: sond_tvfm_item.c braucht nur diesen Header (und die
 * Eigentümer-Schnittstelle sond_tvfm_owner.h), nicht den Baum. Aus
 * sond_treeviewfm_private.h herausgelöst, das ihn einbindet.
 */

#include <glib.h>
#include <glib-object.h>

#include "sond_tvfm_item.h"
#include "sond_tvfm_owner.h"

G_BEGIN_DECLS

//SOND_TVFM_ITEM
typedef struct {
	/* Eigentümer (Baum/Ansicht), s. sond_tvfm_owner.h. "stvfm" ist der alte Name
	 * desselben Zeigers für den GTK3-Dateibaum SondTreeviewFM. */
	union {
		SondTVFMOwner *owner;
		SondTreeviewFM *stvfm;
	};
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
	/* Änderungszeit, Größe und Verzeichnis-Kennzeichen des Dateisystem-
	 * Eintrags (Spalten "Änderungsdatum"/"Größe", Sortierung). Beim Einlesen
	 * eines Verzeichnisses aus dem dortigen sond_stat() übernommen
	 * (stat_done); für anderswo erzeugte Items (Einfügen) beim ersten Bedarf
	 * nachgeholt, s. item_fs_stat() in sond_treeviewfm.c. Für Einträge in
	 * Containern ohne Bedeutung. */
	gint64 mtime;
	gint64 size;
	gboolean is_dir;
	gboolean stat_done;
	/* Wirksames Eingangsdatum (JJJJMMTT, 0 = keine Angabe) und der daraus
	 * abgeleitete Sortierschlüssel (Sekunden); beim ersten Bedarf über das
	 * Vfunc get_eingang der Unterklasse geholt (eingang_done), s.
	 * item_eingang() in sond_treeviewfm.c. */
	gint eingang;
	gint64 eingang_key;
	gboolean eingang_done;
} SondTVFMItemPrivate;

/* Freund-Accessor für sond_seadrive.c, analog sond_treeviewfm_get_priv().
 * Definiert in sond_tvfm_item.c. */
SondTVFMItemPrivate *sond_tvfm_item_get_priv(SondTVFMItem *item);

/* Modul-interne "Freund"-API zwischen sond_treeviewfm.c und
 * sond_tvfm_item.c: diese fünf Funktionen werden sowohl von der Item-Logik
 * (sond_tvfm_item.c) selbst als auch vom Baum-Code in sond_treeviewfm.c
 * (Rename/Kontextmenü-Löschen/Umbenennen-Handler) aufgerufen. Bewusst NICHT in sond_tvfm_item.h (also nicht Teil der
 * öffentlichen SondTVFMItem-API für den Rest der Anwendung). Implementiert
 * in sond_tvfm_item.c. */
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

G_END_DECLS

#endif // SOND_TVFM_ITEM_PRIVATE_H_INCLUDED
