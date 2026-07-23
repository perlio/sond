/*
 * renderer.c
 *
 *  Created on: 14.12.2025
 *      Author: pkrieger
 */


#include <gtk/gtk.h>
#include <cairo.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "misc.h"
#include "sond_log_and_error.h"
#include "sond_fileparts.h"
#include "sond_file_helper.h"
#include "sond_gmessage_helper.h"
#include "sond_text_extract.h"

#define DEFAULT_RENDER_WIDTH 650


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
    guint statusbar_context;

    char *searchable_text;
    GList *search_results;
    int current_search_index;

    gboolean panning;
    double pan_start_x;
    double pan_start_y;

    /* Navigation */
    SondFilePart *sfp;          /* aktuelle Datei, NULL wenn standalone */
    GtkWidget *btn_prev;
    GtkWidget *btn_next;
    GtkWidget *nav_label;
} SurfaceViewer;

typedef enum {
    DOC_TYPE_UNKNOWN,
    DOC_TYPE_HTML,
    DOC_TYPE_ODT,
    DOC_TYPE_DOCX,
    DOC_TYPE_DOC,
    DOC_TYPE_IMAGE,
    DOC_TYPE_PLAIN,
    DOC_TYPE_EML
} DocumentType;

typedef struct {
    cairo_surface_t *surface;
    int width;
    int height;
    char *searchable_text;
    DocumentType type;
} RenderedDocument;


// Forward declarations
static void update_display(SurfaceViewer *viewer);
static void update_statusbar(SurfaceViewer *viewer, const char *message);
static RenderedDocument* render_document_from_bytes(GBytes *bytes, int render_width,
        SondFilePart *sfp);

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

/**
 * Gibt ein RenderedDocument frei
 */
void free_rendered_document(RenderedDocument *doc) {
    if (!doc) return;

    if (doc->surface) {
        cairo_surface_destroy(doc->surface);
    }
    if (doc->searchable_text) {
        g_free(doc->searchable_text);
    }
    g_free(doc);
}

/*
 * Lädt eine neue Datei in denselben Viewer.
 * Gibt TRUE zurück bei Erfolg.
 */
static gboolean navigate_to_sfp(SurfaceViewer *viewer, SondFilePart *new_sfp) {
    GError *error = NULL;
    GBytes *bytes = NULL;
    RenderedDocument *rd = NULL;

    /* Bytes der neuen Datei holen - sond_file_part_get_bytes ist static,
     * wir rufen sond_file_part_open nicht auf (würde neues Fenster öffnen).
     * Stattdessen schreiben wir in tmp, lesen zurück — oder wir nutzen
     * den schon vorhandenen Pfad. Sauberer: wir exportieren eine
     * Hilfsfunktion. Hier nutzen wir write_to_tmp_file als Brücke.
     *
     * Da sond_file_part_get_bytes intern static ist, umgehen wir das über
     * sond_file_part_write_to_tmp_file + g_file_get_contents.
     * Besser: Wir fügen sond_file_part_get_bytes_public hinzu — aber das
     * würde weitere Headeränderungen bedeuten. Pragmatisch: tmp-Datei.
     */
    gchar *tmp = sond_file_part_write_to_tmp_file(new_sfp, &error);
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
        sond_remove(tmp, NULL);
        g_free(tmp);
        return FALSE;
    }
    sond_remove(tmp, NULL);
    g_free(tmp);

    bytes = g_bytes_new_take(data, len);
    rd = render_document_from_bytes(bytes, 0, new_sfp);
    g_bytes_unref(bytes);

    if (!rd) {
        LOG_WARN("%s\nrender_document_from_bytes fehlgeschlagen", __func__);
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

    free_rendered_document(rd);

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

static void on_nav_prev(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    GPtrArray *siblings = get_siblings(viewer->sfp);
    if (!siblings) return;

    gint idx = find_sfp_index(siblings, viewer->sfp);
    if (idx > 0)
        navigate_to_sfp(viewer, g_ptr_array_index(siblings, idx - 1));
    g_ptr_array_free(siblings, FALSE);
}

static void on_nav_next(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    GPtrArray *siblings = get_siblings(viewer->sfp);
    if (!siblings) return;

    gint idx = find_sfp_index(siblings, viewer->sfp);
    if (idx >= 0 && idx < (gint)siblings->len - 1)
        navigate_to_sfp(viewer, g_ptr_array_index(siblings, idx + 1));
    g_ptr_array_free(siblings, FALSE);
}

// === STATUSBAR ===

static void update_statusbar(SurfaceViewer *viewer, const char *message) {
    gtk_statusbar_pop(GTK_STATUSBAR(viewer->statusbar), viewer->statusbar_context);
    gtk_statusbar_push(GTK_STATUSBAR(viewer->statusbar), viewer->statusbar_context, message);
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
    viewer->zoom_level *= 1.2;
    if (viewer->zoom_level > 5.0) viewer->zoom_level = 5.0;
    update_display(viewer);
    update_statusbar(viewer, "Zoomed in");
}

static void on_zoom_out(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    viewer->zoom_level /= 1.2;
    if (viewer->zoom_level < 0.1) viewer->zoom_level = 0.1;
    update_display(viewer);
    update_statusbar(viewer, "Zoomed out");
}

static void on_zoom_fit(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    viewer->zoom_level = 1.0;
    update_display(viewer);
    update_statusbar(viewer, "Zoom reset to 100%");
}

// === MOUSE WHEEL ZOOM ===

static gboolean on_scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;

    if (event->state & GDK_CONTROL_MASK) {
        if (event->direction == GDK_SCROLL_UP) {
            on_zoom_in(NULL, viewer);
        } else if (event->direction == GDK_SCROLL_DOWN) {
            on_zoom_out(NULL, viewer);
        }
        return TRUE;
    }

    return FALSE;
}

// === PAN (Middle Mouse) ===

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;

    if (event->button == 2) {
        viewer->panning = TRUE;
        viewer->pan_start_x = event->x;
        viewer->pan_start_y = event->y;

        GdkWindow *gdk_window = gtk_widget_get_window(widget);
        GdkCursor *cursor = gdk_cursor_new_for_display(
            gdk_display_get_default(), GDK_FLEUR);
        gdk_window_set_cursor(gdk_window, cursor);
        g_object_unref(cursor);

        return TRUE;
    }

    return FALSE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;

    if (event->button == 2) {
        viewer->panning = FALSE;

        GdkWindow *gdk_window = gtk_widget_get_window(widget);
        gdk_window_set_cursor(gdk_window, NULL);

        return TRUE;
    }

    return FALSE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;

    if (viewer->panning) {
        double dx = event->x - viewer->pan_start_x;
        double dy = event->y - viewer->pan_start_y;

        GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(
            GTK_SCROLLED_WINDOW(viewer->scrolled));
        GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(
            GTK_SCROLLED_WINDOW(viewer->scrolled));

        gtk_adjustment_set_value(hadj, gtk_adjustment_get_value(hadj) - dx);
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_value(vadj) - dy);

        viewer->pan_start_x = event->x - dx;
        viewer->pan_start_y = event->y - dy;

        return TRUE;
    }

    return FALSE;
}

// === FULLSCREEN ===

static void toggle_fullscreen(SurfaceViewer *viewer) {
    if (viewer->fullscreen) {
        gtk_window_unfullscreen(GTK_WINDOW(viewer->window));
        gtk_widget_show(viewer->toolbar);
        gtk_widget_show(viewer->statusbar);
        viewer->fullscreen = FALSE;
        update_statusbar(viewer, "Fullscreen off");
    } else {
        gtk_window_fullscreen(GTK_WINDOW(viewer->window));
        gtk_widget_hide(viewer->toolbar);
        gtk_widget_hide(viewer->statusbar);
        viewer->fullscreen = TRUE;
    }
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;

    if (event->keyval == GDK_KEY_F11) {
        toggle_fullscreen(viewer);
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Escape && viewer->fullscreen) {
        toggle_fullscreen(viewer);
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Left) {
        on_nav_prev(NULL, viewer);
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Right) {
        on_nav_next(NULL, viewer);
        return TRUE;
    }

    return FALSE;
}

// === PRINT ===

static void draw_page_for_print(GtkPrintOperation *op, GtkPrintContext *context,
                                gint page_nr, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    cairo_t *cr = gtk_print_context_get_cairo_context(context);

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

    GtkPrintOperation *print = gtk_print_operation_new();
    gtk_print_operation_set_n_pages(print, 1);
    gtk_print_operation_set_unit(print, GTK_UNIT_PIXEL);

    g_signal_connect(print, "draw-page", G_CALLBACK(draw_page_for_print), viewer);

    GError *error = NULL;
    GtkPrintOperationResult result = gtk_print_operation_run(
        print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
        GTK_WINDOW(viewer->window), &error);

    if (result == GTK_PRINT_OPERATION_RESULT_ERROR) {
        display_message(viewer->window, "Print error: ",
                error ? error->message : "?", NULL);
        g_clear_error(&error);
    } else {
        update_statusbar(viewer, "Document printed");
    }

    g_object_unref(print);
}

// === PDF EXPORT ===

static void on_export_pdf(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;

    char *filename = filename_speichern(GTK_WINDOW(viewer->window),
            "Save as PDF", "document.pdf");
    if (!filename)
        return;

    GtkPrintOperation *print = gtk_print_operation_new();
    gtk_print_operation_set_n_pages(print, 1);
    gtk_print_operation_set_unit(print, GTK_UNIT_PIXEL);

    GtkPrintSettings *settings = gtk_print_settings_new();
    gtk_print_settings_set(settings, GTK_PRINT_SETTINGS_OUTPUT_URI,
                          g_filename_to_uri(filename, NULL, NULL));
    gtk_print_operation_set_print_settings(print, settings);

    g_signal_connect(print, "draw-page", G_CALLBACK(draw_page_for_print), viewer);

    GError *error = NULL;
    GtkPrintOperationResult result = gtk_print_operation_run(
        print, GTK_PRINT_OPERATION_ACTION_EXPORT,
        GTK_WINDOW(viewer->window), &error);

    if (result == GTK_PRINT_OPERATION_RESULT_ERROR) {
        display_message(viewer->window, "Export error: ",
                error ? error->message : "Druckerfehler", NULL);
        g_clear_error(&error);
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "Exported to %s", filename);
        update_statusbar(viewer, msg);
    }

    g_object_unref(settings);
    g_object_unref(print);
    g_free(filename);
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
    const char *search_text = gtk_entry_get_text(GTK_ENTRY(viewer->search_entry));

    if (search_text && *search_text) {
        find_all_occurrences(viewer, search_text);
        show_search_result(viewer);
    }
}

static void on_search_next(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;

    if (viewer->search_results) {
        viewer->current_search_index++;
        show_search_result(viewer);
    }
}

static void on_search_prev(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;

    if (viewer->search_results) {
        viewer->current_search_index--;
        show_search_result(viewer);
    }
}

static void on_search_activate(GtkEntry *entry, gpointer data) {
    on_search(GTK_WIDGET(entry), data);
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
    cleanup_viewer((SurfaceViewer*)data);
}

// === HAUPTFUNKTION ===

// === DRAWING AREA CALLBACK ===
static gboolean on_drawing_area_draw(GtkWidget *widget,
        cairo_t *cr, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;
    if (!viewer->original_surface) return FALSE;

    cairo_scale(cr, viewer->zoom_level, viewer->zoom_level);
    cairo_set_source_surface(cr, viewer->original_surface, 0, 0);
    cairo_paint(cr);

    return FALSE;
}

/**
 * Erstellt ein Fenster mit Cairo Surface Viewer
 *
 * @param surface Cairo Surface zum Anzeigen (wird kopiert, Original kann danach freigegeben werden)
 * @param width Breite der Surface
 * @param height Höhe der Surface
 * @param title Fenstertitel (optional, NULL für Standard)
 * @param searchable_text Text zum Durchsuchen (optional, NULL wenn keine Suche gewünscht)
 * @return GTK Window Widget
 */
static GtkWidget* show_surface_viewer(cairo_surface_t *surface, int width, int height,
                               const char *title, const char *searchable_text,
                               SondFilePart *sfp) {
    if (!surface) return NULL;

    SurfaceViewer *viewer = g_new0(SurfaceViewer, 1);

    // Surface kopieren (damit Original freigegeben werden kann)
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
    viewer->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(viewer->window), title ? title : "Surface Viewer");
    gtk_window_set_default_size(GTK_WINDOW(viewer->window),
                               width + 50,
                               height < 800 ? height + 100 : 800);

    g_signal_connect(viewer->window, "key-press-event", G_CALLBACK(on_key_press), viewer);
    g_signal_connect(viewer->window, "destroy", G_CALLBACK(on_window_destroy), viewer);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(viewer->window), vbox);

    // Toolbar
    viewer->toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), viewer->toolbar, FALSE, FALSE, 5);

    // Navigations-Buttons (Pfeil links/rechts)
    viewer->btn_prev = gtk_button_new_with_label("◀");
    viewer->nav_label = gtk_label_new("");
    viewer->btn_next = gtk_button_new_with_label("▶");

    g_signal_connect(viewer->btn_prev, "clicked", G_CALLBACK(on_nav_prev), viewer);
    g_signal_connect(viewer->btn_next, "clicked", G_CALLBACK(on_nav_next), viewer);

    gtk_box_pack_start(GTK_BOX(viewer->toolbar), viewer->btn_prev, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(viewer->toolbar), viewer->nav_label, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(viewer->toolbar), viewer->btn_next, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(viewer->toolbar),
                      gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 10);

    // Zoom-Buttons
    GtkWidget *btn_zoom_in = gtk_button_new_with_label("Zoom +");
    GtkWidget *btn_zoom_out = gtk_button_new_with_label("Zoom -");
    GtkWidget *btn_zoom_fit = gtk_button_new_with_label("Fit");
    viewer->zoom_label = gtk_label_new("Zoom: 100%");

    g_signal_connect(btn_zoom_in, "clicked", G_CALLBACK(on_zoom_in), viewer);
    g_signal_connect(btn_zoom_out, "clicked", G_CALLBACK(on_zoom_out), viewer);
    g_signal_connect(btn_zoom_fit, "clicked", G_CALLBACK(on_zoom_fit), viewer);

    gtk_box_pack_start(GTK_BOX(viewer->toolbar), btn_zoom_in, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(viewer->toolbar), btn_zoom_out, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(viewer->toolbar), btn_zoom_fit, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(viewer->toolbar), viewer->zoom_label, FALSE, FALSE, 10);

    gtk_box_pack_start(GTK_BOX(viewer->toolbar),
                      gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 10);

    // Print & Export
    GtkWidget *btn_print = gtk_button_new_with_label("Print");
    GtkWidget *btn_pdf = gtk_button_new_with_label("Export PDF");
    g_signal_connect(btn_print, "clicked", G_CALLBACK(on_print), viewer);
    g_signal_connect(btn_pdf, "clicked", G_CALLBACK(on_export_pdf), viewer);
    gtk_box_pack_start(GTK_BOX(viewer->toolbar), btn_print, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(viewer->toolbar), btn_pdf, FALSE, FALSE, 0);

    // Suche (nur wenn Text vorhanden)
    if (viewer->searchable_text) {
        gtk_box_pack_start(GTK_BOX(viewer->toolbar),
                          gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 10);

        gtk_box_pack_start(GTK_BOX(viewer->toolbar), gtk_label_new("Search:"), FALSE, FALSE, 5);
        viewer->search_entry = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(viewer->search_entry), 15);
        g_signal_connect(viewer->search_entry, "activate", G_CALLBACK(on_search_activate), viewer);
        gtk_box_pack_start(GTK_BOX(viewer->toolbar), viewer->search_entry, FALSE, FALSE, 0);

        GtkWidget *btn_search = gtk_button_new_with_label("Find");
        GtkWidget *btn_prev = gtk_button_new_with_label("◄");
        GtkWidget *btn_next = gtk_button_new_with_label("►");
        g_signal_connect(btn_search, "clicked", G_CALLBACK(on_search), viewer);
        g_signal_connect(btn_prev, "clicked", G_CALLBACK(on_search_prev), viewer);
        g_signal_connect(btn_next, "clicked", G_CALLBACK(on_search_next), viewer);
        gtk_box_pack_start(GTK_BOX(viewer->toolbar), btn_search, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(viewer->toolbar), btn_prev, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(viewer->toolbar), btn_next, FALSE, FALSE, 0);
    }

    // ScrolledWindow
	viewer->scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(viewer->scrolled),
								   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(vbox), viewer->scrolled, TRUE, TRUE, 0);

	// DrawingArea statt EventBox + GtkImage
	viewer->image = gtk_drawing_area_new();
	gtk_widget_set_size_request(viewer->image,
								viewer->original_width,
								viewer->original_height);

	gtk_widget_set_events(viewer->image,
						  GDK_SCROLL_MASK |
						  GDK_BUTTON_PRESS_MASK |
						  GDK_BUTTON_RELEASE_MASK |
						  GDK_POINTER_MOTION_MASK);

	g_signal_connect(viewer->image, "draw",
					 G_CALLBACK(on_drawing_area_draw), viewer);
	g_signal_connect(viewer->image, "scroll-event",
					 G_CALLBACK(on_scroll_event), viewer);
	g_signal_connect(viewer->image, "button-press-event",
					 G_CALLBACK(on_button_press), viewer);
	g_signal_connect(viewer->image, "button-release-event",
					 G_CALLBACK(on_button_release), viewer);
	g_signal_connect(viewer->image, "motion-notify-event",
					 G_CALLBACK(on_motion_notify), viewer);

	gtk_scrolled_window_add_with_viewport(
			GTK_SCROLLED_WINDOW(viewer->scrolled), viewer->image);

    // Statusbar
    viewer->statusbar = gtk_statusbar_new();
    viewer->statusbar_context = gtk_statusbar_get_context_id(
        GTK_STATUSBAR(viewer->statusbar), "main");
    gtk_box_pack_start(GTK_BOX(vbox), viewer->statusbar, FALSE, FALSE, 0);
    update_statusbar(viewer, "Ready");

    // Tastenkombinationen
#if GTK_MAJOR_VERSION < 4
    GtkAccelGroup *accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(viewer->window), accel_group);

    gtk_widget_add_accelerator(btn_zoom_in, "clicked", accel_group,
                              GDK_KEY_plus, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(btn_zoom_out, "clicked", accel_group,
                              GDK_KEY_minus, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(btn_zoom_fit, "clicked", accel_group,
                              GDK_KEY_0, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(btn_print, "clicked", accel_group,
                              GDK_KEY_p, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    if (viewer->searchable_text) {
        gtk_widget_add_accelerator(viewer->search_entry, "grab-focus", accel_group,
                                  GDK_KEY_f, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }
#endif

    gtk_widget_show_all(viewer->window);

    /* Navigations-Zustand initial setzen */
    update_nav_state(viewer);

    return viewer->window;
}

static cairo_surface_t* render_text_to_surface(
        const char *text, PangoAttrList *attrs,
        int width, int max_height,
        int *out_height) {

    /* Höhe berechnen */
    cairo_surface_t *tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *tmp_cr = cairo_create(tmp);
    PangoLayout *layout = pango_cairo_create_layout(tmp_cr);
    pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text, -1);
    if (attrs) pango_layout_set_attributes(layout, attrs);
    int pw, ph;
    pango_layout_get_pixel_size(layout, &pw, &ph);
    g_object_unref(layout);
    cairo_destroy(tmp_cr);
    cairo_surface_destroy(tmp);

    gboolean truncated = (ph > max_height);
    if (truncated) ph = max_height;
    int height = ph + 20;
    if (out_height) *out_height = truncated ? max_height + 1 : height;

    /* Rendern */
    cairo_surface_t *surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        LOG_WARN("render_text_to_surface: cairo_image_surface_create "
                 "fehlgeschlagen (width=%d height=%d): %s",
                 width, height,
                 cairo_status_to_string(cairo_surface_status(surface)));
        cairo_surface_destroy(surface);
        return NULL;
    }

    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_paint(cr);

	if (truncated) {
		cairo_set_source_rgb(cr, 0.8, 0.0, 0.0);
		PangoLayout *hint = pango_cairo_create_layout(cr);
		pango_layout_set_text(hint,
				"[Darstellung abgeschnitten — Inhalt zu lang]", -1);
		cairo_move_to(cr, 10, 10);
		pango_cairo_show_layout(cr, hint);
		g_object_unref(hint);
	}

	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	layout = pango_cairo_create_layout(cr);
	pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
	pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_text(layout, text, -1);
	if (attrs) pango_layout_set_attributes(layout, attrs);
	cairo_move_to(cr, 10, truncated ? 30 : 10);
	pango_cairo_show_layout(cr, layout);
	g_object_unref(layout);

	cairo_destroy(cr);
    return surface;
}

// === TYPE DETECTION ===

// === HTML RENDERING ===

/*
 * Text-Extraktion kommt aus sond_text_extract.c - dieselbe Funktion, die
 * auch beim Indizieren verwendet wird. Nur so bleiben die char_pos-
 * Offsets aus der Indexsuche und der hier angezeigte Text konsistent
 * (siehe sond_text_extract.h).
 */
static RenderedDocument* render_html(guchar const *buf, gsize size, int width) {
    GPtrArray *segs = sond_text_extract_html(buf, size);
    if (segs->len == 0) { g_ptr_array_unref(segs); return NULL; }

    SondTextSegment *seg = g_ptr_array_index(segs, 0);

    int height = 0;
    cairo_surface_t *surface = render_text_to_surface(
            seg->text, NULL, width, 30000, &height);
    if (!surface) { g_ptr_array_unref(segs); return NULL; }

    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = width;
    result->height = height;
    result->searchable_text = g_strdup(seg->text);
    result->type = DOC_TYPE_HTML;

    g_ptr_array_unref(segs);
    return result;
}

// === IMAGE RENDERING ===

static RenderedDocument* render_image(GBytes *bytes, int max_width) {
    gsize len;
    const guchar *data = g_bytes_get_data(bytes, &len);

    GError *error = NULL;
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();

    if (!gdk_pixbuf_loader_write(loader, data, len, &error)) {
    	LOG_WARN("Failed to load image: %s", error->message);
        g_error_free(error);
        g_object_unref(loader);
        return NULL;
    }

    gdk_pixbuf_loader_close(loader, NULL);
    GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);

    if (!pixbuf) {
        g_object_unref(loader);
        return NULL;
    }

    int img_width = gdk_pixbuf_get_width(pixbuf);
    int img_height = gdk_pixbuf_get_height(pixbuf);

    // Skaliere wenn zu breit
    double scale = 1.0;
    if (img_width > max_width) {
        scale = (double)max_width / img_width;
    }

    int display_width = (int)(img_width * scale);
    int display_height = (int)(img_height * scale);

    // Cairo Surface erstellen
    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, display_width, display_height);
    cairo_t *cr = cairo_create(surface);

    // Weißer Hintergrund
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Bild skalieren und zeichnen
    if (scale != 1.0) {
        cairo_scale(cr, scale, scale);
    }

    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
    cairo_paint(cr);

    cairo_destroy(cr);
    g_object_unref(loader);

    // RenderedDocument erstellen
    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = display_width;
    result->height = display_height;
    result->searchable_text = g_strdup("[Image - No searchable text]");
    result->type = DOC_TYPE_IMAGE;

    return result;
}


// === ODT/DOCX RENDERING (Generisch) ===

/*
 * Extraktion (ZIP/XML-Parsing) kommt aus sond_text_extract.c - dieselbe
 * Funktion, die auch beim Indizieren verwendet wird. out_attrs liefert
 * zusätzlich die Fett/Überschriften-Formatierung für die Anzeige, ohne
 * dass der Text dafür ein zweites Mal (potenziell abweichend) extrahiert
 * werden muss.
 */
static RenderedDocument* render_office_document(GBytes *bytes,
                                                 int width, DocumentType type) {
    gsize len;
    const guchar *data = g_bytes_get_data(bytes, &len);

    PangoAttrList *attr_list = NULL;
    GPtrArray *segs = (type == DOC_TYPE_ODT)
            ? sond_text_extract_odt(data, len, &attr_list)
            : sond_text_extract_docx(data, len, &attr_list);

    if (segs->len == 0) {
        LOG_WARN("render_office_document: Extraktion fehlgeschlagen (%s)",
                type == DOC_TYPE_ODT ? "ODT" : "DOCX");
        g_ptr_array_unref(segs);
        if (attr_list) pango_attr_list_unref(attr_list);
        return NULL;
    }

    SondTextSegment *seg = g_ptr_array_index(segs, 0);

    int height = 0;
    cairo_surface_t *surface = render_text_to_surface(
            seg->text, attr_list, width, 30000, &height);
    if (attr_list) pango_attr_list_unref(attr_list);
    if (!surface) { g_ptr_array_unref(segs); return NULL; }

    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = width;
    result->height = height;
    result->searchable_text = g_strdup(seg->text);
    result->type = type;

    g_ptr_array_unref(segs);
    return result;
}

// === PLAIN TEXT RENDERING ===

/*
 * Extraktion (Encoding-Korrektur + CRLF -> LF) kommt aus
 * sond_text_extract.c - dieselbe Funktion, die auch beim Indizieren
 * verwendet wird (siehe sond_text_extract.h). Vorher unterschied sich
 * diese Normalisierung von der beim Indizieren verwendeten, wodurch die
 * char_pos-Offsets aus der Indexsuche hier nicht mehr passten.
 */
static RenderedDocument* render_plain_text(guchar const *data, gsize len, int width) {
    GPtrArray *segs = sond_text_extract_plain(data, len);
    if (segs->len == 0) { g_ptr_array_unref(segs); return NULL; }

    SondTextSegment *seg = g_ptr_array_index(segs, 0);

    int height = 0;
    cairo_surface_t *surface = render_text_to_surface(
            seg->text, NULL, width, 30000, &height);
    if (!surface) { g_ptr_array_unref(segs); return NULL; }

    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = width;
    result->height = height;
    result->searchable_text = g_strdup(seg->text);
    result->type = DOC_TYPE_PLAIN;

    g_ptr_array_unref(segs);
    return result;
}

// === DOC RENDERING (Fallback) ===

static RenderedDocument* render_doc(int width) {
    GString *text = g_string_new("Microsoft Word DOC Format\n\n");
    g_string_append(text, "Dieses Format (.doc) kann nicht direkt dargestellt werden.\n\n");
    g_string_append(text, "Bitte konvertieren Sie die Datei in DOCX oder ODT.");

    char *text_str = g_string_free(text, FALSE);
    int height = 0;
    cairo_surface_t *surface = render_text_to_surface(
            text_str, NULL, width, 30000, &height);
    if (!surface) { g_free(text_str); return NULL; }

    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = width;
    result->height = height;
    result->searchable_text = text_str;
    result->type = DOC_TYPE_DOC;
    return result;
}

// === HAUPTFUNKTION ===

/**
 * Rendert einen Dokument-Stream zu einer Cairo Surface
 *
 * @param bytes GBytes mit Dokumentdaten
 * @param render_width Gewünschte Breite (0 = Standard 650px)
 * @return RenderedDocument oder NULL bei Fehler
 */
/*
 * Text (Header + Body) kommt komplett aus sond_text_extract.c
 * (sond_text_extract_gmessage) - dieselbe Funktion, die auch beim
 * Indizieren verwendet wird. Vorher bauten Indexer und Renderer den
 * E-Mail-Text getrennt und unterschiedlich formatiert auf (Indexer nur
 * Header ohne Body, Renderer Header+Body in anderer Reihenfolge), so
 * dass char_pos-Offsets aus der Indexsuche hier nie an der richtigen
 * Stelle landeten. Bild-Anhänge betreffen nur die Darstellung und werden
 * separat über sond_text_extract_gmessage_images() geholt.
 */
static RenderedDocument* render_gmessage(GBytes* bytes, int width) {
    if (!bytes) return NULL;
    if (width <= 0) width = DEFAULT_RENDER_WIDTH;

    gsize len;
    const guchar* data = g_bytes_get_data(bytes, &len);

    GPtrArray *segs = sond_text_extract_gmessage(data, len);
    if (segs->len == 0) {
        LOG_WARN("render_gmessage: Extraktion fehlgeschlagen oder leere Nachricht");
        g_ptr_array_unref(segs);
        return NULL;
    }
    SondTextSegment *seg = g_ptr_array_index(segs, 0);
    const gchar *full_text = seg->text;

    /* ---- Bild-Parts holen und als Pixbufs vorbereiten ---- */
    GPtrArray* images = sond_text_extract_gmessage_images(data, len);

    GPtrArray* image_surfaces = g_ptr_array_new_with_free_func(
            (GDestroyNotify)cairo_surface_destroy);
    GPtrArray* image_labels = g_ptr_array_new_with_free_func(g_free);

    for (guint i = 0; i < images->len; i++) {
        SondEmlImage* img = g_ptr_array_index(images, i);

        GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
        GError* perr = NULL;
        gdk_pixbuf_loader_write(loader, img->image_data, img->image_len, &perr);
        g_clear_error(&perr);
        gdk_pixbuf_loader_close(loader, NULL);

        GdkPixbuf* pb = gdk_pixbuf_loader_get_pixbuf(loader);
        if (!pb) {
            GdkPixbufAnimation* anim = gdk_pixbuf_loader_get_animation(loader);
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            if (anim)
                pb = gdk_pixbuf_animation_get_static_image(anim);
            G_GNUC_END_IGNORE_DEPRECATIONS
        }
        if (pb) {
            int iw = gdk_pixbuf_get_width(pb);
            int ih = gdk_pixbuf_get_height(pb);
            double scale = 1.0;
            if (iw > width - 20)
                scale = (double)(width - 20) / iw;
            int dw = (int)(iw * scale);
            int dh = (int)(ih * scale);
            cairo_surface_t* img_surf = cairo_image_surface_create(
                    CAIRO_FORMAT_ARGB32, dw, dh);
            cairo_t* ic = cairo_create(img_surf);
            cairo_scale(ic, scale, scale);
            gdk_cairo_set_source_pixbuf(ic, pb, 0, 0);
            cairo_paint(ic);
            cairo_destroy(ic);
            g_ptr_array_add(image_surfaces, img_surf);
            g_ptr_array_add(image_labels,
                    img->filename
                    ? g_strdup_printf("%s — %s", img->mime_type, img->filename)
                    : g_strdup(img->mime_type));
        } else {
            LOG_WARN("%s\nBild-Part konnte nicht geladen werden: %s",
                    __func__, img->mime_type ? img->mime_type : "?");
        }
        g_object_unref(loader);
    }
    g_ptr_array_unref(images);

    /* ---- PangoAttrList für Header (fett + Monospace bis zur ersten
     * Leerzeile) ---- */
    const char* sep_pos = strstr(full_text, "\n\n");
    guint header_end = sep_pos
            ? (guint)(sep_pos - full_text)
            : 0;

    PangoAttrList* attrs = pango_attr_list_new();
    if (header_end > 0) {
        PangoAttribute* bold = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
        bold->start_index = 0;
        bold->end_index   = header_end;
        pango_attr_list_insert(attrs, bold);

        PangoAttribute* mono = pango_attr_family_new("Monospace");
        mono->start_index = 0;
        mono->end_index   = header_end;
        pango_attr_list_insert(attrs, mono);
    }

    /* ---- Text rendern ---- */
    int height = 0;
    cairo_surface_t* surface = render_text_to_surface(
            full_text, attrs, width, 30000, &height);
    pango_attr_list_unref(attrs);
    if (!surface) {
        g_ptr_array_unref(segs);
        g_ptr_array_unref(image_surfaces);
        g_ptr_array_unref(image_labels);
        return NULL;
    }

    /* ---- Bilder unter Text anfügen ---- */
    int total_height = height;
    int img_gap = 12;

    for (guint i = 0; i < image_surfaces->len; i++) {
        cairo_surface_t* is = g_ptr_array_index(image_surfaces, i);
        total_height += img_gap + 18 + img_gap
                + cairo_image_surface_get_height(is);
    }

    if (image_surfaces->len > 0) {
        if (total_height > 30000) total_height = 30000;

        cairo_surface_t* combined = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, width, total_height);
        if (cairo_surface_status(combined) != CAIRO_STATUS_SUCCESS) {
            LOG_WARN("%s\ncombined surface fehlgeschlagen", __func__);
            cairo_surface_destroy(combined);
        } else {
            cairo_t* cc = cairo_create(combined);
            cairo_set_source_rgb(cc, 1.0, 1.0, 1.0);
            cairo_paint(cc);

            cairo_set_source_surface(cc, surface, 0, 0);
            cairo_paint(cc);
            cairo_surface_destroy(surface);

            int y_off = height;
            cairo_set_source_rgb(cc, 0.0, 0.0, 0.0);
            for (guint i = 0; i < image_surfaces->len; i++) {
                cairo_surface_t* is = g_ptr_array_index(image_surfaces, i);
                const char* lbl = g_ptr_array_index(image_labels, i);
                int iw = cairo_image_surface_get_width(is);
                int ih = cairo_image_surface_get_height(is);

                cairo_set_line_width(cc, 1.0);
                cairo_move_to(cc, 10, y_off + img_gap / 2.0);
                cairo_line_to(cc, width - 10, y_off + img_gap / 2.0);
                cairo_stroke(cc);
                y_off += img_gap;

                PangoLayout* lbl_layout = pango_cairo_create_layout(cc);
                pango_layout_set_text(lbl_layout, lbl, -1);
                PangoAttrList* lbl_attrs = pango_attr_list_new();
                PangoAttribute* lbl_bold = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                lbl_bold->start_index = 0;
                lbl_bold->end_index   = G_MAXUINT;
                pango_attr_list_insert(lbl_attrs, lbl_bold);
                pango_layout_set_attributes(lbl_layout, lbl_attrs);
                pango_attr_list_unref(lbl_attrs);
                cairo_move_to(cc, 10, y_off);
                pango_cairo_show_layout(cc, lbl_layout);
                g_object_unref(lbl_layout);
                y_off += 18;

                int x_off = (width - iw) / 2;
                if (x_off < 0) x_off = 0;
                cairo_set_source_surface(cc, is, x_off, y_off + img_gap / 2);
                cairo_paint(cc);
                y_off += img_gap + ih;

                cairo_set_source_rgb(cc, 0.0, 0.0, 0.0);

                if (y_off >= total_height) break;
            }

            cairo_destroy(cc);
            surface = combined;
            height  = total_height;
        }
    }

    g_ptr_array_unref(image_surfaces);
    g_ptr_array_unref(image_labels);

    RenderedDocument* result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width   = width;
    result->height  = height;
    result->searchable_text = g_strdup(full_text);
    result->type = DOC_TYPE_EML;

    g_ptr_array_unref(segs);
    return result;
}

static RenderedDocument* render_document_from_bytes(GBytes *bytes, int render_width,
        SondFilePart *sfp) {
    if (!bytes) {
    	LOG_WARN("Invalid bytes");
        return NULL;
    }

    if (render_width <= 0) {
        render_width = DEFAULT_RENDER_WIDTH;
    }

    /* Dokumenttyp: wenn sfp bekannt, direkt daraus ableiten */
    DocumentType type;
    if (sfp && SOND_IS_FILE_PART_GMESSAGE(sfp)) {
        type = DOC_TYPE_EML;
    } else if (sfp && (SOND_IS_FILE_PART_PDF(sfp) || SOND_IS_FILE_PART_ZIP(sfp))) {
        type = DOC_TYPE_UNKNOWN; /* wird nicht hier gerendert */
    } else if (sfp && SOND_IS_FILE_PART_LEAF(sfp)) {
        /* MIME-Type des Leaf direkt auswerten — wurde bereits durch
         * mime_guess_content_type (libmagic) korrekt bestimmt */
        const gchar *mime = sond_file_part_leaf_get_mime_type(
                SOND_FILE_PART_LEAF(sfp));
        if (!mime)
            type = DOC_TYPE_PLAIN;
        else if (g_str_has_prefix(mime, "image/"))
            type = DOC_TYPE_IMAGE;
        else if (g_str_equal(mime, "text/html"))
            type = DOC_TYPE_HTML;
        else if (g_str_equal(mime, "application/vnd.oasis.opendocument.text"))
            type = DOC_TYPE_ODT;
        else if (g_str_equal(mime, "application/vnd.openxmlformats-officedocument.wordprocessingml.document"))
            type = DOC_TYPE_DOCX;
        else if (g_str_equal(mime, "application/msword"))
            type = DOC_TYPE_DOC;
        else if (g_str_equal(mime, "message/rfc822"))
            type = DOC_TYPE_EML;
        else if (g_str_has_prefix(mime, "text/"))
            type = DOC_TYPE_PLAIN;
        else
            type = DOC_TYPE_PLAIN; /* unbekannt → als Text darstellen */
    } else {
        /* kein sfp — sollte nicht vorkommen, Fallback: Plain */
        type = DOC_TYPE_PLAIN;
    }

    RenderedDocument *result = NULL;

    // Je nach Typ rendern
    switch (type) {
    case DOC_TYPE_HTML: {
        gsize len;
        const guchar *data = g_bytes_get_data(bytes, &len);
        /* Encoding-Erkennung (UTF-8 / <meta charset> / windows-1252-
         * Fallback) passiert in sond_text_extract_html() - identisch
         * zur Indizierung. */
        result = render_html(data, len, render_width);
        break;
    }
    case DOC_TYPE_IMAGE:
            result = render_image(bytes, render_width);
            break;

        case DOC_TYPE_ODT:
        case DOC_TYPE_DOCX:
            result = render_office_document(bytes, render_width, type);
            break;

        case DOC_TYPE_DOC:
            result = render_doc(render_width);
            break;

        case DOC_TYPE_PLAIN:
        case DOC_TYPE_UNKNOWN:
        default: {
            gsize len;
            const guchar *data = g_bytes_get_data(bytes, &len);
            result = render_plain_text(data, len, render_width);
            break;
        }

        case DOC_TYPE_EML:
            result = render_gmessage(bytes, render_width);
            break;
    }

    return result;
}

/**
 * Typ als String zurückgeben
 */
const char* document_type_string(DocumentType type) {
    switch (type) {
        case DOC_TYPE_HTML: return "HTML";
        case DOC_TYPE_ODT: return "ODT";
        case DOC_TYPE_DOCX: return "DOCX";
        case DOC_TYPE_DOC: return "DOC (Legacy)";
        case DOC_TYPE_IMAGE: return "Image";
        case DOC_TYPE_PLAIN: return "Plain Text";
        case DOC_TYPE_EML: return "E-Mail";
        case DOC_TYPE_UNKNOWN:
        default: return "Unknown";
    }
}

/* =========================================================================
 * HIGHLIGHT-RENDERING
 * ======================================================================= */

/*
 * Rendert wie render_document_from_bytes, übergibt aber zusätzlich
 * einen Highlight-Term und eine Byte-Position im Flat-Text des Dokuments.
 * Der Term wird im Pango-Layout gelb hinterlegt; der Viewer scrollt beim
 * Öffnen zur ersten Fundstelle.
 *
 * char_pos_in_doc  – Byte-Offset im extrahierten searchable_text
 *                    (wie er vom Index gespeichert wird).
 *                    -1  → einfach zum ersten Vorkommen von term springen.
 *
 * WICHTIG: sfp muss durchgereicht werden, sonst kann
 * render_document_from_bytes() den Dokumenttyp nicht bestimmen und
 * rendert JEDES Dokument (auch HTML/DOCX/ODT/E-Mail) als Rohtext - dann
 * stimmt weder die Darstellung noch der char_pos-Offset (das war vorher
 * der Fall: sfp wurde hier fest auf NULL gesetzt).
 */
static RenderedDocument *
render_document_with_highlight(GBytes *bytes, SondFilePart *sfp, int render_width,
        const char *term, int char_pos_in_doc,
        int *out_highlight_y)
{
    if (!bytes || !term || !*term) return NULL;
    if (render_width <= 0) render_width = DEFAULT_RENDER_WIDTH;

    /* Erst normal rendern um den searchable_text zu bekommen */
    RenderedDocument *rd = render_document_from_bytes(bytes, render_width, sfp);
    if (!rd) return NULL;
    if (!rd->searchable_text) return rd; /* kein Text → unverändert zurück */

    /* ---------------------------------------------------------------
     * Trefferposition im searchable_text bestimmen
     * ------------------------------------------------------------- */
    const char *text    = rd->searchable_text;
    gsize       text_len = strlen(text);
    gsize       term_len = strlen(term);

    /* Groß-/Kleinschreibung ignorieren: beide Seiten casefold */
    char *text_cf = g_utf8_casefold(text, (gssize)text_len);
    char *term_cf = g_utf8_casefold(term, (gssize)term_len);

    /* Startposition: versuche char_pos_in_doc, sonst erstes Vorkommen */
    const char *hit = NULL;
    if (char_pos_in_doc >= 0 && (gsize)char_pos_in_doc < strlen(text_cf)) {
        /* Suche ab char_pos_in_doc rückwärts bis Wortanfang, damit
         * der Index-Offset exakt passt */
        hit = strstr(text_cf + char_pos_in_doc, term_cf);
        if (!hit) /* Fallback: erstes Vorkommen */
            hit = strstr(text_cf, term_cf);
    } else {
        hit = strstr(text_cf, term_cf);
    }

    if (!hit) {
        g_free(text_cf);
        g_free(term_cf);
        if (out_highlight_y) *out_highlight_y = 0;
        return rd;
    }

    gsize byte_start = (gsize)(hit - text_cf);
    gsize byte_end   = byte_start + term_len;
    g_free(text_cf);
    g_free(term_cf);

    /* ---------------------------------------------------------------
     * Surface neu mit Highlight-Attribut rendern
     * Wir bauen ein Pango-Layout auf dem original searchable_text
     * mit einem gelben Background-Attribut an byte_start..byte_end.
     * ------------------------------------------------------------- */
    int width  = rd->width;
    int height = rd->height;

    cairo_surface_t *surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);

    /* Weißer Hintergrund */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text, -1);

    /* Gelbes Highlight-Attribut */
    PangoAttrList *attrs = pango_attr_list_new();
    PangoAttribute *bg = pango_attr_background_new(
            0xFFFF, 0xEE00, 0x0000); /* RGB16: gelb */
    bg->start_index = (guint)byte_start;
    bg->end_index   = (guint)byte_end;
    pango_attr_list_insert(attrs, bg);
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);

    cairo_move_to(cr, 10, 10);
    pango_cairo_show_layout(cr, layout);

    /* Y-Position des Treffers für Scroll ermitteln */
    if (out_highlight_y) {
        PangoRectangle rect = {0};
        int idx = (int)byte_start;
        pango_layout_index_to_pos(layout, idx, &rect);
        *out_highlight_y = (int)(rect.y / PANGO_SCALE) + 10; /* +10 Rand */
    }

    g_object_unref(layout);
    cairo_destroy(cr);

    /* Alte Surface ersetzen */
    cairo_surface_destroy(rd->surface);
    rd->surface = surface;

    return rd;
}

/* Scrollt den SurfaceViewer nach dem Anzeigen zur Y-Position */
static gboolean
scroll_to_highlight_idle(gpointer data)
{
    gpointer *pair  = (gpointer *) data;
    GtkWidget *scrolled = GTK_WIDGET(pair[0]);
    int        y        = GPOINTER_TO_INT(pair[1]);
    g_free(pair);

    GtkAdjustment *vadj =
            gtk_scrolled_window_get_vadjustment(
                    GTK_SCROLLED_WINDOW(scrolled));
    double upper = gtk_adjustment_get_upper(vadj);
    double page  = gtk_adjustment_get_page_size(vadj);
    double target = (double)y - page / 3.0; /* Treffer im oberen Drittel */
    if (target < 0) target = 0;
    if (target > upper - page) target = upper - page;
    if (target < 0) target = 0;
    gtk_adjustment_set_value(vadj, target);

    return G_SOURCE_REMOVE;
}

gint sond_render_with_term(GBytes *input, SondFilePart *sfp,
        gchar const *title, gchar const *term,
        gint char_pos_in_doc, GError **error)
{
    if (!input) {
        if (error) *error = g_error_new(SOND_ERROR, 0,
                "%s\nKein Eingabe-Stream", __func__);
        return -1;
    }

    int highlight_y = 0;
    RenderedDocument *rd;

    if (term && *term)
        rd = render_document_with_highlight(input, sfp, 0, term,
                char_pos_in_doc, &highlight_y);
    else
        rd = render_document_from_bytes(input, 0, sfp);

    if (!rd) {
        if (error) *error = g_error_new(SOND_ERROR, 0,
                "%s\nStream konnte nicht gerendert werden", __func__);
        return -1;
    }

    const char *window_title = title ? title
            : (sfp ? sond_file_part_get_path(sfp)
                   : document_type_string(rd->type));

    GtkWidget *win = show_surface_viewer(rd->surface, rd->width, rd->height,
            window_title, rd->searchable_text, sfp);
    rd->surface = NULL;
    free_rendered_document(rd);

    if (!win) {
        if (error) *error = g_error_new(SOND_ERROR, 0,
                "%s\nFenster konnte nicht erstellt werden", __func__);
        return -1;
    }

    /* Zur Trefferposition scrollen (nach dem ersten GTK-Durchlauf) */
    if (highlight_y > 0) {
        /* ScrolledWindow aus dem Viewer-Fenster holen:
         * Aufbau: window > vbox > scrolled (2. Kind) */
        GtkWidget *vbox     = gtk_bin_get_child(GTK_BIN(win));
        GtkWidget *scrolled = NULL;
        if (vbox) {
            GList *children = gtk_container_get_children(GTK_CONTAINER(vbox));
            if (children && children->next)
                scrolled = GTK_WIDGET(children->next->data);
            g_list_free(children);
        }
        if (scrolled) {
            gpointer *pair = g_new(gpointer, 2);
            pair[0] = scrolled;
            pair[1] = GINT_TO_POINTER(highlight_y);
            g_idle_add(scroll_to_highlight_idle, pair);
        }
    }

    return 0;
}

// === EML RENDERING ===

gint sond_render(GBytes* input, SondFilePart* sfp, gchar const* title,
		GError** error) {
	RenderedDocument* rd = NULL;
	GtkWidget* widget = NULL;
	const char* window_title = NULL;

	rd = render_document_from_bytes(input, 0, sfp);
	if (!rd) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nStream konnte nicht gerendert werden", __func__);

		return -1;
	}

	if (title)
		window_title = title;
	else if (sfp)
		window_title = sond_file_part_get_path(sfp);
	else
		window_title = document_type_string(rd->type);

	widget = show_surface_viewer(rd->surface, rd->width, rd->height,
			window_title, rd->searchable_text, sfp);
	rd->surface = NULL;
	free_rendered_document(rd);
	if (!widget) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nStream konnte nicht gerendert werden", __func__);

		return -1;
	}

	return 0;
}
