/*
 * renderer.c
 *
 *  Created on: 14.12.2025
 *      Author: pkrieger
 */


#include <gtk/gtk.h>
#include <cairo.h>
#include <string.h>
#include <mupdf/fitz.h>
#include <lexbor/html/html.h>
#include <string.h>
#include <unistd.h>

#include "sond.h"
#include "sond_log_and_error.h"

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
} SurfaceViewer;

// Forward declarations
static void update_display(SurfaceViewer *viewer);
static void update_statusbar(SurfaceViewer *viewer, const char *message);

// === STATUSBAR ===

static void update_statusbar(SurfaceViewer *viewer, const char *message) {
    gtk_statusbar_pop(GTK_STATUSBAR(viewer->statusbar), viewer->statusbar_context);
    gtk_statusbar_push(GTK_STATUSBAR(viewer->statusbar), viewer->statusbar_context, message);
}

// === ZOOM ===

static void update_display(SurfaceViewer *viewer) {
    if (!viewer->original_surface) return;

    int new_width = (int)(viewer->original_width * viewer->zoom_level);
    int new_height = (int)(viewer->original_height * viewer->zoom_level);

    cairo_surface_t *scaled = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, new_width, new_height);
    cairo_t *cr = cairo_create(scaled);

    cairo_scale(cr, viewer->zoom_level, viewer->zoom_level);
    cairo_set_source_surface(cr, viewer->original_surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    GdkPixbuf *pixbuf = gdk_pixbuf_get_from_surface(scaled, 0, 0, new_width, new_height);
    gtk_image_set_from_pixbuf(GTK_IMAGE(viewer->image), pixbuf);

    char zoom_text[32];
    snprintf(zoom_text, sizeof(zoom_text), "Zoom: %d%%", (int)(viewer->zoom_level * 100));
    gtk_label_set_text(GTK_LABEL(viewer->zoom_label), zoom_text);

    g_object_unref(pixbuf);
    cairo_surface_destroy(scaled);
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
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(viewer->window), GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Print error: %s", error->message);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_error_free(error);
    } else {
        update_statusbar(viewer, "Document printed");
    }

    g_object_unref(print);
}

// === PDF EXPORT ===

static void on_export_pdf(GtkWidget *widget, gpointer data) {
    SurfaceViewer *viewer = (SurfaceViewer*)data;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Save as PDF", GTK_WINDOW(viewer->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT, NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "document.pdf");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        // Verwende GTK Print statt cairo-pdf
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
            GtkWidget *err_dialog = gtk_message_dialog_new(
                GTK_WINDOW(viewer->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                "Export error: %s", (error) ? error->message : "Druckerfehler");
            gtk_dialog_run(GTK_DIALOG(err_dialog));
            gtk_widget_destroy(err_dialog);
            g_error_free(error);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "Exported to %s", filename);
            update_statusbar(viewer, msg);
        }

        g_object_unref(settings);
        g_object_unref(print);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
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
    g_free(viewer);
}

static void on_window_destroy(GtkWidget *widget, gpointer data) {
    cleanup_viewer((SurfaceViewer*)data);
}

// === HAUPTFUNKTION ===

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
                               const char *title, const char *searchable_text) {
    if (!surface) return NULL;

    SurfaceViewer *viewer = g_new0(SurfaceViewer, 1);

    // Surface kopieren (damit Original freigegeben werden kann)
    viewer->original_surface = cairo_surface_create_similar(
        surface, CAIRO_CONTENT_COLOR_ALPHA, width, height);
    cairo_t *cr = cairo_create(viewer->original_surface);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    viewer->original_width = width;
    viewer->original_height = height;
    viewer->zoom_level = 1.0;
    viewer->fullscreen = FALSE;

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

    // ScrolledWindow mit Image
    viewer->scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(viewer->scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), viewer->scrolled, TRUE, TRUE, 0);

    // Event Box für Mouse-Events
    GtkWidget *event_box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(viewer->scrolled), event_box);

    g_signal_connect(event_box, "scroll-event", G_CALLBACK(on_scroll_event), viewer);
    g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_button_press), viewer);
    g_signal_connect(event_box, "button-release-event", G_CALLBACK(on_button_release), viewer);
    g_signal_connect(event_box, "motion-notify-event", G_CALLBACK(on_motion_notify), viewer);
    gtk_widget_set_events(event_box, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK |
                         GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);

    GdkPixbuf *pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);
    viewer->image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);

    gtk_container_add(GTK_CONTAINER(event_box), viewer->image);

    // Statusbar
    viewer->statusbar = gtk_statusbar_new();
    viewer->statusbar_context = gtk_statusbar_get_context_id(
        GTK_STATUSBAR(viewer->statusbar), "main");
    gtk_box_pack_start(GTK_BOX(vbox), viewer->statusbar, FALSE, FALSE, 0);
    update_statusbar(viewer, "Ready");

    // Tastenkombinationen
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

    gtk_widget_show_all(viewer->window);

    return viewer->window;
}

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <zip.h>
#include <time.h>

#define DEFAULT_RENDER_WIDTH 650

typedef enum {
    DOC_TYPE_UNKNOWN,
    DOC_TYPE_HTML,
    DOC_TYPE_ODT,
    DOC_TYPE_DOCX,
    DOC_TYPE_DOC,
    DOC_TYPE_IMAGE
} DocumentType;

typedef struct {
    cairo_surface_t *surface;
    int width;
    int height;
    char *searchable_text;
    DocumentType type;
} RenderedDocument;

// === TYPE DETECTION ===

static gboolean check_zip_contains_file(unsigned char *data, size_t len, const char *filename) {
    zip_error_t error;
    zip_source_t *src;
    zip_t *archive;

    zip_error_init(&error);
    src = zip_source_buffer_create(data, len, 0, &error);
    if (!src) {
        zip_error_fini(&error);
        return FALSE;
    }

    archive = zip_open_from_source(src, ZIP_RDONLY, &error);
    if (!archive) {
        zip_source_free(src);
        zip_error_fini(&error);
        return FALSE;
    }

    struct zip_stat st;
    zip_stat_init(&st);
    gboolean found = (zip_stat(archive, filename, 0, &st) == 0);

    zip_close(archive);
    return found;
}

static DocumentType detect_document_type(fz_context *ctx, fz_buffer *buf) {
    unsigned char *data;
    size_t len = fz_buffer_storage(ctx, buf, &data);

    if (len < 4) return DOC_TYPE_UNKNOWN;

    // ZIP-Signatur (ODT/DOCX sind beide ZIP)
    if (data[0] == 0x50 && data[1] == 0x4B && data[2] == 0x03 && data[3] == 0x04) {
        // Unterscheide ODT von DOCX
        if (check_zip_contains_file(data, len, "content.xml")) {
            return DOC_TYPE_ODT;
        }
        if (check_zip_contains_file(data, len, "word/document.xml")) {
            return DOC_TYPE_DOCX;
        }
        return DOC_TYPE_UNKNOWN;  // Anderes ZIP
    }

    // DOC (Microsoft Compound File Binary Format)
    if (len >= 8 && data[0] == 0xD0 && data[1] == 0xCF &&
        data[2] == 0x11 && data[3] == 0xE0 &&
        data[4] == 0xA1 && data[5] == 0xB1 &&
        data[6] == 0x1A && data[7] == 0xE1) {
        return DOC_TYPE_DOC;
    }

    // PNG-Signatur
    if (len >= 8 && data[0] == 0x89 && data[1] == 'P' &&
        data[2] == 'N' && data[3] == 'G') {
        return DOC_TYPE_IMAGE;
    }

    // JPEG-Signatur
    if (data[0] == 0xFF && data[1] == 0xD8) {
        return DOC_TYPE_IMAGE;
    }

    // GIF-Signatur
    if (len >= 6 && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) {
        return DOC_TYPE_IMAGE;
    }

    // BMP-Signatur
    if (len >= 2 && data[0] == 'B' && data[1] == 'M') {
        return DOC_TYPE_IMAGE;
    }

    // WebP-Signatur
    if (len >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0) {
        return DOC_TYPE_IMAGE;
    }

    // HTML-Erkennung
    char *str = (char*)data;
    size_t check_len = (len > 512) ? 512 : len;
    char check_buf[513];
    strncpy(check_buf, str, check_len);
    check_buf[check_len] = '\0';

    char *lower = g_ascii_strdown(check_buf, check_len);
    if (strstr(lower, "<html") || strstr(lower, "<!doctype") ||
        strstr(lower, "<?xml") || strstr(lower, "<head") || strstr(lower, "<body")) {
        g_free(lower);
        return DOC_TYPE_HTML;
    }
    g_free(lower);

    return DOC_TYPE_UNKNOWN;
}

// === TEXT EXTRACTION FROM HTML ===

static void extract_text_recursive(lxb_dom_node_t *node, GString *text) {
    if (!node) return;

    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        size_t len;
        const lxb_char_t *content = lxb_dom_node_text_content(node, &len);
        if (content && len > 0) {
            g_string_append_len(text, (const char*)content, len);
        }
    }

    // Block-Elemente mit Zeilenumbrüchen
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *elem = lxb_dom_interface_element(node);
        size_t tag_len;
        const lxb_char_t *tag = lxb_dom_element_qualified_name(elem, &tag_len);

        if (tag_len == 2 && memcmp(tag, "br", 2) == 0) {
            g_string_append(text, "\n");
        } else if (tag_len == 1 && memcmp(tag, "p", 1) == 0) {
            if (text->len > 0 && text->str[text->len-1] != '\n') {
                g_string_append(text, "\n\n");
            }
        } else if ((tag_len == 2 && (memcmp(tag, "h1", 2) == 0 ||
                                     memcmp(tag, "h2", 2) == 0 ||
                                     memcmp(tag, "h3", 2) == 0 ||
                                     memcmp(tag, "h4", 2) == 0 ||
                                     memcmp(tag, "h5", 2) == 0 ||
                                     memcmp(tag, "h6", 2) == 0)) ||
                   (tag_len == 3 && memcmp(tag, "div", 3) == 0) ||
                   (tag_len == 10 && memcmp(tag, "blockquote", 10) == 0)) {
            if (text->len > 0 && text->str[text->len-1] != '\n') {
                g_string_append(text, "\n");
            }
        }
    }

    // Rekursiv durch Kinder
    lxb_dom_node_t *child = node->first_child;
    while (child) {
        extract_text_recursive(child, text);
        child = child->next;
    }

    // Nach Block-Elementen Zeilenumbruch
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *elem = lxb_dom_interface_element(node);
        size_t tag_len;
        const lxb_char_t *tag = lxb_dom_element_qualified_name(elem, &tag_len);

        if ((tag_len == 1 && memcmp(tag, "p", 1) == 0) ||
            (tag_len == 2 && (memcmp(tag, "h1", 2) == 0 ||
                             memcmp(tag, "h2", 2) == 0 ||
                             memcmp(tag, "h3", 2) == 0)) ||
            (tag_len == 3 && memcmp(tag, "div", 3) == 0)) {
            if (text->len > 0 && text->str[text->len-1] != '\n') {
                g_string_append(text, "\n");
            }
        }
    }
}

// === HTML RENDERING ===

static RenderedDocument* render_html(const char *html, int width) {
    lxb_html_document_t *doc = lxb_html_document_create();
    if (!doc) return NULL;

    lxb_status_t status = lxb_html_document_parse(doc,
        (const lxb_char_t*)html, strlen(html));

    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        return NULL;
    }

    // Text extrahieren
    GString *text = g_string_new("");
    lxb_dom_node_t *body = lxb_dom_interface_node(doc->body);
    if (body) {
        extract_text_recursive(body, text);
    }

    // Höhe berechnen
    cairo_surface_t *tmp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    if (cairo_surface_status(tmp_surface) != CAIRO_STATUS_SUCCESS) {
        LOG_WARN("Failed to create temporary surface for height calculation");
        cairo_surface_destroy(tmp_surface);
        g_string_free(text, TRUE);
        lxb_html_document_destroy(doc);
        return NULL;
    }
    
    cairo_t *tmp_cr = cairo_create(tmp_surface);
    if (cairo_status(tmp_cr) != CAIRO_STATUS_SUCCESS) {
        LOG_WARN("Failed to create temporary cairo context");
        cairo_destroy(tmp_cr);
        cairo_surface_destroy(tmp_surface);
        g_string_free(text, TRUE);
        lxb_html_document_destroy(doc);
        return NULL;
    }

    PangoLayout *layout = pango_cairo_create_layout(tmp_cr);
    pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text->str, -1);

    int pango_width, pango_height;
    pango_layout_get_pixel_size(layout, &pango_width, &pango_height);
    int height = pango_height + 20;

    g_object_unref(layout);
    cairo_destroy(tmp_cr);
    cairo_surface_destroy(tmp_surface);

    // Echte Surface rendern
    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        LOG_WARN("Failed to create render surface (width=%d, height=%d)", width, height);
        cairo_surface_destroy(surface);
        g_string_free(text, TRUE);
        lxb_html_document_destroy(doc);
        return NULL;
    }
    
    cairo_t *cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        LOG_WARN("Failed to create cairo context for rendering");
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_string_free(text, TRUE);
        lxb_html_document_destroy(doc);
        return NULL;
    }

    // Weißer Hintergrund
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Schwarzer Text
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);

    layout = pango_cairo_create_layout(cr);
    pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text->str, -1);

    cairo_move_to(cr, 10, 10);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
    cairo_destroy(cr);
    lxb_html_document_destroy(doc);

    // RenderedDocument erstellen
    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = width;
    result->height = height;
    result->searchable_text = g_string_free(text, FALSE);
    result->type = DOC_TYPE_HTML;

    return result;
}

// === IMAGE RENDERING ===

static RenderedDocument* render_image(fz_context *ctx, fz_buffer *buf, int max_width) {
    unsigned char *data;
    size_t len = fz_buffer_storage(ctx, buf, &data);

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

// Verarbeite DOCX-Node (ähnlich zu ODT, aber andere Element-Namen)
static char* extract_from_zip(unsigned char *zip_data, size_t zip_len, const char *filename, size_t *out_len) {
    zip_error_t error;
    zip_source_t *src;
    zip_t *archive;

    // ZIP aus Memory öffnen
    zip_error_init(&error);
    src = zip_source_buffer_create(zip_data, zip_len, 0, &error);
    if (!src) {
    	LOG_WARN("Failed to create zip source: %s", zip_error_strerror(&error));
        zip_error_fini(&error);
        return NULL;
    }

    archive = zip_open_from_source(src, ZIP_RDONLY, &error);
    if (!archive) {
    	LOG_WARN("Failed to open zip: %s", zip_error_strerror(&error));
        zip_source_free(src);
        zip_error_fini(&error);
        return NULL;
    }

    // Datei im Archiv finden
    struct zip_stat st;
    zip_stat_init(&st);
    if (zip_stat(archive, filename, 0, &st) != 0) {
    	LOG_WARN("File '%s' not found in archive", filename);
        zip_close(archive);
        return NULL;
    }

    // Datei öffnen
    zip_file_t *file = zip_fopen(archive, filename, 0);
    if (!file) {
    	LOG_WARN("Failed to open '%s' in archive", filename);
        zip_close(archive);
        return NULL;
    }

    // Inhalt lesen
    char *content = g_malloc(st.size + 1);
    zip_int64_t bytes_read = zip_fread(file, content, st.size);

    if (bytes_read < 0) {
    	LOG_WARN("Failed to read '%s'", filename);
        g_free(content);
        zip_fclose(file);
        zip_close(archive);
        return NULL;
    }

    content[bytes_read] = '\0';

    if (out_len) {
        *out_len = bytes_read;
    }

    zip_fclose(file);
    zip_close(archive);

    return content;
}

static void process_docx_node(xmlNode *node, GString *text, PangoAttrList **attr_list, int *char_offset) {
    if (!node) return;

    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type == XML_TEXT_NODE) {
            if (cur->content) {
                const char *content = (const char*)cur->content;
                g_string_append(text, content);
                if (attr_list && char_offset) {
                    *char_offset += strlen(content);
                }
            }
        } else if (cur->type == XML_ELEMENT_NODE) {
            const char *name = (const char*)cur->name;

            // DOCX: w:t = Text
            if (strcmp(name, "t") == 0) {
                if (cur->children && cur->children->content) {
                    const char *content = (const char*)cur->children->content;
                    g_string_append(text, content);
                    *char_offset += strlen(content);
                }
                continue;
            }

            // DOCX: w:p = Paragraph
            if (strcmp(name, "p") == 0) {
                process_docx_node(cur->children, text, attr_list, char_offset);
                g_string_append(text, "\n\n");
                *char_offset += 2;
                continue;
            }

            // DOCX: w:br = Line Break
            if (strcmp(name, "br") == 0) {
                g_string_append(text, "\n");
                (*char_offset)++;
                continue;
            }

            // DOCX: w:tab = Tab
            if (strcmp(name, "tab") == 0) {
                g_string_append(text, "    ");
                *char_offset += 4;
                continue;
            }

            // DOCX: w:r = Run (Textlauf mit Formatierung)
            if (strcmp(name, "r") == 0) {
                int run_start = *char_offset;
                gboolean is_bold = FALSE;

                // Prüfe auf Formatierung (w:rPr)
                for (xmlNode *child = cur->children; child; child = child->next) {
                    if (child->type == XML_ELEMENT_NODE && strcmp((char*)child->name, "rPr") == 0) {
                        // Suche nach w:b (bold)
                        for (xmlNode *prop = child->children; prop; prop = prop->next) {
                            if (prop->type == XML_ELEMENT_NODE && strcmp((char*)prop->name, "b") == 0) {
                                is_bold = TRUE;
                                break;
                            }
                        }
                    }
                }

                process_docx_node(cur->children, text, attr_list, char_offset);

                if (is_bold && attr_list && *attr_list) {
                    PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                    attr->start_index = run_start;
                    attr->end_index = *char_offset;
                    pango_attr_list_insert(*attr_list, attr);
                }
                continue;
            }

            // Rekursiv
            process_docx_node(cur->children, text, attr_list, char_offset);
        }
    }
}

static void process_odt_node(xmlNode *node, GString *text, PangoAttrList **attr_list, int *char_offset) {
    if (!node) return;

    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type == XML_TEXT_NODE) {
            if (cur->content) {
                const char *content = (const char*)cur->content;
                g_string_append(text, content);
                if (attr_list && char_offset) {
                    *char_offset += strlen(content);
                }
            }
        } else if (cur->type == XML_ELEMENT_NODE) {
            const char *name = (const char*)cur->name;

            // Überschriften
            if (strcmp(name, "h") == 0) {
                g_string_append(text, "\n");
                (*char_offset)++;

                int heading_start = *char_offset;
                process_odt_node(cur->children, text, attr_list, char_offset);

                // Überschrift fett und größer machen
                if (attr_list && *attr_list) {
                    PangoAttribute *attr;

                    attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                    attr->start_index = heading_start;
                    attr->end_index = *char_offset;
                    pango_attr_list_insert(*attr_list, attr);

                    attr = pango_attr_scale_new(1.5);
                    attr->start_index = heading_start;
                    attr->end_index = *char_offset;
                    pango_attr_list_insert(*attr_list, attr);
                }

                g_string_append(text, "\n\n");
                *char_offset += 2;
                continue;
            }

            // Absätze
            if (strcmp(name, "p") == 0) {
                process_odt_node(cur->children, text, attr_list, char_offset);
                g_string_append(text, "\n\n");
                *char_offset += 2;
                continue;
            }

            // Listen
            if (strcmp(name, "list-item") == 0) {
                g_string_append(text, "• ");
                *char_offset += 4;
                process_odt_node(cur->children, text, attr_list, char_offset);
                continue;
            }

            // Tabellenzellen
            if (strcmp(name, "table-cell") == 0) {
                process_odt_node(cur->children, text, attr_list, char_offset);
                g_string_append(text, " | ");
                *char_offset += 3;
                continue;
            }

            // Tabellenzeilen
            if (strcmp(name, "table-row") == 0) {
                process_odt_node(cur->children, text, attr_list, char_offset);
                g_string_append(text, "\n");
                (*char_offset)++;
                continue;
            }

            // Fett
            if (strcmp(name, "span") == 0) {
                xmlChar *style = xmlGetProp(cur, (xmlChar*)"style-name");
                int span_start = *char_offset;

                process_odt_node(cur->children, text, attr_list, char_offset);

                // Vereinfacht: Manche Styles sind fett
                if (style && (strstr((char*)style, "Bold") || strstr((char*)style, "bold"))) {
                    if (attr_list && *attr_list) {
                        PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                        attr->start_index = span_start;
                        attr->end_index = *char_offset;
                        pango_attr_list_insert(*attr_list, attr);
                    }
                }

                if (style) xmlFree(style);
                continue;
            }

            // Line Break
            if (strcmp(name, "line-break") == 0) {
                g_string_append(text, "\n");
                (*char_offset)++;
                continue;
            }

            // Tab
            if (strcmp(name, "tab") == 0) {
                g_string_append(text, "    ");
                *char_offset += 4;
                continue;
            }

            // Rekursiv für andere Elemente
            process_odt_node(cur->children, text, attr_list, char_offset);
        }
    }
}

static RenderedDocument* render_office_document(fz_context *ctx, fz_buffer *buf,
                                                 int width, DocumentType type) {
    unsigned char *data;
    size_t len = fz_buffer_storage(ctx, buf, &data);

    const char *xml_file;
    if (type == DOC_TYPE_ODT) {
        xml_file = "content.xml";
    } else if (type == DOC_TYPE_DOCX) {
        xml_file = "word/document.xml";
    } else {
        return NULL;
    }

    // XML aus ZIP extrahieren
    size_t xml_len;
    char *xml_content = extract_from_zip(data, len, xml_file, &xml_len);

    if (!xml_content) {
    	LOG_WARN("Failed to extract %s from document", xml_file);
        return NULL;
    }

    // XML parsen
    xmlDoc *doc = xmlReadMemory(xml_content, xml_len, xml_file, NULL, 0);
    g_free(xml_content);

    if (!doc) {
    	LOG_WARN("Failed to parse %s", xml_file);
        return NULL;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    if (!root) {
        xmlFreeDoc(doc);
        return NULL;
    }

    // Text mit Formatierung extrahieren
    GString *text = g_string_new("");
    PangoAttrList *attr_list = pango_attr_list_new();
    int char_offset = 0;

    if (type == DOC_TYPE_ODT) {
        // ODT: office:body > office:text
        for (xmlNode *node = root->children; node; node = node->next) {
            if (node->type == XML_ELEMENT_NODE &&
                strcmp((char*)node->name, "body") == 0) {
                for (xmlNode *child = node->children; child; child = child->next) {
                    if (child->type == XML_ELEMENT_NODE) {
                        process_odt_node(child->children, text, &attr_list, &char_offset);
                    }
                }
            }
        }
    } else if (type == DOC_TYPE_DOCX) {
        // DOCX: w:document > w:body
        for (xmlNode *node = root->children; node; node = node->next) {
            if (node->type == XML_ELEMENT_NODE && strcmp((char*)node->name, "body") == 0) {
                process_docx_node(node->children, text, &attr_list, &char_offset);
            }
        }
    }

    xmlFreeDoc(doc);

    if (text->len == 0) {
        g_string_append_printf(text, "%s Document\n\n(No readable content found)",
                              type == DOC_TYPE_ODT ? "ODT" : "DOCX");
    }

    // Höhe berechnen
    cairo_surface_t *tmp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *tmp_cr = cairo_create(tmp_surface);

    PangoLayout *layout = pango_cairo_create_layout(tmp_cr);
    pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text->str, -1);
    pango_layout_set_attributes(layout, attr_list);

    int pango_width, pango_height;
    pango_layout_get_pixel_size(layout, &pango_width, &pango_height);
    int height = pango_height + 20;

    g_object_unref(layout);
    cairo_destroy(tmp_cr);
    cairo_surface_destroy(tmp_surface);

    // Echte Surface rendern
    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);

    layout = pango_cairo_create_layout(cr);
    pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text->str, -1);
    pango_layout_set_attributes(layout, attr_list);

    cairo_move_to(cr, 10, 10);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
    cairo_destroy(cr);
    pango_attr_list_unref(attr_list);

    // RenderedDocument erstellen
    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = width;
    result->height = height;
    result->searchable_text = g_string_free(text, FALSE);
    result->type = type;

    return result;
}

// === DOC RENDERING (Fallback) ===

static RenderedDocument* render_doc(fz_context *ctx, fz_buffer *buf, int width) {
    // DOC ist binär und sehr komplex
    // Fallback: Zeige Hinweis, dass Konvertierung nötig ist

    GString *text = g_string_new("Microsoft Word DOC Format\n\n");
    g_string_append(text, "This is a legacy binary format (.doc) that cannot be rendered directly.\n\n");
    g_string_append(text, "Suggestions:\n");
    g_string_append(text, "• Convert to DOCX using Microsoft Word or LibreOffice\n");
    g_string_append(text, "• Use 'antiword' command-line tool to extract text\n");
    g_string_append(text, "• Use LibreOffice headless mode to convert:\n");
    g_string_append(text, "  libreoffice --headless --convert-to docx file.doc\n");

    // Einfaches Rendering
    cairo_surface_t *tmp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *tmp_cr = cairo_create(tmp_surface);

    PangoLayout *layout = pango_cairo_create_layout(tmp_cr);
    pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text->str, -1);

    int pango_width, pango_height;
    pango_layout_get_pixel_size(layout, &pango_width, &pango_height);
    int height = pango_height + 20;

    g_object_unref(layout);
    cairo_destroy(tmp_cr);
    cairo_surface_destroy(tmp_surface);

    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);

    layout = pango_cairo_create_layout(cr);
    pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text->str, -1);

    cairo_move_to(cr, 10, 10);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
    cairo_destroy(cr);

    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = width;
    result->height = height;
    result->searchable_text = g_string_free(text, FALSE);
    result->type = DOC_TYPE_DOC;

    return result;
}

// Extrahiere Datei aus ZIP-Archiv
// ODT XML zu formatiertem Text konvertieren
static RenderedDocument* render_odt(fz_context *ctx, fz_buffer *buf, int width) {
    unsigned char *data;
    size_t len = fz_buffer_storage(ctx, buf, &data);

    // content.xml aus ZIP extrahieren
    size_t xml_len;
    char *xml_content = extract_from_zip(data, len, "content.xml", &xml_len);

    if (!xml_content) {
    	LOG_WARN("Failed to extract content.xml from ODT");
        return NULL;
    }

    // XML parsen
    xmlDoc *doc = xmlReadMemory(xml_content, xml_len, "content.xml", NULL, 0);
    g_free(xml_content);

    if (!doc) {
    	LOG_WARN("Failed to parse content.xml");
        return NULL;
    }

    // Root-Element
    xmlNode *root = xmlDocGetRootElement(doc);
    if (!root) {
        xmlFreeDoc(doc);
        return NULL;
    }

    // Text mit Formatierung extrahieren
    GString *text = g_string_new("");
    PangoAttrList *attr_list = pango_attr_list_new();
    int char_offset = 0;

    // Finde office:body > office:text
    for (xmlNode *node = root->children; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE &&
            strcmp((char*)node->name, "body") == 0) {
            for (xmlNode *child = node->children; child; child = child->next) {
                if (child->type == XML_ELEMENT_NODE) {
                    process_odt_node(child->children, text, &attr_list, &char_offset);
                }
            }
        }
    }

    xmlFreeDoc(doc);

    if (text->len == 0) {
        g_string_append(text, "ODT Document\n\n(No readable content found)");
    }

    // Höhe berechnen
    cairo_surface_t *tmp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *tmp_cr = cairo_create(tmp_surface);

    PangoLayout *layout = pango_cairo_create_layout(tmp_cr);
    pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text->str, -1);
    pango_layout_set_attributes(layout, attr_list);

    int pango_width, pango_height;
    pango_layout_get_pixel_size(layout, &pango_width, &pango_height);
    int height = pango_height + 20;

    g_object_unref(layout);
    cairo_destroy(tmp_cr);
    cairo_surface_destroy(tmp_surface);

    // Echte Surface rendern
    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);

    layout = pango_cairo_create_layout(cr);
    pango_layout_set_width(layout, (width - 20) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, text->str, -1);
    pango_layout_set_attributes(layout, attr_list);

    cairo_move_to(cr, 10, 10);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
    cairo_destroy(cr);
    pango_attr_list_unref(attr_list);

    // RenderedDocument erstellen
    RenderedDocument *result = g_new0(RenderedDocument, 1);
    result->surface = surface;
    result->width = width;
    result->height = height;
    result->searchable_text = g_string_free(text, FALSE);
    result->type = DOC_TYPE_ODT;

    return result;
}

// === HAUPTFUNKTION ===

/**
 * Rendert einen Dokument-Stream zu einer Cairo Surface
 *
 * @param ctx MuPDF Context
 * @param stream fz_stream mit Dokumentdaten
 * @param render_width Gewünschte Breite (0 = Standard 650px)
 * @return RenderedDocument oder NULL bei Fehler
 */
RenderedDocument* render_document_from_stream(fz_context *ctx,
		fz_buffer* buf, int render_width) {
    if (!ctx || !buf) {
    	LOG_WARN("Invalid context or buffer");
        return NULL;
    }

    if (render_width <= 0) {
        render_width = DEFAULT_RENDER_WIDTH;
    }

    // Dokumenttyp erkennen
    DocumentType type = detect_document_type(ctx, buf);

    RenderedDocument *result = NULL;

    // Je nach Typ rendern
    switch (type) {
        case DOC_TYPE_HTML: {
            unsigned char *data;
            size_t len = fz_buffer_storage(ctx, buf, &data);
            char *html = g_strndup((char*)data, len);
            result = render_html(html, render_width);
            g_free(html);
            break;
        }

        case DOC_TYPE_IMAGE:
            result = render_image(ctx, buf, render_width);
            break;

        case DOC_TYPE_ODT:
        case DOC_TYPE_DOCX:
            result = render_office_document(ctx, buf, render_width, type);
            break;

        case DOC_TYPE_DOC:
            result = render_doc(ctx, buf, render_width);
            break;

        case DOC_TYPE_UNKNOWN:
        default: {
            // Fallback: Als Plain Text behandeln
            unsigned char *data;
            size_t len = fz_buffer_storage(ctx, buf, &data);
            char *text = g_strndup((char*)data, len);

            // Erstelle einfache Textdarstellung
            GString *formatted = g_string_new("Unknown Document Type\n\n");
            g_string_append(formatted, text);
            g_free(text);

            result = render_html(formatted->str, render_width);
            if (result) {
                result->type = DOC_TYPE_UNKNOWN;
            }
            g_string_free(formatted, TRUE);
            break;
        }
    }

    return result;
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
        case DOC_TYPE_UNKNOWN:
        default: return "Unknown";
    }
}

gint sond_render(fz_context* ctx, fz_buffer* input, gchar const* title,
		GError** error) {
	RenderedDocument* rd = NULL;
	GtkWidget* widget = NULL;

	rd = render_document_from_stream(ctx, input, 0);
	if (!rd) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nStream konnte nicht gerendert werden", __func__);

		return -1;
	}

	widget = show_surface_viewer(rd->surface, rd->width, rd->height,
			title ? title : document_type_string(rd->type),
			rd->searchable_text);
	free_rendered_document(rd);
	if (!widget) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nStream konnte nicht gerendert werden", __func__);

		return -1;
	}

	return 0;
}
