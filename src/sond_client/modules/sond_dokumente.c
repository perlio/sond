/*
 sond (sond_dokumente.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_dokumente.h"

#include <glib/gstdio.h>

#include "sond_import.h"
#include "sond_module_akte.h"
#include "sond_zond_start.h"
#include "../../gtk4/sond_treeviewfm.h"
#include "../../sond_client/sond_client.h"
#include "../../seadrive/sond_seadrive_core.h"
#include "../../tvfm/sond_tvfm_status.h"
#include "../../sond_index.h"

typedef struct {
    gchar *lib_dir;
    gchar *lib_name;
    GtkWidget *window;
    GtkWidget *tree;
    GtkWidget *schalter;       /* nur Projektdateien offline verfügbar */
    GtkWidget *schalter_ganz;  /* ganzes Projektverzeichnis offline verfügbar */
    GtkWidget *status;         /* Anzeige des Sync-Zustands */
    SondClient *client;
    SondAkteLease *lease;      /* gehört dem Akten-Modul */
    gint64 akte_id;            /* 0 = unbekannt */
    gchar *job_token;          /* Kennung des laufenden Jobs, NULL = keiner */
    GtkWidget *btn_abbruch;    /* Job abbrechen */
    GtkWidget *job_label;      /* Fortschritt des Index-Jobs */
    GtkWidget *dialog;         /* offener Dialog "Index-Job" (schwach) */
    guint job_timer;
    guint sync_hoch;           /* Dateien, die noch nicht hochgeladen sind (↑) */
    GtkWidget *ganz_hinweis;   /* "teilweise offline" bzw. "wird angewendet" */
    gint ganz_ziel;            /* nach dem Umschalten erwartet: 1 alles, 0 nichts, -1 keins */
    gint64 ganz_ablauf;        /* bis wann (monotonic, µs) das Ziel erwartet wird */
    gboolean setze_schalter;   /* Schalter werden gerade programmgesteuert gesetzt */
    gboolean projekt_wunsch;   /* Zustand von "nur Projektdateien", solange "ganz" aus ist */
    SondIndexCtx *index_ctx;   /* Index-DB, nur lesend; NULL, wenn nicht lesbar */
    gchar *index_pfad;
    gint64 index_mtime;        /* Stand der Datei beim letzten Öffnen */
    gint64 index_size;
    guint index_timer;
} DokData;

static void dok_free(gpointer data) {
    DokData *d = data;

    if (d->index_timer)
        g_source_remove(d->index_timer);
    if (d->job_timer)
        g_source_remove(d->job_timer);
    if (d->dialog) {
        g_object_remove_weak_pointer(G_OBJECT(d->dialog), (gpointer*) &d->dialog);
        gtk_window_destroy(GTK_WINDOW(d->dialog));
    }
    g_clear_object(&d->client);
    if (d->tree)
        g_object_remove_weak_pointer(G_OBJECT(d->tree), (gpointer*) &d->tree);
    sond_index_ctx_free(d->index_ctx);
    g_free(d->job_token);
    g_free(d->index_pfad);
    g_free(d->lib_dir);
    g_free(d->lib_name);
    g_free(d);
}

static void meldung(GtkWindow *parent, const gchar *titel, const gchar *text) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", titel);

    gtk_alert_dialog_set_detail(dialog, text);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_show(dialog, parent);
    g_object_unref(dialog);
}

/* Index-DB für das Index-Symbol der Dateien: die synchronisierte Datei, nur lesend. */
static SondIndexCtx* index_ctx_von(gpointer data) {
    return ((DokData*) data)->index_ctx;
}

/* Öffnet die Index-DB neu, wenn sie sich geändert hat (Seafile ersetzt die Datei beim
 * Abgleich, die alte Verbindung bliebe auf dem alten Stand) oder noch nicht lesbar war,
 * und frischt dann die Symbole auf. Eine noch nicht geladene SeaDrive-Datei wird nicht
 * geöffnet, das würde den Download in der Oberfläche auslösen. */
static gboolean index_pruefen(gpointer data) {
    DokData *d = data;
    GStatBuf st = { 0 };
    gboolean vorhanden = !g_stat(d->index_pfad, &st);
    gint64 mtime = vorhanden ? (gint64) st.st_mtime : 0;
    gint64 groesse = vorhanden ? (gint64) st.st_size : -1;
    gboolean geaendert = (mtime != d->index_mtime || groesse != d->index_size);

    if (!geaendert && (d->index_ctx || !vorhanden))
        return G_SOURCE_CONTINUE;

    if (vorhanden) {
        SondSeadriveBadge badge = sond_tvfm_badge_seadrive_live(d->index_pfad);

        if (badge == SOND_SEADRIVE_BADGE_OFFLINE || badge == SOND_SEADRIVE_BADGE_PENDING)
            return G_SOURCE_CONTINUE; //noch nicht lokal, später wieder
    }

    sond_index_ctx_free(d->index_ctx);
    d->index_ctx = NULL;
    d->index_mtime = mtime;
    d->index_size = groesse;

    if (vorhanden) {
        GError *error = NULL;

        d->index_ctx = sond_index_ctx_new_readonly(d->index_pfad, &error);
        if (!d->index_ctx)
            g_debug("Index-DB nicht lesbar: %s", error ? error->message : "?");
        g_clear_error(&error);
    }

    if (d->tree)
        sond_treeviewfm_refresh_status(SOND_TREEVIEWFM(d->tree));

    return G_SOURCE_CONTINUE;
}

#ifdef G_OS_WIN32
static void ganz_abgleichen(DokData *d);
#endif

/* Sync-Zustand wie in zond: alles synchron, oder wie viele Dateien noch geladen (↓)
 * bzw. hochgeladen (↑) werden */
static void on_seadrive_status(SondTreeviewFM *tree, guint down, guint up, gpointer data) {
    DokData *d = data;
    GString *text = NULL;

    d->sync_hoch = up;
#ifdef G_OS_WIN32
    ganz_abgleichen(d);
#endif
    if (!d->status)
        return;

    if (!sond_treeviewfm_is_seadrive_path(tree)) {
        gtk_label_set_text(GTK_LABEL(d->status), "");

        return;
    }

    if (down == 0 && up == 0) {
        gtk_label_set_text(GTK_LABEL(d->status), "SeaDrive: ✓ alles synchron");

        return;
    }

    text = g_string_new("SeaDrive:");
    if (down > 0)
        g_string_append_printf(text, " ↓ %u", down);
    if (up > 0)
        g_string_append_printf(text, " ↑ %u", up);
    gtk_label_set_text(GTK_LABEL(d->status), text->str);
    g_string_free(text, TRUE);
}

#ifdef G_OS_WIN32

/* Die Dateien, die zond zum Arbeiten braucht: Projektdatei, Index (mit den
 * SQLite-Begleitdateien) und das Verzeichnis .sond. Nur vorhandene. */
static GPtrArray* projekt_pfade(DokData *d) {
    GPtrArray *pfade = g_ptr_array_new_with_free_func(g_free);
    static const gchar *index_namen[] = { ".sond_index.db", ".sond_index.db-wal",
            ".sond_index.db-shm", ".sond" };
    gchar *projekt = sond_zond_project_file(d->lib_dir, d->lib_name);

    if (g_file_test(projekt, G_FILE_TEST_EXISTS))
        g_ptr_array_add(pfade, projekt);
    else
        g_free(projekt);

    for (guint i = 0; i < G_N_ELEMENTS(index_namen); i++) {
        gchar *pfad = g_build_filename(d->lib_dir, index_namen[i], NULL);

        if (g_file_test(pfad, G_FILE_TEST_EXISTS))
            g_ptr_array_add(pfade, pfad);
        else
            g_free(pfad);
    }

    return pfade;
}

/* Projektdateien angeheftet? Maßgeblich ist die Projektdatei (die Datei, ohne die
 * zond gar nicht startet). */
static gboolean projekt_angeheftet(DokData *d) {
    GPtrArray *pfade = projekt_pfade(d);
    gboolean angeheftet = FALSE;

    if (pfade->len > 0)
        angeheftet = (sond_seadrive_get_pin_state(g_ptr_array_index(pfade, 0)) ==
                STVFM_PIN_STATE_PINNED);
    g_ptr_array_unref(pfade);

    return angeheftet;
}

/* Projektdateien anheften oder die Haltung aufheben. Bei Fehlern erscheint eine Meldung;
 * TRUE, wenn alle geändert werden konnten. */
static gboolean projekt_setzen(DokData *d, gboolean an) {
    GPtrArray *pfade = projekt_pfade(d);
    GPtrArray *fehler = g_ptr_array_new_with_free_func(g_free);
    gboolean ok = TRUE;

    for (guint i = 0; i < pfade->len; i++) {
        const gchar *pfad = g_ptr_array_index(pfade, i);
        GError *error = NULL;
        //das Verzeichnis .sond rekursiv, Dateien einzeln
        gboolean verzeichnis = g_file_test(pfad, G_FILE_TEST_IS_DIR);

        if (!sond_seadrive_set_pin_state(pfad, an ? STVFM_PIN_STATE_PINNED :
                STVFM_PIN_STATE_UNSPECIFIED, verzeichnis, &error))
            sond_seadrive_pin_note_failure(fehler, pfad, error);
        g_clear_error(&error);
    }

    if (fehler->len > 0) {
        GString *text = g_string_new("Nicht alle Projektdateien ließen sich ändern:\n");

        for (guint i = 0; i < MIN(fehler->len, 5); i++)
            g_string_append_printf(text, "\n%s",
                    (const gchar*) g_ptr_array_index(fehler, i));
        meldung(GTK_WINDOW(d->window), "SeaDrive", text->str);
        g_string_free(text, TRUE);
        ok = FALSE;
    }

    g_ptr_array_unref(fehler);
    g_ptr_array_unref(pfade);

    return ok;
}

//Schalter ohne Rückwirkung setzen
static void schalter_setzen(DokData *d, GtkWidget *schalter, gboolean an) {
    gboolean vorher = d->setze_schalter;

    d->setze_schalter = TRUE;
    gtk_switch_set_active(GTK_SWITCH(schalter), an);
    d->setze_schalter = vorher;
}

static void on_schalter_aktiv(GObject *schalter, GParamSpec *pspec, gpointer user_data) {
    DokData *d = user_data;
    gboolean an = gtk_switch_get_active(GTK_SWITCH(schalter));

    (void) pspec;

    if (d->setze_schalter)
        return;

    d->projekt_wunsch = an;
    if (!projekt_setzen(d, an)) {
        d->projekt_wunsch = projekt_angeheftet(d);
        schalter_setzen(d, d->schalter, d->projekt_wunsch); //tatsächlichen Zustand zeigen
    }
}

//Ist die Wurzel des Projektverzeichnisses angeheftet?
static gboolean ganz_pruefen_pin(DokData *d) {
    return sond_seadrive_get_pin_state(d->lib_dir) == STVFM_PIN_STATE_PINNED;
}

/* Ganzes Projektverzeichnis (rekursiv auf der Wurzel; neue Dateien erben es). Aus: die
 * Haltung der Wurzel aufheben und die Projektdateien wieder so setzen, wie sie vorher
 * gewählt waren. Dateien, die schon lokal sind, bleiben es bis zum Freigeben. */
static void on_schalter_ganz_aktiv(GObject *schalter, GParamSpec *pspec, gpointer user_data) {
    DokData *d = user_data;
    gboolean an = gtk_switch_get_active(GTK_SWITCH(schalter));
    GError *error = NULL;

    (void) pspec;

    if (d->setze_schalter)
        return;

    if (!sond_seadrive_set_pin_state(d->lib_dir, an ? STVFM_PIN_STATE_PINNED :
            STVFM_PIN_STATE_UNSPECIFIED, TRUE, &error)) {
        meldung(GTK_WINDOW(d->window), "SeaDrive", error ? error->message : "unbekannt");
        g_clear_error(&error);
        schalter_setzen(d, d->schalter_ganz, ganz_pruefen_pin(d));

        return;
    }

    //die Dateien ändern sich nun nach und nach; bis der Zustand erreicht ist, zeigt der
    //Schalter die Absicht und der Hinweis den Fortschritt
    d->ganz_ziel = an ? 1 : 0;
    d->ganz_ablauf = g_get_monotonic_time() + 120 * G_USEC_PER_SEC;

    if (an) {
        //alles ist offline, die Projektdateien gehören dazu
        schalter_setzen(d, d->schalter, TRUE);
        gtk_widget_set_sensitive(d->schalter, FALSE);
    } else {
        gtk_widget_set_sensitive(d->schalter, TRUE);
        if (d->projekt_wunsch)
            projekt_setzen(d, TRUE);
        schalter_setzen(d, d->schalter, projekt_angeheftet(d));
    }
}

/* Zeigt, wie viele Dateien tatsächlich angeheftet sind: alle = Schalter an, keine =
 * aus, dazwischen bleibt er aus und der Hinweis nennt den Mischzustand (z.B. nach
 * "Platz freigeben" für einzelne Dateien). Nach dem Umschalten gilt bis zum Erreichen
 * des Ziels die Absicht. */
static void ganz_abgleichen(DokData *d) {
    guint gesamt = 0;
    guint angeheftet = 0;
    gboolean voll = FALSE;
    gboolean leer = FALSE;
    gchar *text = NULL;

    if (!d->tree || !d->schalter_ganz ||
            !sond_treeviewfm_get_pin_summary(SOND_TREEVIEWFM(d->tree), &gesamt, &angeheftet) ||
            gesamt == 0)
        return;

    voll = (angeheftet >= gesamt);
    leer = (angeheftet == 0);

    if (d->ganz_ziel >= 0 && g_get_monotonic_time() > d->ganz_ablauf)
        d->ganz_ziel = -1;
    if ((d->ganz_ziel == 1 && voll) || (d->ganz_ziel == 0 && leer))
        d->ganz_ziel = -1;

    if (d->ganz_ziel >= 0) {
        //Umstellung läuft: Absicht zeigen, Fortschritt nennen
        text = g_strdup_printf("wird angewendet (%u von %u Dateien angeheftet)",
                angeheftet, gesamt);
    } else {
        schalter_setzen(d, d->schalter_ganz, voll);
        if (!voll && !leer)
            text = g_strdup_printf("teilweise offline (%u von %u Dateien angeheftet)",
                    angeheftet, gesamt);

        if (voll) {
            schalter_setzen(d, d->schalter, TRUE);
            gtk_widget_set_sensitive(d->schalter, FALSE);
        } else if (!gtk_widget_get_sensitive(d->schalter)) {
            gtk_widget_set_sensitive(d->schalter, TRUE);
            d->projekt_wunsch = projekt_angeheftet(d);
            schalter_setzen(d, d->schalter, d->projekt_wunsch);
        }
    }

    gtk_label_set_text(GTK_LABEL(d->ganz_hinweis), text ? text : "");
    g_free(text);
}

#endif /* G_OS_WIN32 */

/* ------------------------------------------------------------------ */
/*  Auswahl für einen Index-Job an den Server                          */
/* ------------------------------------------------------------------ */

/* Der Job ist zu Ende (oder verschwunden): die Schreibberechtigung zurückholen, die
 * ihm übergeben wurde */
static void job_beendet(DokData *d, OcrStatus *status) {
    GError *error = NULL;
    GString *text = g_string_new(NULL);

    if (status && status->cancelled)
        g_string_append_printf(text, "Index-Job abgebrochen nach %d Dateien",
                status->processed_files);
    else if (status && status->error_message)
        g_string_append_printf(text, "Index-Job nicht gelaufen: %s", status->error_message);
    else if (status)
        g_string_append_printf(text, "Index-Job abgeschlossen: %d Dateien (%d PDFs)",
                status->processed_files, status->processed_pdfs);
    else
        g_string_append(text, "Index-Job beendet");

    if (d->lease && d->akte_id > 0 && d->job_token) {
        SondLeaseResult res = sond_akte_lease_reclaim_from_job(d->lease, d->akte_id,
                d->job_token, TRUE, &error);

        if (res == SOND_LEASE_OK)
            g_string_append(text, "; die Schreibberechtigung ist wieder bei diesem Rechner");
        else if (res == SOND_LEASE_ERROR)
            g_string_append(text, "; die Schreibberechtigung konnte noch nicht "
                    "zurückgeholt werden (Server nicht erreichbar), das geschieht beim "
                    "nächsten Öffnen der Akte");
        g_clear_error(&error);
    }

    gtk_label_set_text(GTK_LABEL(d->job_label), text->str);
    g_string_free(text, TRUE);
    gtk_widget_set_visible(d->btn_abbruch, FALSE);
    g_clear_pointer(&d->job_token, g_free);

    if (d->tree)
        sond_treeviewfm_refresh_status(SOND_TREEVIEWFM(d->tree));
}

/* Fortschritt des Jobs auf dem Server (alle 3 Sekunden, solange er läuft) */
static gboolean job_pruefen(gpointer data) {
    DokData *d = data;
    GError *error = NULL;
    OcrStatus *status = sond_client_ocr_get_status(d->client, d->lib_name, &error);
    gboolean laeuft = status && status->running &&
            (!d->job_token || !g_strcmp0(status->job_token, d->job_token));

    if (error) {
        //Server gerade nicht erreichbar: weiter versuchen
        gtk_label_set_text(GTK_LABEL(d->job_label),
                "Index-Job: Status nicht abrufbar, neuer Versuch ...");
        g_clear_error(&error);

        return G_SOURCE_CONTINUE;
    }

    if (laeuft) {
        gchar *text = g_strdup_printf("Index-Job läuft: %d von %d Dateien (%d PDFs)",
                status->processed_files, status->total_files, status->processed_pdfs);

        gtk_label_set_text(GTK_LABEL(d->job_label), text);
        g_free(text);
        sond_client_ocr_status_free(status);

        return G_SOURCE_CONTINUE;
    }

    job_beendet(d, status);
    sond_client_ocr_status_free(status);
    d->job_timer = 0;

    return G_SOURCE_REMOVE;
}

typedef struct {
    DokData *d;
    GtkWidget *auswahl_modus;
    GtkWidget *neu_check;
    GPtrArray *dateien;      /* Pfade relativ zum Projektverzeichnis (g_free) */
    GPtrArray *verzeichnisse;
} IndexDlg;

static void indexdlg_free(gpointer data) {
    IndexDlg *dlg = data;

    g_ptr_array_unref(dlg->dateien);
    g_ptr_array_unref(dlg->verzeichnisse);
    g_free(dlg);
}

/* Kennung für den Auftrag: nur Zeichen, die der Server annimmt */
static gchar* job_token_neu(void) {
    gchar *uuid = g_uuid_string_random();
    gchar *token = g_strconcat("j-", uuid, NULL);

    g_free(uuid);

    return token;
}

static void on_abbruch_clicked(GtkButton *knopf, gpointer user_data) {
    DokData *d = user_data;
    GError *error = NULL;

    (void) knopf;

    if (!sond_client_ocr_cancel_job(d->client, d->lib_name, &error))
        meldung(GTK_WINDOW(d->window), "Abbruch nicht möglich",
                error ? error->message : "unbekannt");
    else
        gtk_label_set_text(GTK_LABEL(d->job_label),
                "Abbruch angefordert, der Job endet nach der laufenden Datei ...");
    g_clear_error(&error);
}

/* Übergibt die Schreibberechtigung an den Job und startet ihn. Voraussetzungen: dieses
 * Gerät hält sie, zond hat gespeichert und liest nur noch, alles ist hochgeladen. */
static void job_senden(IndexDlg *dlg) {
    DokData *d = dlg->d;
    GError *error = NULL;
    guint modus = gtk_drop_down_get_selected(GTK_DROP_DOWN(dlg->auswahl_modus));
    gboolean neu = gtk_check_button_get_active(GTK_CHECK_BUTTON(dlg->neu_check));
    gchar **dateien = NULL;
    gchar **verzeichnisse = NULL;
    gchar *token = NULL;
    SondLeaseResult res = SOND_LEASE_ERROR;
    gboolean ok = FALSE;
    gint offen = 0;

    if (d->job_token) {
        meldung(GTK_WINDOW(d->window), "Index-Job", "Es läuft bereits ein Index-Job.");

        return;
    }

    if (!d->lease || d->akte_id <= 0 || !sond_akte_lease_may_write(d->lease, d->akte_id)) {
        meldung(GTK_WINDOW(d->window), "Schreibberechtigung fehlt",
                "Ein Index-Job schreibt in das Projektverzeichnis (Index, Texterkennung). "
                "Dafür brauchen Sie die Schreibberechtigung der Akte. Sie erhalten sie, "
                "wenn Sie im Akten-Fenster zond starten (bzw. die Akte bearbeiten).");

        return;
    }

    if (!sond_akte_lease_zond_settle(d->lease, d->akte_id, 8000)) {
        meldung(GTK_WINDOW(d->window), "zond hat noch nicht gespeichert",
                "zond konnte nicht innerhalb weniger Sekunden speichern und "
                "schreibgeschützt werden. Bitte zond schließen (dabei speichern) und "
                "dann erneut senden.");

        return;
    }

    offen = sond_akte_lease_unsynced(d->lease, d->akte_id);
    if (offen > 0) {
        gchar *text = g_strdup_printf("Es sind noch %d Dateien nicht hochgeladen. Der "
                "Server sieht Änderungen erst, wenn sie hochgeladen sind. Bitte warten, "
                "bis die Anzeige \"alles synchron\" zeigt.", offen);

        meldung(GTK_WINDOW(d->window), "Noch nicht alles hochgeladen", text);
        g_free(text);

        return;
    }

    token = job_token_neu();
    res = sond_akte_lease_handover_to_job(d->lease, d->akte_id, token, &error);
    if (res != SOND_LEASE_OK) {
        meldung(GTK_WINDOW(d->window), "Übergabe nicht möglich",
                res == SOND_LEASE_LOST ? "Die Schreibberechtigung gehört diesem Rechner "
                        "nicht mehr." : (error ? error->message :
                        "Die Schreibberechtigung konnte nicht übergeben werden."));
        g_clear_error(&error);
        g_free(token);

        return;
    }

    dateien = g_new0(gchar*, dlg->dateien->len + 1);
    verzeichnisse = g_new0(gchar*, dlg->verzeichnisse->len + 1);
    for (guint i = 0; i < dlg->dateien->len; i++)
        dateien[i] = g_ptr_array_index(dlg->dateien, i);
    for (guint i = 0; i < dlg->verzeichnisse->len; i++)
        verzeichnisse[i] = g_ptr_array_index(dlg->verzeichnisse, i);

    ok = sond_client_ocr_start_job_ex(d->client, d->lib_name, neu,
            modus == 0 ? SOND_CLIENT_OCR_NONE :
            modus == 2 ? SOND_CLIENT_OCR_FORCE : SOND_CLIENT_OCR_CHECK,
            (const gchar * const*) dateien, (const gchar * const*) verzeichnisse, token,
            &error);
    g_free(dateien);   //die Strings gehören dem Dialog
    g_free(verzeichnisse);

    if (!ok) {
        //Start gescheitert: die Berechtigung sofort wieder zurücknehmen
        GError *reclaim_error = NULL;

        sond_akte_lease_reclaim_from_job(d->lease, d->akte_id, token, TRUE, &reclaim_error);
        g_clear_error(&reclaim_error);
        meldung(GTK_WINDOW(d->window), "Index-Job konnte nicht gestartet werden",
                error ? error->message : "unbekannt");
        g_clear_error(&error);
        g_free(token);

        return;
    }

    d->job_token = token;
    gtk_label_set_text(GTK_LABEL(d->job_label),
            "Index-Job gestartet, die Schreibberechtigung liegt beim Job ...");
    gtk_widget_set_visible(d->btn_abbruch, TRUE);
    if (!d->job_timer)
        d->job_timer = g_timeout_add_seconds(3, job_pruefen, d);
}

static void on_indexdlg_senden(GtkButton *button, gpointer user_data) {
    IndexDlg *dlg = user_data;
    DokData *d = dlg->d;

    (void) button;

    job_senden(dlg);
    gtk_window_destroy(GTK_WINDOW(d->dialog));
}

static void on_indexdlg_abbrechen(GtkButton *button, gpointer user_data) {
    IndexDlg *dlg = user_data;

    (void) button;

    gtk_window_destroy(GTK_WINDOW(dlg->d->dialog));
}

/* Die markierten Einträge des Dateisystems als Pfade relativ zum Projektverzeichnis,
 * getrennt nach Dateien und Verzeichnissen */
static void auswahl_sammeln(DokData *d, GPtrArray *dateien, GPtrArray *verzeichnisse) {
    const gchar *wurzel = sond_treeviewfm_get_root(SOND_TREEVIEWFM(d->tree));
    GPtrArray *pfade = sond_treeviewfm_get_selected_paths(SOND_TREEVIEWFM(d->tree));
    gsize laenge = wurzel ? strlen(wurzel) : 0;

    for (guint i = 0; i < pfade->len; i++) {
        const gchar *voll = g_ptr_array_index(pfade, i);

        if (!laenge || !g_str_has_prefix(voll, wurzel) || voll[laenge] != '/' ||
                !voll[laenge + 1])
            continue;

        g_ptr_array_add(g_file_test(voll, G_FILE_TEST_IS_DIR) ? verzeichnisse : dateien,
                g_strdup(voll + laenge + 1));
    }
    g_ptr_array_unref(pfade);
}

static void index_dialog_zeigen(DokData *d) {
    IndexDlg *dlg = NULL;
    GtkWidget *box = NULL;
    GtkWidget *knoepfe = NULL;
    GtkWidget *knopf = NULL;
    GtkWidget *label = NULL;
    gchar *zusammen = NULL;
    static const char *modi[] = { "Kein OCR (nur indizieren)",
            "OCR nur für Seiten ohne Text (empfohlen)",
            "Immer OCR (vorhandener Text wird ersetzt)", NULL };

    if (d->dialog) {
        gtk_window_present(GTK_WINDOW(d->dialog));

        return;
    }

    dlg = g_new0(IndexDlg, 1);
    dlg->d = d;
    dlg->dateien = g_ptr_array_new_with_free_func(g_free);
    dlg->verzeichnisse = g_ptr_array_new_with_free_func(g_free);
    auswahl_sammeln(d, dlg->dateien, dlg->verzeichnisse);

    if (dlg->dateien->len == 0 && dlg->verzeichnisse->len == 0) {
        meldung(GTK_WINDOW(d->window), "Index-Job",
                "Bitte zuerst Dateien oder Verzeichnisse im Baum markieren.");
        indexdlg_free(dlg);

        return;
    }

    d->dialog = gtk_window_new();
    g_object_add_weak_pointer(G_OBJECT(d->dialog), (gpointer*) &d->dialog);
    gtk_window_set_title(GTK_WINDOW(d->dialog), "Zum Indizieren an den Server senden");
    gtk_window_set_transient_for(GTK_WINDOW(d->dialog), GTK_WINDOW(d->window));
    gtk_window_set_modal(GTK_WINDOW(d->dialog), TRUE);
    g_object_set_data_full(G_OBJECT(d->dialog), "sond-indexdlg", dlg, indexdlg_free);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 14);
    gtk_widget_set_margin_end(box, 14);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_window_set_child(GTK_WINDOW(d->dialog), box);

    zusammen = g_strdup_printf("%u %s und %u %s (mit allem darunter) werden an den Server "
            "übergeben.", dlg->dateien->len, dlg->dateien->len == 1 ? "Datei" : "Dateien",
            dlg->verzeichnisse->len,
            dlg->verzeichnisse->len == 1 ? "Verzeichnis" : "Verzeichnisse");
    label = gtk_label_new(zusammen);
    g_free(zusammen);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(box), label);

    label = gtk_label_new("Texterkennung:");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_box_append(GTK_BOX(box), label);
    dlg->auswahl_modus = gtk_drop_down_new_from_strings(modi);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dlg->auswahl_modus), 1);
    gtk_box_append(GTK_BOX(box), dlg->auswahl_modus);

    dlg->neu_check = gtk_check_button_new_with_label(
            "Auch unveränderte, schon verarbeitete Dateien erneut verarbeiten "
            "(vorhandener Index dieser Dateien wird ersetzt)");
    gtk_box_append(GTK_BOX(box), dlg->neu_check);

    label = gtk_label_new("Schon verarbeitete, unveränderte Dateien werden übersprungen, "
            "bereits indizierte Seiten bleiben erhalten. Mit der Option oben wird alles "
            "neu verarbeitet. Während des Jobs liegt die Schreibberechtigung beim Server.");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_add_css_class(label, "dim-label");
    gtk_box_append(GTK_BOX(box), label);

    knoepfe = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(knoepfe, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(box), knoepfe);
    knopf = gtk_button_new_with_label("Abbrechen");
    g_signal_connect(knopf, "clicked", G_CALLBACK(on_indexdlg_abbrechen), dlg);
    gtk_box_append(GTK_BOX(knoepfe), knopf);
    knopf = gtk_button_new_with_label("An Server senden");
    gtk_widget_add_css_class(knopf, "suggested-action");
    g_signal_connect(knopf, "clicked", G_CALLBACK(on_indexdlg_senden), dlg);
    gtk_box_append(GTK_BOX(knoepfe), knopf);

    gtk_window_present(GTK_WINDOW(d->dialog));
}

static void on_index_knopf(GtkButton *knopf, gpointer user_data) {
    (void) knopf;

    index_dialog_zeigen(user_data);
}

static void on_index_aktion(GSimpleAction *aktion, GVariant *parameter, gpointer user_data) {
    (void) aktion;
    (void) parameter;

    index_dialog_zeigen(user_data);
}

void sond_dokumente_show(SondClient *client, SondAkteLease *lease, GtkWindow *parent,
        guint lfd_nr, guint year, const gchar *bezeichnung, gint64 akte_id) {
    GError *error = NULL;
    gchar *root = NULL;
    DokData *d = NULL;
    gchar *titel = NULL;
    GtkWidget *box = NULL;
    GtkWidget *kopf = NULL;
    GtkWidget *label = NULL;

    root = sond_client_find_seadrive_root(client, &error);
    if (!root) {
        meldung(parent, "SeaDrive nicht gefunden", error ? error->message : "unbekannt");
        g_clear_error(&error);

        return;
    }

    d = g_new0(DokData, 1);
    d->client = g_object_ref(client);
    d->lease = lease;
    d->akte_id = akte_id;
    d->lib_name = sond_module_akte_library_name(lfd_nr, year);
    d->lib_dir = sond_import_find_library_dir(root, d->lib_name, &error);
    g_free(root);

    if (!d->lib_dir) {
        meldung(parent, "Akte im SeaDrive-Laufwerk nicht erreichbar",
                error ? error->message : "unbekannt");
        g_clear_error(&error);
        dok_free(d);

        return;
    }

    titel = g_strdup_printf("Dokumentenverzeichnis %s%s%s", d->lib_name,
            (bezeichnung && *bezeichnung) ? " - " : "",
            (bezeichnung && *bezeichnung) ? bezeichnung : "");
    d->window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(d->window), titel);
    gtk_window_set_default_size(GTK_WINDOW(d->window), 900, 600);
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(d->window), parent);
    g_free(titel);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_window_set_child(GTK_WINDOW(d->window), box);

    //Kopfzeile: Sync-Zustand und Offline-Haltung
    kopf = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), kopf);
    d->status = gtk_label_new("");
    gtk_widget_set_hexpand(d->status, TRUE);
    gtk_label_set_xalign(GTK_LABEL(d->status), 0.0);
    gtk_widget_set_tooltip_text(d->status,
            "↓ n: Dateien, die noch auf diesen Rechner geladen werden "
            "(offline gehalten, noch nicht lokal)\n"
            "↑ m: lokale Änderungen, die noch nicht hochgeladen sind\n"
            "✓: alles abgeglichen");
    gtk_box_append(GTK_BOX(kopf), d->status);

    label = gtk_button_new_with_label("Auswahl zum Indizieren senden ...");
    gtk_widget_set_tooltip_text(label,
            "Übergibt die markierten Dateien und Verzeichnisse dem Server zur "
            "Indizierung (und Texterkennung)");
    g_signal_connect(label, "clicked", G_CALLBACK(on_index_knopf), d);
    gtk_box_append(GTK_BOX(kopf), label);

    {
        GtkWidget *job_zeile = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

        d->job_label = gtk_label_new("");
        gtk_widget_set_hexpand(d->job_label, TRUE);
        gtk_label_set_xalign(GTK_LABEL(d->job_label), 0.0);
        gtk_box_append(GTK_BOX(job_zeile), d->job_label);
        d->btn_abbruch = gtk_button_new_with_label("Job abbrechen");
        gtk_widget_set_visible(d->btn_abbruch, FALSE);
        g_signal_connect(d->btn_abbruch, "clicked", G_CALLBACK(on_abbruch_clicked), d);
        gtk_box_append(GTK_BOX(job_zeile), d->btn_abbruch);
        gtk_box_append(GTK_BOX(box), job_zeile);
    }

    kopf = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(kopf), 4);
    gtk_grid_set_column_spacing(GTK_GRID(kopf), 10);
    gtk_box_append(GTK_BOX(box), kopf);

    d->schalter_ganz = gtk_switch_new();
    gtk_widget_set_valign(d->schalter_ganz, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(d->schalter_ganz,
            "Hält das ganze Projektverzeichnis dauerhaft auf diesem Rechner, auch neu "
            "hinzukommende Dateien");
    gtk_grid_attach(GTK_GRID(kopf), d->schalter_ganz, 0, 0, 1, 1);
    label = gtk_label_new("Ganzes Projektverzeichnis offline verfügbar machen");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_grid_attach(GTK_GRID(kopf), label, 1, 0, 1, 1);
    d->ganz_hinweis = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(d->ganz_hinweis), 0.0);
    gtk_widget_add_css_class(d->ganz_hinweis, "dim-label");
    gtk_grid_attach(GTK_GRID(kopf), d->ganz_hinweis, 2, 0, 1, 1);
    d->ganz_ziel = -1;

    d->schalter = gtk_switch_new();
    gtk_widget_set_valign(d->schalter, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(d->schalter, 24);
    gtk_widget_set_tooltip_text(d->schalter,
            "Hält die Projektdatei, den Index und das Verzeichnis .sond dauerhaft "
            "auf diesem Rechner, damit zond auch ohne Verbindung arbeitet");
    gtk_grid_attach(GTK_GRID(kopf), d->schalter, 0, 1, 1, 1);
    label = gtk_label_new("Nur die Projektdateien (Projektdatei, Index) offline verfügbar machen");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_grid_attach(GTK_GRID(kopf), label, 1, 1, 1, 1);

    //Baum
    d->tree = sond_treeviewfm_new();
    gtk_widget_set_vexpand(d->tree, TRUE);
    gtk_box_append(GTK_BOX(box), d->tree);
    g_object_add_weak_pointer(G_OBJECT(d->tree), (gpointer*) &d->tree);
    g_signal_connect(d->tree, "seadrive-status", G_CALLBACK(on_seadrive_status), d);

    //Kontextmenü: Auswahl zum Indizieren senden
    {
        static const GActionEntry aktionen[] = {
            { "index-senden", on_index_aktion, NULL, NULL, NULL }
        };
        SondTreeview *basis = SOND_TREEVIEW(d->tree);

        g_action_map_add_action_entries(sond_treeview_get_action_map(basis), aktionen,
                G_N_ELEMENTS(aktionen), d);
        g_menu_append(sond_treeview_get_menu(basis),
                "Zum Indizieren an den Server senden ...", "tv.index-senden");
    }

    //Index-Symbole: Index-DB lesen und bei Änderungen neu öffnen
    d->index_pfad = g_build_filename(d->lib_dir, ".sond_index.db", NULL);
    d->index_mtime = -1;
    d->index_size = -2;
    sond_treeviewfm_set_index_ctx_func(SOND_TREEVIEWFM(d->tree), index_ctx_von, d);
    index_pruefen(d);
    d->index_timer = g_timeout_add_seconds(5, index_pruefen, d);

    if (sond_treeviewfm_set_root(SOND_TREEVIEWFM(d->tree), d->lib_dir, &error)) {
        meldung(parent, "Verzeichnis nicht lesbar", error ? error->message : "unbekannt");
        g_clear_error(&error);
    }

#ifdef G_OS_WIN32
    if (sond_treeviewfm_is_seadrive_path(SOND_TREEVIEWFM(d->tree))) {
        gboolean ganz = ganz_pruefen_pin(d);

        d->projekt_wunsch = projekt_angeheftet(d);
        schalter_setzen(d, d->schalter_ganz, ganz);
        schalter_setzen(d, d->schalter, ganz || d->projekt_wunsch);
        gtk_widget_set_sensitive(d->schalter, !ganz);
        g_signal_connect(d->schalter_ganz, "notify::active",
                G_CALLBACK(on_schalter_ganz_aktiv), d);
        g_signal_connect(d->schalter, "notify::active",
                G_CALLBACK(on_schalter_aktiv), d);
        gtk_label_set_text(GTK_LABEL(d->status), "SeaDrive: wird geprüft …");
    } else {
        gtk_widget_set_sensitive(d->schalter_ganz, FALSE);
        gtk_widget_set_sensitive(d->schalter, FALSE);
    }
#else
    gtk_widget_set_sensitive(d->schalter_ganz, FALSE);
    gtk_widget_set_sensitive(d->schalter, FALSE);
#endif

    //DokData lebt mit dem Fenster
    g_object_set_data_full(G_OBJECT(d->window), "sond-dok", d, dok_free);
    gtk_window_present(GTK_WINDOW(d->window));
}
