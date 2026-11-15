/*
 sond (gtk4/sond_treeviewfm.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_treeviewfm.h"

#include <string.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include "../sond_log_and_error.h"
#include "../sond_fileparts.h"
#include "../sond_file_helper.h"
#include "../sond_tvfm_item.h"
#include "../sond_tvfm_item_private.h"
#include "../sond_tvfm_owner.h"
#include "../sond_seadrive_core.h"
#include "../sond_icon_util.h"
#include "../sond_tvfm_badges.h"
#include "../sond_tvfm_index.h"
#include "../sond_mime.h"
#ifdef G_OS_WIN32
#include "../sond_seadrive_watcher.h"
#endif
#include "../zond/10init/icons.h"

typedef struct {
	gchar *root;
	gboolean is_seadrive;
	SondTreeviewFMIndexCtxFunc index_ctx_func;
	gpointer index_ctx_func_data;
	GFileMonitor *root_monitor; //Zugänge von außen auf Wurzel-Ebene
	guint root_monitor_timer;   //Entprellung
#ifdef G_OS_WIN32
	SondSeadriveWatcher *watcher; //nur bei einer Wurzel im SeaDrive-Laufwerk
	guint refresh_idle;           //gebündelte Auffrischung der Symbole
#endif
} SondTreeviewFMPrivate;

static void sond_treeviewfm_owner_init(SondTVFMOwnerInterface *iface);

G_DEFINE_TYPE_WITH_CODE(SondTreeviewFM, sond_treeviewfm, SOND_TYPE_TREEVIEW,
		G_ADD_PRIVATE(SondTreeviewFM)
		G_IMPLEMENT_INTERFACE(SOND_TYPE_TVFM_OWNER, sond_treeviewfm_owner_init))

enum {
	SIGNAL_PINS_CHANGED, SIGNAL_SEADRIVE_STATUS, N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

/* ------------------------------------------------------------------------
 * Eigentümer-Schnittstelle (sond_tvfm_owner.h)
 * --------------------------------------------------------------------- */

static gchar const* owner_get_root(SondTVFMOwner *owner) {
	return sond_treeviewfm_get_root(SOND_TREEVIEWFM(owner));
}

static gboolean owner_is_seadrive_path(SondTVFMOwner *owner) {
	return sond_treeviewfm_is_seadrive_path(SOND_TREEVIEWFM(owner));
}

static gboolean owner_supports_sections(SondTVFMOwner *owner) {
	return SOND_TREEVIEWFM_GET_CLASS(owner)->has_sections != NULL;
}

static gboolean owner_has_sections(SondTVFMOwner *owner, SondTVFMItem *item) {
	return SOND_TREEVIEWFM_GET_CLASS(owner)->has_sections ?
			SOND_TREEVIEWFM_GET_CLASS(owner)->has_sections(item) : FALSE;
}

static gboolean owner_supports_load_sections(SondTVFMOwner *owner) {
	return SOND_TREEVIEWFM_GET_CLASS(owner)->load_sections != NULL;
}

static gint owner_load_sections(SondTVFMOwner *owner, SondTVFMItem *item,
		GPtrArray **arr_children, GError **error) {
	return SOND_TREEVIEWFM_GET_CLASS(owner)->load_sections ?
			SOND_TREEVIEWFM_GET_CLASS(owner)->load_sections(item, arr_children,
					error) : -1;
}

static gboolean owner_supports_delete_section(SondTVFMOwner *owner) {
	return SOND_TREEVIEWFM_GET_CLASS(owner)->delete_section != NULL;
}

static gint owner_delete_section(SondTVFMOwner *owner, SondTVFMItem *item,
		GError **error) {
	return SOND_TREEVIEWFM_GET_CLASS(owner)->delete_section ?
			SOND_TREEVIEWFM_GET_CLASS(owner)->delete_section(item, error) : 0;
}

static gint owner_before_move(SondTVFMOwner *owner, SondTVFMItem *item,
		SondTVFMItem *parent, gchar const *base, gint index_to, GError **error,
		gpointer *ctx) {
	gint res = 0;

	g_signal_emit(owner, SOND_TREEVIEWFM_GET_CLASS(owner)->signal_before_move, 0,
			item, parent, base, index_to, error, ctx, &res);

	return res;
}

static gint owner_before_delete(SondTVFMOwner *owner, SondTVFMItem *item,
		GError **error, gpointer *ctx) {
	gint res = 0;

	g_signal_emit(owner, SOND_TREEVIEWFM_GET_CLASS(owner)->signal_before_delete, 0,
			item, error, ctx, &res);

	return res;
}

static gint owner_before_insert(SondTVFMOwner *owner, SondTVFMItem *item,
		SondTVFMItem *parent, gchar const *base, gint index_to, GError **error) {
	gint res = 0;

	g_signal_emit(owner, SOND_TREEVIEWFM_GET_CLASS(owner)->signal_before_insert, 0,
			item, parent, base, index_to, error, &res);

	return res;
}

static void owner_after(SondTVFMOwner *owner, gboolean success, gpointer ctx) {
	g_signal_emit(owner, SOND_TREEVIEWFM_GET_CLASS(owner)->signal_after, 0,
			success, ctx);
}

static void sond_treeviewfm_owner_init(SondTVFMOwnerInterface *iface) {
	iface->get_root = owner_get_root;
	iface->is_seadrive_path = owner_is_seadrive_path;
	iface->supports_sections = owner_supports_sections;
	iface->has_sections = owner_has_sections;
	iface->supports_load_sections = owner_supports_load_sections;
	iface->load_sections = owner_load_sections;
	iface->supports_delete_section = owner_supports_delete_section;
	iface->delete_section = owner_delete_section;
	iface->before_move = owner_before_move;
	iface->before_delete = owner_before_delete;
	iface->before_insert = owner_before_insert;
	iface->after = owner_after;
}

/* ------------------------------------------------------------------------
 * Hilfen
 * --------------------------------------------------------------------- */

/* Voller Pfad eines Dateisystem-Eintrags (g_free()); NULL für Einträge in Containern */
static gchar* item_full_path(SondTreeviewFM *self, SondTVFMItem *item,
		gboolean *is_dir) {
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
	gchar *rel = NULL;
	gchar *full = NULL;

	if (!priv->root || !sond_tvfm_item_fs_path(item, &rel, is_dir))
		return NULL;

	full = g_strconcat(priv->root, "/", rel, NULL);
	g_free(rel);

	return full;
}

static gboolean item_is_dir(SondTVFMItem *item) {
	gboolean dir = FALSE;

	if (sond_tvfm_item_fs_stat(item, NULL, NULL, &dir))
		return dir;

	return sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_DIR;
}

/* ------------------------------------------------------------------------
 * Kinder laden (träge)
 * --------------------------------------------------------------------- */

/* GtkTreeListModel ruft create_func schon, um zu prüfen, ob eine Zeile aufklappbar
 * ist, und wirft das Ergebnis dann weg. Die Kinder dürfen deshalb erst gelesen
 * werden, wenn jemand sie wirklich braucht (Aufklappen): diese Liste lädt beim
 * ersten Zugriff auf ihre Einträge. */
#define SOND_TYPE_LAZY_CHILDREN sond_lazy_children_get_type( )
G_DECLARE_FINAL_TYPE(SondLazyChildren, sond_lazy_children, SOND, LAZY_CHILDREN, GObject)

struct _SondLazyChildren {
	GObject parent_instance;
	SondTVFMItem *item;
	GPtrArray *children;
	gboolean loaded;
};

static void lazy_ensure_loaded(SondLazyChildren *self) {
	GError *error = NULL;

	if (self->loaded)
		return;
	self->loaded = TRUE;

	if (sond_tvfm_item_load_children(self->item, &self->children, NULL, &error)) {
		LOG_WARN("Kinder von '%s' nicht geladen: %s\n",
				sond_tvfm_item_get_display_name(self->item) ?
						sond_tvfm_item_get_display_name(self->item) : "?",
				error ? error->message : "?");
		g_clear_error(&error);
		self->children = g_ptr_array_new_with_free_func(g_object_unref);
	}
}

static GType lazy_get_item_type(GListModel *list) {
	(void) list;

	return SOND_TYPE_TVFM_ITEM;
}

static guint lazy_get_n_items(GListModel *list) {
	SondLazyChildren *self = SOND_LAZY_CHILDREN(list);

	lazy_ensure_loaded(self);

	return self->children ? self->children->len : 0;
}

static gpointer lazy_get_item(GListModel *list, guint position) {
	SondLazyChildren *self = SOND_LAZY_CHILDREN(list);

	lazy_ensure_loaded(self);
	if (!self->children || position >= self->children->len)
		return NULL;

	return g_object_ref(g_ptr_array_index(self->children, position));
}

static void lazy_list_model_init(GListModelInterface *iface) {
	iface->get_item_type = lazy_get_item_type;
	iface->get_n_items = lazy_get_n_items;
	iface->get_item = lazy_get_item;
}

G_DEFINE_TYPE_WITH_CODE(SondLazyChildren, sond_lazy_children, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(G_TYPE_LIST_MODEL, lazy_list_model_init))

static void lazy_finalize(GObject *object) {
	SondLazyChildren *self = SOND_LAZY_CHILDREN(object);

	g_clear_object(&self->item);
	g_clear_pointer(&self->children, g_ptr_array_unref);

	G_OBJECT_CLASS(sond_lazy_children_parent_class)->finalize(object);
}

static void sond_lazy_children_class_init(SondLazyChildrenClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = lazy_finalize;
}

static void sond_lazy_children_init(SondLazyChildren *self) {
	(void) self;
}


/* Änderungen an einer Kinderliste (nach Anlegen, Umbenennen, Löschen); sie melden sich
 * über items-changed, der Baum zeigt sie an */
static SondLazyChildren* lazy_from_model(GListModel *model) {
	return (model && SOND_IS_LAZY_CHILDREN(model)) ? SOND_LAZY_CHILDREN(model) : NULL;
}

//fertig geladene Liste (Wurzel); Eigentum des Arrays geht an die Liste
static SondLazyChildren* lazy_new_loaded(GPtrArray *items) {
	SondLazyChildren *lazy = g_object_new(SOND_TYPE_LAZY_CHILDREN, NULL);

	lazy->children = items;
	lazy->loaded = TRUE;

	return lazy;
}

static gint lazy_index_of(SondLazyChildren *self, SondTVFMItem *item) {
	if (!self->children)
		return -1;

	for (guint i = 0; i < self->children->len; i++)
		if (g_ptr_array_index(self->children, i) == item)
			return (gint) i;

	return -1;
}

static void lazy_insert(SondLazyChildren *self, SondTVFMItem *item) {
	lazy_ensure_loaded(self);
	if (!self->children)
		self->children = g_ptr_array_new_with_free_func(g_object_unref);

	g_ptr_array_add(self->children, g_object_ref(item));
	g_list_model_items_changed(G_LIST_MODEL(self), self->children->len - 1, 0, 1);
}

static void lazy_remove(SondLazyChildren *self, SondTVFMItem *item) {
	gint index = lazy_index_of(self, item);

	if (index < 0)
		return;

	g_ptr_array_remove_index(self->children, (guint) index);
	g_list_model_items_changed(G_LIST_MODEL(self), (guint) index, 1, 0);
}

//ersetzt den Eintrag durch einen anderen (z.B. Platzhalter durch Container)
static void lazy_replace(SondLazyChildren *self, SondTVFMItem *old_item,
		SondTVFMItem *new_item) {
	gint index = lazy_index_of(self, old_item);
	gpointer old_ref = NULL;

	if (index < 0)
		return;

	old_ref = g_ptr_array_index(self->children, index);
	g_ptr_array_index(self->children, index) = g_object_ref(new_item);
	g_list_model_items_changed(G_LIST_MODEL(self), (guint) index, 1, 1);
	g_object_unref(old_ref);
}

//Eintrag hat sich geändert (Name, Größe, ...): Zeile neu aufbauen und neu einsortieren
static void lazy_touch(SondLazyChildren *self, SondTVFMItem *item) {
	gint index = lazy_index_of(self, item);

	if (index >= 0)
		g_list_model_items_changed(G_LIST_MODEL(self), (guint) index, 1, 1);
}

static GListModel* fm_create_children(SondTreeview *base, gpointer object) {
	SondTVFMItem *item = SOND_TVFM_ITEM(object);
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
	SondLazyChildren *lazy = NULL;

	(void) base;

	if (item_priv->type != SOND_TVFM_ITEM_TYPE_DIR || !item_priv->has_children)
		return NULL;

	//keine Ein-/Ausgabe hier, die Kinder werden erst beim Aufklappen gelesen
	lazy = g_object_new(SOND_TYPE_LAZY_CHILDREN, NULL);
	lazy->item = g_object_ref(item);

	return G_LIST_MODEL(lazy);
}

/* ------------------------------------------------------------------------
 * Sortierung: Verzeichnisse zuerst, dann nach der Spalte
 * --------------------------------------------------------------------- */

static gint compare_dirs_first(SondTVFMItem *a, SondTVFMItem *b, gboolean *decided) {
	gboolean dir_a = item_is_dir(a);
	gboolean dir_b = item_is_dir(b);

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
static SondSeadriveBadge default_file_badge(SondTreeviewFM *self, gchar const *full_path) {
	(void) self;

	return sond_tvfm_badge_seadrive_live(full_path);
}

/* Standard der Klasse: Ordner-Status aus dem Watcher (nur im SeaDrive-Laufwerk) */
static SondSeadriveDirStatus default_dir_status(SondTreeviewFM *self,
		gchar const *full_path) {
#ifdef G_OS_WIN32
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);

	return sond_seadrive_watcher_get_dir_status(priv->watcher, full_path);
#else
	(void) self;
	(void) full_path;

	return SOND_SEADRIVE_DIR_STATUS_NONE;
#endif
}

/* Standard der Klasse: Status aus der Index-DB über den gesetzten Kontext */
static SondIndexStatus default_index_status(SondTreeviewFM *self, SondTVFMItem *item) {
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
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
	priv = sond_treeviewfm_get_instance_private(self);

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

static void dnd_attach(GtkWidget *cell, GtkListItem *listitem);

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
	dnd_attach(expander, listitem);
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

	gtk_label_set_text(GTK_LABEL(label), sond_tvfm_item_get_display_name(item));

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

/* ------------------------------------------------------------------------
 * Wurzel, Auswahl, Anheften
 * --------------------------------------------------------------------- */

const gchar* sond_treeviewfm_get_root(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = NULL;

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), NULL);
	priv = sond_treeviewfm_get_instance_private(self);

	return priv->root;
}

gboolean sond_treeviewfm_is_seadrive_path(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = NULL;

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), FALSE);
	priv = sond_treeviewfm_get_instance_private(self);

	return priv->is_seadrive;
}

static void update_pin_actions(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
	GActionMap *map = sond_treeview_get_action_map(SOND_TREEVIEW(self));
	static const gchar *names[] = { "pin", "unpin", "free" };

	for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
		GAction *action = g_action_map_lookup_action(map, names[i]);

		if (action)
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), priv->is_seadrive);
	}
}

/* --- Zugänge von außen auf der obersten Ebene --- */

/* Fügt Einträge, die auf der obersten Ebene von außen hinzugekommen sind, in den Baum
 * ein. Idempotent: vorhandene Zeilen bleiben unberührt. Löschen oder Umbenennen von
 * außen wird bewusst nicht nachgeführt. */
static void root_add_new(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
	SondLazyChildren *lazy = lazy_from_model(sond_treeview_get_roots(SOND_TREEVIEW(self)));
	SondTVFMItem *item_root = NULL;
	GPtrArray *children = NULL;
	GHashTable *known = NULL;
	GError *error = NULL;

	if (!priv->root || !lazy)
		return;

	item_root = sond_tvfm_item_create(self, NULL, NULL);
	if (sond_tvfm_item_load_children(item_root, &children, NULL, &error)) {
		LOG_WARN("Wurzelverzeichnis konnte nicht neu gelesen werden: %s",
				error ? error->message : "?");
		g_clear_error(&error);
		g_object_unref(item_root);

		return;
	}

	known = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	for (guint i = 0; lazy->children && i < lazy->children->len; i++) {
		gchar const *name = sond_tvfm_item_get_basename(g_ptr_array_index(lazy->children, i));

		if (name)
			g_hash_table_add(known, g_strdup(name));
	}

	for (guint i = 0; i < children->len; i++) {
		SondTVFMItem *child = g_ptr_array_index(children, i);
		gchar const *name = sond_tvfm_item_get_basename(child);

		if (name && !g_hash_table_contains(known, name))
			lazy_insert(lazy, child);
	}

	g_hash_table_unref(known);
	g_ptr_array_unref(children);
	g_object_unref(item_root);
}

static gboolean root_monitor_timeout(gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);

	priv->root_monitor_timer = 0;
	root_add_new(self);

	return G_SOURCE_REMOVE;
}

static void root_monitor_changed(GFileMonitor *monitor, GFile *file, GFile *other,
		GFileMonitorEvent event, gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);

	(void) monitor;
	(void) file;
	(void) other;

	switch (event) {
	case G_FILE_MONITOR_EVENT_CREATED:
	case G_FILE_MONITOR_EVENT_MOVED_IN:
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
		break;
	case G_FILE_MONITOR_EVENT_CHANGED:
		//Datei wird noch geschrieben: nur weiter warten, wenn etwas ansteht
		if (!priv->root_monitor_timer)
			return;
		break;
	default:
		return;
	}

	if (priv->root_monitor_timer)
		g_source_remove(priv->root_monitor_timer);
	priv->root_monitor_timer = g_timeout_add(500, root_monitor_timeout, self);
}

static void root_monitor_stop(SondTreeviewFMPrivate *priv) {
	if (priv->root_monitor_timer) {
		g_source_remove(priv->root_monitor_timer);
		priv->root_monitor_timer = 0;
	}

	if (priv->root_monitor) {
		g_file_monitor_cancel(priv->root_monitor);
		g_object_unref(priv->root_monitor);
		priv->root_monitor = NULL;
	}
}

static void root_monitor_start(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
	GFile *file_root = NULL;
	GError *error = NULL;

	file_root = g_file_new_for_path(priv->root);
	priv->root_monitor = g_file_monitor_directory(file_root, G_FILE_MONITOR_WATCH_MOVES,
			NULL, &error);
	g_object_unref(file_root);

	if (!priv->root_monitor) {
		LOG_WARN("Wurzelverzeichnis kann nicht überwacht werden: %s",
				error ? error->message : "?");
		g_clear_error(&error);

		return;
	}

	g_signal_connect(priv->root_monitor, "changed", G_CALLBACK(root_monitor_changed), self);
}

#ifdef G_OS_WIN32
/* --- Anbindung des Watchers (sond_seadrive_watcher.c) --- */

static gboolean watcher_refresh_idle(gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);

	priv->refresh_idle = 0;
	g_signal_emit(self, signals[SIGNAL_PINS_CHANGED], 0);

	return G_SOURCE_REMOVE;
}

//Bei vielen Ereignissen hintereinander die Symbole nur einmal auffrischen
static void watcher_changed_cb(SondSeadriveWatcher *w, gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);

	(void) w;

	if (!priv->refresh_idle)
		priv->refresh_idle = g_idle_add_full(G_PRIORITY_LOW, watcher_refresh_idle,
				g_object_ref(self), g_object_unref);
}

static void watcher_status_cb(SondSeadriveWatcher *w, guint pending_down,
		guint pending_up, gpointer data) {
	(void) w;

	g_signal_emit(SOND_TREEVIEWFM(data), signals[SIGNAL_SEADRIVE_STATUS], 0,
			pending_down, pending_up);
}

static gboolean match_fs_rel(gpointer item, gpointer user_data) {
	gchar *rel = NULL;
	gboolean match = FALSE;

	if (sond_tvfm_item_fs_path(SOND_TVFM_ITEM(item), &rel, NULL))
		match = !g_strcmp0(rel, (gchar const*) user_data);
	g_free(rel);

	return match;
}

//Pfad relativ zur Wurzel (Zeiger in full_path) oder NULL
static gchar const* rel_path_of(SondTreeviewFMPrivate *priv, gchar const *full_path) {
	gsize len = priv->root ? strlen(priv->root) : 0;

	if (!len || !g_str_has_prefix(full_path, priv->root) || full_path[len] != '/' ||
			!full_path[len + 1])
		return NULL;

	return full_path + len + 1;
}

//Ersetzt die Zeile des Eintrags bei rel durch new_item; die Zeile muss sichtbar sein
static void replace_row_item(SondTreeviewFM *self, SondTVFMItem *old_item,
		SondTVFMItem *new_item, gchar const *rel) {
	SondTreeview *base = SOND_TREEVIEW(self);
	GtkTreeListRow *row = sond_treeview_find_row(base, match_fs_rel, (gpointer) rel,
			NULL);
	SondLazyChildren *lazy = NULL;

	if (!row)
		return;

	lazy = lazy_from_model(sond_treeview_row_get_container(base, row));
	if (lazy)
		lazy_replace(lazy, old_item, new_item);
	g_object_unref(row);
}

/* Eine Datei wurde geladen: war sie beim Einlesen ein einfaches Blatt (offline, nicht
 * auf Kinder geprüft), jetzt durch den richtigen Typ (PDF, ZIP, E-Mail) ersetzen */
static void watcher_hydrated_cb(SondSeadriveWatcher *w, gchar const *path, gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
	SondTreeview *base = SOND_TREEVIEW(self);
	gchar const *rel = rel_path_of(priv, path);
	GtkTreeListRow *row = NULL;
	SondTVFMItem *item = NULL;
	SondTVFMItem *item_new = NULL;
	SondFilePart *sfp_old = NULL;
	SondFilePart *sfp_new = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;

	(void) w;

	if (!rel)
		return;

	row = sond_treeview_find_row(base, match_fs_rel, (gpointer) rel, NULL);
	if (!row)
		return; //nicht sichtbar: beim Aufklappen wird richtig geladen

	item = gtk_tree_list_row_get_item(row);
	g_object_unref(row);
	if (!item)
		return;

	sfp_old = sond_tvfm_item_get_sond_file_part(item);
	if (sond_tvfm_item_get_item_type(item) != SOND_TVFM_ITEM_TYPE_LEAF ||
			!SOND_IS_FILE_PART_LEAF(sfp_old)) {
		g_object_unref(item);

		return;
	}

	//das alte Blatt aus der Liste geöffneter Dateien nehmen, sonst gibt create() es zurück
	arr = sond_file_part_get_arr_opened_files(sond_file_part_get_parent(sfp_old));
	if (arr)
		g_ptr_array_remove_fast(arr, sfp_old);

	sfp_new = sond_file_part_create(sond_file_part_get_parent(sfp_old), rel, &error);
	if (!sfp_new) {
		LOG_WARN("SondFilePart kann nicht geöffnet werden:\n%s",
				error ? error->message : "?");
		g_clear_error(&error);
		g_object_unref(item);

		return;
	}

	item_new = sond_tvfm_item_create(self, sfp_new, NULL);
	g_object_unref(sfp_new);
	replace_row_item(self, item, item_new, rel);
	g_object_unref(item_new);
	g_object_unref(item);
}

/* Eine Datei wurde aus dem lokalen Speicher genommen: eine PDF, die als Container
 * (mit Seiten und Anhängen) angezeigt wird, wieder durch ein Blatt ersetzen. ZIP und
 * E-Mail bleiben Container, das Aufklappen lädt sie bei Bedarf neu. */
static void watcher_dehydrated_cb(SondSeadriveWatcher *w, gchar const *path,
		gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
	SondTreeview *base = SOND_TREEVIEW(self);
	gchar const *rel = rel_path_of(priv, path);
	GtkTreeListRow *row = NULL;
	SondTVFMItem *item = NULL;
	SondTVFMItem *item_new = NULL;
	SondFilePart *sfp_old = NULL;
	SondFilePart *sfp_new = NULL;
	GPtrArray *arr = NULL;
	gchar const *mime = NULL;

	(void) w;

	if (!rel)
		return;

	row = sond_treeview_find_row(base, match_fs_rel, (gpointer) rel, NULL);
	if (!row)
		return;

	item = gtk_tree_list_row_get_item(row);
	g_object_unref(row);
	if (!item)
		return;

	sfp_old = sond_tvfm_item_get_sond_file_part(item);
	if (sond_tvfm_item_get_item_type(item) != SOND_TVFM_ITEM_TYPE_DIR || !sfp_old ||
			sond_tvfm_item_get_path_or_section(item) ||
			!SOND_IS_FILE_PART_PDF(sfp_old)) {
		g_object_unref(item);

		return;
	}

	arr = sond_file_part_get_arr_opened_files(sond_file_part_get_parent(sfp_old));
	if (arr)
		g_ptr_array_remove_fast(arr, sfp_old);

	mime = mime_from_extension(rel);
	sfp_new = sond_file_part_create_leaf(rel, sond_file_part_get_parent(sfp_old),
			mime ? mime : "application/octet-stream");
	if (!sfp_new) {
		g_object_unref(item);

		return;
	}

	item_new = sond_tvfm_item_create(self, sfp_new, NULL);
	g_object_unref(sfp_new);
	replace_row_item(self, item, item_new, rel);
	g_object_unref(item_new);
	g_object_unref(item);
}

static void watcher_start(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
	SondSeadriveWatcher *w = NULL;

	if (priv->watcher || !priv->root || !priv->is_seadrive)
		return;

	w = sond_seadrive_watcher_new(priv->root);
	g_signal_connect(w, "changed", G_CALLBACK(watcher_changed_cb), self);
	g_signal_connect(w, "status-changed", G_CALLBACK(watcher_status_cb), self);
	g_signal_connect(w, "hydrated", G_CALLBACK(watcher_hydrated_cb), self);
	g_signal_connect(w, "dehydrated", G_CALLBACK(watcher_dehydrated_cb), self);
	priv->watcher = w;
	sond_seadrive_watcher_start(w);
}

/* Löst den Watcher vom Widget; danach kommen keine Signale mehr. Das Warten auf den
 * Thread (bis ca. 20 s) übernimmt der Reaper des Watchers; er berührt das Widget nicht. */
static void watcher_stop(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
	SondSeadriveWatcher *w = priv->watcher;

	if (!w)
		return;

	priv->watcher = NULL;
	g_signal_handlers_disconnect_by_data(w, self);
	sond_seadrive_watcher_stop_async(w);
	g_object_unref(w);
	g_signal_emit(self, signals[SIGNAL_SEADRIVE_STATUS], 0, (guint) 0, (guint) 0);
}
#endif

gint sond_treeviewfm_set_root(SondTreeviewFM *self, const gchar *root,
		GError **error) {
	SondTreeviewFMPrivate *priv = NULL;
	SondTVFMItem *root_item = NULL;
	GPtrArray *children = NULL;
	GListModel *model = NULL;
	SondFilePartClass *sfp_class = g_type_class_peek_static(SOND_TYPE_FILE_PART);

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), -1);
	priv = sond_treeviewfm_get_instance_private(self);

	root_monitor_stop(priv);
#ifdef G_OS_WIN32
	watcher_stop(self);
#endif
	g_free(priv->root);
	priv->root = NULL;
	priv->is_seadrive = FALSE;
	g_free(sfp_class->path_root);
	sfp_class->path_root = NULL;

	if (!root) {
		sond_treeview_set_roots(SOND_TREEVIEW(self),
				G_LIST_MODEL(g_list_store_new(SOND_TYPE_TVFM_ITEM)));
		update_pin_actions(self);

		return 0;
	}

	priv->root = g_strdup(root);
	//abschließende Trennzeichen entfernen ("C:/" bleibt unverändert)
	while (strlen(priv->root) > 1 && (g_str_has_suffix(priv->root, "/") ||
			g_str_has_suffix(priv->root, "\\")) && !g_str_has_suffix(priv->root, ":/")
			&& !g_str_has_suffix(priv->root, ":\\"))
		priv->root[strlen(priv->root) - 1] = '\0';

	//die Dateiteile lösen ihre Pfade gegen die Wurzel auf (klassenweit, s. sond_fileparts.h)
	sfp_class->path_root = g_strdup(priv->root);

#ifdef G_OS_WIN32
	priv->is_seadrive = sond_seadrive_is_seadrive_path(priv->root);
#endif
	update_pin_actions(self);

	//die Wurzel ist ein Verzeichnis-Item ohne Pfad; seine Kinder sind die oberste Ebene
	root_item = sond_tvfm_item_create(self, NULL, NULL);
	if (!root_item) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Wurzel '%s' nicht lesbar", root);
		g_free(sfp_class->path_root);
		sfp_class->path_root = NULL;
		g_clear_pointer(&priv->root, g_free);
		priv->is_seadrive = FALSE;

		return -1;
	}

	if (sond_tvfm_item_load_children(root_item, &children, NULL, error)) {
		g_object_unref(root_item);
		g_free(sfp_class->path_root);
		sfp_class->path_root = NULL;
		g_clear_pointer(&priv->root, g_free);
		priv->is_seadrive = FALSE;

		return -1;
	}

	model = G_LIST_MODEL(lazy_new_loaded(children));
	g_object_unref(root_item);

	sond_treeview_set_roots(SOND_TREEVIEW(self), model);

#ifdef G_OS_WIN32
	watcher_start(self);
#endif
	root_monitor_start(self);

	return 0;
}

GPtrArray* sond_treeviewfm_get_selected_paths(SondTreeviewFM *self) {
	GPtrArray *items = NULL;
	GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), paths);

	items = sond_treeview_get_selected_items(SOND_TREEVIEW(self));
	for (guint i = 0; i < items->len; i++) {
		gchar *full = item_full_path(self, g_ptr_array_index(items, i), NULL);

		if (full)
			g_ptr_array_add(paths, full);
	}
	g_ptr_array_unref(items);

	return paths;
}

void sond_treeviewfm_refresh_status(SondTreeviewFM *self) {
	g_return_if_fail(SOND_IS_TREEVIEWFM(self));

	g_signal_emit(self, signals[SIGNAL_PINS_CHANGED], 0);
}

#ifdef G_OS_WIN32
/* Meldung über gescheiterte Einträge (die ersten fünf) */
static void report_failures(SondTreeviewFM *self, GPtrArray *failed) {
	GString *text = g_string_new(NULL);
	GtkAlertDialog *dialog = NULL;
	GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(self));

	if (failed->len == 1)
		g_string_append(text, "1 Eintrag konnte nicht geändert werden:\n");
	else
		g_string_append_printf(text,
				"%u Einträge konnten nicht geändert werden:\n", failed->len);
	for (guint i = 0; i < MIN(failed->len, 5); i++)
		g_string_append_printf(text, "\n%s",
				(const gchar*) g_ptr_array_index(failed, i));
	if (failed->len > 5)
		g_string_append_printf(text, "\n... und %u weitere", failed->len - 5);

	dialog = gtk_alert_dialog_new("SeaDrive");
	gtk_alert_dialog_set_detail(dialog, text->str);
	gtk_alert_dialog_set_modal(dialog, TRUE);
	gtk_alert_dialog_show(dialog, GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL);
	g_object_unref(dialog);
	g_string_free(text, TRUE);
}
#endif

void sond_treeviewfm_pin_selection(SondTreeviewFM *self, guint pin_state) {
#ifdef G_OS_WIN32
	SondTreeviewFMPrivate *priv = NULL;
	GPtrArray *items = NULL;
	GPtrArray *failed = NULL;

	g_return_if_fail(SOND_IS_TREEVIEWFM(self));
	priv = sond_treeviewfm_get_instance_private(self);

	if (!priv->is_seadrive)
		return;

	items = sond_treeview_get_selected_items(SOND_TREEVIEW(self));
	failed = g_ptr_array_new_with_free_func(g_free);

	for (guint i = 0; i < items->len; i++) {
		gboolean dir = FALSE;
		gchar *full = item_full_path(self, g_ptr_array_index(items, i), &dir);
		GError *error = NULL;

		if (!full)
			continue;

		//Verzeichnisse rekursiv: der Zustand gilt für alles darunter
		if (!sond_seadrive_set_pin_state(full, pin_state, dir, &error))
			sond_seadrive_pin_note_failure(failed, full, error);
		g_clear_error(&error);
		g_free(full);
	}

	sond_treeviewfm_refresh_status(self);
	if (failed->len > 0)
		report_failures(self, failed);

	g_ptr_array_unref(failed);
	g_ptr_array_unref(items);
#else
	(void) self;
	(void) pin_state;
#endif
}

/* ------------------------------------------------------------------------
 * Bearbeiten: Neues Verzeichnis, Umbenennen, Löschen (über die Item-Schicht, mit
 * denselben Signalen wie in der GTK3-Fassung)
 * --------------------------------------------------------------------- */

static GtkWindow* parent_window(SondTreeviewFM *self) {
	GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(self));

	return GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
}

static void show_error(SondTreeviewFM *self, gchar const *title, gchar const *message) {
	GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", title);

	gtk_alert_dialog_set_detail(dialog, message ? message : "");
	gtk_alert_dialog_set_modal(dialog, TRUE);
	gtk_alert_dialog_show(dialog, parent_window(self));
	g_object_unref(dialog);
}

/* Das Eltern-Item einer Zeile (neue Referenz): das Item der Elternzeile, bei
 * Einträgen der obersten Ebene das Wurzel-Verzeichnis */
static SondTVFMItem* parent_item_of_row(SondTreeviewFM *self, GtkTreeListRow *row) {
	GtkTreeListRow *parent = gtk_tree_list_row_get_parent(row);
	SondTVFMItem *item = NULL;

	if (parent) {
		item = gtk_tree_list_row_get_item(parent);
		g_object_unref(parent);

		return item;
	}

	return sond_tvfm_item_create(self, NULL, NULL);
}

static gboolean match_same(gpointer item, gpointer user_data) {
	return item == user_data;
}

static gboolean match_path(gpointer item, gpointer user_data) {
	return !g_strcmp0(sond_tvfm_item_get_path_or_section(SOND_TVFM_ITEM(item)),
			(gchar const*) user_data);
}

/* --- Neues Verzeichnis --- */

static void make_directory(SondTreeviewFM *self, gboolean child) {
	SondTreeview *base = SOND_TREEVIEW(self);
	GPtrArray *rows = sond_treeview_get_selected_rows(base);
	GtkTreeListRow *row = NULL;
	SondTVFMItem *selected = NULL;
	SondTVFMItem *parent = NULL;
	SondTVFMItem *created = NULL;
	gchar *path = NULL;
	GError *error = NULL;

	if (rows->len != 1) {
		show_error(self, "Verzeichnis anlegen", "Bitte genau einen Eintrag markieren.");
		g_ptr_array_unref(rows);

		return;
	}

	row = g_ptr_array_index(rows, 0);
	selected = gtk_tree_list_row_get_item(row);
	parent = child ? g_object_ref(selected) : parent_item_of_row(self, row);

	if (sond_tvfm_item_get_item_type(parent) != SOND_TVFM_ITEM_TYPE_DIR) {
		show_error(self, "Verzeichnis anlegen",
				"In diesem Eintrag kann kein Verzeichnis angelegt werden.");
		goto out;
	}

	if (sond_tvfm_item_mkdir(parent, child, &path, &error)) {
		show_error(self, "Verzeichnis kann nicht eingefügt werden",
				error ? error->message : "unbekannt");
		goto out;
	}

	if (!child) {
		//gleiche Ebene: in die Liste des Eintrags einfügen und markieren
		SondLazyChildren *lazy = lazy_from_model(sond_treeview_row_get_container(base, row));

		created = sond_tvfm_item_create(self, sond_tvfm_item_get_sond_file_part(parent),
				path);
		if (lazy) {
			lazy_insert(lazy, created);
			sond_treeview_select_item(base, created);
		}
	} else {
		//Unterebene
		SondTVFMItemPrivate *parent_priv = sond_tvfm_item_get_priv(parent);
		GtkTreeListRow *parent_row = g_object_ref(row);

		if (!parent_priv->has_children) {
			//der Eintrag war leer, jetzt ist er aufklappbar: Zeile neu aufbauen
			SondLazyChildren *lazy = lazy_from_model(
					sond_treeview_row_get_container(base, row));

			parent_priv->has_children = TRUE;
			if (lazy) {
				lazy_touch(lazy, parent);
				g_object_unref(parent_row);
				parent_row = sond_treeview_find_row(base, match_same, parent, NULL);
			}
		}

		if (parent_row) {
			if (gtk_tree_list_row_get_expanded(parent_row)) {
				//schon aufgeklappt: den neuen Eintrag ergänzen
				SondLazyChildren *lazy = lazy_from_model(
						gtk_tree_list_row_get_children(parent_row));

				created = sond_tvfm_item_create(self,
						sond_tvfm_item_get_sond_file_part(parent), path);
				if (lazy)
					lazy_insert(lazy, created);
			} else {
				//das Aufklappen liest das Verzeichnis, der neue Eintrag ist dabei
				gtk_tree_list_row_set_expanded(parent_row, TRUE);
			}
			g_object_unref(parent_row);
		}
		sond_treeview_select_matching(base, match_path, path);
	}

out:
	g_free(path);
	g_clear_error(&error);
	g_clear_object(&created);
	g_clear_object(&parent);
	g_clear_object(&selected);
	g_ptr_array_unref(rows);
}

static void action_mkdir_same(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	make_directory(SOND_TREEVIEWFM(data), FALSE);
}

static void action_mkdir_child(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	make_directory(SOND_TREEVIEWFM(data), TRUE);
}

/* --- Umbenennen --- */

/* Benennt den Eintrag um (Prüfung des Namens, Signale vor und nach der Änderung,
 * Nachführen von Zusatzangaben und geöffneten Dateien). 0 bei Erfolg, -1 bei Fehler. */
static gint rename_item(SondTreeviewFM *self, SondTVFMItem *item, SondTVFMItem *parent,
		gchar const *text_new, GError **error) {
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(self);
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
	gint res = 0;
	gint rc = 0;
	gpointer ctx = NULL;
	g_autofree gchar *path_old = NULL;
	g_autofree gchar *fs_old = NULL;
	g_autofree gchar *fs_new = NULL;
	gboolean fs_is_dir = FALSE;
	gboolean fs_known = FALSE;

	if (!sond_tvfm_item_is_valid_filename(text_new)) {
		g_set_error(error, g_quark_from_static_string("sond"), 0,
				"Der Name ist leer oder enthält unzulässige Zeichen");

		return -1;
	}

	/* index_to -1: Umbenennen an Ort und Stelle, kein Verschieben */
	g_signal_emit(self, klass->signal_before_move, 0, item, parent, text_new, -1,
			error, &ctx, &res);
	if (res)
		return -1;

	/* Verzeichnis: alten Pfad sichern - sond_tvfm_item_rename() setzt path_or_section
	 * auf den neuen (vollen) Pfad */
	path_old = g_strdup(item_priv->path_or_section);

	//Pfad vor dem Umbenennen merken (für die Nachführung von Zusatzangaben)
	fs_known = sond_tvfm_item_fs_path(item, &fs_old, &fs_is_dir);

	rc = sond_tvfm_item_rename(item, parent, text_new, error);
	g_signal_emit(self, klass->signal_after, 0, (rc == 0) ? TRUE : FALSE, ctx);
	if (rc)
		return -1;

	//Zusatzangaben (Eingangsstempel) auf den neuen Pfad nachführen
	if (fs_known && sond_tvfm_item_fs_path(item, &fs_new, NULL) &&
			g_strcmp0(fs_old, fs_new) && klass->fs_entry_renamed)
		klass->fs_entry_renamed(self, fs_old, fs_new, fs_is_dir);

	//Pfade bereits geöffneter Dateien darin nachführen
	if (path_old && item_priv->path_or_section)
		sond_tvfm_item_adjust_sfps_in_dir(item_priv->sond_file_part,
				item_priv->sond_file_part, path_old, item_priv->path_or_section);

	g_free(item_priv->display_name);
	item_priv->display_name = g_strdup(text_new);

	return 0;
}

typedef struct {
	SondTreeviewFM *self; //Referenz
	SondTVFMItem *item;
	SondTVFMItem *parent;
	SondLazyChildren *container;
	GtkWidget *window;
	GtkWidget *entry;
} RenameDialog;

static void rename_dialog_free(RenameDialog *d) {
	g_clear_object(&d->self);
	g_clear_object(&d->item);
	g_clear_object(&d->parent);
	g_clear_object(&d->container);
	g_free(d);
}

static void rename_dialog_ok(GtkWidget *widget, gpointer user_data) {
	RenameDialog *d = user_data;
	gchar *text = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(d->entry))));
	GError *error = NULL;

	(void) widget;

	if (g_strcmp0(text, sond_tvfm_item_get_display_name(d->item))) {
		if (rename_item(d->self, d->item, d->parent, text, &error)) {
			//das Schließen gibt d frei: vorher alles Nötige sichern
			SondTreeviewFM *self = g_object_ref(d->self);
			gchar *message = g_strdup(error ? error->message : "unbekannt");

			g_clear_error(&error);
			g_free(text);
			gtk_window_close(GTK_WINDOW(d->window));
			show_error(self, "Umbenennen nicht möglich", message);
			g_free(message);
			g_object_unref(self);

			return;
		}
		if (d->container) {
			lazy_touch(d->container, d->item); //neu anzeigen und einsortieren
			sond_treeview_select_item(SOND_TREEVIEW(d->self), d->item);
		}
	}

	g_free(text);
	gtk_window_close(GTK_WINDOW(d->window));
}

static void rename_dialog_cancel(GtkWidget *widget, gpointer user_data) {
	RenameDialog *d = user_data;

	(void) widget;

	gtk_window_close(GTK_WINDOW(d->window));
}

static void rename_dialog_destroyed(GtkWidget *widget, gpointer user_data) {
	(void) widget;

	rename_dialog_free(user_data);
}

static void rename_selected(SondTreeviewFM *self) {
	SondTreeview *base = SOND_TREEVIEW(self);
	GPtrArray *rows = sond_treeview_get_selected_rows(base);
	RenameDialog *d = NULL;
	GtkWidget *box = NULL;
	GtkWidget *buttons = NULL;
	GtkWidget *ok = NULL;
	GtkWidget *cancel = NULL;
	gchar const *name = NULL;
	gchar const *dot = NULL;

	if (rows->len != 1) {
		show_error(self, "Umbenennen", "Bitte genau einen Eintrag markieren.");
		g_ptr_array_unref(rows);

		return;
	}

	d = g_new0(RenameDialog, 1);
	d->self = g_object_ref(self);
	d->item = gtk_tree_list_row_get_item(g_ptr_array_index(rows, 0));
	d->parent = parent_item_of_row(self, g_ptr_array_index(rows, 0));
	d->container = lazy_from_model(sond_treeview_row_get_container(base,
			g_ptr_array_index(rows, 0)));
	if (d->container)
		g_object_ref(d->container);
	g_ptr_array_unref(rows);

	name = sond_tvfm_item_get_display_name(d->item);

	d->window = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(d->window), "Umbenennen");
	gtk_window_set_modal(GTK_WINDOW(d->window), TRUE);
	gtk_window_set_transient_for(GTK_WINDOW(d->window), parent_window(self));
	gtk_window_set_default_size(GTK_WINDOW(d->window), 420, -1);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_start(box, 14);
	gtk_widget_set_margin_end(box, 14);
	gtk_widget_set_margin_top(box, 14);
	gtk_widget_set_margin_bottom(box, 14);
	gtk_window_set_child(GTK_WINDOW(d->window), box);

	d->entry = gtk_entry_new();
	gtk_editable_set_text(GTK_EDITABLE(d->entry), name ? name : "");
	gtk_entry_set_activates_default(GTK_ENTRY(d->entry), TRUE);
	gtk_box_append(GTK_BOX(box), d->entry);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(box), buttons);

	cancel = gtk_button_new_with_label("Abbrechen");
	g_signal_connect(cancel, "clicked", G_CALLBACK(rename_dialog_cancel), d);
	gtk_box_append(GTK_BOX(buttons), cancel);

	ok = gtk_button_new_with_label("Ok");
	g_signal_connect(ok, "clicked", G_CALLBACK(rename_dialog_ok), d);
	gtk_box_append(GTK_BOX(buttons), ok);
	gtk_window_set_default_widget(GTK_WINDOW(d->window), ok);

	g_signal_connect(d->window, "destroy", G_CALLBACK(rename_dialog_destroyed), d);

	gtk_window_present(GTK_WINDOW(d->window));

	//ohne die Endung markieren (bei einem Verzeichnis den ganzen Namen)
	dot = name ? strrchr(name, '.') : NULL;
	gtk_widget_grab_focus(d->entry);
	if (dot && dot != name && !(sond_tvfm_item_get_item_type(d->item) ==
			SOND_TVFM_ITEM_TYPE_DIR && !sond_tvfm_item_get_sond_file_part(d->item)))
		gtk_editable_select_region(GTK_EDITABLE(d->entry), 0,
				(gint) g_utf8_pointer_to_offset(name, dot));
	else
		gtk_editable_select_region(GTK_EDITABLE(d->entry), 0, -1);
}

static void action_rename(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	rename_selected(SOND_TREEVIEWFM(data));
}

/* --- Löschen --- */

/* Löscht die Markierten. Ist ein Verzeichnis und etwas darin markiert, wird nur das
 * Verzeichnis gelöscht. Beim ersten Fehler ist Schluss (wie in GTK3). */
static void delete_selected(SondTreeviewFM *self) {
	SondTreeview *base = SOND_TREEVIEW(self);
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(self);
	GPtrArray *rows = sond_treeview_get_selected_rows(base);
	GHashTable *selected = g_hash_table_new(g_direct_hash, g_direct_equal);
	GError *error = NULL;

	for (guint i = 0; i < rows->len; i++) {
		gpointer item = gtk_tree_list_row_get_item(g_ptr_array_index(rows, i));

		if (item) {
			g_hash_table_add(selected, item);
			g_object_unref(item);
		}
	}

	for (guint i = 0; i < rows->len; i++) {
		GtkTreeListRow *row = g_ptr_array_index(rows, i);
		SondTVFMItem *item = gtk_tree_list_row_get_item(row);
		GtkTreeListRow *ancestor = gtk_tree_list_row_get_parent(row);
		gboolean covered = FALSE;
		gint res = 0;
		gint rc = 0;
		gpointer ctx = NULL;
		gchar *fs_rel = NULL;
		gboolean fs_is_dir = FALSE;
		gboolean fs_known = FALSE;

		//Vorfahr ebenfalls markiert: mit ihm erledigt
		while (ancestor && !covered) {
			gpointer ancestor_item = gtk_tree_list_row_get_item(ancestor);
			GtkTreeListRow *next = gtk_tree_list_row_get_parent(ancestor);

			covered = ancestor_item && g_hash_table_contains(selected, ancestor_item);
			if (ancestor_item)
				g_object_unref(ancestor_item);
			g_object_unref(ancestor);
			ancestor = next;
		}
		g_clear_object(&ancestor);
		if (covered || !item) {
			g_clear_object(&item);

			continue;
		}

		g_signal_emit(self, klass->signal_before_delete, 0, item, &error, &ctx, &res);
		if (res == 1) {
			g_object_unref(item);

			continue;
		}
		if (res == -1) {
			g_object_unref(item);
			break;
		}

		//Pfad vor dem Löschen merken (danach lässt sich der Eintrag nicht mehr abfragen)
		fs_known = sond_tvfm_item_fs_path(item, &fs_rel, &fs_is_dir);

		rc = sond_tvfm_item_delete(item, &error);
		g_signal_emit(self, klass->signal_after, 0, (rc == 0) ? TRUE : FALSE, ctx);
		if (rc) {
			g_free(fs_rel);
			g_object_unref(item);
			break;
		}

		if (fs_known && klass->fs_entry_deleted)
			klass->fs_entry_deleted(self, fs_rel, fs_is_dir);
		g_free(fs_rel);

		{
			SondLazyChildren *lazy = lazy_from_model(
					sond_treeview_row_get_container(base, row));

			if (lazy)
				lazy_remove(lazy, item);
		}
		g_object_unref(item);
	}

	if (error) {
		show_error(self, "Löschen nicht möglich", error->message);
		g_clear_error(&error);
	}

	g_hash_table_unref(selected);
	g_ptr_array_unref(rows);
}

static void delete_confirmed(GObject *source, GAsyncResult *result, gpointer user_data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(user_data);
	GError *error = NULL;
	gint button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);

	g_clear_error(&error);
	if (button == 0)
		delete_selected(self);

	g_object_unref(self);
}

static void action_delete(GSimpleAction *action, GVariant *parameter, gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	GPtrArray *items = sond_treeview_get_selected_items(SOND_TREEVIEW(self));
	GtkAlertDialog *dialog = NULL;
	gchar *text = NULL;
	const char *buttons[] = { "Löschen", "Abbrechen", NULL };

	(void) action;
	(void) parameter;

	if (items->len == 0) {
		g_ptr_array_unref(items);

		return;
	}

	if (!SOND_TREEVIEWFM_GET_CLASS(self)->confirm_delete) {
		g_ptr_array_unref(items);
		delete_selected(self);

		return;
	}

	text = items->len == 1 ?
			g_strdup_printf("\"%s\" löschen?", sond_tvfm_item_get_display_name(
					g_ptr_array_index(items, 0))) :
			g_strdup_printf("%u Einträge löschen?", items->len);
	g_ptr_array_unref(items);

	dialog = gtk_alert_dialog_new("%s", text);
	gtk_alert_dialog_set_detail(dialog, "Das lässt sich nicht rückgängig machen. "
			"Verzeichnisse werden mit ihrem Inhalt gelöscht.");
	gtk_alert_dialog_set_buttons(dialog, buttons);
	gtk_alert_dialog_set_default_button(dialog, 1);
	gtk_alert_dialog_set_cancel_button(dialog, 1);
	gtk_alert_dialog_set_modal(dialog, TRUE);
	gtk_alert_dialog_choose(dialog, parent_window(self), NULL, delete_confirmed,
			g_object_ref(self));
	g_object_unref(dialog);
	g_free(text);
}

/* ------------------------------------------------------------------------
 * Öffnen
 * --------------------------------------------------------------------- */

static gint default_open_item(SondTreeviewFM *self, SondTVFMItem *item,
		gboolean open_with, GError **error) {
	gboolean is_dir = FALSE;
	gchar *full = item_full_path(self, item, &is_dir);
	gboolean ok = FALSE;

	if (!full) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				"Einträge in Containern lassen sich in dieser Ansicht noch nicht öffnen");

		return -1;
	}

	ok = sond_open(full, open_with, error);
	g_free(full);

	return ok ? 0 : -1;
}

static void open_row(SondTreeviewFM *self, GtkTreeListRow *row, gboolean open_with) {
	SondTVFMItem *item = gtk_tree_list_row_get_item(row);
	GError *error = NULL;

	if (!item)
		return;

	if (sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_DIR) {
		//Doppelklick auf ein Verzeichnis: auf- bzw. zuklappen
		if (!open_with && gtk_tree_list_row_is_expandable(row))
			gtk_tree_list_row_set_expanded(row, !gtk_tree_list_row_get_expanded(row));
		g_object_unref(item);

		return;
	}

	if (SOND_TREEVIEWFM_GET_CLASS(self)->open_item &&
			SOND_TREEVIEWFM_GET_CLASS(self)->open_item(self, item, open_with, &error))
		show_error(self, "Fehler beim Öffnen", error ? error->message : "unbekannt");
	g_clear_error(&error);
	g_object_unref(item);
}

static void open_selected(SondTreeviewFM *self, gboolean open_with) {
	GPtrArray *rows = sond_treeview_get_selected_rows(SOND_TREEVIEW(self));

	if (rows->len == 1)
		open_row(self, g_ptr_array_index(rows, 0), open_with);
	g_ptr_array_unref(rows);
}

static void action_open(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	open_selected(SOND_TREEVIEWFM(data), FALSE);
}

static void action_open_with(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	open_selected(SOND_TREEVIEWFM(data), TRUE);
}

static void on_activate(GtkColumnView *column_view, guint position, gpointer data) {
	GListModel *model = G_LIST_MODEL(gtk_column_view_get_model(column_view));
	GtkTreeListRow *row = g_list_model_get_item(model, position);

	if (row) {
		open_row(SOND_TREEVIEWFM(data), row, FALSE);
		g_object_unref(row);
	}
}

/* ------------------------------------------------------------------------
 * Kopieren, Ausschneiden, Einfügen, Ziehen und Ablegen
 *
 * Quelle und Ziel sind Einträge des Dateisystems (auch SeaDrive); Inhalt von
 * Containern (ZIP, PDF, E-Mail) lässt sich noch nicht verschieben oder kopieren.
 * --------------------------------------------------------------------- */

typedef struct {
	GPtrArray *items; //SondTVFMItem (Referenzen)
	gboolean cut;
	GWeakRef view;
	gboolean view_init;
} FmClipboard;

static FmClipboard fm_clipboard = { NULL, FALSE, { { NULL } }, FALSE };

static void clipboard_clear(void) {
	if (fm_clipboard.items)
		g_ptr_array_unref(fm_clipboard.items);
	fm_clipboard.items = NULL;
	fm_clipboard.cut = FALSE;
}

static void clipboard_set(SondTreeviewFM *self, GPtrArray *items, gboolean cut) {
	if (!fm_clipboard.view_init) {
		g_weak_ref_init(&fm_clipboard.view, NULL);
		fm_clipboard.view_init = TRUE;
	}

	clipboard_clear();
	fm_clipboard.items = items;
	fm_clipboard.cut = cut;
	g_weak_ref_set(&fm_clipboard.view, self);
}

/* Die markierten Einträge ohne solche, deren Vorfahr ebenfalls markiert ist
 * (Referenzen, g_ptr_array_unref()) */
static GPtrArray* selected_top_items(SondTreeviewFM *self) {
	GPtrArray *rows = sond_treeview_get_selected_rows(SOND_TREEVIEW(self));
	GPtrArray *result = g_ptr_array_new_with_free_func(g_object_unref);
	GHashTable *selected = g_hash_table_new(g_direct_hash, g_direct_equal);

	for (guint i = 0; i < rows->len; i++) {
		gpointer item = gtk_tree_list_row_get_item(g_ptr_array_index(rows, i));

		if (item) {
			g_hash_table_add(selected, item);
			g_object_unref(item);
		}
	}

	for (guint i = 0; i < rows->len; i++) {
		GtkTreeListRow *row = g_ptr_array_index(rows, i);
		SondTVFMItem *item = gtk_tree_list_row_get_item(row);
		GtkTreeListRow *ancestor = gtk_tree_list_row_get_parent(row);
		gboolean covered = FALSE;

		while (ancestor && !covered) {
			gpointer ancestor_item = gtk_tree_list_row_get_item(ancestor);
			GtkTreeListRow *next = gtk_tree_list_row_get_parent(ancestor);

			covered = ancestor_item && g_hash_table_contains(selected, ancestor_item);
			if (ancestor_item)
				g_object_unref(ancestor_item);
			g_object_unref(ancestor);
			ancestor = next;
		}
		g_clear_object(&ancestor);

		if (item && !covered)
			g_ptr_array_add(result, item);
		else
			g_clear_object(&item);
	}

	g_hash_table_unref(selected);
	g_ptr_array_unref(rows);

	return result;
}

static gboolean items_all_in_fs(GPtrArray *items) {
	for (guint i = 0; i < items->len; i++)
		if (!sond_tvfm_item_fs_path(g_ptr_array_index(items, i), NULL, NULL))
			return FALSE;

	return items->len > 0;
}

static void copy_or_cut(SondTreeviewFM *self, gboolean cut) {
	GPtrArray *items = selected_top_items(self);

	if (!items_all_in_fs(items)) {
		if (items->len)
			show_error(self, cut ? "Ausschneiden" : "Kopieren",
					"Einträge in Containern lassen sich in dieser Ansicht noch nicht "
					"kopieren oder verschieben.");
		g_ptr_array_unref(items);

		return;
	}

	clipboard_set(self, items, cut);
}

/* Fügt item unter parent ein (Kopie oder Verschieben); bei vergebenem Namen wird
 * ein Zähler angehängt. 0 = ok (*base_inserted: endgültiger Name), 1 = übersprungen,
 * -1 = Fehler. */
static gint paste_one(SondTreeviewFM *self, SondTVFMItem *item, SondTVFMItem *parent,
		gboolean cut, gchar **base_inserted, GError **error) {
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(self);
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
	g_autofree gchar *base = NULL;
	gchar const *dot = NULL;
	gboolean has_ext = FALSE;
	gint max_tries = 100;

	base = g_strdup(item_priv->display_name ? item_priv->display_name :
			sond_tvfm_item_get_basename(item));
	if (!base)
		base = g_strdup("UNNAMED");

	dot = strrchr(base, '.');
	has_ext = !item_priv->path_or_section && dot && dot != base;

	for (gint i = 0; i < max_tries; i++) {
		g_autofree gchar *trial = (i == 0) ? g_strdup(base) :
				sond_filename_add_counter(base, (guint) i, has_ext);
		gint rc = 0;

		if (cut)
			rc = sond_tvfm_item_move(item, parent, trial, 0, error);
		else {
			//sond_tvfm_item_copy() löst selbst kein Signal aus (anders als _move())
			gint res = 0;

			g_signal_emit(self, klass->signal_before_insert, 0, item, parent, trial, 0,
					error, &res);
			if (res)
				rc = -1;
			else {
				rc = sond_tvfm_item_copy(item, parent, trial, 0, error);
				g_signal_emit(self, klass->signal_after_insert, 0,
						(rc == 0) ? TRUE : FALSE);
			}
		}

		if (rc == -1) {
			if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
				g_clear_error(error);

				continue; //nächster Zähler
			}

			return -1;
		}
		if (rc == 1)
			return 1;

		*base_inserted = g_strdup(trial);

		return 0;
	}

	g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
			"Kein eindeutiger Zielname nach %d Versuchen", max_tries);

	return -1;
}

//Neuer Eintrag für das eingefügte Objekt (Referenz) am Zielpfad path_new
static SondTVFMItem* pasted_item_new(SondTreeviewFM *self, SondTVFMItem *item,
		gchar const *path_new) {
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
	SondFilePart *sfp_new = NULL;
	SondTVFMItem *created = NULL;

	if (item_priv->sond_file_part && !item_priv->path_or_section) {
		SondFilePart *sfp = item_priv->sond_file_part;

		sfp_new = g_object_new(G_OBJECT_TYPE(sfp), NULL);
		sond_file_part_set_path(sfp_new, path_new);
		if (SOND_IS_FILE_PART_LEAF(sfp_new))
			sond_file_part_leaf_set_mime_type(SOND_FILE_PART_LEAF(sfp_new),
					sond_file_part_leaf_get_mime_type(SOND_FILE_PART_LEAF(sfp)));
		sond_file_part_set_has_children(sfp_new, sond_file_part_get_has_children(sfp));
		sond_file_part_set_lock_entry(sfp_new, sond_file_part_get_lock_entry(sfp));
		sond_file_part_set_lock_content(sfp_new, sond_file_part_get_lock_content(sfp));
	}

	created = sond_tvfm_item_create(self, sfp_new,
			item_priv->path_or_section ? path_new : NULL);
	if (sfp_new)
		g_object_unref(sfp_new);

	if (item_priv->path_or_section)
		sond_tvfm_item_get_priv(created)->has_children = item_priv->has_children;

	return created;
}

typedef struct {
	SondTVFMItem *item;
	gchar *old_rel;
} MovedSource;

static void moved_source_free(gpointer data) {
	MovedSource *m = data;

	g_free(m->old_rel);
	g_free(m);
}

static gboolean match_moved_source(gpointer item, gpointer user_data) {
	MovedSource *m = user_data;
	gchar *rel = NULL;
	gboolean match = (item == (gpointer) m->item);

	if (!match && sond_tvfm_item_fs_path(SOND_TVFM_ITEM(item), &rel, NULL))
		match = !g_strcmp0(rel, m->old_rel);
	g_free(rel);

	return match;
}

/* Fügt items in das Verzeichnis ein, das zur Zeile row gehört: deren Inhalt
 * (child) oder das Verzeichnis, in dem sie steht. cut = verschieben, sonst kopieren. */
static void paste_items(SondTreeviewFM *self, GPtrArray *items, gboolean cut,
		GtkTreeListRow *row, gboolean child) {
	SondTreeview *base = SOND_TREEVIEW(self);
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(self);
	SondTVFMItem *parent = NULL;
	gchar const *parent_rel = NULL;
	GPtrArray *created = g_ptr_array_new_with_free_func(g_object_unref);
	GPtrArray *moved = g_ptr_array_new_with_free_func(moved_source_free);
	GPtrArray *failed = g_ptr_array_new_with_free_func(g_free);
	GError *error = NULL;
	gboolean eingang_changed = FALSE;

	parent = child ? gtk_tree_list_row_get_item(row) : parent_item_of_row(self, row);
	if (!parent || sond_tvfm_item_get_item_type(parent) != SOND_TVFM_ITEM_TYPE_DIR ||
			sond_tvfm_item_get_sond_file_part(parent)) {
		show_error(self, "Einfügen",
				"In diesen Eintrag lässt sich nichts einfügen (nur Verzeichnisse des "
				"Dateisystems).");
		goto out;
	}
	parent_rel = sond_tvfm_item_get_path_or_section(parent);

	for (guint i = 0; i < items->len; i++) {
		SondTVFMItem *item = g_ptr_array_index(items, i);
		SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
		g_autofree gchar *fs_old = NULL;
		g_autofree gchar *path_old = NULL;
		g_autofree gchar *base_inserted = NULL;
		g_autofree gchar *path_new = NULL;
		gboolean is_dir = FALSE;
		gint rc = 0;

		if (!sond_tvfm_item_fs_path(item, &fs_old, &is_dir)) {
			g_set_error(&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
					"\"%s\" liegt nicht im Dateisystem", item_priv->display_name ?
							item_priv->display_name : "?");
			break;
		}

		//ein Verzeichnis nicht in sich selbst
		if (is_dir && parent_rel && (!g_strcmp0(parent_rel, fs_old) ||
				(g_str_has_prefix(parent_rel, fs_old) &&
						parent_rel[strlen(fs_old)] == '/'))) {
			g_set_error(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
					"\"%s\" lässt sich nicht in sich selbst einfügen", fs_old);
			break;
		}

		//Verschieben in das eigene Verzeichnis: nichts zu tun
		if (cut) {
			g_autofree gchar *dir = g_path_get_dirname(fs_old);

			if (!g_strcmp0(g_strcmp0(dir, ".") ? dir : "", parent_rel ? parent_rel : ""))
				continue;
		}

		path_old = g_strdup(item_priv->path_or_section);

		rc = paste_one(self, item, parent, cut, &base_inserted, &error);
		if (rc == -1) {
			//nicht geladener SeaDrive-Platzhalter, gesperrte Datei: merken, weiter
			if (sond_error_is_cloud(error) ||
					g_error_matches(error, G_IO_ERROR, G_IO_ERROR_BUSY)) {
				g_ptr_array_add(failed, g_strdup(item_priv->display_name ?
						item_priv->display_name : "(ohne Namen)"));
				g_clear_error(&error);

				continue;
			}

			break;
		}
		if (rc == 1)
			continue;

		path_new = parent_rel ? g_strconcat(parent_rel, "/", base_inserted, NULL) :
				g_strdup(base_inserted);

		//Zusatzangaben (Eingangsstempel) nachführen
		if (klass->fs_entry_moved &&
				klass->fs_entry_moved(self, fs_old, path_new, is_dir, !cut))
			eingang_changed = TRUE;

		//Pfade bereits geöffneter Dateien im verschobenen Verzeichnis nachführen
		if (cut && path_old)
			sond_tvfm_item_adjust_sfps_in_dir(item_priv->sond_file_part, NULL,
					path_old, path_new);

		g_ptr_array_add(created, pasted_item_new(self, item, path_new));

		if (cut) {
			MovedSource *m = g_new0(MovedSource, 1);

			m->item = item;
			m->old_rel = g_strdup(fs_old);
			g_ptr_array_add(moved, m);
		}
	}

	//Anzeige: neue Einträge in die Liste des Ziels
	if (created->len) {
		if (child) {
			SondTVFMItemPrivate *parent_priv = sond_tvfm_item_get_priv(parent);
			GtkTreeListRow *parent_row = g_object_ref(row);

			if (!parent_priv->has_children) {
				//das Verzeichnis war leer, jetzt ist es aufklappbar: Zeile neu aufbauen
				SondLazyChildren *lazy = lazy_from_model(
						sond_treeview_row_get_container(base, row));

				parent_priv->has_children = TRUE;
				if (lazy) {
					lazy_touch(lazy, parent);
					g_object_unref(parent_row);
					parent_row = sond_treeview_find_row(base, match_same, parent, NULL);
				}
			}

			if (parent_row) {
				if (gtk_tree_list_row_get_expanded(parent_row)) {
					SondLazyChildren *lazy = lazy_from_model(
							gtk_tree_list_row_get_children(parent_row));

					for (guint i = 0; lazy && i < created->len; i++)
						lazy_insert(lazy, g_ptr_array_index(created, i));
				} else
					gtk_tree_list_row_set_expanded(parent_row, TRUE); //liest von der Platte
				g_object_unref(parent_row);
			}
		} else {
			SondLazyChildren *lazy = lazy_from_model(
					sond_treeview_row_get_container(base, row));

			for (guint i = 0; lazy && i < created->len; i++)
				lazy_insert(lazy, g_ptr_array_index(created, i));
		}
		sond_treeview_select_item(base, g_ptr_array_index(created, created->len - 1));
	}

	//Quellen beim Verschieben aus der Anzeige nehmen
	for (guint i = 0; i < moved->len; i++) {
		MovedSource *m = g_ptr_array_index(moved, i);
		GtkTreeListRow *source_row = sond_treeview_find_row(base, match_moved_source, m,
				NULL);

		if (source_row) {
			SondLazyChildren *lazy = lazy_from_model(
					sond_treeview_row_get_container(base, source_row));

			if (lazy) {
				SondTVFMItem *shown = gtk_tree_list_row_get_item(source_row);

				lazy_remove(lazy, shown);
				g_object_unref(shown);
			}
			g_object_unref(source_row);
		}
	}

	if (eingang_changed)
		sond_treeviewfm_refresh_status(self);

out:
	if (error || failed->len) {
		GString *text = g_string_new(error ? error->message : "");

		if (failed->len) {
			if (text->len)
				g_string_append(text, "\n\n");
			g_string_append_printf(text, "%u %s konnten nicht %s werden "
					"(nicht geladen oder in Benutzung):", failed->len,
					failed->len == 1 ? "Eintrag" : "Einträge",
					cut ? "verschoben" : "kopiert");
			for (guint i = 0; i < MIN(failed->len, 5); i++)
				g_string_append_printf(text, "\n%s",
						(gchar const*) g_ptr_array_index(failed, i));
			if (failed->len > 5)
				g_string_append_printf(text, "\n... und %u weitere", failed->len - 5);
		}
		show_error(self, cut ? "Verschieben" : "Einfügen", text->str);
		g_string_free(text, TRUE);
	}

	g_clear_error(&error);
	g_clear_object(&parent);
	g_ptr_array_unref(created);
	g_ptr_array_unref(moved);
	g_ptr_array_unref(failed);
}

static void paste_clipboard(SondTreeviewFM *self, gboolean child_forced, gboolean child) {
	GPtrArray *rows = sond_treeview_get_selected_rows(SOND_TREEVIEW(self));
	SondTreeviewFM *source = fm_clipboard.view_init ?
			g_weak_ref_get(&fm_clipboard.view) : NULL;

	if (!fm_clipboard.items || !fm_clipboard.items->len) {
		//nichts zu tun
	} else if (source != self) {
		show_error(self, "Einfügen", "Die Zwischenablage stammt aus einer anderen "
				"Ansicht.");
	} else if (rows->len != 1) {
		show_error(self, "Einfügen", "Bitte genau einen Eintrag als Ziel markieren.");
	} else {
		GtkTreeListRow *row = g_ptr_array_index(rows, 0);
		GPtrArray *items = g_ptr_array_ref(fm_clipboard.items);
		gboolean cut = fm_clipboard.cut;

		if (!child_forced) {
			//ein markiertes Verzeichnis: hinein, sonst auf gleicher Ebene
			SondTVFMItem *selected = gtk_tree_list_row_get_item(row);

			child = selected &&
					sond_tvfm_item_get_item_type(selected) == SOND_TVFM_ITEM_TYPE_DIR &&
					!sond_tvfm_item_get_sond_file_part(selected);
			g_clear_object(&selected);
		}

		paste_items(self, items, cut, row, child);
		g_ptr_array_unref(items);
		if (cut)
			clipboard_clear();
	}

	g_clear_object(&source);
	g_ptr_array_unref(rows);
}

static void action_copy(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	copy_or_cut(SOND_TREEVIEWFM(data), FALSE);
}

static void action_cut(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	copy_or_cut(SOND_TREEVIEWFM(data), TRUE);
}

static void action_paste(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	paste_clipboard(SOND_TREEVIEWFM(data), FALSE, FALSE);
}

static void action_paste_same(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	paste_clipboard(SOND_TREEVIEWFM(data), TRUE, FALSE);
}

static void action_paste_child(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	paste_clipboard(SOND_TREEVIEWFM(data), TRUE, TRUE);
}

/* --- Ziehen und Ablegen (innerhalb einer Ansicht): Verschieben, mit Strg Kopieren --- */

static GPtrArray *dnd_items = NULL;
static SondTreeviewFM *dnd_view = NULL;
static gboolean dnd_copy = FALSE; //Strg beim Ziehen gedrückt

#define DND_LISTITEM_KEY "sond-dnd-listitem"

static SondTreeviewFM* dnd_view_of(GtkEventController *controller) {
	GtkWidget *widget = gtk_event_controller_get_widget(controller);

	return SOND_TREEVIEWFM(gtk_widget_get_ancestor(widget, SOND_TYPE_TREEVIEWFM));
}

static GdkContentProvider* dnd_prepare(GtkDragSource *source, double x, double y,
		gpointer data) {
	GtkListItem *listitem = g_object_get_data(G_OBJECT(source), DND_LISTITEM_KEY);
	SondTreeviewFM *view = dnd_view_of(GTK_EVENT_CONTROLLER(source));
	SondTVFMItem *item = listitem ? sond_treeview_listitem_get_item(listitem) : NULL;
	GPtrArray *items = NULL;

	(void) x;
	(void) y;
	(void) data;

	if (!view || !item) {
		g_clear_object(&item);

		return NULL;
	}

	if (gtk_list_item_get_selected(listitem))
		items = selected_top_items(view);
	else {
		items = g_ptr_array_new_with_free_func(g_object_unref);
		g_ptr_array_add(items, g_object_ref(item));
	}
	g_object_unref(item);

	if (!items_all_in_fs(items)) {
		g_ptr_array_unref(items);

		return NULL;
	}

	if (dnd_items)
		g_ptr_array_unref(dnd_items);
	dnd_items = items;
	dnd_view = view;

	//Nutzlast: die Einträge (GPtrArray von SondTVFMItem); Ziele anderer Ansichten
	//(Zond: Anbinden im Inhaltsbaum) nehmen denselben Typ an
	return gdk_content_provider_new_typed(G_TYPE_PTR_ARRAY, items);
}

static void dnd_begin(GtkDragSource *source, GdkDrag *drag, gpointer data) {
	GtkWidget *label = NULL;
	gchar *text = NULL;

	(void) source;
	(void) data;

	if (!dnd_items || !dnd_items->len)
		return;

	text = dnd_items->len == 1 ?
			g_strdup(sond_tvfm_item_get_display_name(g_ptr_array_index(dnd_items, 0))) :
			g_strdup_printf("%u Einträge", dnd_items->len);
	label = gtk_label_new(text);
	gtk_widget_add_css_class(label, "app-notification");
	gtk_drag_icon_set_child(GTK_DRAG_ICON(gtk_drag_icon_get_for_drag(drag)), label);
	g_free(text);
}

static void dnd_end(GtkDragSource *source, GdkDrag *drag, gboolean delete_data,
		gpointer data) {
	(void) source;
	(void) drag;
	(void) delete_data;
	(void) data;

	if (dnd_items)
		g_ptr_array_unref(dnd_items);
	dnd_items = NULL;
	dnd_view = NULL;
}

/* Ist Strg gedrückt? Unter Windows läuft der Ziehvorgang in der Schleife von OLE,
 * GTK erhält dabei keine Tastaturereignisse: Ereigniszustand und Tastaturgerät
 * melden Strg nie (gemessen), nur das System weiß es. */
static gboolean ctrl_pressed(GtkEventController *controller) {
#ifdef G_OS_WIN32
	(void) controller;

	return (GetAsyncKeyState(VK_CONTROL) & 0x8000) ? TRUE : FALSE;
#else
	return (gtk_event_controller_get_current_event_state(controller) &
			GDK_CONTROL_MASK) ? TRUE : FALSE;
#endif
}

static GdkDragAction dnd_motion(GtkDropTarget *target, double x, double y,
		gpointer data) {
	(void) x;
	(void) y;
	(void) data;

	dnd_copy = ctrl_pressed(GTK_EVENT_CONTROLLER(target));

	return dnd_copy ? GDK_ACTION_COPY : GDK_ACTION_MOVE;
}

typedef struct {
	SondTreeviewFM *view;   //Referenz
	GPtrArray *items;       //Referenz
	GtkTreeListRow *row;    //Referenz
	gboolean cut;
	gboolean child;
} DropJob;

//erst nach Ende des Ziehvorgangs ausführen: beim Verschieben verschwindet die
//Quellzeile, solange der Ziehvorgang noch läuft
static gboolean dnd_drop_idle(gpointer data) {
	DropJob *job = data;

	paste_items(job->view, job->items, job->cut, job->row, job->child);
	g_object_unref(job->view);
	g_ptr_array_unref(job->items);
	g_object_unref(job->row);
	g_free(job);

	return G_SOURCE_REMOVE;
}

static gboolean dnd_drop(GtkDropTarget *target, const GValue *value, double x, double y,
		gpointer data) {
	GtkListItem *listitem = g_object_get_data(G_OBJECT(target), DND_LISTITEM_KEY);
	SondTreeviewFM *view = dnd_view_of(GTK_EVENT_CONTROLLER(target));
	GtkTreeListRow *row = listitem ? gtk_list_item_get_item(listitem) : NULL;
	GPtrArray *items = G_VALUE_HOLDS(value, G_TYPE_PTR_ARRAY) ?
			g_value_get_boxed(value) : NULL;
	SondTVFMItem *item = NULL;
	DropJob *job = NULL;
	GdkDrop *drop = gtk_drop_target_get_current_drop(target);
	GdkDrag *drag = drop ? gdk_drop_get_drag(drop) : NULL;
	gboolean copy = dnd_copy;

	(void) x;
	(void) y;
	(void) data;

	if (!view || !row || !items || dnd_view != view)
		return FALSE;

	if ((drag && gdk_drag_get_selected_action(drag) == GDK_ACTION_COPY) ||
			ctrl_pressed(GTK_EVENT_CONTROLLER(target)))
		copy = TRUE;

	item = gtk_tree_list_row_get_item(row);
	job = g_new0(DropJob, 1);
	job->view = g_object_ref(view);
	job->items = g_ptr_array_ref(items);
	job->row = g_object_ref(row);
	job->cut = !copy;
	job->child = item && sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_DIR &&
			!sond_tvfm_item_get_sond_file_part(item);
	g_clear_object(&item);
	g_idle_add(dnd_drop_idle, job);

	return TRUE;
}

static void dnd_attach(GtkWidget *cell, GtkListItem *listitem) {
	GtkDragSource *source = gtk_drag_source_new();
	GtkDropTarget *target = gtk_drop_target_new(G_TYPE_PTR_ARRAY,
			GDK_ACTION_MOVE | GDK_ACTION_COPY);

	gtk_drag_source_set_actions(source, GDK_ACTION_MOVE | GDK_ACTION_COPY);
	g_object_set_data(G_OBJECT(source), DND_LISTITEM_KEY, listitem);
	g_signal_connect(source, "prepare", G_CALLBACK(dnd_prepare), NULL);
	g_signal_connect(source, "drag-begin", G_CALLBACK(dnd_begin), NULL);
	g_signal_connect(source, "drag-end", G_CALLBACK(dnd_end), NULL);
	gtk_widget_add_controller(cell, GTK_EVENT_CONTROLLER(source));

	g_object_set_data(G_OBJECT(target), DND_LISTITEM_KEY, listitem);
	g_signal_connect(target, "motion", G_CALLBACK(dnd_motion), NULL);
	g_signal_connect(target, "drop", G_CALLBACK(dnd_drop), NULL);
	gtk_widget_add_controller(cell, GTK_EVENT_CONTROLLER(target));
}

/* ------------------------------------------------------------------------
 * Kontextmenü
 * --------------------------------------------------------------------- */

static void action_pin(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	sond_treeviewfm_pin_selection(SOND_TREEVIEWFM(data), STVFM_PIN_STATE_PINNED);
}

static void action_unpin(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	sond_treeviewfm_pin_selection(SOND_TREEVIEWFM(data),
			STVFM_PIN_STATE_UNSPECIFIED);
}

static void action_free(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	sond_treeviewfm_pin_selection(SOND_TREEVIEWFM(data), STVFM_PIN_STATE_UNPINNED);
}

static void action_refresh(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	sond_treeviewfm_refresh_status(SOND_TREEVIEWFM(data));
}

/* ------------------------------------------------------------------------
 * Klasse
 * --------------------------------------------------------------------- */

static void sond_treeviewfm_finalize(GObject *object) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(object);
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
	SondFilePartClass *sfp_class = g_type_class_peek_static(SOND_TYPE_FILE_PART);

	//path_root liegt auf Klassenebene (s. sond_fileparts.h): nur freigeben, wenn es unser Pfad ist
	if (sfp_class && priv->root && !g_strcmp0(sfp_class->path_root, priv->root)) {
		g_free(sfp_class->path_root);
		sfp_class->path_root = NULL;
	}
	root_monitor_stop(priv);
#ifdef G_OS_WIN32
	if (priv->watcher) {
		g_signal_handlers_disconnect_by_data(priv->watcher, object);
		sond_seadrive_watcher_stop_async(priv->watcher);
		g_clear_object(&priv->watcher);
	}
#endif
	g_free(priv->root);

	G_OBJECT_CLASS(sond_treeviewfm_parent_class)->finalize(object);
}

static void register_icons(void) {
	static gboolean registered = FALSE;

	if (registered)
		return;
	registered = TRUE;

	//die eigenen Symbole (pdf, pdf-folder, ...) wie in Zond als Ressource im Icon-Theme
	g_resources_register(icons_get_resource());
	gtk_icon_theme_add_resource_path(
			gtk_icon_theme_get_for_display(gdk_display_get_default()), "/icons");
}

/* Standard: Name (mit Symbol), Geändert, Größe */
static void default_setup_columns(SondTreeviewFM *self) {
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

	sorter = GTK_SORTER(gtk_custom_sorter_new(compare_date, NULL, NULL));
	sond_treeview_add_column(base, "Geändert", text_factory(CELL_DATE), sorter, FALSE);

	sorter = GTK_SORTER(gtk_custom_sorter_new(compare_size, NULL, NULL));
	sond_treeview_add_column(base, "Größe", text_factory(CELL_SIZE), sorter, FALSE);

	gtk_column_view_sort_by_column(sond_treeview_get_column_view(base), column,
			GTK_SORT_ASCENDING);
}

/* Standard: Offline-Menü (SeaDrive), das auch Zond verwendet */
static void default_setup_menu(SondTreeviewFM *self) {
	SondTreeview *base = SOND_TREEVIEW(self);
	GMenu *menu = sond_treeview_get_menu(base);
	GMenu *section = g_menu_new();
	GMenu *edit = g_menu_new();
	GMenu *new_dir = g_menu_new();
	GMenu *open_sec = g_menu_new();
	GMenu *paste_sub = g_menu_new();
	GtkEventController *shortcuts = NULL;
	static const GActionEntry entries[] = {
		{ "open", action_open, NULL, NULL, NULL },
		{ "open-with", action_open_with, NULL, NULL, NULL },
		{ "copy", action_copy, NULL, NULL, NULL },
		{ "cut", action_cut, NULL, NULL, NULL },
		{ "paste", action_paste, NULL, NULL, NULL },
		{ "paste-same", action_paste_same, NULL, NULL, NULL },
		{ "paste-child", action_paste_child, NULL, NULL, NULL },
		{ "mkdir-same", action_mkdir_same, NULL, NULL, NULL },
		{ "mkdir-child", action_mkdir_child, NULL, NULL, NULL },
		{ "rename", action_rename, NULL, NULL, NULL },
		{ "delete", action_delete, NULL, NULL, NULL },
		{ "pin", action_pin, NULL, NULL, NULL },
		{ "unpin", action_unpin, NULL, NULL, NULL },
		{ "free", action_free, NULL, NULL, NULL },
		{ "refresh", action_refresh, NULL, NULL, NULL }
	};

	g_action_map_add_action_entries(sond_treeview_get_action_map(base), entries,
			G_N_ELEMENTS(entries), self);
	//Öffnen
	g_menu_append(open_sec, "Öffnen", "tv.open");
	g_menu_append(open_sec, "Öffnen mit", "tv.open-with");
	g_menu_append_section(menu, NULL, G_MENU_MODEL(open_sec));
	g_object_unref(open_sec);

	//Bearbeiten
	g_menu_append(edit, "Kopieren", "tv.copy");
	g_menu_append(edit, "Ausschneiden", "tv.cut");
	g_menu_append(paste_sub, "Gleiche Ebene", "tv.paste-same");
	g_menu_append(paste_sub, "Unterebene", "tv.paste-child");
	g_menu_append_submenu(edit, "Einfügen", G_MENU_MODEL(paste_sub));
	g_object_unref(paste_sub);
	g_menu_append(new_dir, "Gleiche Ebene", "tv.mkdir-same");
	g_menu_append(new_dir, "Unterebene", "tv.mkdir-child");
	g_menu_append_submenu(edit, "Neues Verzeichnis", G_MENU_MODEL(new_dir));
	g_menu_append(edit, "Umbenennen", "tv.rename");
	g_menu_append(edit, "Löschen", "tv.delete");
	g_menu_append_section(menu, NULL, G_MENU_MODEL(edit));
	g_object_unref(new_dir);
	g_object_unref(edit);

	//Tastenkürzel: F2 umbenennen, Entf löschen
	shortcuts = gtk_shortcut_controller_new();
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
			gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_F2, 0),
					gtk_named_action_new("tv.rename")));
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
			gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Delete, 0),
					gtk_named_action_new("tv.delete")));
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
			gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_c, GDK_CONTROL_MASK),
					gtk_named_action_new("tv.copy")));
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
			gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_x, GDK_CONTROL_MASK),
					gtk_named_action_new("tv.cut")));
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
			gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_v, GDK_CONTROL_MASK),
					gtk_named_action_new("tv.paste")));
	gtk_widget_add_controller(GTK_WIDGET(self), shortcuts);

	g_menu_append(menu, "Immer offline halten", "tv.pin");
	g_menu_append(menu, "Offline-Haltung aufheben", "tv.unpin");
	g_menu_append(menu, "Platz freigeben (nur online)", "tv.free");
	g_menu_append(section, "Anzeige aktualisieren", "tv.refresh");
	g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
	g_object_unref(section);
}

/* Spalten und Menü erst hier aufbauen, damit Unterklassen (deren Vfuncs schon
 * stehen) sie überschreiben oder ergänzen können */
static void sond_treeviewfm_constructed(GObject *object) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(object);
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(self);

	G_OBJECT_CLASS(sond_treeviewfm_parent_class)->constructed(object);

	register_icons();

	g_signal_connect(sond_treeview_get_column_view(SOND_TREEVIEW(self)), "activate",
			G_CALLBACK(on_activate), self);

	if (klass->setup_columns)
		klass->setup_columns(self);
	if (klass->setup_menu)
		klass->setup_menu(self);

	update_pin_actions(self);
}

static void sond_treeviewfm_class_init(SondTreeviewFMClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_treeviewfm_finalize;
	G_OBJECT_CLASS(klass)->constructed = sond_treeviewfm_constructed;

	//SondFilePart braucht seine Klasse (path_root, geöffnete Dateien) schon vor der
	//ersten Instanz; die Referenz bleibt für die Laufzeit des Programms bestehen
	g_type_class_ref(SOND_TYPE_FILE_PART);
	SOND_TREEVIEW_CLASS(klass)->create_children = fm_create_children;

	klass->setup_columns = default_setup_columns;
	klass->setup_menu = default_setup_menu;
	klass->file_badge = default_file_badge;
	klass->dir_status = default_dir_status;
	klass->index_status = default_index_status;
	klass->get_section_index_status = NULL;
	klass->has_sections = NULL;
	klass->load_sections = NULL;
	klass->delete_section = NULL;
	klass->fs_entry_deleted = NULL;
	klass->fs_entry_renamed = NULL;
	klass->fs_entry_moved = NULL;
	klass->open_item = default_open_item;
	klass->confirm_delete = TRUE;

	//der Pin-Zustand einiger Einträge hat sich geändert: Anzeige neu lesen
	signals[SIGNAL_PINS_CHANGED] = g_signal_new("pins-changed",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 0);

	//die Zähler "wird heruntergeladen" und "nicht synchron" im SeaDrive-Laufwerk
	//haben sich geändert (beim Schließen: 0, 0)
	signals[SIGNAL_SEADRIVE_STATUS] = g_signal_new("seadrive-status",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	//Signale vor und nach Änderungen (Item-Schicht über SondTVFMOwner)
	klass->signal_before_move = g_signal_new("before-move",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_INT, 6,
			SOND_TYPE_TVFM_ITEM,
			SOND_TYPE_TVFM_ITEM,
			G_TYPE_STRING,
			G_TYPE_INT,
			G_TYPE_POINTER,   /* GError** */
			G_TYPE_POINTER);  /* Transaktions-Kontext (out) */

	klass->signal_before_insert = g_signal_new("before-insert",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_INT, 5,
			SOND_TYPE_TVFM_ITEM,
			SOND_TYPE_TVFM_ITEM,
			G_TYPE_STRING,
			G_TYPE_INT,
			G_TYPE_POINTER);

	klass->signal_before_delete = g_signal_new("before-delete",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_INT, 3,
			SOND_TYPE_TVFM_ITEM,
			G_TYPE_POINTER,   /* GError** */
			G_TYPE_POINTER);  /* Transaktions-Kontext (out) */

	klass->signal_after = g_signal_new("after",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 2,
			G_TYPE_BOOLEAN,
			G_TYPE_POINTER);  /* der von before-* gelieferte Kontext */

	klass->signal_after_insert = g_signal_new("after-insert",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 1,
			G_TYPE_BOOLEAN);
}

static void sond_treeviewfm_init(SondTreeviewFM *self) {
	(void) self;
}

GtkWidget* sond_treeviewfm_new(void) {
	return g_object_new(SOND_TYPE_TREEVIEWFM, NULL);
}
