/*
 sond (gtk4/sond_treeviewfm_cells.c) - Akten, Beweisstücke, Unterlagen
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

/* Sortierung, Symbole, Zellen und Spalten (Teil von SondTreeviewFM, s. sond_treeviewfm_private.h) */

#include "sond_treeviewfm_private.h"

#include <string.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include "../sond_log_and_error.h"
#include "../sond_fileparts.h"
#include "../sond_file_helper.h"
#include "../tvfm/sond_tvfm_item.h"
#include "../tvfm/sond_tvfm_item_private.h"
#include "../tvfm/sond_tvfm_owner.h"
#include "../seadrive/sond_seadrive_core.h"
#include "../sond_icon_util.h"
#include "../tvfm/sond_tvfm_status.h"
#include "../sond_mime.h"
#include "../sond_eingang_zettel.h"
#ifdef G_OS_WIN32
#include "../seadrive/sond_seadrive_watcher.h"
#endif
#include "../icons.h"

/* ------------------------------------------------------------------------
 * Sortierung: Verzeichnisse zuerst, dann nach der Spalte
 * --------------------------------------------------------------------- */

static gint compare_dirs_first(SondTVFMItem *a, SondTVFMItem *b, gboolean *decided) {
	gboolean dir_a = fm_item_is_dir(a);
	gboolean dir_b = fm_item_is_dir(b);

	*decided = (dir_a != dir_b);

	return dir_a ? -1 : 1;
}

static gint compare_name(gconstpointer a, gconstpointer b, gpointer data) {
	gboolean decided = FALSE;
	gint result = compare_dirs_first((SondTVFMItem*) a, (SondTVFMItem*) b, &decided);
	gchar const *name_a = NULL;
	gchar const *name_b = NULL;

	(void) data;

	if (decided)
		return result;

	name_a = sond_tvfm_item_get_display_name((SondTVFMItem*) a);
	name_b = sond_tvfm_item_get_display_name((SondTVFMItem*) b);

	return g_utf8_collate(name_a ? name_a : "", name_b ? name_b : "");
}

static gint compare_date(gconstpointer a, gconstpointer b, gpointer data) {
	gboolean decided = FALSE;
	gint result = compare_dirs_first((SondTVFMItem*) a, (SondTVFMItem*) b, &decided);
	gint64 mtime_a = 0;
	gint64 mtime_b = 0;

	(void) data;

	if (decided)
		return result;

	sond_tvfm_item_fs_stat((SondTVFMItem*) a, &mtime_a, NULL, NULL);
	sond_tvfm_item_fs_stat((SondTVFMItem*) b, &mtime_b, NULL, NULL);

	return (mtime_a > mtime_b) - (mtime_a < mtime_b);
}

static gint compare_size(gconstpointer a, gconstpointer b, gpointer data) {
	gboolean decided = FALSE;
	gint result = compare_dirs_first((SondTVFMItem*) a, (SondTVFMItem*) b, &decided);
	gint64 size_a = 0;
	gint64 size_b = 0;

	(void) data;

	if (decided)
		return result;

	sond_tvfm_item_fs_stat((SondTVFMItem*) a, NULL, &size_a, NULL);
	sond_tvfm_item_fs_stat((SondTVFMItem*) b, NULL, &size_b, NULL);

	return (size_a > size_b) - (size_a < size_b);
}

/* Wirksames Eingangsdatum (JJJJMMTT, 0 = keine Angabe) des Eintrags. Beim ersten Bedarf über das
 * Vfunc get_eingang der Unterklasse geholt und im Item gemerkt. Ist die Auskunft (noch) nicht
 * verfügbar, wird später erneut gefragt. */
static gint item_eingang(SondTreeviewFM *view, SondTVFMItem *item) {
	SondTVFMItemPrivate *priv = sond_tvfm_item_get_priv(item);
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(view);
	gchar *rel = NULL;
	gboolean is_dir = FALSE;
	gint result = 0;

	if (priv->eingang_done)
		return priv->eingang;

	if (!klass->get_eingang || !sond_tvfm_item_fs_path(item, &rel, &is_dir))
		return 0;

	result = klass->get_eingang(view, rel, is_dir);
	g_free(rel);
	if (result < 0) //noch nicht verfügbar
		return 0;

	priv->eingang = result;
	priv->eingang_key = (result > 0) ? sond_eingang_int_zu_unix(result) : 0;
	priv->eingang_done = TRUE;

	return result;
}

//Sortierschlüssel: Eingangsdatum, ohne Angabe die Änderungszeit
static gint64 eingang_sort_key(SondTreeviewFM *view, SondTVFMItem *item) {
	gint64 mtime = 0;

	item_eingang(view, item);
	sond_tvfm_item_fs_stat(item, &mtime, NULL, NULL);

	return sond_tvfm_item_get_priv(item)->eingang_key > 0 ?
			sond_tvfm_item_get_priv(item)->eingang_key : mtime;
}

//Nur Einträge des Dateisystems werden verglichen; Inhalte von Containern behalten ihre Reihenfolge
static gint compare_eingang(gconstpointer a, gconstpointer b, gpointer data) {
	SondTreeviewFM *view = SOND_TREEVIEWFM(data);
	gint64 key_a = 0;
	gint64 key_b = 0;

	if (!sond_tvfm_item_fs_path((SondTVFMItem*) a, NULL, NULL) ||
			!sond_tvfm_item_fs_path((SondTVFMItem*) b, NULL, NULL))
		return 0;

	key_a = eingang_sort_key(view, (SondTVFMItem*) a);
	key_b = eingang_sort_key(view, (SondTVFMItem*) b);

	return (key_a > key_b) - (key_a < key_b);
}

/* ------------------------------------------------------------------------
 * Symbole: Basis-Icon (Name aus dem Item, wie in der GTK3-Fassung) mit bis zu
 * vier Overlays; welche, entscheidet sond_tvfm_item_get_badges()
 * --------------------------------------------------------------------- */

#define ICON_PX 16
#define OVERLAY_PX MAX(ICON_PX / 2, 8)
#define ICON_KEY "sond-icon-widgets"
#define ITEM_KEY "sond-item"

typedef struct {
	GtkWidget *base;
	GtkWidget *corner[4]; //Index = SondIconCorner
} IconWidgets;

//Pixbuf (nicht vormultipliziert, RGBA) in eine Textur umsetzen; Eigentum des Pixbufs
//geht an die Funktion
static GdkTexture* texture_from_pixbuf(GdkPixbuf *pixbuf) {
	GdkTexture *texture = NULL;
	GBytes *bytes = NULL;
	gint width = 0;
	gint height = 0;
	gint stride = 0;

	if (!pixbuf)
		return NULL;

	width = gdk_pixbuf_get_width(pixbuf);
	height = gdk_pixbuf_get_height(pixbuf);
	stride = gdk_pixbuf_get_rowstride(pixbuf);
	bytes = g_bytes_new(gdk_pixbuf_read_pixels(pixbuf), (gsize) stride * height);
	texture = gdk_memory_texture_new(width, height,
			gdk_pixbuf_get_has_alpha(pixbuf) ? GDK_MEMORY_R8G8B8A8 : GDK_MEMORY_R8G8B8,
			bytes, (gsize) stride);
	g_bytes_unref(bytes);
	g_object_unref(pixbuf);

	return texture;
}

/* Notsymbole, falls das Icon-Theme weder den Namen des Items noch ein allgemeines
 * Symbol kennt (auf einem schlanken Windows-Setup ohne vollständiges Theme) */
static GdkTexture* fallback_texture(gboolean dir) {
	static GdkTexture *textures[2] = { NULL, NULL };
	cairo_surface_t *surface = NULL;
	cairo_t *cr = NULL;
	GBytes *bytes = NULL;
	const guchar *data = NULL;
	gint stride = 0;

	if (textures[dir ? 1 : 0])
		return textures[dir ? 1 : 0];

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ICON_PX, ICON_PX);
	cr = cairo_create(surface);
	cairo_scale(cr, ICON_PX / 20.0, ICON_PX / 20.0);

	if (dir) {
		cairo_set_source_rgb(cr, 0.85, 0.62, 0.15);
		cairo_rectangle(cr, 1.5, 3.5, 7, 3);
		cairo_fill(cr);
		cairo_set_source_rgb(cr, 0.96, 0.78, 0.30);
		cairo_rectangle(cr, 1.5, 5.5, 17, 11);
		cairo_fill(cr);
	} else {
		cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
		cairo_rectangle(cr, 4, 1, 12, 18);
		cairo_fill_preserve(cr);
		cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
	}
	cairo_destroy(cr);
	cairo_surface_flush(surface);

	data = cairo_image_surface_get_data(surface);
	stride = cairo_image_surface_get_stride(surface);
	bytes = g_bytes_new(data, (gsize) stride * ICON_PX);
	textures[dir ? 1 : 0] = gdk_memory_texture_new(ICON_PX, ICON_PX,
			GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, (gsize) stride);
	g_bytes_unref(bytes);
	cairo_surface_destroy(surface);

	return textures[dir ? 1 : 0];
}

static void set_base_icon(GtkImage *image, SondTVFMItem *item) {
	GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
	gboolean dir = sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_DIR;
	gchar const *name = sond_icon_util_normalize_name(sond_tvfm_item_get_icon_name(item));
	gchar *symbolic = name ? g_strconcat(name, "-symbolic", NULL) : NULL;
	gchar const *candidates[] = { name, symbolic,
			dir ? "folder" : "text-x-generic",
			dir ? "folder-symbolic" : "text-x-generic-symbolic",
			dir ? "inode-directory-symbolic" : "document-symbolic", NULL };

	for (guint i = 0; candidates[i]; i++) {
		if (gtk_icon_theme_has_icon(theme, candidates[i])) {
			gtk_image_set_from_icon_name(image, candidates[i]);
			g_free(symbolic);

			return;
		}
	}
	g_free(symbolic);

	gtk_image_set_from_paintable(image, GDK_PAINTABLE(fallback_texture(dir)));
}

/* Badge-Texturen: einmal je Ausprägung gezeichnet */
static GdkTexture* seadrive_texture(SondSeadriveBadge badge) {
	static GdkTexture *cache[4] = { NULL };

	if ((guint) badge >= G_N_ELEMENTS(cache) || badge == SOND_SEADRIVE_BADGE_NONE)
		return NULL;
	if (!cache[badge])
		cache[badge] = texture_from_pixbuf(
				sond_icon_util_seadrive_badge_pixbuf(badge, OVERLAY_PX));

	return cache[badge];
}

static GdkTexture* dir_status_texture(SondSeadriveDirStatus status) {
	static GdkTexture *cache[4] = { NULL };

	if ((guint) status >= G_N_ELEMENTS(cache) || status == SOND_SEADRIVE_DIR_STATUS_NONE)
		return NULL;
	if (!cache[status])
		cache[status] = texture_from_pixbuf(
				sond_icon_util_seadrive_dir_badge_pixbuf(status, OVERLAY_PX));

	return cache[status];
}

static GdkTexture* index_texture(SondIndexStatus status) {
	static GdkTexture *cache[4] = { NULL };

	if ((guint) status >= G_N_ELEMENTS(cache) || status == SOND_INDEX_STATUS_NONE)
		return NULL;
	if (!cache[status])
		cache[status] = texture_from_pixbuf(
				sond_icon_util_status_badge_pixbuf(status, OVERLAY_PX));

	return cache[status];
}

static GdkTexture* attachment_texture(GtkWidget *widget) {
	static GdkTexture *cache = NULL;

	if (!cache)
		cache = texture_from_pixbuf(
				sond_icon_util_attachment_badge_pixbuf(widget, OVERLAY_PX));

	return cache;
}

static GdkTexture* lock_texture(GtkWidget *widget, gboolean unlocked) {
	static GdkTexture *cache[2] = { NULL, NULL };

	if (!cache[unlocked ? 1 : 0])
		cache[unlocked ? 1 : 0] = texture_from_pixbuf(
				sond_icon_util_locked_badge_pixbuf(widget, OVERLAY_PX, unlocked));

	return cache[unlocked ? 1 : 0];
}

static void corner_set(IconWidgets *w, SondIconCorner corner, GdkTexture *texture) {
	GtkWidget *image = w->corner[corner];

	if (texture)
		gtk_image_set_from_paintable(GTK_IMAGE(image), GDK_PAINTABLE(texture));
	gtk_widget_set_visible(image, texture != NULL);
}

/* SeaDrive-Zustand, Index, Anhang und Schloss, die der Baum nicht selbst hält */
/* Standard der Klasse: Zustand aus den Dateiattributen (kein Watcher) */
SondSeadriveBadge fm_default_file_badge(SondTreeviewFM *self, gchar const *full_path) {
	(void) self;

	return sond_tvfm_badge_seadrive_live(full_path);
}

/* Standard der Klasse: Ordner-Status aus dem Watcher (nur im SeaDrive-Laufwerk) */
SondSeadriveDirStatus fm_default_dir_status(SondTreeviewFM *self,
		gchar const *full_path) {
#ifdef G_OS_WIN32
	SondTreeviewFMPrivate *priv = fm_get_priv(self);

	return sond_seadrive_watcher_get_dir_status(priv->watcher, full_path);
#else
	(void) self;
	(void) full_path;

	return SOND_SEADRIVE_DIR_STATUS_NONE;
#endif
}

/* Standard der Klasse: Status aus der Index-DB über den gesetzten Kontext */
SondIndexStatus fm_default_index_status(SondTreeviewFM *self, SondTVFMItem *item) {
	SondTreeviewFMPrivate *priv = fm_get_priv(self);
	SondIndexCtx *index_ctx = NULL;

	if (!priv->index_ctx_func)
		return SOND_INDEX_STATUS_NONE;

	index_ctx = priv->index_ctx_func(priv->index_ctx_func_data);
	if (!index_ctx)
		return SOND_INDEX_STATUS_NONE;

	if (sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_LEAF_SECTION)
		return SOND_TREEVIEWFM_GET_CLASS(self)->get_section_index_status ?
				SOND_TREEVIEWFM_GET_CLASS(self)->get_section_index_status(item, index_ctx) :
				SOND_INDEX_STATUS_NONE;

	return sond_tvfm_item_index_status(item, index_ctx);
}

void sond_treeviewfm_set_index_ctx_func(SondTreeviewFM *self,
		SondTreeviewFMIndexCtxFunc func, gpointer user_data) {
	SondTreeviewFMPrivate *priv = NULL;

	g_return_if_fail(SOND_IS_TREEVIEWFM(self));
	priv = fm_get_priv(self);

	priv->index_ctx_func = func;
	priv->index_ctx_func_data = user_data;
}

/* Weiterleitung an die Vfuncs der Klasse (Unterklassen überschreiben sie) */
static SondSeadriveBadge fm_source_file_badge(gpointer data, gchar const *full_path) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);

	return SOND_TREEVIEWFM_GET_CLASS(self)->file_badge ?
			SOND_TREEVIEWFM_GET_CLASS(self)->file_badge(self, full_path) :
			SOND_SEADRIVE_BADGE_NONE;
}

static SondSeadriveDirStatus fm_source_dir_status(gpointer data, gchar const *full_path) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);

	return SOND_TREEVIEWFM_GET_CLASS(self)->dir_status ?
			SOND_TREEVIEWFM_GET_CLASS(self)->dir_status(self, full_path) :
			SOND_SEADRIVE_DIR_STATUS_NONE;
}

static SondIndexStatus fm_source_index_status(gpointer data, SondTVFMItem *item) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);

	return SOND_TREEVIEWFM_GET_CLASS(self)->index_status ?
			SOND_TREEVIEWFM_GET_CLASS(self)->index_status(self, item) :
			SOND_INDEX_STATUS_NONE;
}

static GtkWidget* icon_new(void) {
	GtkWidget *overlay = gtk_overlay_new();
	IconWidgets *w = g_new0(IconWidgets, 1);
	static const GtkAlign halign[4] = { GTK_ALIGN_START, GTK_ALIGN_END,
			GTK_ALIGN_START, GTK_ALIGN_END };
	static const GtkAlign valign[4] = { GTK_ALIGN_END, GTK_ALIGN_END,
			GTK_ALIGN_START, GTK_ALIGN_START };

	w->base = gtk_image_new();
	gtk_image_set_pixel_size(GTK_IMAGE(w->base), ICON_PX);
	gtk_overlay_set_child(GTK_OVERLAY(overlay), w->base);

	//Ecken: SondIconCorner (unten links, unten rechts, oben links, oben rechts)
	for (guint i = 0; i < 4; i++) {
		GtkWidget *image = gtk_image_new();

		gtk_image_set_pixel_size(GTK_IMAGE(image), OVERLAY_PX);
		gtk_widget_set_halign(image, halign[i]);
		gtk_widget_set_valign(image, valign[i]);
		gtk_widget_set_visible(image, FALSE);
		gtk_overlay_add_overlay(GTK_OVERLAY(overlay), image);
		w->corner[i] = image;
	}

	g_object_set_data_full(G_OBJECT(overlay), ICON_KEY, w, g_free);

	return overlay;
}

/* Hinweistext des Symbols aus den Badges */
static gchar* badges_tooltip(SondTVFMBadges const *badges) {
	GString *text = g_string_new(NULL);
	gchar const *sea = sond_tvfm_badge_seadrive_text(badges->seadrive);
	gchar const *dir = sond_tvfm_badge_dir_status_text(badges->dir_status);

	if (sea)
		g_string_append(text, sea);
	else if (dir)
		g_string_append(text, dir);

	if (badges->lock != SOND_LOCK_NONE) {
		if (text->len)
			g_string_append(text, "\n");
		g_string_append(text, badges->lock == SOND_LOCK_UNLOCKED ?
				"Passwortgeschützt (entsperrt)" : "Passwortgeschützt");
	}
	if (badges->attachment) {
		if (text->len)
			g_string_append(text, "\n");
		g_string_append(text, "Anhang");
	}

	return g_string_free(text, text->len == 0);
}

/* Symbol des Eintrags (Basis-Icon und Overlays) neu lesen */
static void icon_refresh(SondTreeviewFM *self, GtkWidget *overlay, SondTVFMItem *item) {
	IconWidgets *w = g_object_get_data(G_OBJECT(overlay), ICON_KEY);
	SondTVFMBadgeSources sources = { 0 };
	SondTVFMBadges badges;
	gchar *tooltip = NULL;

	set_base_icon(GTK_IMAGE(w->base), item);

	sources.file_badge = fm_source_file_badge;
	sources.dir_status = fm_source_dir_status;
	sources.index_status = fm_source_index_status;
	sond_tvfm_item_get_badges(item, SOND_TVFM_OWNER(self), &sources, self, &badges);

	//Unten rechts: SeaDrive-Zustand der Datei bzw. des Ordners, sonst der Teilbaum-Status
	corner_set(w, SOND_ICON_CORNER_BOTTOM_RIGHT,
			badges.seadrive != SOND_SEADRIVE_BADGE_NONE ?
					seadrive_texture(badges.seadrive) :
					dir_status_texture(badges.dir_status));
	corner_set(w, SOND_ICON_CORNER_BOTTOM_LEFT, index_texture(badges.index));
	corner_set(w, SOND_ICON_CORNER_TOP_RIGHT,
			badges.attachment ? attachment_texture(GTK_WIDGET(self)) : NULL);
	corner_set(w, SOND_ICON_CORNER_TOP_LEFT,
			badges.lock != SOND_LOCK_NONE ?
					lock_texture(GTK_WIDGET(self), badges.lock == SOND_LOCK_UNLOCKED) :
					NULL);

	tooltip = badges_tooltip(&badges);
	gtk_widget_set_tooltip_text(overlay, tooltip);
	g_free(tooltip);
}

/* ------------------------------------------------------------------------
 * Zellen
 * --------------------------------------------------------------------- */

typedef enum {
	CELL_DATE, CELL_SIZE
} CellKind;

/* Der Baum zur Factory: beim Binden hängt die Zelle nicht immer schon im Widgetbaum,
 * ein Suchen über die Vorfahren schlägt dann fehl und die Zeile bliebe ohne Symbol.
 * Deshalb trägt die Factory eine schwache Referenz auf den Baum. */
#define VIEW_REF_KEY "sond-view-ref"

static void view_ref_free(gpointer data) {
	g_weak_ref_clear(data);
	g_free(data);
}

static void factory_set_view(GtkListItemFactory *factory, SondTreeviewFM *view) {
	GWeakRef *ref = g_new0(GWeakRef, 1);

	g_weak_ref_init(ref, view);
	g_object_set_data_full(G_OBJECT(factory), VIEW_REF_KEY, ref, view_ref_free);
}

//neue Referenz (g_object_unref()) oder NULL
static SondTreeviewFM* factory_get_view(GtkSignalListItemFactory *factory) {
	GWeakRef *ref = g_object_get_data(G_OBJECT(factory), VIEW_REF_KEY);

	return ref ? g_weak_ref_get(ref) : NULL;
}

#define HIT_CSS_CLASS "sond-hit"

//Text der Namenszelle: Name; bei Abschnitten der Text der Unterklasse (Fehlermeldung bei Fehler)
static gchar* name_cell_text(SondTreeviewFM *view, SondTVFMItem *item) {
	SondTreeviewFMClass *klass = view ? SOND_TREEVIEWFM_GET_CLASS(view) : NULL;
	gchar *text = NULL;

	if (sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_LEAF_SECTION && klass &&
			klass->text_from_section) {
		GError *error = NULL;

		if (klass->text_from_section(item, &text, &error)) {
			text = g_strdup(error ? error->message : "?");
			g_clear_error(&error);
		}

		return text ? text : g_strdup("");
	}

	return g_strdup(sond_tvfm_item_get_display_name(item));
}

//Hervorhebung der Zeile (Vfunc deter_background)
static void name_cell_highlight(SondTreeviewFM *view, SondTVFMItem *item, GtkWidget *label) {
	SondTreeviewFMClass *klass = view ? SOND_TREEVIEWFM_GET_CLASS(view) : NULL;
	gint rc = 0;

	if (klass && klass->deter_background) {
		GError *error = NULL;

		rc = klass->deter_background(item, &error);
		if (rc == -1) {
			LOG_WARN("Fehler bei Ermittlung background: %s", error ? error->message : "?");
			g_clear_error(&error);
			rc = 0;
		}
	}

	if (rc == 1)
		gtk_widget_add_css_class(label, HIT_CSS_CLASS);
	else
		gtk_widget_remove_css_class(label, HIT_CSS_CLASS);
}

static void name_setup(GtkSignalListItemFactory *factory, GtkListItem *listitem,
		gpointer data) {
	GtkWidget *expander = gtk_tree_expander_new();
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *icon = icon_new();
	GtkWidget *label = gtk_label_new(NULL);

	(void) factory;
	(void) data;

	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_box_append(GTK_BOX(box), icon);
	gtk_box_append(GTK_BOX(box), label);
	gtk_tree_expander_set_child(GTK_TREE_EXPANDER(expander), box);
	gtk_list_item_set_child(listitem, expander);

	sond_treeview_attach_context_menu(expander, listitem);
	fm_dnd_attach(expander, listitem);
}

static void pins_changed_cb(SondTreeviewFM *self, gpointer overlay) {
	SondTVFMItem *item = g_object_get_data(G_OBJECT(overlay), ITEM_KEY);

	if (item)
		icon_refresh(self, GTK_WIDGET(overlay), item);
}

static void name_bind(GtkSignalListItemFactory *factory, GtkListItem *listitem,
		gpointer data) {
	GtkTreeListRow *row = gtk_list_item_get_item(listitem);
	GtkWidget *expander = gtk_list_item_get_child(listitem);
	GtkWidget *box = gtk_tree_expander_get_child(GTK_TREE_EXPANDER(expander));
	GtkWidget *icon = gtk_widget_get_first_child(box);
	GtkWidget *label = gtk_widget_get_last_child(box);
	SondTreeviewFM *view = factory_get_view(factory);
	SondTVFMItem *item = NULL;

	(void) data;

	gtk_tree_expander_set_list_row(GTK_TREE_EXPANDER(expander), row);

	item = sond_treeview_listitem_get_item(listitem);
	if (!item) {
		g_clear_object(&view);

		return;
	}

	{
		gchar *text = name_cell_text(view, item);

		gtk_label_set_text(GTK_LABEL(label), text);
		g_free(text);
		name_cell_highlight(view, item, label);
	}

	if (view) {
		icon_refresh(view, icon, item);

		//das Symbol folgt Änderungen des Pin-Zustands
		g_object_set_data_full(G_OBJECT(icon), ITEM_KEY, g_object_ref(item),
				g_object_unref);
		g_signal_handlers_disconnect_by_data(view, icon); //nie doppelt verbunden
		g_signal_connect_object(view, "pins-changed", G_CALLBACK(pins_changed_cb),
				icon, 0);
	}

	g_clear_object(&view);
	g_object_unref(item);
}

static void name_unbind(GtkSignalListItemFactory *factory, GtkListItem *listitem,
		gpointer data) {
	GtkWidget *expander = gtk_list_item_get_child(listitem);
	GtkWidget *box = expander ?
			gtk_tree_expander_get_child(GTK_TREE_EXPANDER(expander)) : NULL;
	GtkWidget *icon = box ? gtk_widget_get_first_child(box) : NULL;
	SondTreeviewFM *view = factory_get_view(factory);

	(void) data;

	if (!icon) {
		g_clear_object(&view);

		return;
	}

	if (view)
		g_signal_handlers_disconnect_by_data(view, icon);
	g_object_set_data(G_OBJECT(icon), ITEM_KEY, NULL);
	g_clear_object(&view);
}

static void text_setup(GtkSignalListItemFactory *factory, GtkListItem *listitem,
		gpointer data) {
	GtkWidget *label = gtk_label_new(NULL);

	(void) factory;
	(void) data;

	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_list_item_set_child(listitem, label);

	sond_treeview_attach_context_menu(label, listitem);
}

static gchar* cell_text(SondTVFMItem *item, CellKind kind) {
	gint64 mtime = 0;
	gint64 size = 0;
	gboolean dir = FALSE;

	switch (kind) {
	case CELL_DATE:
		if (sond_tvfm_item_fs_stat(item, &mtime, NULL, NULL)) {
			GDateTime *dt = g_date_time_new_from_unix_local(mtime);
			gchar *text = dt ? g_date_time_format(dt, "%d.%m.%Y %H:%M") : NULL;

			if (dt)
				g_date_time_unref(dt);

			return text ? text : g_strdup("");
		}
		return g_strdup("");
	case CELL_SIZE:
		if (sond_tvfm_item_fs_stat(item, NULL, &size, &dir) && !dir)
			return g_format_size((guint64) size);
		return g_strdup("");
	}

	return g_strdup("");
}

static void text_bind(GtkSignalListItemFactory *factory, GtkListItem *listitem,
		gpointer data) {
	CellKind kind = (CellKind) GPOINTER_TO_INT(data);
	GtkWidget *label = gtk_list_item_get_child(listitem);
	SondTVFMItem *item = sond_treeview_listitem_get_item(listitem);
	gchar *text = NULL;

	(void) factory;

	if (!item)
		return;

	text = cell_text(item, kind);
	gtk_label_set_text(GTK_LABEL(label), text);
	g_free(text);
	g_object_unref(item);
}

static GtkListItemFactory* text_factory(CellKind kind) {
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();

	g_signal_connect(factory, "setup", G_CALLBACK(text_setup), NULL);
	g_signal_connect(factory, "bind", G_CALLBACK(text_bind), GINT_TO_POINTER(kind));

	return factory;
}

//Anzeige: Datum, Gedankenstrich ohne Angabe, leer für Einträge in Containern
static gchar* eingang_text(SondTreeviewFM *view, SondTVFMItem *item) {
	gint eingang = 0;

	if (!sond_tvfm_item_fs_path(item, NULL, NULL))
		return g_strdup("");

	eingang = item_eingang(view, item);

	return eingang > 0 ? sond_eingang_int_zu_anzeige(eingang) : g_strdup("–");
}

static void eingang_changed_cb(SondTreeviewFM *view, gpointer label) {
	SondTVFMItem *item = g_object_get_data(G_OBJECT(label), ITEM_KEY);
	gchar *text = NULL;

	if (!item)
		return;

	text = eingang_text(view, item);
	gtk_label_set_text(GTK_LABEL(label), text);
	g_free(text);
}

static void eingang_bind(GtkSignalListItemFactory *factory, GtkListItem *listitem,
		gpointer data) {
	GtkWidget *label = gtk_list_item_get_child(listitem);
	SondTVFMItem *item = sond_treeview_listitem_get_item(listitem);
	SondTreeviewFM *view = factory_get_view(factory);
	gchar *text = NULL;

	(void) data;

	if (!item || !view) {
		g_clear_object(&item);
		g_clear_object(&view);

		return;
	}

	text = eingang_text(view, item);
	gtk_label_set_text(GTK_LABEL(label), text);
	g_free(text);

	//die Zelle folgt neuen Eingangsangaben
	g_object_set_data_full(G_OBJECT(label), ITEM_KEY, g_object_ref(item), g_object_unref);
	g_signal_handlers_disconnect_by_data(view, label); //nie doppelt verbunden
	g_signal_connect_object(view, "eingang-changed", G_CALLBACK(eingang_changed_cb), label, 0);

	g_object_unref(view);
	g_object_unref(item);
}

static void eingang_unbind(GtkSignalListItemFactory *factory, GtkListItem *listitem,
		gpointer data) {
	GtkWidget *label = gtk_list_item_get_child(listitem);
	SondTreeviewFM *view = factory_get_view(factory);

	(void) data;

	if (view)
		g_signal_handlers_disconnect_by_data(view, label);
	if (label)
		g_object_set_data(G_OBJECT(label), ITEM_KEY, NULL);
	g_clear_object(&view);
}

static gboolean reset_eingang_match(gpointer item, gpointer user_data) {
	(void) user_data;

	sond_tvfm_item_get_priv(SOND_TVFM_ITEM(item))->eingang_done = FALSE;

	return FALSE; //alle sichtbaren Zeilen durchgehen
}

void sond_treeviewfm_eingang_refresh(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = NULL;

	g_return_if_fail(SOND_IS_TREEVIEWFM(self));
	priv = fm_get_priv(self);

	//Zeilen, die nicht sichtbar sind, lesen ihre Angaben beim Aufklappen frisch
	sond_treeview_find_row(SOND_TREEVIEW(self), reset_eingang_match, NULL, NULL);
	g_signal_emit(self, fm_signals[SIGNAL_EINGANG_CHANGED], 0);

	//ist nach der Spalte sortiert, neu sortieren
	if (priv->column_eingang)
		gtk_sorter_changed(gtk_column_view_column_get_sorter(priv->column_eingang),
				GTK_SORTER_CHANGE_DIFFERENT);
}

void fm_register_icons(void) {
	static gboolean registered = FALSE;

	if (registered)
		return;
	registered = TRUE;

	//Hervorhebung von Zeilen (Vfunc deter_background)
	{
		GtkCssProvider *provider = gtk_css_provider_new();

		gtk_css_provider_load_from_string(provider,
				"label." HIT_CSS_CLASS " { background-color: rgba(255, 213, 79, 0.45); }");
		gtk_style_context_add_provider_for_display(gdk_display_get_default(),
				GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref(provider);
	}

	//die eigenen Symbole (pdf, pdf-folder, ...) wie in Zond als Ressource im Icon-Theme
	g_resources_register(icons_get_resource());
	gtk_icon_theme_add_resource_path(
			gtk_icon_theme_get_for_display(gdk_display_get_default()), "/icons");
}

/* Standard: Name (mit Symbol), Geändert, Größe */
void fm_default_setup_columns(SondTreeviewFM *self) {
	SondTreeview *base = SOND_TREEVIEW(self);
	GtkSorter *sorter = NULL;
	GtkColumnViewColumn *column = NULL;
	GtkListItemFactory *factory = NULL;

	factory = gtk_signal_list_item_factory_new();
	g_signal_connect(factory, "setup", G_CALLBACK(name_setup), NULL);
	g_signal_connect(factory, "bind", G_CALLBACK(name_bind), NULL);
	g_signal_connect(factory, "unbind", G_CALLBACK(name_unbind), NULL);
	factory_set_view(factory, self);
	sorter = GTK_SORTER(gtk_custom_sorter_new(compare_name, NULL, NULL));
	column = sond_treeview_add_column(base, "Name", factory, sorter, TRUE);

	//Eingang nur, wenn eine Unterklasse die Angaben liefert (Vfunc get_eingang)
	if (SOND_TREEVIEWFM_GET_CLASS(self)->get_eingang) {
		factory = gtk_signal_list_item_factory_new();
		g_signal_connect(factory, "setup", G_CALLBACK(text_setup), NULL);
		g_signal_connect(factory, "bind", G_CALLBACK(eingang_bind), NULL);
		g_signal_connect(factory, "unbind", G_CALLBACK(eingang_unbind), NULL);
		factory_set_view(factory, self);
		sorter = GTK_SORTER(gtk_custom_sorter_new(compare_eingang, self, NULL));
		fm_get_priv(self)->column_eingang = sond_treeview_add_column(base, "Eingang",
				factory, sorter, FALSE);
	}

	sorter = GTK_SORTER(gtk_custom_sorter_new(compare_date, NULL, NULL));
	sond_treeview_add_column(base, "Geändert", text_factory(CELL_DATE), sorter, FALSE);

	sorter = GTK_SORTER(gtk_custom_sorter_new(compare_size, NULL, NULL));
	sond_treeview_add_column(base, "Größe", text_factory(CELL_SIZE), sorter, FALSE);

	gtk_column_view_sort_by_column(sond_treeview_get_column_view(base), column,
			GTK_SORT_ASCENDING);
}
