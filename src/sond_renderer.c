/*
 * renderer.c
 *
 *  Created on: 14.12.2025
 *      Author: pkrieger
 *
 * Viewer-Fenster für gerenderte Dokumente (Zoom, Pan, Suche, Navigation, Druck). Läuft unter GTK3
 * und GTK4 aus derselben Quelle (sond_gtk_compat.h). Das Rendern selbst steckt in sond_render_doc.c.
 */

#include "sond_renderer.h"

#include <gtk/gtk.h>
#include <cairo.h>
#include <string.h>
#include <stdio.h>

#include "sond_gtk_compat.h"
#include "sond_render_doc.h"
#include "sond_log_and_error.h"
#include "sond_fileparts.h"
#include "sond_file_helper.h"
#include "sond_tmpfiles.h"

typedef struct {
    cairo_surface_t *original_surface;
    int original_width;
    int original_height;

    double zoom_level;
    gboolean fullscreen;

    GtkWidget *window;
    GtkWidget *image;
    GtkWidget *scrolled;
    GtkWidget *toolbar;
    GtkWidget *zoom_label;
    GtkWidget *search_entry;
    GtkWidget *statusbar;

    char *searchable_text;
    GList *search_results;
    int current_search_index;

    /* Pan mit der mittleren Maustaste */
    double pan_start_h;
    double pan_start_v;

    /* Navigation */
    SondFilePart *sfp;          /* aktuelle Datei, NULL wenn standalone */
    GtkWidget *btn_prev;
    GtkWidget *btn_next;
    GtkWidget *nav_label;
} SurfaceViewer;

// Forward declarations
static void update_display(SurfaceViewer *viewer);
static void update_statusbar(SurfaceViewer *viewer, const char *message);

// === NAVIGATION ===

/*
 * Liefert alle navigierbaren Geschwister (SondFilePartLeaf) im gleichen
 * virtuellen Verzeichnis wie sfp, geordnet wie im Array.
 * Gibt NULL zurück wenn sfp NULL ist.
 */
static GPtrArray* get_siblings(SondFilePart *sfp) {
    if (!sfp) return NULL;

    SondFilePart *parent = sond_file_part_get_parent(sfp);
    GPtrArray *all = sond_file_part_get_arr_opened_files(parent);
    if (!all || all->len == 0) return NULL;

    GPtrArray *siblings = g_ptr_array_new();
    for (guint i = 0; i < all->len; i++) {
        SondFilePart *s = g_ptr_array_index(all, i);
        /* Nur Leafs, die denselben Parent haben */
        if (SOND_IS_FILE_PART_LEAF(s) &&
            sond_file_part_get_parent(s) == parent)
            g_ptr_array_add(siblings, s);
    }
    return siblings;
}

static gint find_sfp_index(GPtrArray *arr, SondFilePart *sfp) {
    for (guint i = 0; i < arr->len; i++)
        if (g_ptr_array_index(arr, i) == sfp)
            return (gint)i;
    return -1;
}

static void update_nav_state(SurfaceViewer *viewer) {
    if (!viewer->sfp || !viewer->btn_prev) return;

    GPtrArray *siblings = get_siblings(viewer->sfp);
    if (!siblings || siblings->len <= 1) {
        gtk_widget_set_sensitive(viewer->btn_prev, FALSE);
        gtk_widget_set_sensitive(viewer->btn_next, FALSE);
        if (viewer->nav_label)
            gtk_label_set_text(GTK_LABEL(viewer->nav_label), "");
        if (siblings) g_ptr_array_free(siblings, FALSE);
        return;
    }

    gint idx = find_sfp_index(siblings, viewer->sfp);
    gtk_widget_set_sensitive(viewer->btn_prev, idx > 0);
    gtk_widget_set_sensitive(viewer->btn_next, idx < (gint)siblings->len - 1);

    if (viewer->nav_label) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d / %d", idx + 1, (int)siblings->len);
        gtk_label_set_text(GTK_LABEL(viewer->nav_label), buf);
    }
    g_ptr_array_free(siblings, FALSE);
}

/*
 * Lädt eine neue Datei in denselben Viewer.
 * Gibt TRUE zurück bei Erfolg.
 *
 * sond_file_part_get_bytes ist static, sond_file_part_open würde ein neues Fenster öffnen;
 * deshalb der Umweg über eine tmp-Datei.
 */
static gboolean navigate_to_sfp_do(SurfaceViewer *viewer, SondFilePart *new_sfp) {
    GError *error = NULL;
    GBytes *bytes = NULL;
    RenderedDocument *rd = NULL;

    gchar *tmp = sond_file_part_write_to_tmp_file(new_sfp, FALSE, &error);
    if (!tmp) {
        LOG_WARN("%s\n%s", __func__, error ? error->message : "?");
        g_clear_error(&error);
        return FALSE;
    }

    gsize len = 0;
    guchar *data = NULL;
    if (!sond_file_get_contents(tmp, (gchar**)&data, &len, &error)) {
        LOG_WARN("%s\n%s", __func__, error ? error->message : "?");
        g_clear_error(&error);
        sond_tmp_remove(tmp, NULL);
        g_free(tmp);
        return FALSE;
    }
    sond_tmp_remove(tmp, NULL);
    g_free(tmp);

    bytes = g_bytes_new_take(data, len);
    rd = sond_render_doc_new(bytes, 0, new_sfp);
    g_bytes_unref(bytes);

    if (!rd) {
        LOG_WARN("%s\nRendern fehlgeschlagen", __func__);
        return FALSE;
    }

    /* Surface tauschen */
    if (viewer->original_surface)
        cairo_surface_destroy(viewer->original_surface);

    viewer->original_surface = rd->surface;
    rd->surface = NULL;
    viewer->original_width  = rd->width;
    viewer->original_height = rd->height;
    viewer->zoom_level = 1.0;

    if (viewer->searchable_text) g_free(viewer->searchable_text);
    viewer->searchable_text = g_strdup(rd->searchable_text);
    if (viewer->search_results) { g_list_free(viewer->search_results); viewer->search_results = NULL; }
    viewer->current_search_index = 0;

    sond_render_doc_free(rd);

    /* sfp aktualisieren */
    if (viewer->sfp) g_object_unref(viewer->sfp);
    viewer->sfp = g_object_ref(new_sfp);

    /* Titel aktualisieren */
    gtk_window_set_title(GTK_WINDOW(viewer->window),
                         sond_file_part_get_path(new_sfp));

    /* Anzeige aktualisieren */
    update_display(viewer);
    update_nav_state(viewer);
    update_statusbar(viewer, sond_file_part_get_path(new_sfp));

    return TRUE;
}

/* Wie navigate_to_sfp_do(), entsperrt ein gesperrtes Geschwister vorher (Dialog;
 * ein Platzhalter bekommt dabei seinen echten Typ). Bei Abbruch bleibt das
 * aktuelle Dokument stehen. */
static gboolean navigate_to_sfp(SurfaceViewer *viewer, SondFilePart *new_sfp) {
    SondFilePart *sfp_unlocked = NULL;
    gboolean ret = FALSE;

    if (sond_file_part_needs_unlock(new_sfp)) {
        GError *error = NULL;

        sfp_unlocked = sond_file_part_unlock(new_sfp, &error);
        if (!sfp_unlocked) {
            if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                LOG_WARN("%s\n%s", __func__, error ? error->message : "?");
            g_clear_error(&error);

            return FALSE;
        }
        new_sfp = sfp_unlocked;
    }

    ret = navigate_to_sfp_do(viewer, new_sfp);

    if (sfp_unlocked)
        g_object_unref(sfp_unlocked);

    return ret;
}

static void on_nav_prev(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    GPtrArray *siblings = get_siblings(viewer->sfp);
    (void) widget;
    if (!siblings) return;

    gint idx = find_sfp_index(siblings, viewer->sfp);
    if (idx > 0)
        navigate_to_sfp(viewer, g_ptr_array_index(siblings, idx - 1));
    g_ptr_array_free(siblings, FALSE);
}

static void on_nav_next(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    GPtrArray *siblings = get_siblings(viewer->sfp);
    (void) widget;
    if (!siblings) return;

    gint idx = find_sfp_index(siblings, viewer->sfp);
    if (idx >= 0 && idx < (gint)siblings->len - 1)
        navigate_to_sfp(viewer, g_ptr_array_index(siblings, idx + 1));
    g_ptr_array_free(siblings, FALSE);
}

// === STATUSZEILE ===

static void update_statusbar(SurfaceViewer *viewer, const char *message) {
    gtk_label_set_text(GTK_LABEL(viewer->statusbar), message ? message : "");
}

// === ZOOM ===

static void update_display(SurfaceViewer *viewer) {
    if (!viewer->original_surface) return;

    int new_width  = (int)(viewer->original_width  * viewer->zoom_level);
    int new_height = (int)(viewer->original_height * viewer->zoom_level);

    gtk_widget_set_size_request(viewer->image, new_width, new_height);
    gtk_widget_queue_draw(viewer->image);

    char zoom_text[32];
    snprintf(zoom_text, sizeof(zoom_text), "Zoom: %d%%",
             (int)(viewer->zoom_level * 100));
    gtk_label_set_text(GTK_LABEL(viewer->zoom_label), zoom_text);
}

static void on_zoom_in(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    (void) widget;
    viewer->zoom_level *= 1.2;
    if (viewer->zoom_level > 5.0) viewer->zoom_level = 5.0;
    update_display(viewer);
    update_statusbar(viewer, "Zoomed in");
}

static void on_zoom_out(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    (void) widget;
    viewer->zoom_level /= 1.2;
    if (viewer->zoom_level < 0.1) viewer->zoom_level = 0.1;
    update_display(viewer);
    update_statusbar(viewer, "Zoomed out");
}

static void on_zoom_fit(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    (void) widget;
    viewer->zoom_level = 1.0;
    update_display(viewer);
    update_statusbar(viewer, "Zoom reset to 100%");
}

// === MAUSRAD-ZOOM (Strg + Rad) ===

static gboolean on_scroll(GtkEventControllerScroll *controller, gdouble dx, gdouble dy,
        gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    (void) dx;

    if (!(sond_compat_scroll_state(controller) & GDK_CONTROL_MASK) || dy == 0.0)
        return FALSE;

    if (dy < 0.0)
        on_zoom_in(NULL, viewer);
    else
        on_zoom_out(NULL, viewer);

    return TRUE;
}

// === PAN (mittlere Maustaste) ===

static void on_drag_begin(GtkGestureDrag *gesture, gdouble x, gdouble y, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    (void) gesture; (void) x; (void) y;

    viewer->pan_start_h = gtk_adjustment_get_value(
            gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(viewer->scrolled)));
    viewer->pan_start_v = gtk_adjustment_get_value(
            gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(viewer->scrolled)));
    sond_compat_widget_set_cursor(viewer->scrolled, "grabbing");
}

static void on_drag_update(GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    (void) gesture;

    gtk_adjustment_set_value(
            gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(viewer->scrolled)),
            viewer->pan_start_h - dx);
    gtk_adjustment_set_value(
            gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(viewer->scrolled)),
            viewer->pan_start_v - dy);
}

static void on_drag_end(GtkGestureDrag *gesture, gdouble dx, gdouble dy, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    (void) gesture; (void) dx; (void) dy;

    sond_compat_widget_set_cursor(viewer->scrolled, NULL);
}

// === FULLSCREEN ===

static void toggle_fullscreen(SurfaceViewer *viewer) {
    if (viewer->fullscreen) {
        gtk_window_unfullscreen(GTK_WINDOW(viewer->window));
        gtk_widget_set_visible(viewer->toolbar, TRUE);
        gtk_widget_set_visible(viewer->statusbar, TRUE);
        viewer->fullscreen = FALSE;
        update_statusbar(viewer, "Fullscreen off");
    } else {
        gtk_window_fullscreen(GTK_WINDOW(viewer->window));
        gtk_widget_set_visible(viewer->toolbar, FALSE);
        gtk_widget_set_visible(viewer->statusbar, FALSE);
        viewer->fullscreen = TRUE;
    }
}

// === PRINT ===

static void draw_page_for_print(GtkPrintOperation *op, GtkPrintContext *context,
                                gint page_nr, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    cairo_t *cr = gtk_print_context_get_cairo_context(context);
    (void) op; (void) page_nr;

    double page_width = gtk_print_context_get_width(context);
    double page_height = gtk_print_context_get_height(context);

    double scale_x = page_width / viewer->original_width;
    double scale_y = page_height / viewer->original_height;
    double scale = (scale_x < scale_y) ? scale_x : scale_y;

    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, viewer->original_surface, 0, 0);
    cairo_paint(cr);
}

static void on_print(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    (void) widget;

    GtkPrintOperation *print = gtk_print_operation_new();
    gtk_print_operation_set_n_pages(print, 1);
    gtk_print_operation_set_unit(print, GTK_UNIT_PIXEL);

    g_signal_connect(print, "draw-page", G_CALLBACK(draw_page_for_print), viewer);

    GError *error = NULL;
    GtkPrintOperationResult result = gtk_print_operation_run(
        print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
        GTK_WINDOW(viewer->window), &error);

    if (result == GTK_PRINT_OPERATION_RESULT_ERROR) {
        gchar *text = g_strdup_printf("Print error: %s", error ? error->message : "?");

        sond_compat_message(GTK_WINDOW(viewer->window), text);
        g_free(text);
        g_clear_error(&error);
    } else {
        update_statusbar(viewer, "Document printed");
    }

    g_object_unref(print);
}

// === PDF EXPORT ===

static void export_pdf_to(gchar *filename, gpointer data) {
    GtkWidget *window = data;
    SurfaceViewer *viewer = g_object_get_data(G_OBJECT(window), "sond-viewer");

    /* Fenster während der Dateiauswahl geschlossen */
    if (filename && viewer) {
        GtkPrintOperation *print = gtk_print_operation_new();
        GtkPrintSettings *settings = gtk_print_settings_new();
        gchar *uri = g_filename_to_uri(filename, NULL, NULL);
        GError *error = NULL;
        GtkPrintOperationResult result;

        gtk_print_operation_set_n_pages(print, 1);
        gtk_print_operation_set_unit(print, GTK_UNIT_PIXEL);
        gtk_print_settings_set(settings, GTK_PRINT_SETTINGS_OUTPUT_URI, uri);
        gtk_print_operation_set_print_settings(print, settings);
        g_signal_connect(print, "draw-page", G_CALLBACK(draw_page_for_print), viewer);

        result = gtk_print_operation_run(print, GTK_PRINT_OPERATION_ACTION_EXPORT,
                GTK_WINDOW(viewer->window), &error);

        if (result == GTK_PRINT_OPERATION_RESULT_ERROR) {
            gchar *text = g_strdup_printf("Export error: %s",
                    error ? error->message : "Druckerfehler");

            sond_compat_message(GTK_WINDOW(viewer->window), text);
            g_free(text);
            g_clear_error(&error);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "Exported to %s", filename);
            update_statusbar(viewer, msg);
        }

        g_free(uri);
        g_object_unref(settings);
        g_object_unref(print);
    }

    g_free(filename);
    g_object_unref(window);
}

static void on_export_pdf(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    (void) widget;

    sond_compat_save_file(GTK_WINDOW(viewer->window), "Save as PDF", "document.pdf",
            export_pdf_to, g_object_ref(viewer->window));
}

// === SEARCH ===

static void find_all_occurrences(SurfaceViewer *viewer, const char *search_text) {
    if (viewer->search_results) {
        g_list_free(viewer->search_results);
        viewer->search_results = NULL;
    }

    if (!viewer->searchable_text || !search_text || !*search_text) {
        return;
    }

    char *text_lower = g_ascii_strdown(viewer->searchable_text, -1);
    char *search_lower = g_ascii_strdown(search_text, -1);

    char *ptr = text_lower;
    size_t search_len = strlen(search_lower);

    while ((ptr = strstr(ptr, search_lower)) != NULL) {
        size_t offset = ptr - text_lower;
        viewer->search_results = g_list_append(viewer->search_results,
                                              GINT_TO_POINTER(offset));
        ptr += search_len;
    }

    g_free(text_lower);
    g_free(search_lower);

    viewer->current_search_index = 0;
}

static void show_search_result(SurfaceViewer *viewer) {
    if (!viewer->search_results) {
        update_statusbar(viewer, "No matches found");
        return;
    }

    int total = g_list_length(viewer->search_results);

    if (viewer->current_search_index >= total) {
        viewer->current_search_index = 0;
    }
    if (viewer->current_search_index < 0) {
        viewer->current_search_index = total - 1;
    }

    size_t offset = GPOINTER_TO_INT(
        g_list_nth_data(viewer->search_results, viewer->current_search_index));

    char context[200];
    size_t start = (offset > 50) ? offset - 50 : 0;
    size_t len = 150;

    if (start + len > strlen(viewer->searchable_text)) {
        len = strlen(viewer->searchable_text) - start;
    }

    strncpy(context, viewer->searchable_text + start, len);
    context[len] = '\0';

    char msg[300];
    snprintf(msg, sizeof(msg), "Match %d/%d: ...%s...",
             viewer->current_search_index + 1, total, context);
    update_statusbar(viewer, msg);
}

static void on_search(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    const char *search_text = sond_compat_entry_get_text(GTK_ENTRY(viewer->search_entry));
    (void) widget;

    if (search_text && *search_text) {
        find_all_occurrences(viewer, search_text);
        show_search_result(viewer);
    }
}

static void on_search_next(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    (void) widget;

    if (viewer->search_results) {
        viewer->current_search_index++;
        show_search_result(viewer);
    }
}

static void on_search_prev(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    (void) widget;

    if (viewer->search_results) {
        viewer->current_search_index--;
        show_search_result(viewer);
    }
}

static void on_search_activate(GtkEntry *entry, gpointer data) {
    on_search(GTK_WIDGET(entry), data);
}

// === TASTEN ===

static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
        GdkModifierType state, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(viewer->window));
    (void) controller; (void) keycode;

    if (state & GDK_CONTROL_MASK) {
        switch (keyval) {
        case GDK_KEY_plus: case GDK_KEY_equal: case GDK_KEY_KP_Add:
            on_zoom_in(NULL, viewer);
            return TRUE;
        case GDK_KEY_minus: case GDK_KEY_KP_Subtract:
            on_zoom_out(NULL, viewer);
            return TRUE;
        case GDK_KEY_0:
            on_zoom_fit(NULL, viewer);
            return TRUE;
        case GDK_KEY_p:
            on_print(NULL, viewer);
            return TRUE;
        case GDK_KEY_f:
            if (viewer->search_entry) {
                gtk_widget_grab_focus(viewer->search_entry);
                return TRUE;
            }
            break;
        default:
            break;
        }

        return FALSE;
    }

    if (keyval == GDK_KEY_F11) {
        toggle_fullscreen(viewer);
        return TRUE;
    }

    if (keyval == GDK_KEY_Escape && viewer->fullscreen) {
        toggle_fullscreen(viewer);
        return TRUE;
    }

    /* Pfeile gehören dem Suchfeld, solange es den Fokus hat */
    if (focus && GTK_IS_EDITABLE(focus))
        return FALSE;

    if (keyval == GDK_KEY_Left) {
        on_nav_prev(NULL, viewer);
        return TRUE;
    }

    if (keyval == GDK_KEY_Right) {
        on_nav_next(NULL, viewer);
        return TRUE;
    }

    return FALSE;
}

// === CLEANUP ===

static void cleanup_viewer(SurfaceViewer *viewer) {
    if (viewer->original_surface) {
        cairo_surface_destroy(viewer->original_surface);
    }
    if (viewer->searchable_text) {
        g_free(viewer->searchable_text);
    }
    if (viewer->search_results) {
        g_list_free(viewer->search_results);
    }
    if (viewer->sfp) {
        g_object_unref(viewer->sfp);
    }
    g_free(viewer);
}

static void on_window_destroy(GtkWidget *widget, gpointer data) {
    g_object_set_data(G_OBJECT(widget), "sond-viewer", NULL);
    cleanup_viewer((SurfaceViewer*)data);
}

// === ZEICHNEN ===

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    (void) area; (void) width; (void) height;

    if (!viewer->original_surface) return;

    cairo_scale(cr, viewer->zoom_level, viewer->zoom_level);
    cairo_set_source_surface(cr, viewer->original_surface, 0, 0);
    cairo_paint(cr);
}

// === FENSTER ===

static GtkWidget* toolbar_separator(void) {
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);

    gtk_widget_set_margin_start(sep, 5);
    gtk_widget_set_margin_end(sep, 5);

    return sep;
}

/*
 * Erstellt ein Fenster mit Cairo Surface Viewer. Die Surface geht in den Besitz des Viewers über.
 * title: Fenstertitel; searchable_text: Text für die Suche (NULL: keine Suche).
 */
static GtkWidget* show_surface_viewer(cairo_surface_t *surface, int width, int height,
                               const char *title, const char *searchable_text,
                               SondFilePart *sfp, SurfaceViewer **out_viewer) {
    if (!surface) return NULL;

    SurfaceViewer *viewer = g_new0(SurfaceViewer, 1);

    viewer->original_surface = surface;
    viewer->original_width = width;
    viewer->original_height = height;
    viewer->zoom_level = 1.0;
    viewer->fullscreen = FALSE;
    viewer->sfp = sfp ? g_object_ref(sfp) : NULL;

    if (searchable_text) {
        viewer->searchable_text = g_strdup(searchable_text);
    }

    // GUI erstellen
    viewer->window = sond_compat_window_new();
    g_object_set_data(G_OBJECT(viewer->window), "sond-viewer", viewer);
    gtk_window_set_title(GTK_WINDOW(viewer->window), title ? title : "Surface Viewer");
    gtk_window_set_default_size(GTK_WINDOW(viewer->window),
                               width + 50,
                               height < 800 ? height + 100 : 800);

    g_signal_connect(viewer->window, "destroy", G_CALLBACK(on_window_destroy), viewer);
    sond_compat_add_key_handler(viewer->window, on_key_pressed, viewer);
    sond_compat_add_scroll_handler(viewer->window, on_scroll, viewer);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    sond_compat_window_set_child(GTK_WINDOW(viewer->window), vbox);

    // Toolbar
    viewer->toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_top(viewer->toolbar, 5);
    gtk_widget_set_margin_bottom(viewer->toolbar, 5);
    gtk_widget_set_margin_start(viewer->toolbar, 5);
    sond_compat_box_append(GTK_BOX(vbox), viewer->toolbar, FALSE);

    // Navigations-Buttons (Pfeil links/rechts)
    viewer->btn_prev = gtk_button_new_with_label("◀");
    viewer->nav_label = gtk_label_new("");
    viewer->btn_next = gtk_button_new_with_label("▶");

    g_signal_connect(viewer->btn_prev, "clicked", G_CALLBACK(on_nav_prev), viewer);
    g_signal_connect(viewer->btn_next, "clicked", G_CALLBACK(on_nav_next), viewer);

    sond_compat_box_append(GTK_BOX(viewer->toolbar), viewer->btn_prev, FALSE);
    sond_compat_box_append(GTK_BOX(viewer->toolbar), viewer->nav_label, FALSE);
    sond_compat_box_append(GTK_BOX(viewer->toolbar), viewer->btn_next, FALSE);
    sond_compat_box_append(GTK_BOX(viewer->toolbar), toolbar_separator(), FALSE);

    // Zoom-Buttons
    GtkWidget *btn_zoom_in = gtk_button_new_with_label("Zoom +");
    GtkWidget *btn_zoom_out = gtk_button_new_with_label("Zoom -");
    GtkWidget *btn_zoom_fit = gtk_button_new_with_label("Fit");
    viewer->zoom_label = gtk_label_new("Zoom: 100%");

    g_signal_connect(btn_zoom_in, "clicked", G_CALLBACK(on_zoom_in), viewer);
    g_signal_connect(btn_zoom_out, "clicked", G_CALLBACK(on_zoom_out), viewer);
    g_signal_connect(btn_zoom_fit, "clicked", G_CALLBACK(on_zoom_fit), viewer);

    sond_compat_box_append(GTK_BOX(viewer->toolbar), btn_zoom_in, FALSE);
    sond_compat_box_append(GTK_BOX(viewer->toolbar), btn_zoom_out, FALSE);
    sond_compat_box_append(GTK_BOX(viewer->toolbar), btn_zoom_fit, FALSE);
    sond_compat_box_append(GTK_BOX(viewer->toolbar), viewer->zoom_label, FALSE);
    sond_compat_box_append(GTK_BOX(viewer->toolbar), toolbar_separator(), FALSE);

    // Print & Export
    GtkWidget *btn_print = gtk_button_new_with_label("Print");
    GtkWidget *btn_pdf = gtk_button_new_with_label("Export PDF");
    g_signal_connect(btn_print, "clicked", G_CALLBACK(on_print), viewer);
    g_signal_connect(btn_pdf, "clicked", G_CALLBACK(on_export_pdf), viewer);
    sond_compat_box_append(GTK_BOX(viewer->toolbar), btn_print, FALSE);
    sond_compat_box_append(GTK_BOX(viewer->toolbar), btn_pdf, FALSE);

    // Suche (nur wenn Text vorhanden)
    if (viewer->searchable_text) {
        sond_compat_box_append(GTK_BOX(viewer->toolbar), toolbar_separator(), FALSE);
        sond_compat_box_append(GTK_BOX(viewer->toolbar), gtk_label_new("Search:"), FALSE);

        viewer->search_entry = gtk_entry_new();
        sond_compat_entry_set_width_chars(GTK_ENTRY(viewer->search_entry), 15);
        g_signal_connect(viewer->search_entry, "activate", G_CALLBACK(on_search_activate), viewer);
        sond_compat_box_append(GTK_BOX(viewer->toolbar), viewer->search_entry, FALSE);

        GtkWidget *btn_search = gtk_button_new_with_label("Find");
        GtkWidget *btn_sprev = gtk_button_new_with_label("◄");
        GtkWidget *btn_snext = gtk_button_new_with_label("►");
        g_signal_connect(btn_search, "clicked", G_CALLBACK(on_search), viewer);
        g_signal_connect(btn_sprev, "clicked", G_CALLBACK(on_search_prev), viewer);
        g_signal_connect(btn_snext, "clicked", G_CALLBACK(on_search_next), viewer);
        sond_compat_box_append(GTK_BOX(viewer->toolbar), btn_search, FALSE);
        sond_compat_box_append(GTK_BOX(viewer->toolbar), btn_sprev, FALSE);
        sond_compat_box_append(GTK_BOX(viewer->toolbar), btn_snext, FALSE);
    }

    // ScrolledWindow mit der Zeichenfläche
    viewer->scrolled = sond_compat_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(viewer->scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    sond_compat_box_append(GTK_BOX(vbox), viewer->scrolled, TRUE);

    viewer->image = gtk_drawing_area_new();
    gtk_widget_set_size_request(viewer->image, viewer->original_width, viewer->original_height);
    sond_compat_drawing_area_set_draw_func(GTK_DRAWING_AREA(viewer->image), on_draw, viewer);
    sond_compat_scrolled_window_set_child(GTK_SCROLLED_WINDOW(viewer->scrolled), viewer->image);

    /* Pan: am Scrollbereich, nicht an der Zeichenfläche, weil sich deren Koordinaten beim
     * Verschieben mitbewegen */
    {
        GtkGesture *drag = sond_compat_add_drag(viewer->scrolled, 2);

        g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), viewer);
        g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), viewer);
        g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), viewer);
    }

    // Statuszeile
    viewer->statusbar = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(viewer->statusbar), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(viewer->statusbar), PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_start(viewer->statusbar, 5);
    gtk_widget_set_margin_top(viewer->statusbar, 3);
    gtk_widget_set_margin_bottom(viewer->statusbar, 3);
    sond_compat_box_append(GTK_BOX(vbox), viewer->statusbar, FALSE);
    update_statusbar(viewer, "Ready");

    sond_compat_window_present(GTK_WINDOW(viewer->window));

    /* Navigations-Zustand initial setzen */
    update_nav_state(viewer);

    if (out_viewer)
        *out_viewer = viewer;

    return viewer->window;
}

/* Scrollt nach dem Anzeigen zur Trefferposition. Die Bereichsgröße steht erst nach dem
 * ersten Layout fest, deshalb mit wenigen Wiederholungen. */
typedef struct {
    GtkWidget *scrolled;
    int y;
    int tries;
} HighlightScroll;

static gboolean scroll_to_highlight_idle(gpointer data)
{
    HighlightScroll *hs = data;
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(
            GTK_SCROLLED_WINDOW(hs->scrolled));
    double upper = gtk_adjustment_get_upper(vadj);
    double page  = gtk_adjustment_get_page_size(vadj);

    if (upper - page <= 0.0 && hs->tries++ < 20)
        return G_SOURCE_CONTINUE;

    double target = (double)hs->y - page / 3.0; /* Treffer im oberen Drittel */
    if (target > upper - page) target = upper - page;
    if (target < 0) target = 0;
    gtk_adjustment_set_value(vadj, target);

    g_object_unref(hs->scrolled);
    g_free(hs);

    return G_SOURCE_REMOVE;
}

// === API ===

static gint show_rendered(RenderedDocument *rd, SondFilePart *sfp, gchar const *title,
        int highlight_y, GError **error)
{
    const char *window_title = title ? title
            : (sfp ? sond_file_part_get_path(sfp)
                   : sond_render_doc_type_string(rd->type));
    SurfaceViewer *viewer = NULL;
    GtkWidget *win = show_surface_viewer(rd->surface, rd->width, rd->height,
            window_title, rd->searchable_text, sfp, &viewer);

    rd->surface = NULL;
    sond_render_doc_free(rd);

    if (!win) {
        if (error) *error = g_error_new(SOND_ERROR, 0,
                "%s\nFenster konnte nicht erstellt werden", __func__);
        return -1;
    }

    if (highlight_y > 0) {
        HighlightScroll *hs = g_new(HighlightScroll, 1);

        hs->scrolled = g_object_ref(viewer->scrolled);
        hs->y = highlight_y;
        hs->tries = 0;
        g_idle_add(scroll_to_highlight_idle, hs);
    }

    return 0;
}

gint sond_render_with_term(GBytes *input, SondFilePart *sfp,
        gchar const *title, gchar const *term,
        gint char_pos_in_doc, GError **error)
{
    int highlight_y = 0;
    RenderedDocument *rd = NULL;

    if (!input) {
        if (error) *error = g_error_new(SOND_ERROR, 0,
                "%s\nKein Eingabe-Stream", __func__);
        return -1;
    }

    if (g_bytes_get_size(input) == 0) {
        if (error) *error = g_error_new(SOND_ERROR, 0,
                "%s\nDatei ist leer", __func__);
        return -1;
    }

    if (term && *term)
        rd = sond_render_doc_new_with_term(input, sfp, 0, term, char_pos_in_doc, &highlight_y);
    else
        rd = sond_render_doc_new(input, 0, sfp);

    if (!rd) {
        if (error) *error = g_error_new(SOND_ERROR, 0,
                "%s\nStream konnte nicht gerendert werden", __func__);
        return -1;
    }

    return show_rendered(rd, sfp, title, highlight_y, error);
}

gint sond_render(GBytes* input, SondFilePart* sfp, gchar const* title,
		GError** error) {
	return sond_render_with_term(input, sfp, title, NULL, -1, error);
}

/* Gibt es für sfp einen eigenen Viewer (Text außer CSV, Bild, ODT, DOCX, E-Mail)? */
gboolean sond_renderer_can_render(SondFilePart* sfp) {
	gchar const* mime = NULL;

	if (!sfp)
		return FALSE;
	if (SOND_IS_FILE_PART_GMESSAGE(sfp))
		return TRUE;
	if (!SOND_IS_FILE_PART_LEAF(sfp))
		return FALSE;

	mime = sond_file_part_leaf_get_mime_type(SOND_FILE_PART_LEAF(sfp));
	if (!mime)
		return FALSE;

	return (g_str_has_prefix(mime, "text/") && g_strcmp0(mime, "text/csv")) ||
			g_str_has_prefix(mime, "image/") ||
			!g_strcmp0(mime, "application/vnd.oasis.opendocument.text") ||
			!g_strcmp0(mime, "application/vnd.openxmlformats-officedocument.wordprocessingml.document");
}

/* Öffnet sfp im eigenen Viewer, sonst mit dem Standardprogramm bzw. "Öffnen mit" (open_with
 * erzwingt Letzteres). */
gint sond_renderer_open_file_part(SondFilePart* sfp, gboolean open_with,
		GError** error) {
	g_return_val_if_fail(sfp, -1);

	if (!open_with && sond_renderer_can_render(sfp)) {
		GBytes* bytes = NULL;
		gint rc = 0;

		bytes = sond_file_part_get_bytes(sfp, error);
		if (!bytes)
			return -1;

		rc = sond_render(bytes, sfp, NULL, error);
		g_bytes_unref(bytes);
		if (rc)
			return -1;
	}
	else if (sond_file_part_open_external(sfp, open_with, error))
		return -1;

	return 0;
}
