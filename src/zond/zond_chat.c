/*
 zond (zond_chat.c) - Akten, Beweisstücke, Unterlagen
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

#include "zond_chat.h"

#include <gtk/gtk.h>
#include <glib.h>

#include "../misc.h"
#include "../sond_index.h"
#include "../sond_chat.h"
#include "../sond_result_view.h"
#include "../sond_process_file.h"

#include "zond_init.h"
#include "zond_indexsuche.h"
#include "zond_pdf_document.h"

#include "20allgemein/project.h"

#include "99conv/general.h"
#include "40viewer/viewer.h"

/* Wieviele Fundstellen höchstens an das Chat-Modell als Kontext übergeben
 * werden (muss mit SOND_CHAT_MAX_CONTEXT_HITS in sond_chat.c nicht exakt
 * übereinstimmen - sond_chat_answer() kappt selbst zusätzlich - aber mehr
 * als hier geholt anzuzeigen wäre ohnehin nutzlos). */
#define ZOND_CHAT_TOP_K 15

/* -------------------------------------------------------------------------
 * Hintergrund-Thread: semantische Suche + Antwortgenerierung
 *
 * Beides sind blockierende, auf CPU je nach Modell/Hardware bis zu ca.
 * einer Minute dauernde Aufrufe (llama.cpp). Analog zum bestehenden Muster
 * für die Indizierung (siehe headerbar.c, do_index_thread/do_index_erstellen):
 * eigener GThread, der Hauptthread pumpt per gtk_main_iteration_do(FALSE)
 * die GTK-Ereignisschleife weiter, bis der Thread fertig ist - so bleibt das
 * Fenster sichtbar/repaint-fähig, ohne die eigentliche GTK-Objektnutzung
 * (die weiterhin nur im Hauptthread stattfindet) threadübergreifend zu
 * verteilen.
 * ---------------------------------------------------------------------- */

struct _ThreadDataChat {
    SondIndexCtx *index_ctx;
    SondChatCtx  *chat_ctx;
    gchar        *question;

    GPtrArray    *hits;   /* out: (transfer full) GPtrArray von SondIndexHit* */
    gchar        *answer; /* out: (transfer full)                             */
    GError       *error;  /* out */

    gint          done;
};

static gpointer
do_chat_thread(gpointer data) {
    struct _ThreadDataChat *td = (struct _ThreadDataChat*) data;

    td->hits = sond_index_semantic_search(td->index_ctx, td->question,
            ZOND_CHAT_TOP_K, &td->error);

    if (td->hits)
        td->answer = sond_chat_answer(td->chat_ctx, td->question, td->hits,
                &td->error);

    g_atomic_int_set(&td->done, 1);

    return NULL;
}

/* -------------------------------------------------------------------------
 * Fundstellen-Fenster: dieselbe Spaltenkonvention/Navigationslogik wie bei
 * der Indexsuche (zond_indexsuche_row_activated) - hier ohne Suchbegriff
 * ("index-search-term" bleibt ungesetzt), da eine semantische Suche keinen
 * wörtlichen Treffer liefert, der im Viewer markiert werden könnte.
 * ---------------------------------------------------------------------- */

static void
zond_chat_show_hits(Projekt *zond, GPtrArray *hits) {
    gchar const *cols[] = { "Datei", "Seite", "Fundstelle", "", "", NULL };
    GtkWidget *rv = sond_result_view_new(
            GTK_WINDOW(zond->app_window),
            "Chat: Fundstellen",
            cols,
            G_CALLBACK(zond_indexsuche_row_activated),
            zond);

    for (guint i = 0; i < hits->len; i++) {
        SondIndexHit *hit = g_ptr_array_index(hits, i);
        gint page_nr_akt = hit->page_nr;

        /* Live gegen anhängige (noch nicht gespeicherte) Page-Inserts/
         * -Deletes im ggf. offenen Viewer übersetzen - siehe ausführlicher
         * Kommentar in zond_indexsuche.c an derselben Stelle. */
        if (page_nr_akt >= 0) {
            ZondPdfDocument *zpdfd_open =
                    zond_pdf_document_find_by_filename(hit->filename);

            if (zpdfd_open) {
                Anbindung anbindung = { { page_nr_akt, 0 },
                        { page_nr_akt, EOP } };

                anbindung_aktualisieren(zpdfd_open, &anbindung);
                page_nr_akt = anbindung.von.seite;
            }
        }

        gchar *page_str = (page_nr_akt >= 0)
                ? g_strdup_printf("%d", page_nr_akt + 1)
                : g_strdup("");
        gchar *char_pos_str = g_strdup_printf("%d", hit->char_pos_in_page);
        gchar *page_raw_str = g_strdup_printf("%d", hit->page_nr);
        gchar const *row[] = {
                hit->filename,
                page_str,
                hit->snippet ? hit->snippet : "",
                char_pos_str,
                page_raw_str,
                NULL
        };
        sond_result_view_append(rv, row);
        g_free(page_str);
        g_free(char_pos_str);
        g_free(page_raw_str);
    }

    gtk_widget_show_all(rv);
}

/* -------------------------------------------------------------------------
 * Antwort-Fenster: einfaches, nicht-modales Fenster mit der generierten
 * Antwort (Fließtext, mit Zeilenumbruch, nicht editierbar).
 * ---------------------------------------------------------------------- */

static void
zond_chat_show_answer(Projekt *zond, gchar const *question, gchar const *answer) {
    GtkWidget *window     = NULL;
    GtkWidget *scrolled   = NULL;
    GtkWidget *textview   = NULL;
    GtkTextBuffer *buffer = NULL;
    gchar *title          = NULL;

    title = g_strdup_printf("Chat: „%s“", question);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(zond->app_window));
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 400);
    g_free(title);

    g_signal_connect(window, "delete-event", G_CALLBACK(gtk_widget_destroy), NULL);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(scrolled), 6);
    gtk_container_add(GTK_CONTAINER(window), scrolled);

    textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scrolled), textview);

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    gtk_text_buffer_set_text(buffer, answer, -1);

    gtk_widget_show_all(window);
}

/* -------------------------------------------------------------------------
 * Öffentliche Funktion: Frage-Dialog + Ablauf
 * ---------------------------------------------------------------------- */

static void
zond_chat_do(Projekt *zond) {
    GtkWidget *dialog       = NULL;
    GtkWidget *content      = NULL;
    GtkWidget *box          = NULL;
    GtkWidget *label        = NULL;
    GtkWidget *scrolled     = NULL;
    GtkWidget *textview     = NULL;
    GtkTextBuffer *buffer   = NULL;
    gint       response     = 0;

    if (!zond->wctx || !zond->wctx->index_ctx) {
        display_message(zond->app_window,
                "Kein Index vorhanden.\n"
                "Bitte zuerst Dateien indizieren.", NULL);
        return;
    }

    if (!sond_index_ctx_has_embeddings(zond->wctx->index_ctx)) {
        /* Nur zur Diagnose erneut ermitteln, welcher Pfad beim Öffnen des
         * Projekts (project_open(), project.c) tatsächlich versucht wurde -
         * der eigentliche Fehlergrund (Datei fehlt? falsches Format? zu
         * wenig RAM?) steht nur als g_warning() auf stderr, nicht in der
         * GUI; hier zumindest der geprüfte Pfad, damit der Nutzer nicht
         * raten muss. */
        gchar *embedding_model_path = resolve_model_path(zond,
                "embedding-model-path", "Qwen3-Embedding-0.6B-Q8_0.gguf");

        display_message(zond->app_window,
                "Kein Embedding-Modell geladen.\n\n"
                "Für die Chat-Funktion wird ein lokales Embedding-Modell "
                "benötigt. Geprüfter Pfad:\n",
                embedding_model_path,
                "\n\n(Einstellung „embedding-model-path“, sonst Standardablage "
                "<Programmverzeichnis>/../models). Prüfen, ob die Datei genau "
                "dort unter genau diesem Namen liegt, und danach das Projekt "
                "erneut öffnen. Genauerer Fehlergrund steht als Warnung auf "
                "der Konsole (stderr).",
                NULL);
        g_free(embedding_model_path);
        return;
    }

    /* Chat-Modell lazy anlegen (bleibt für die Laufzeit der Anwendung
     * bestehen - Neuladen bei jeder Frage wäre bei mehreren GB Modellgröße
     * viel zu langsam). */
    if (!zond->chat_ctx) {
        gchar  *chat_model_path = resolve_model_path(zond, "chat-model-path",
                "Qwen3-8B-Q4_K_M.gguf");
        GError *error = NULL;

        zond->chat_ctx = sond_chat_ctx_new(chat_model_path, 0, &error);
        g_free(chat_model_path);

        if (!zond->chat_ctx) {
            display_message(zond->app_window,
                    "Chat-Modell konnte nicht geladen werden:\n\n",
                    error ? error->message : "?",
                    "\n\nBitte prüfen, ob die Datei unter der Einstellung "
                    "„chat-model-path“ bzw. unter "
                    "<Programmverzeichnis>/../models/Qwen3-8B-Q4_K_M.gguf "
                    "vorhanden ist.", NULL);
            g_clear_error(&error);
            return;
        }
    }

    /* --- Eingabe-Dialog (mehrzeilig, da Chat-Fragen meist länger als ein
     * Suchbegriff sind) --- */
    dialog = gtk_dialog_new_with_buttons(
            "Chat mit dem Index",
            GTK_WINDOW(zond->app_window),
            GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
            "Fragen",    GTK_RESPONSE_OK,
            "Abbrechen", GTK_RESPONSE_CANCEL,
            NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 450, 200);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(content), box);

    label = gtk_label_new("Frage an den Index (z.B. „Was hat Zeuge "
            "Müller gesagt?“):");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
            GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, -1, 100);
    gtk_box_pack_start(GTK_BOX(box), scrolled, TRUE, TRUE, 0);

    textview = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scrolled), textview);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));

    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(textview);

    response = my_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_OK) {
        GtkTextIter start = { 0 };
        GtkTextIter end   = { 0 };
        gchar *question   = NULL;

        gtk_text_buffer_get_bounds(buffer, &start, &end);
        question = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        g_strstrip(question);

        gtk_widget_destroy(dialog);

        if (question && *question) {
            struct _ThreadDataChat *td = g_new0(struct _ThreadDataChat, 1);
            InfoWindow *info_window    = NULL;
            gint        cancel         = 0;
            GThread    *thread         = NULL;

            td->index_ctx = zond->wctx->index_ctx;
            td->chat_ctx  = zond->chat_ctx;
            td->question  = question; /* Ownership geht an td */

            info_window = info_window_open(zond->app_window, &cancel,
                    "Antwort wird erzeugt");
            info_window_set_message(info_window,
                    "Suche relevante Textstellen und erzeuge Antwort "
                    "(kann je nach Rechner bis zu einer Minute dauern) ...");

            thread = g_thread_new("sond-chat", do_chat_thread, td);
            if (!thread) {
                info_window_close(info_window);
                display_message(zond->app_window,
                        "Fehler\n\n", "Thread konnte nicht erzeugt werden", NULL);
                g_free(td->question);
                g_free(td);
                return;
            }

            /* Keine reine Busy-Loop - siehe ausführliche Begründung an der
             * analogen Stelle in headerbar.c (do_index_erstellen): sonst
             * konkurriert der Hauptthread per Dauer-Polling um CPU-Kerne mit
             * den Rechen-Threads von sond_index_semantic_search()/
             * sond_chat_answer() (die alle verfügbaren Kerne nutzen). */
            while (!g_atomic_int_get(&td->done)) {
                while (gtk_events_pending())
                    gtk_main_iteration_do(FALSE);
                g_usleep(20000);
            }
            g_thread_join(thread);
            info_window_close(info_window);

            if (!td->hits) {
                display_message(zond->app_window,
                        "Fehler bei der semantischen Suche:\n\n",
                        td->error ? td->error->message : "?", NULL);
            } else if (!td->answer) {
                display_message(zond->app_window,
                        "Fehler bei der Antwortgenerierung:\n\n",
                        td->error ? td->error->message : "?", NULL);
            } else {
                zond_chat_show_answer(zond, td->question, td->answer);
                if (td->hits->len > 0)
                    zond_chat_show_hits(zond, td->hits);
            }

            g_clear_error(&td->error);
            if (td->hits)
                g_ptr_array_unref(td->hits);
            g_free(td->answer);
            g_free(td->question);
            g_free(td);
        } else
            g_free(question);
    } else
        gtk_widget_destroy(dialog);
}

void
zond_chat_activate(GtkMenuItem *item, gpointer data) {
    zond_chat_do((Projekt*) data);
}
