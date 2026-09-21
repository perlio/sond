#ifndef SOND_TVFM_ITEM_H_INCLUDED
#define SOND_TVFM_ITEM_H_INCLUDED

/*
 * sond_tvfm_item.h (18.09.2026, Refactoring "wie wäre es, wenn man
 * stvfm_item aus sond_treeviewfm herausnimmt?"): SondTVFMItem - das
 * GObject-Derivat, das EINEN Knoten im SondTreeviewFM-Baum repräsentiert
 * (Datei/Verzeichnis/Section, s. SondTVFMItemType) - war bisher komplett
 * in sond_treeviewfm.h/.c mituntergebracht, obwohl es inhaltlich ein
 * eigenständiges Modell ist (Erzeugen, Kinder laden, Umbenennen/Kopieren/
 * Verschieben/Löschen der zugrundeliegenden Datei/des Verzeichnisses).
 * Reine Verschiebung, keine Verhaltensänderung - die öffentliche API
 * bleibt exakt wie vorher, nur der Ort hat sich geändert. sond_treeviewfm.h
 * inkludiert diesen Header jetzt, damit für alle bisherigen Includer von
 * sond_treeviewfm.h transparent nichts anders aussieht.
 */

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

typedef struct _SondFilePart SondFilePart;
/* Vorwärtsdeklaration - die eigentliche G_DECLARE_DERIVABLE_TYPE()-
 * Deklaration von SondTreeviewFM steht (unverändert) in sond_treeviewfm.h,
 * das diesen Header seinerseits VOR jener Deklaration inkludiert (s.
 * dortigen Kommentar). Identische Vorwärtsdeklaration ist in C zulässig -
 * analog zu SondFilePart oben, das genauso schon immer nur vorwärts-
 * deklariert war (echte Definition in sond_fileparts.h). */
typedef struct _SondTreeviewFM SondTreeviewFM;

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

SondTVFMItemType sond_tvfm_item_get_item_type(SondTVFMItem*);

gchar const* sond_tvfm_item_get_path_or_section(SondTVFMItem *);

gchar const* sond_tvfm_item_get_display_name(SondTVFMItem*);

/* Beschriftung für den Anbinden-Pfad (Marker-Knoten "Pagetree"/"Message"
 * -> echter Dateiname, s. Doc-Kommentar an der Definition, sond_tvfm_item.c).
 * Rückgabe ist immer neu alloziert - Aufrufer muss g_free()en. */
gchar* sond_tvfm_item_get_anbinden_label(SondTVFMItem*);

SondFilePart* sond_tvfm_item_get_sond_file_part(SondTVFMItem*);

SondTreeviewFM* sond_tvfm_item_get_stvfm(SondTVFMItem *);

void sond_tvfm_item_set_icon_name(SondTVFMItem*, gchar const*);

gchar const* sond_tvfm_item_get_icon_name(SondTVFMItem*);

SondTVFMItem* sond_tvfm_item_create(SondTreeviewFM*,
		SondFilePart *, gchar const*);

/* SondTVFMProgress:
 *
 * Optionaler Fortschritts-/Abbruch-Kontext für sond_tvfm_item_load_children()
 * (relevant v.a. für sond_tvfm_item_load_zip_dir() bei großen Archiven, wo
 * das Einlesen/MIME-Sniffen aller Einträge spürbar dauern kann - s.
 * ausführlichen Fund/Entwurf in ToDo.c, 16.09.2026).
 *
 * NULL als Parameter überall = altes Verhalten (kein Pumping, kein Abbruch
 * möglich) - alle bestehenden Aufrufer außer dem Anbinden-Pfad übergeben
 * weiterhin NULL.
 *
 * cancel: Zeiger auf ein außen gehaltenes Flag (z.B. info_window->cancel);
 *   wird periodisch geprüft. Ist *cancel != 0, wird das Laden weiterer
 *   Kinder abgebrochen - bereits geladene Kinder werden unverändert
 *   zurückgegeben (rc bleibt 0, kein Fehler), der Aufrufer bricht die
 *   Rekursion an seiner gewohnten Abbruch-Prüfstelle ab. Das ist hier
 *   unkritisch möglich, weil load_children rein lesend ist (im Unterschied
 *   zu SondProcessFileCtx, das auch schreibt und daher nur an sicheren
 *   Stellen abbrechen darf).
 * progress_func/progress_func_data: wird periodisch (nicht pro Eintrag)
 *   aufgerufen, z.B. um GTK-Events zu pumpen und/oder eine
 *   Fortschrittsanzeige zu aktualisieren. text kann NULL sein (dann nur
 *   pumpen, keine neue Anzeige). */
typedef struct _SondTVFMProgress {
	gint *cancel;
	void (*progress_func)(gpointer progress_func_data, gchar const *text);
	gpointer progress_func_data;
} SondTVFMProgress;

gint sond_tvfm_item_load_children(SondTVFMItem*, GPtrArray**, SondTVFMProgress*, GError**);

G_END_DECLS

#endif // SOND_TVFM_ITEM_H_INCLUDED
