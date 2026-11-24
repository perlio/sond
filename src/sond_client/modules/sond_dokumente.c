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
#include "../../gtk4/sond_projekt_treeviewfm.h"
#include "../../sond_client/sond_client.h"
#include "../../seadrive/sond_seadrive_core.h"
#include "../../sond_zond_ctl.h"
#include "../sond_zond_prozess.h"
#include "../../sond_eingang_cache.h"
#include "../../tvfm/sond_tvfm_status.h"
#include "../../tvfm/sond_tvfm_unlock.h"
#include "../sond_password_dialog.h"
#include "../../sond_index.h"
#include "../../znd/dbase_zond.h"

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
    guint job_modus;           /* gewählter OCR-Modus (Auswahl im Dialog), für den Nachlauf */
    GHashTable *uebersprungen; /* Fileparts, die der Nutzer nicht entsperren wollte/konnte */
    GPtrArray *nachlauf_dateien; /* entsperrte Dateien, für die ein Nachlauf noch aussteht (g_free) */
    guint nachlauf_timer;
    GtkWidget *btn_abbruch;    /* Job abbrechen */
    GtkWidget *job_label;      /* Fortschritt des Index-Jobs */
    GtkWidget *dialog;         /* offener Dialog "Index-Job" (schwach) */
    guint job_timer;
    guint sync_hoch;           /* Dateien, die noch nicht hochgeladen sind (↑) */
    guint sync_runter;         /* angeheftete Dateien, die noch geladen werden (↓) */
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
    DBaseZond *projekt_db;     /* Projektdatei (.ZND), nur lesend; NULL, wenn nicht lesbar */
    gchar *projekt_pfad;
    gint64 projekt_mtime;      /* Stand der Datei beim letzten Öffnen */
    gint64 projekt_size;
    GtkWidget *sperre;         /* Hinweis, warum der Baum nichts ändern darf */
    SondEingangCache *eingang_cache; /* lokale Tabelle der Eingangsangaben; NULL, wenn nicht verfügbar */
    DBaseZond *projekt_schreib; /* nur während eines Änderungsvorgangs offen */
    SondIndexCtx *index_schreib; /* dito */
} DokData;

//offene Dokumentenfenster: Library-Name -> Fenster
static GHashTable *offene_fenster = NULL;
static void (*offen_func)(gpointer) = NULL;
static gpointer offen_data = NULL;

gboolean sond_dokumente_is_open(const gchar *lib_name) {
    return lib_name && offene_fenster && g_hash_table_contains(offene_fenster, lib_name);
}

void sond_dokumente_set_func(void (*func)(gpointer), gpointer data) {
    offen_func = func;
    offen_data = data;
}

static void dok_free(gpointer data) {
    DokData *d = data;

    if (d->lib_name && sond_dokumente_is_open(d->lib_name) &&
            g_hash_table_lookup(offene_fenster, d->lib_name) == (gpointer) d->window) {
        g_hash_table_remove(offene_fenster, d->lib_name);
        if (offen_func)
            offen_func(offen_data);
    }

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
    sond_eingang_cache_free(d->eingang_cache);
    dbase_zond_free(d->projekt_db);
    dbase_zond_free(d->projekt_schreib);
    sond_index_ctx_free(d->index_schreib);
    g_free(d->projekt_pfad);
    g_free(d->job_token);
    if (d->nachlauf_timer)
        g_source_remove(d->nachlauf_timer);
    if (d->nachlauf_dateien)
        g_ptr_array_unref(d->nachlauf_dateien);
    if (d->uebersprungen)
        g_hash_table_destroy(d->uebersprungen);
    sond_password_clear(); //Passwörter nur für die Dauer dieses Fensters
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

//weiter unten
static gboolean schreibberechtigung_holen(DokData *d, GError **error);
static void unlock_hooks_init(SondUnlockHooks *hooks, DokData *d);

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

/* Projektdatei wie die Index-DB: nur lesend, bei geänderter Datei neu öffnen (Seafile ersetzt
 * sie beim Abgleich). Eine noch nicht geladene SeaDrive-Datei wird nicht geöffnet. Fehlt die
 * Projektdatei oder ist sie nicht lesbar, bleibt projekt_db NULL. */
static void projekt_pruefen(DokData *d) {
    GStatBuf st = { 0 };
    gboolean vorhanden = !g_stat(d->projekt_pfad, &st);
    gint64 mtime = vorhanden ? (gint64) st.st_mtime : 0;
    gint64 groesse = vorhanden ? (gint64) st.st_size : -1;

    if (mtime == d->projekt_mtime && groesse == d->projekt_size &&
            (d->projekt_db || !vorhanden))
        return;

    if (vorhanden) {
        SondSeadriveBadge badge = sond_tvfm_badge_seadrive_live(d->projekt_pfad);

        if (badge == SOND_SEADRIVE_BADGE_OFFLINE || badge == SOND_SEADRIVE_BADGE_PENDING)
            return; //noch nicht lokal, später wieder
    }

    dbase_zond_free(d->projekt_db);
    d->projekt_db = NULL;
    d->projekt_mtime = mtime;
    d->projekt_size = groesse;

    if (vorhanden) {
        GError *error = NULL;

        d->projekt_db = dbase_zond_new_store_readonly(d->projekt_pfad, &error);
        if (!d->projekt_db)
            g_debug("Projektdatei nicht lesbar: %s", error ? error->message : "?");
        g_clear_error(&error);
    }

    //Abschnitte und Treffer im Baum aus der neuen Projekt-DB lesen
    if (d->tree)
        sond_treeviewfm_refresh_names(SOND_TREEVIEWFM(d->tree));
}

/* Hat zond das Projekt offen? Die vom Client gestarteten Prozesse gelten als verlässlich, sie
 * sperren schon das Öffnen des Fensters. Zusätzlich als Sicherheitsnetz (zond am Client vorbei
 * gestartet) die Steuerdatei zur Projektdatei. */
static gboolean zond_laeuft(DokData *d) {
    SondZondCtlState st = { 0 };
    gboolean laeuft = FALSE;

    if (d->lib_name && sond_zond_prozess_laeuft(d->lib_name))
        return TRUE;

    if (d->projekt_pfad) {
        sond_zond_ctl_read_state(d->projekt_pfad, &st);
        laeuft = st.running;
        sond_zond_ctl_state_clear(&st);
    }

    return laeuft || (d->lease && d->akte_id > 0 &&
            sond_akte_lease_zond_open(d->lease, d->akte_id));
}

/* Solange ein Index-Job läuft, darf der Baum nichts ändern (die Aktionen sind ausgegraut). Fehlt
 * dagegen nur die Schreibberechtigung, bleibt er bedienbar: sie wird bei der Aktion geholt. zond kann
 * nicht gleichzeitig laufen (der Client öffnet dieses Fenster nur, wenn für die Akte kein zond
 * läuft); die Prüfung vor jeder Aktion (projekt_modus) bleibt als Sicherheitsnetz. */
static void sperre_pruefen(DokData *d) {
    gchar const *grund = NULL;

    if (!d->tree)
        return;

    if (d->job_token)
        grund = "Ein Index-Job läuft. Solange er läuft, sind keine Änderungen möglich.";

    sond_treeviewfm_set_read_only(SOND_TREEVIEWFM(d->tree), grund);
    if (d->sperre) {
        gtk_label_set_text(GTK_LABEL(d->sperre), grund ? grund : "");
        gtk_widget_set_visible(d->sperre, grund != NULL);
    }
}

/* Die lokale Tabelle der Eingangsangaben anlegen und die Zettel der Akte einlesen. Sie liegt lokal
 * (nie im synchronisierten Projektordner), je Projektverzeichnis eine Datei. Ein Fehlschlag ist
 * nicht kritisch: der Baum zeigt dann keine Eingangsangaben. */
static void eingang_oeffnen(DokData *d) {
    g_autofree gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, d->lib_dir, -1);
    g_autofree gchar *dir = g_build_filename(g_get_user_data_dir(), "sond", "eingang", NULL);
    g_autofree gchar *name = g_strconcat(hash, ".db", NULL);
    g_autofree gchar *pfad = NULL;
    GError *error = NULL;

    g_mkdir_with_parents(dir, 0700);
    pfad = g_build_filename(dir, name, NULL);

    d->eingang_cache = sond_eingang_cache_open(pfad, d->lib_dir, &error);
    if (!d->eingang_cache) {
        g_warning("Eingangsangaben nicht verfügbar: %s", error ? error->message : "?");
        g_clear_error(&error);

        return;
    }

    if (sond_eingang_cache_reload(d->eingang_cache, NULL, &error)) {
        g_warning("Zettel nicht vollständig eingelesen: %s", error ? error->message : "?");
        g_clear_error(&error);
    }
}

static gboolean dateien_pruefen(gpointer data) {
    projekt_pruefen(data);
    sperre_pruefen(data);

    return index_pruefen(data);
}

/* Sync-Zustand wie in zond: alles synchron, oder wie viele Dateien noch geladen (↓)
 * bzw. hochgeladen (↑) werden */
static void on_seadrive_status(SondTreeviewFM *tree, guint down, guint up, gpointer data) {
    DokData *d = data;
    GString *text = NULL;

    d->sync_hoch = up;
    d->sync_runter = down;
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
        if (!voll && !leer) {
            if (d->sync_runter > 0)
                text = g_strdup_printf("teilweise offline (%u von %u Dateien angeheftet, "
                        "%u davon noch im Laden)", angeheftet, gesamt, d->sync_runter);
            else
                text = g_strdup_printf("teilweise offline (%u von %u Dateien angeheftet)",
                        angeheftet, gesamt);
        } else if (voll && d->sync_runter > 0)
            //alle angeheftet, aber der Download läuft noch
            text = g_strdup_printf("wird geladen (%u von %u Dateien lokal)",
                    gesamt > d->sync_runter ? gesamt - d->sync_runter : 0, gesamt);

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

static void gesperrte_nachbearbeiten(DokData *d, OcrStatus *status);

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

    //passwortgeschützte Teile hat der Server übersprungen: entsperren und nachindizieren
    if (status && status->locked && status->locked->len > 0 && !status->cancelled)
        gesperrte_nachbearbeiten(d, status);
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

typedef enum {
    JOB_GESTARTET,
    JOB_FEHLER,      //Meldung wurde gezeigt
    JOB_WARTEN       //noch nicht alles hochgeladen (nur mit auf_upload_warten)
} JobStart;

/* Übergibt die Schreibberechtigung an den Job und startet ihn für die Dateien und Verzeichnisse
 * (Pfade relativ zum Projektverzeichnis; GPtrArray von gchar*). Voraussetzungen: dieses Gerät hält
 * sie, zond hat gespeichert und liest nur noch, alles ist hochgeladen (auf_upload_warten: sonst
 * JOB_WARTEN ohne Meldung, der Aufrufer versucht es später erneut). */
static JobStart job_starten(DokData *d, GPtrArray *dateien_arr, GPtrArray *verzeichnisse_arr,
        guint modus, gboolean neu, gboolean auf_upload_warten) {
    GError *error = NULL;
    gchar **dateien = NULL;
    gchar **verzeichnisse = NULL;
    gchar *token = NULL;
    SondLeaseResult res = SOND_LEASE_ERROR;
    gboolean ok = FALSE;
    gint offen = 0;

    if (d->job_token) {
        meldung(GTK_WINDOW(d->window), "Index-Job", "Es läuft bereits ein Index-Job.");

        return JOB_FEHLER;
    }

    if (!d->lease || d->akte_id <= 0 || !sond_akte_lease_may_write(d->lease, d->akte_id)) {
        meldung(GTK_WINDOW(d->window), "Schreibberechtigung fehlt",
                "Ein Index-Job schreibt in das Projektverzeichnis (Index, Texterkennung). "
                "Dafür brauchen Sie die Schreibberechtigung der Akte. Sie erhalten sie, "
                "wenn Sie im Akten-Fenster zond starten (bzw. die Akte bearbeiten).");

        return JOB_FEHLER;
    }

    if (!sond_akte_lease_zond_settle(d->lease, d->akte_id, 8000)) {
        meldung(GTK_WINDOW(d->window), "zond hat noch nicht gespeichert",
                "zond konnte nicht innerhalb weniger Sekunden speichern und "
                "schreibgeschützt werden. Bitte zond schließen (dabei speichern) und "
                "dann erneut senden.");

        return JOB_FEHLER;
    }

    offen = sond_akte_lease_unsynced(d->lease, d->akte_id);
    if (offen > 0) {
        gchar *text = NULL;

        if (auf_upload_warten) {
            text = g_strdup_printf("Warte auf den Upload der entsperrten Dateien "
                    "(%d offen) ...", offen);
            gtk_label_set_text(GTK_LABEL(d->job_label), text);
            g_free(text);

            return JOB_WARTEN;
        }

        text = g_strdup_printf("Es sind noch %d Dateien nicht hochgeladen. Der "
                "Server sieht Änderungen erst, wenn sie hochgeladen sind. Bitte warten, "
                "bis die Anzeige \"alles synchron\" zeigt.", offen);
        meldung(GTK_WINDOW(d->window), "Noch nicht alles hochgeladen", text);
        g_free(text);

        return JOB_FEHLER;
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

        return JOB_FEHLER;
    }

    dateien = g_new0(gchar*, dateien_arr->len + 1);
    verzeichnisse = g_new0(gchar*, verzeichnisse_arr->len + 1);
    for (guint i = 0; i < dateien_arr->len; i++)
        dateien[i] = g_ptr_array_index(dateien_arr, i);
    for (guint i = 0; i < verzeichnisse_arr->len; i++)
        verzeichnisse[i] = g_ptr_array_index(verzeichnisse_arr, i);

    ok = sond_client_ocr_start_job_ex(d->client, d->lib_name, neu,
            modus == 0 ? SOND_CLIENT_OCR_NONE :
            modus == 2 ? SOND_CLIENT_OCR_FORCE : SOND_CLIENT_OCR_CHECK,
            (const gchar * const*) dateien, (const gchar * const*) verzeichnisse, token,
            &error);
    g_free(dateien);   //die Strings gehören den Arrays
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

        return JOB_FEHLER;
    }

    d->job_token = token;
    gtk_label_set_text(GTK_LABEL(d->job_label),
            "Index-Job gestartet, die Schreibberechtigung liegt beim Job ...");
    gtk_widget_set_visible(d->btn_abbruch, TRUE);
    if (!d->job_timer)
        d->job_timer = g_timeout_add_seconds(3, job_pruefen, d);

    return JOB_GESTARTET;
}

static void job_senden(IndexDlg *dlg) {
    DokData *d = dlg->d;
    guint modus = gtk_drop_down_get_selected(GTK_DROP_DOWN(dlg->auswahl_modus));
    gboolean neu = gtk_check_button_get_active(GTK_CHECK_BUTTON(dlg->neu_check));

    //neuer Auftrag des Nutzers: was er früher nicht entsperren wollte, darf wieder gefragt werden
    if (d->uebersprungen)
        g_hash_table_remove_all(d->uebersprungen);
    d->job_modus = modus;

    job_starten(d, dlg->dateien, dlg->verzeichnisse, modus, neu, FALSE);
}

/* ------------------------------------------------------------------ */
/*  Passwortgeschützte Teile: entsperren und nachindizieren            */
/* ------------------------------------------------------------------ */

static gboolean nachlauf_tick(gpointer data) {
    DokData *d = data;
    GPtrArray *keine = g_ptr_array_new_with_free_func(g_free);
    JobStart ergebnis = JOB_FEHLER;

    if (!d->nachlauf_dateien) {
        g_ptr_array_unref(keine);
        d->nachlauf_timer = 0;

        return G_SOURCE_REMOVE;
    }
    if (d->job_token) {
        g_ptr_array_unref(keine);

        return G_SOURCE_CONTINUE; //ein anderer Job läuft noch
    }

    //gezielt nur die entsperrten Dateien, die Änderungszeit ist neu, sie werden verarbeitet
    ergebnis = job_starten(d, d->nachlauf_dateien, keine, d->job_modus, FALSE, TRUE);
    g_ptr_array_unref(keine);

    if (ergebnis == JOB_WARTEN)
        return G_SOURCE_CONTINUE;

    g_clear_pointer(&d->nachlauf_dateien, g_ptr_array_unref);
    d->nachlauf_timer = 0;

    return G_SOURCE_REMOVE;
}

typedef struct {
    DokData *d;
    GPtrArray *items; //SondLockedItem* (Kopien)
} LockedCtx;

static void locked_item_free(gpointer p) {
    SondLockedItem *item = p;

    g_free(item->filepart);
    g_free(item);
}

static void locked_ctx_free(LockedCtx *ctx) {
    g_ptr_array_unref(ctx->items);
    g_free(ctx);
}

//Die Datei im Dateisystem zu einem Filepart ("a.zip//x.pdf" -> "a.zip")
static gchar* datei_von_filepart(const gchar *filepart) {
    const gchar *sep = g_strstr_len(filepart, -1, "//");

    return sep ? g_strndup(filepart, (gsize) (sep - filepart)) : g_strdup(filepart);
}

static void on_gesperrt_bestaetigt(GObject *quelle, GAsyncResult *ergebnis, gpointer user_data) {
    LockedCtx *ctx = user_data;
    DokData *d = ctx->d;
    GError *error = NULL;
    gint knopf = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(quelle), ergebnis, &error);

    g_clear_error(&error);

    if (knopf == 0 && d->window) {
        SondUnlockHooks hooks;
        SondUnlockResult res;
        GString *text = g_string_new(NULL);

        if (!schreibberechtigung_holen(d, &error)) {
            meldung(GTK_WINDOW(d->window), "Dauerhaft entsperren", error->message);
            g_clear_error(&error);
            g_string_free(text, TRUE);
            locked_ctx_free(ctx);

            return;
        }

        unlock_hooks_init(&hooks, d);
        sond_tvfm_unlock_locked_items(ctx->items, &hooks, &res);

        //nicht entsperrt: in diesem Zyklus nicht noch einmal fragen
        for (guint i = 0; i < res.skipped_fps->len; i++)
            g_hash_table_add(d->uebersprungen,
                    g_strdup(g_ptr_array_index(res.skipped_fps, i)));

        if (res.done > 0) {
            if (!d->nachlauf_dateien)
                d->nachlauf_dateien = g_ptr_array_new_with_free_func(g_free);
            for (guint i = 0; i < res.decrypted->len; i++) {
                gchar *datei = datei_von_filepart(g_ptr_array_index(res.decrypted, i));
                gboolean bekannt = FALSE;

                for (guint k = 0; k < d->nachlauf_dateien->len; k++)
                    bekannt |= !g_strcmp0(g_ptr_array_index(d->nachlauf_dateien, k), datei);
                if (bekannt)
                    g_free(datei);
                else
                    g_ptr_array_add(d->nachlauf_dateien, datei);
            }
        }

        g_string_append_printf(text, "%u Datei(en) entsperrt", res.done);
        if (res.skipped)
            g_string_append_printf(text, ", %u übersprungen", res.skipped);
        if (res.aborted)
            g_string_append(text, "\n\nAbgebrochen.");
        else if (res.done > 0)
            g_string_append(text, "\n\nSobald der Upload fertig ist, werden die entsperrten "
                    "Dateien erneut an den Server übergeben.");
        meldung(GTK_WINDOW(d->window), "Dauerhaft entsperren", text->str);
        g_string_free(text, TRUE);
        sond_tvfm_unlock_result_clear(&res);

        if (d->tree)
            sond_treeviewfm_refresh_status(SOND_TREEVIEWFM(d->tree));

        if (d->nachlauf_dateien && d->nachlauf_dateien->len > 0 && !d->nachlauf_timer)
            d->nachlauf_timer = g_timeout_add_seconds(3, nachlauf_tick, d);
    }

    locked_ctx_free(ctx);
}

/* Der Server hat Teile wegen Passwortschutzes übersprungen. Der Nutzer entscheidet, ob der
 * Schutz entfernt wird (dann Passwortabfrage, Nachlauf nur für diese Dateien). Was schon einmal
 * nicht entsperrt wurde, wird nicht erneut angeboten. */
static void gesperrte_nachbearbeiten(DokData *d, OcrStatus *status) {
    LockedCtx *ctx = NULL;
    GString *liste = NULL;
    GtkAlertDialog *frage = NULL;
    const char *knoepfe[] = { "Entsperren und nachindizieren", "Später", NULL };
    guint n = 0;

    if (!d->uebersprungen)
        d->uebersprungen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    ctx = g_new0(LockedCtx, 1);
    ctx->d = d;
    ctx->items = g_ptr_array_new_with_free_func(locked_item_free);

    for (guint i = 0; i < status->locked->len; i++) {
        OcrLockedPart *teil = g_ptr_array_index(status->locked, i);
        SondLockedItem *item = NULL;

        if (g_hash_table_contains(d->uebersprungen, teil->filepart))
            continue;
        item = g_new0(SondLockedItem, 1);
        item->filepart = g_strdup(teil->filepart);
        item->kind = teil->is_pdf ? SOND_LOCKED_PDF : SOND_LOCKED_ZIP_ENTRY;
        g_ptr_array_add(ctx->items, item);
    }

    n = ctx->items->len;
    if (n == 0) {
        locked_ctx_free(ctx);

        return;
    }

    liste = g_string_new(NULL);
    for (guint i = 0; i < MIN(n, 8); i++)
        g_string_append_printf(liste, "\n%s",
                ((SondLockedItem*) g_ptr_array_index(ctx->items, i))->filepart);
    if (n > 8)
        g_string_append_printf(liste, "\n... und %u weitere", n - 8);

    frage = gtk_alert_dialog_new("%u passwortgeschützte Datei(en) wurden übersprungen", n);
    {
        gchar *detail = g_strdup_printf("Der Index kann nur Dateien ohne Passwortschutz lesen. "
                "Der Passwortschutz wird dafür dauerhaft entfernt: die Dateien werden dabei "
                "verändert, eine Sicherung gibt es nicht. Danach werden sie gezielt noch "
                "einmal an den Server übergeben.\n\nBetroffen:%s", liste->str);

        gtk_alert_dialog_set_detail(frage, detail);
        g_free(detail);
    }
    g_string_free(liste, TRUE);
    gtk_alert_dialog_set_buttons(frage, knoepfe);
    gtk_alert_dialog_set_default_button(frage, 1);
    gtk_alert_dialog_set_cancel_button(frage, 1);
    gtk_alert_dialog_set_modal(frage, TRUE);
    gtk_alert_dialog_choose(frage, GTK_WINDOW(d->window), NULL, on_gesperrt_bestaetigt, ctx);
    g_object_unref(frage);
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

/* ------------------------------------------------------------------ */
/*  Änderungen im Baum: Schreibberechtigung und Index                  */
/* ------------------------------------------------------------------ */

#define SERVER_KURZ_TIMEOUT 5

/* Löschen, Verschieben und Umbenennen verlangen die Schreibberechtigung der Akte (der Index
 * ist eine gemeinsame Datei). Fehlt sie, versucht der Client sie zu holen wie beim Start
 * von zond; sonst bleibt der Baum in diesem Punkt nur lesend. */
static gboolean schreibberechtigung_holen(DokData *d, GError **error) {
    SondLeaseInfo info = { 0 };
    SondLeaseResult res = SOND_LEASE_ERROR;
    GError *lease_error = NULL;
    guint alt = 0;
    gboolean ok = FALSE;

    if (d->job_token) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY,
                "Ein Index-Job läuft. Solange er läuft, sind keine Änderungen möglich.");

        return FALSE;
    }

    if (!d->lease || d->akte_id <= 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                "Die Akte ist dem Client nicht bekannt; Änderungen sind nur mit der "
                "Schreibberechtigung der Akte möglich.");

        return FALSE;
    }

    if (sond_akte_lease_may_write(d->lease, d->akte_id))
        return TRUE;

    alt = sond_client_set_io_timeout(d->client, SERVER_KURZ_TIMEOUT);
    res = sond_akte_lease_acquire(d->lease, d->akte_id, &info, &lease_error);
    sond_client_set_io_timeout(d->client, alt);

    if (res == SOND_LEASE_OK) {
        sond_akte_lease_set_name(d->lease, d->akte_id, d->lib_name);
        ok = TRUE;
    } else if (res == SOND_LEASE_DENIED) {
        if (info.by_job)
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                    "Für diese Akte läuft ein Index-Job auf dem Server. Änderungen sind "
                    "erst danach möglich.");
        else
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                    "Die Akte ist von %s auf %s ausgecheckt. Änderungen sind nur mit der "
                    "Schreibberechtigung möglich.", info.user ? info.user : "?",
                    info.device_name ? info.device_name : "?");
    } else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Die Schreibberechtigung konnte nicht geholt werden (Server nicht "
                "erreichbar). Änderungen sind nur mit ihr möglich.");
    }

    g_clear_error(&lease_error);
    sond_akte_lease_info_clear(&info);

    return ok;
}

//Index-DB zum Schreiben öffnen; NULL, wenn es noch keinen Index gibt
static SondIndexCtx* index_schreibend_oeffnen(DokData *d) {
    GError *error = NULL;
    SondIndexCtx *ctx = NULL;

    if (!g_file_test(d->index_pfad, G_FILE_TEST_EXISTS))
        return NULL;

    ctx = sond_index_ctx_new(d->index_pfad, NULL, 0, 0, &error);
    if (!ctx)
        g_warning("Index-DB nicht zu öffnen, Index wird nicht nachgeführt: %s",
                error ? error->message : "?");
    g_clear_error(&error);

    return ctx;
}

/* Datenbanken eines Änderungsvorgangs (before-* bis after): die Projektdatei schreibend und die
 * Index-DB. Die Pflege (sond_projekt_pflege) liest sie über diese Rückrufe. */
static DBaseZond* pflege_dbase_von(gpointer data) {
    DokData *d = data;

    //während einer Aktion die schreibbare Verbindung, sonst die lesend gehaltene
    return d->projekt_schreib ? d->projekt_schreib : d->projekt_db;
}

static SondIndexCtx* pflege_index_von(gpointer data) {
    return ((DokData*) data)->index_schreib;
}

static void op_ende(DokData *d) {
    dbase_zond_free(d->projekt_schreib);
    d->projekt_schreib = NULL;
    sond_index_ctx_free(d->index_schreib);
    d->index_schreib = NULL;
}

//mit_projekt: auch die Projektdatei schreibend öffnen. Bei FALSE ist nur der Index offen.
static gboolean op_beginnen(DokData *d, gboolean mit_projekt, GError **error) {
    op_ende(d);
    d->index_schreib = index_schreibend_oeffnen(d);

    if (mit_projekt) {
        d->projekt_schreib = dbase_zond_new_store(d->projekt_pfad, error);
        if (!d->projekt_schreib) {
            g_prefix_error(error, "Projektdatei nicht zu öffnen: ");
            op_ende(d);

            return FALSE;
        }
    }

    return TRUE;
}

/* Wie ist die Projektdatei bei einer Änderung zu pflegen? 1: sie existiert, die Verweise werden
 * mit nachgeführt; 0: es gibt keine Projektdatei, nur der Index; -1: nicht möglich (error). Hat zond
 * die Projektdatei offen, würde es die Änderung beim Speichern überschreiben. */
static gint projekt_modus(DokData *d, GError **error) {
    if (!g_file_test(d->projekt_pfad, G_FILE_TEST_EXISTS))
        return 0;

    if (zond_laeuft(d)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY,
                "zond hat das Projekt dieser Akte geöffnet. Bitte zond schließen, "
                "danach kann der Dateibaum geändert werden.");

        return -1;
    }

    return 1;
}

/* Hooks des Projekt-Dateibaums (s. gtk4/sond_projekt_treeviewfm.h): eine ändernde Aktion beginnt
 * mit der Schreibberechtigung und öffnet Projektdatei und Index zum Schreiben, am Ende werden sie
 * wieder geschlossen. */
static gboolean hook_permit(gpointer data, GError **error) {
    return schreibberechtigung_holen((DokData*) data, error);
}

static gint hook_write_begin(gpointer data, gboolean project, GError **error) {
    DokData *d = data;
    gint modus = 0;

    if (!schreibberechtigung_holen(d, error))
        return -1;

    if (project && (modus = projekt_modus(d, error)) < 0)
        return -1;

    if (!op_beginnen(d, modus == 1, error))
        return -1;

    return modus == 1 ? 1 : 0;
}

static void hook_write_end(gpointer data) {
    op_ende((DokData*) data);
}

static SondEingangCache* hook_eingang_cache(gpointer data) {
    return ((DokData*) data)->eingang_cache;
}

static void hook_message(gpointer data, gchar const *title, gchar const *text) {
    meldung(GTK_WINDOW(((DokData*) data)->window), title, text);
}

/* ------------------------------------------------------------------ */
/*  Dauerhaft entsperren                                               */
/* ------------------------------------------------------------------ */

/* Menüpunkt, Bestätigung und Meldungen liegen im Widget (sond_treeviewfm_unlock_selection());
 * hier nur die Rückrufe für den Nachlauf nach einem Index-Lauf und die Prüfung der
 * Schreibberechtigung: die Dateien werden dabei verändert. */
static void unlock_hooks_init(SondUnlockHooks *hooks, DokData *d) {
    memset(hooks, 0, sizeof(SondUnlockHooks));
    if (d->tree)
        sond_treeviewfm_unlock_hooks_init(SOND_TREEVIEWFM(d->tree), hooks);
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

    //schon offen: nach vorn holen
    {
        g_autofree gchar *name = sond_module_akte_library_name(lfd_nr, year);
        GtkWidget *offen = offene_fenster ? g_hash_table_lookup(offene_fenster, name) : NULL;

        if (offen) {
            gtk_window_present(GTK_WINDOW(offen));

            return;
        }
    }

    root = sond_client_find_seadrive_root(client, &error);
    if (!root) {
        meldung(parent, "SeaDrive nicht gefunden", error ? error->message : "unbekannt");
        g_clear_error(&error);

        return;
    }

    sond_client_password_dialog_init();

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

    //Hinweis auf eine Sperre des Baums (zond offen, Index-Job)
    d->sperre = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(d->sperre), 0.0);
    gtk_label_set_wrap(GTK_LABEL(d->sperre), TRUE);
    gtk_widget_add_css_class(d->sperre, "warning");
    gtk_widget_set_visible(d->sperre, FALSE);
    gtk_box_append(GTK_BOX(box), d->sperre);

    //Baum
    d->tree = sond_projekt_treeviewfm_new();
    {
        SondProjektHooks hooks = { .permit = hook_permit, .write_begin = hook_write_begin,
                .write_end = hook_write_end, .dbase = pflege_dbase_von,
                .index_ctx = pflege_index_von, .message = hook_message,
                .eingang_cache = hook_eingang_cache, .data = d };

        sond_projekt_treeviewfm_set_hooks(SOND_PROJEKT_TREEVIEWFM(d->tree), &hooks);
    }
    gtk_widget_set_vexpand(d->tree, TRUE);
    gtk_box_append(GTK_BOX(box), d->tree);
    g_object_add_weak_pointer(G_OBJECT(d->tree), (gpointer*) &d->tree);
    g_signal_connect(d->tree, "seadrive-status", G_CALLBACK(on_seadrive_status), d);

    //Löschen, Verschieben, Umbenennen, Kopieren: Schreibberechtigung holen, Index nachführen

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
    d->projekt_pfad = sond_zond_project_file(d->lib_dir, d->lib_name);
    d->projekt_mtime = -1;
    d->projekt_size = -2;
    dateien_pruefen(d);
    d->index_timer = g_timeout_add_seconds(5, dateien_pruefen, d);

    eingang_oeffnen(d);
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
    if (!offene_fenster)
        offene_fenster = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_hash_table_insert(offene_fenster, g_strdup(d->lib_name), d->window);
    if (offen_func)
        offen_func(offen_data);
    gtk_window_present(GTK_WINDOW(d->window));
}
