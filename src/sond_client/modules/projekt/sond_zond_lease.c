/*
 sond (sond_zond_lease.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_zond_lease.h"

#include "../sond_import.h"
#include "../sond_akten.h"
#include "../sond_akten_suche.h"
#include "sond_zond_start.h"
#include "../../sond_zond_prozess.h"
#include "../../../sond_zond_ctl.h"

#define SERVER_KURZ_TIMEOUT 5
#define LEASE_WAIT_STEP 5
#define LEASE_WAIT_MAX 180

struct _SondZondStarter {
    gint refs;
    gboolean dead;              /* freigegeben: Dialoge, die noch laufen, tun nichts mehr */
    SondClient *client;
    SondAkteLease *lease;
    GtkWidget *parent;
    SondZondStarterChanged changed;
    gpointer changed_data;

    guint wait_source;          /* Warten auf die Freigabe durch den anderen Rechner */
    guint wait_lfd_nr;
    guint wait_year;
    gint64 wait_akte;
    gchar *wait_name;
    gint wait_elapsed;

    gchar *titel;               /* Titelzeile von zond: "1/26    Bezeichnung" */
    gchar *untertitel;          /* Gegenstand */
    gchar *message;             /* Hinweis zum Stand der Lease einer Akte (z. B. Übergabe läuft) */
    gint64 message_akte;
};

//Ergebnis von lease_ensure(): in welchem Modus darf zond starten?
typedef enum {
    LEASE_ZUGRIFF_NEIN,       //gar nicht (dem Nutzer wurde bereits etwas erklärt)
    LEASE_ZUGRIFF_SCHREIBEN,  //mit Schreibberechtigung
    LEASE_ZUGRIFF_LESEN       //nur schreibgeschützt
} LeaseZugriff;

//Daten für Dialog-Rückrufe
typedef struct {
    SondZondStarter *s;       /* Referenz */
    gint64 akte_id;
    guint lfd_nr;
    guint year;
    gchar *name;
} LeaseDlg;

static void starter_start(SondZondStarter *s, guint lfd_nr, guint year, gint64 akte_id,
        gboolean force_ro);
static void lease_denied(SondZondStarter *s, gint64 akte_id, guint lfd_nr, guint year,
        const gchar *name, const SondLeaseInfo *info);

/* ------------------------------------------------------------------ */
/*  Texte                                                              */
/* ------------------------------------------------------------------ */

gchar* sond_lease_idle_text(gint64 seconds) {
    if (seconds < 0)
        return g_strdup("unbekannt");
    if (seconds < 120)
        return g_strdup("gerade eben");
    if (seconds < 3600)
        return g_strdup_printf("vor %" G_GINT64_FORMAT " Min.", seconds / 60);
    if (seconds < 48 * 3600)
        return g_strdup_printf("vor %" G_GINT64_FORMAT " Std.", seconds / 3600);

    return g_strdup_printf("vor %" G_GINT64_FORMAT " Tagen", seconds / 86400);
}

gchar* sond_lease_info_text(const SondLeaseInfo *info) {
    gchar *idle = NULL;
    gchar *text = NULL;

    if (!info->found || !info->held)
        return g_strdup("Projekt: nicht ausgecheckt");

    if (info->expired)
        return g_strdup_printf("Projekt: Berechtigung von %s (%s) abgelaufen, "
                "frei", info->user ? info->user : "?",
                info->device_name ? info->device_name : "?");

    if (info->mine)
        return g_strdup("Projekt: auf diesem Rechner ausgecheckt");

    if (info->by_job)
        return g_strdup_printf("Projekt: Index-Job auf dem Server (gestartet von %s)",
                info->user ? info->user : "?");

    idle = sond_lease_idle_text(info->idle_seconds);
    text = g_strdup_printf("Projekt: ausgecheckt von %s auf %s, zuletzt aktiv "
            "%s%s", info->user ? info->user : "?",
            info->device_name ? info->device_name : "?", idle,
            info->zond_open ? " (zond läuft)" : "");
    g_free(idle);

    return text;
}

/* ------------------------------------------------------------------ */
/*  Starter                                                            */
/* ------------------------------------------------------------------ */

static SondZondStarter* starter_ref(SondZondStarter *s) {
    s->refs++;

    return s;
}

static void starter_unref(SondZondStarter *s) {
    if (--s->refs > 0)
        return;

    g_clear_object(&s->client);
    g_free(s->wait_name);
    g_free(s->titel);
    g_free(s->untertitel);
    g_free(s->message);
    g_free(s);
}

static void starter_changed(SondZondStarter *s) {
    if (!s->dead && s->changed)
        s->changed(s->changed_data);
}

static GtkWindow* starter_fenster(SondZondStarter *s) {
    GtkRoot *root = s->parent ? gtk_widget_get_root(s->parent) : NULL;

    return root && GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
}

static void fehler_dialog(SondZondStarter *s, const gchar *titel, const gchar *text) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", titel);

    gtk_alert_dialog_set_detail(dialog, text);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_show(dialog, starter_fenster(s));
    g_object_unref(dialog);
}

static LeaseDlg* lease_dlg_new(SondZondStarter *s, gint64 akte_id, guint lfd_nr, guint year,
        const gchar *name) {
    LeaseDlg *d = g_new0(LeaseDlg, 1);

    d->s = starter_ref(s);
    d->akte_id = akte_id;
    d->lfd_nr = lfd_nr;
    d->year = year;
    d->name = g_strdup(name ? name : "?");

    return d;
}

static void lease_dlg_free(LeaseDlg *d) {
    starter_unref(d->s);
    g_free(d->name);
    g_free(d);
}

SondZondStarter* sond_zond_starter_new(SondClient *client, SondAkteLease *lease,
        GtkWidget *parent, SondZondStarterChanged changed, gpointer user_data) {
    SondZondStarter *s = g_new0(SondZondStarter, 1);

    s->refs = 1;
    s->client = g_object_ref(client);
    s->lease = lease;
    s->parent = parent;
    s->changed = changed;
    s->changed_data = user_data;

    return s;
}

void sond_zond_starter_free(SondZondStarter *s) {
    if (!s)
        return;

    s->dead = TRUE;
    if (s->wait_source) {
        g_source_remove(s->wait_source);
        s->wait_source = 0;
    }
    starter_unref(s);
}

void sond_zond_starter_set_info(SondZondStarter *s, guint lfd_nr, guint year,
        const gchar *kurz, const gchar *gegenstand) {
    g_return_if_fail(s != NULL);

    g_clear_pointer(&s->titel, g_free);
    g_clear_pointer(&s->untertitel, g_free);

    if (kurz && *kurz) {
        g_autofree gchar *regnr = sond_akten_regnr_anzeige(lfd_nr, year);

        s->titel = g_strdup_printf("%s    %s", regnr, kurz);
    }
    if (gegenstand && *gegenstand)
        s->untertitel = g_strdup(gegenstand);
}

gboolean sond_zond_starter_waiting(SondZondStarter *s) {
    return s && s->wait_source != 0;
}

//Warten beenden (Quelle nur entfernen, wenn sie nicht gerade selbst läuft)
static void lease_wait_stop(SondZondStarter *s) {
    if (s->wait_source) {
        g_source_remove(s->wait_source);
        s->wait_source = 0;
    }
    g_clear_pointer(&s->wait_name, g_free);
    s->wait_akte = 0;
    starter_changed(s);
}

void sond_zond_starter_cancel_wait(SondZondStarter *s) {
    if (s && s->wait_source)
        lease_wait_stop(s);
}

/* ---- Übernahme anfordern und auf die Freigabe warten ---- */

static gboolean lease_wait_tick(gpointer data) {
    SondZondStarter *s = data;
    SondLeaseInfo info = { 0 };
    GError *error = NULL;
    gchar *name = g_strdup(s->wait_name);
    gint64 akte_id = s->wait_akte;
    guint lfd_nr = s->wait_lfd_nr;
    guint year = s->wait_year;
    guint alt = 0;
    SondLeaseResult res = SOND_LEASE_ERROR;
    gboolean weiter = FALSE;

    alt = sond_client_set_io_timeout(s->client, SERVER_KURZ_TIMEOUT);
    res = sond_akte_lease_acquire(s->lease, akte_id, &info, &error);
    sond_client_set_io_timeout(s->client, alt);
    g_clear_error(&error);

    if (res == SOND_LEASE_OK) {
        sond_akte_lease_set_name(s->lease, akte_id, name);
        s->wait_source = 0;
        lease_wait_stop(s);
        starter_start(s, lfd_nr, year, akte_id, FALSE);   //jetzt mit Berechtigung
    } else {
        s->wait_elapsed += LEASE_WAIT_STEP;
        if (s->wait_elapsed < LEASE_WAIT_MAX) {
            weiter = TRUE;
        } else {
            s->wait_source = 0;
            lease_wait_stop(s);
            if (res == SOND_LEASE_DENIED) {
                //nach dem Warten noch einmal fragen, was geschehen soll
                lease_denied(s, akte_id, lfd_nr, year, name, &info);
            } else {
                fehler_dialog(s, "Keine Antwort",
                        "Der Server ist nicht erreichbar. Die Übernahme wurde "
                        "nicht abgeschlossen.");
            }
        }
    }

    sond_akte_lease_info_clear(&info);
    g_free(name);

    return weiter ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static void lease_request_takeover(SondZondStarter *s, gint64 akte_id, guint lfd_nr,
        guint year, const gchar *name) {
    GError *error = NULL;
    guint alt = sond_client_set_io_timeout(s->client, SERVER_KURZ_TIMEOUT);
    SondLeaseResult res = sond_akte_lease_takeover(s->lease, akte_id, FALSE, NULL, &error);

    sond_client_set_io_timeout(s->client, alt);

    if (res == SOND_LEASE_OK) {   //inzwischen frei
        sond_akte_lease_set_name(s->lease, akte_id, name);
        starter_changed(s);
        starter_start(s, lfd_nr, year, akte_id, FALSE);
    } else if (res == SOND_LEASE_REQUESTED) {
        lease_wait_stop(s);
        s->wait_akte = akte_id;
        s->wait_lfd_nr = lfd_nr;
        s->wait_year = year;
        s->wait_name = g_strdup(name);
        s->wait_elapsed = 0;
        s->wait_source = g_timeout_add_seconds(LEASE_WAIT_STEP, lease_wait_tick, s);
        starter_changed(s);
    } else {
        fehler_dialog(s, "Übernahme nicht möglich",
                error ? error->message : "Die Übernahme konnte nicht angefordert werden.");
    }

    g_clear_error(&error);
}

/* Antwort auf die Frage bei einer ausgecheckten Akte: 0 = Übernahme anfordern,
 * 1 = erzwingen, 2 = schreibgeschützt öffnen, sonst nichts */
static void on_lease_force_response(GObject *source, GAsyncResult *result,
        gpointer user_data) {
    LeaseDlg *d = user_data;
    SondZondStarter *s = d->s;
    GError *error = NULL;
    gint button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);

    if (error || s->dead || (button != 0 && button != 1 && button != 2)) {
        g_clear_error(&error);
        lease_dlg_free(d);

        return;
    }

    if (button == 2) {
        starter_start(s, d->lfd_nr, d->year, d->akte_id, TRUE);   //schreibgeschützt öffnen
    } else if (button == 0) {
        lease_request_takeover(s, d->akte_id, d->lfd_nr, d->year, d->name);
    } else {
        guint alt = sond_client_set_io_timeout(s->client, SERVER_KURZ_TIMEOUT);
        SondLeaseResult res = sond_akte_lease_takeover(s->lease, d->akte_id, TRUE, NULL,
                &error);

        sond_client_set_io_timeout(s->client, alt);

        if (res == SOND_LEASE_OK) {
            sond_akte_lease_set_name(s->lease, d->akte_id, d->name);
            starter_changed(s);
            starter_start(s, d->lfd_nr, d->year, d->akte_id, FALSE);   //jetzt mit Berechtigung
        } else {
            fehler_dialog(s, "Übernahme nicht möglich",
                    error ? error->message : "Die Berechtigung konnte nicht übernommen werden.");
            starter_changed(s);
        }
    }

    g_clear_error(&error);
    lease_dlg_free(d);
}

/* Antwort bei einer Akte eines anderen Nutzers: 0 = schreibgeschützt öffnen */
static void on_lease_readonly_response(GObject *source, GAsyncResult *result,
        gpointer user_data) {
    LeaseDlg *d = user_data;
    GError *error = NULL;
    gint button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);

    if (!error && button == 0 && !d->s->dead)
        starter_start(d->s, d->lfd_nr, d->year, d->akte_id, TRUE);

    g_clear_error(&error);
    lease_dlg_free(d);
}

/* Die Berechtigung gehört einem Index-Job auf dem Server. Läuft er noch (laut
 * /ocr/status mit derselben Kennung, oder ist er eben erst gestartet), bleibt nur
 * Lesen. Ist er beendet oder verwaist, wird die Berechtigung zurückgeholt: an den
 * Nutzer, der sie übergeben hatte, sonst frei. */
static void lease_job_denied(SondZondStarter *s, gint64 akte_id, guint lfd_nr, guint year,
        const gchar *name, const SondLeaseInfo *info) {
    GError *error = NULL;
    OcrStatus *status = sond_client_ocr_get_status(s->client, name, &error);
    gboolean alive = FALSE;

    //unbekannt (Server nicht erreichbar) gilt als "läuft noch": nichts anfassen
    if (error)
        alive = TRUE;
    else if (status && status->running && !g_strcmp0(status->job_token, info->job_token))
        alive = TRUE;
    else if (info->idle_seconds >= 0 && info->idle_seconds < 90)
        alive = TRUE;   //eben erst übergeben: der Start kann noch ausstehen

    if (alive) {
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Index-Job läuft");
        const char *buttons_ro[] = { "Schreibgeschützt öffnen", "Abbrechen", NULL };
        gchar *detail = NULL;

        if (status && status->running)
            detail = g_strdup_printf("Für diese Akte läuft ein Index-Job auf dem "
                    "Server (%d von %d Dateien). Solange er läuft, darf niemand "
                    "die Projektdaten schreiben.", status->processed_files,
                    status->total_files);
        else
            detail = g_strdup("Für diese Akte läuft ein Index-Job auf dem Server "
                    "(oder der Server ist gerade nicht erreichbar). Solange er "
                    "läuft, darf niemand die Projektdaten schreiben.");

        gtk_alert_dialog_set_detail(dialog, detail);
        gtk_alert_dialog_set_buttons(dialog, buttons_ro);
        gtk_alert_dialog_set_default_button(dialog, 1);
        gtk_alert_dialog_set_cancel_button(dialog, 1);
        gtk_alert_dialog_set_modal(dialog, TRUE);
        gtk_alert_dialog_choose(dialog, starter_fenster(s), NULL, on_lease_readonly_response,
                lease_dlg_new(s, akte_id, lfd_nr, year, name));
        g_object_unref(dialog);
        g_free(detail);
    } else {
        GError *reclaim_error = NULL;
        SondLeaseResult res = sond_akte_lease_reclaim_from_job(s->lease, akte_id,
                info->job_token, info->same_user, &reclaim_error);

        if (res == SOND_LEASE_OK && info->same_user) {
            //der Job ist fertig: die Berechtigung ist wieder bei diesem Rechner
            sond_akte_lease_set_name(s->lease, akte_id, name);
            starter_changed(s);
            starter_start(s, lfd_nr, year, akte_id, FALSE);
        } else if (res == SOND_LEASE_OK) {
            starter_changed(s);
            fehler_dialog(s, "Akte wieder frei",
                    "Der Index-Job auf dem Server ist beendet. Die Akte ist wieder "
                    "frei, bitte erneut versuchen.");
        } else {
            fehler_dialog(s, "Übernahme nicht möglich",
                    reclaim_error ? reclaim_error->message :
                            "Die Berechtigung konnte nicht aus dem Index-Job "
                            "zurückgeholt werden.");
        }
        g_clear_error(&reclaim_error);
    }

    if (status)
        sond_client_ocr_status_free(status);
    g_clear_error(&error);
}

/* Die Akte ist an einem anderen Rechner/von einem anderen Nutzer ausgecheckt */
static void lease_denied(SondZondStarter *s, gint64 akte_id, guint lfd_nr, guint year,
        const gchar *name, const SondLeaseInfo *info) {
    gchar *idle = NULL;
    gchar *detail = NULL;

    if (info->by_job) {
        lease_job_denied(s, akte_id, lfd_nr, year, name, info);

        return;
    }

    idle = sond_lease_idle_text(info->idle_seconds);

    if (!info->same_user) {
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Akte ist ausgecheckt");
        const char *buttons_ro[] = { "Schreibgeschützt öffnen", "Abbrechen", NULL };

        detail = g_strdup_printf("Die Akte ist von %s auf dem Rechner %s "
                "ausgecheckt (zuletzt aktiv %s).\n\nSie kann nur dort "
                "freigegeben werden. Ohne Lebenszeichen läuft die "
                "Berechtigung nach zwei Tagen ab.",
                info->user ? info->user : "?",
                info->device_name ? info->device_name : "?", idle);
        gtk_alert_dialog_set_detail(dialog, detail);
        gtk_alert_dialog_set_buttons(dialog, buttons_ro);
        gtk_alert_dialog_set_default_button(dialog, 1);
        gtk_alert_dialog_set_cancel_button(dialog, 1);
        gtk_alert_dialog_set_modal(dialog, TRUE);
        gtk_alert_dialog_choose(dialog, starter_fenster(s), NULL, on_lease_readonly_response,
                lease_dlg_new(s, akte_id, lfd_nr, year, name));
        g_object_unref(dialog);
    } else {
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Akte ist ausgecheckt");
        const char *buttons[] = { "Übernahme anfordern", "Erzwingen",
                "Schreibgeschützt öffnen", "Abbrechen", NULL };

        detail = g_strdup_printf("Die Akte ist auf Ihrem anderen Rechner %s "
                "ausgecheckt (zuletzt aktiv %s%s).\n\nÜbernahme anfordern: der "
                "andere Rechner wird gebeten, die Akte freizugeben (das "
                "dauert, bis er sich meldet).\n\nErzwingen übernimmt die "
                "Berechtigung sofort. Was dort noch nicht hochgeladen wurde, "
                "geht dann verloren oder erscheint als Konfliktkopie.\n\n"
                "Schreibgeschützt öffnen zeigt den Stand ohne die Akte zu "
                "ändern.",
                info->device_name ? info->device_name : "?", idle,
                info->zond_open ? ", zond läuft dort" : "");
        gtk_alert_dialog_set_detail(dialog, detail);
        gtk_alert_dialog_set_buttons(dialog, buttons);
        gtk_alert_dialog_set_default_button(dialog, 3);
        gtk_alert_dialog_set_cancel_button(dialog, 3);
        gtk_alert_dialog_set_modal(dialog, TRUE);
        gtk_alert_dialog_choose(dialog, starter_fenster(s), NULL, on_lease_force_response,
                lease_dlg_new(s, akte_id, lfd_nr, year, name));
        g_object_unref(dialog);
    }

    g_free(detail);
    g_free(idle);
}

/* Schreibberechtigung für den zond-Start sicherstellen (*akte_id: Node-ID der Akte, 0 =
 * unbekannt, wird bei Bedarf ermittelt). Bei LEASE_ZUGRIFF_NEIN wurde dem Nutzer bereits
 * erklärt, warum nicht. */
static LeaseZugriff lease_ensure(SondZondStarter *s, guint lfd_nr, guint year,
        const gchar *name, gint64 *akte_id) {
    GError *error = NULL;
    SondLeaseInfo info = { 0 };
    SondLeaseResult res = SOND_LEASE_ERROR;
    LeaseZugriff zugriff = LEASE_ZUGRIFF_NEIN;
    guint alt = sond_client_set_io_timeout(s->client, SERVER_KURZ_TIMEOUT);

    if (*akte_id <= 0)
        *akte_id = sond_akte_lease_find_by_name(s->lease, name);

    if (*akte_id <= 0 && sond_client_is_logged_in(s->client)) {
        //nur lokal bekannte Akte: ihre ID beim Server erfragen
        SondGraphNode *node = sond_akten_suche_node_by_regnr(s->client, lfd_nr, year, &error);

        if (node) {
            *akte_id = sond_graph_node_get_id(node);
            g_object_unref(node);
        }
        g_clear_error(&error);
    }

    if (*akte_id > 0)
        res = sond_akte_lease_acquire(s->lease, *akte_id, &info, &error);

    sond_client_set_io_timeout(s->client, alt);

    switch (res) {
    case SOND_LEASE_OK:
        sond_akte_lease_set_name(s->lease, *akte_id, name);
        zugriff = LEASE_ZUGRIFF_SCHREIBEN;
        break;
    case SOND_LEASE_DENIED:
        lease_denied(s, *akte_id, lfd_nr, year, name, &info);
        break;
    default:
        //Server nicht erreichbar: schreiben, solange die Berechtigung lokal
        //gilt, sonst nur lesen (zond zeigt den Schreibschutz selbst an)
        if (*akte_id > 0 && sond_akte_lease_may_write(s->lease, *akte_id))
            zugriff = LEASE_ZUGRIFF_SCHREIBEN;
        else
            zugriff = LEASE_ZUGRIFF_LESEN;
        break;
    }

    sond_akte_lease_info_clear(&info);
    g_clear_error(&error);
    starter_changed(s);

    return zugriff;
}

/* zond mit der Projektdatei der Akte starten. Mit Schreibberechtigung (Lease) darf zond
 * schreiben; sie wird online geholt und gilt dann auch offline weiter. Ohne sie (oder mit
 * force_ro) öffnet zond das Projekt schreibgeschützt. Der Modus wird zond über eine
 * Steuerdatei mitgeteilt (sond_zond_ctl.h). */
static void starter_start(SondZondStarter *s, guint lfd_nr, guint year, gint64 akte_id,
        gboolean force_ro) {
    LeaseZugriff zugriff = LEASE_ZUGRIFF_LESEN;
    gchar *root = NULL;
    gchar *lib_name = NULL;
    gchar *lib_dir = NULL;
    gchar *exe = NULL;
    gchar *project = NULL;
    GError *error = NULL;
    GPid pid = 0;

    if (s->dead)
        return;

    lib_name = sond_akten_library_name(lfd_nr, year);

    root = sond_client_find_seadrive_root(s->client, &error);
    if (!root) {
        fehler_dialog(s, "SeaDrive nicht gefunden", error ? error->message : "unbekannt");
        goto out;
    }

    lib_dir = sond_import_find_library_dir(root, lib_name, &error);
    if (!lib_dir) {
        fehler_dialog(s, "Akte im SeaDrive-Laufwerk nicht erreichbar",
                error ? error->message : "unbekannt");
        goto out;
    }

    exe = sond_zond_find_exe(sond_client_get_zond_exe(s->client), &error);
    if (!exe) {
        fehler_dialog(s, "zond nicht gefunden", error ? error->message : "unbekannt");
        goto out;
    }

    if (!force_ro) {
        zugriff = lease_ensure(s, lfd_nr, year, lib_name, &akte_id);
        if (zugriff == LEASE_ZUGRIFF_NEIN)
            goto out;
    }

    project = sond_zond_project_file(lib_dir, lib_name);

    //Modus festlegen, bevor zond das Projekt öffnet (es liest ihn beim Öffnen)
    if (zugriff == LEASE_ZUGRIFF_SCHREIBEN && akte_id > 0)
        sond_akte_lease_note_project(s->lease, akte_id, project);
    if (!sond_zond_ctl_set_mode(project, zugriff == LEASE_ZUGRIFF_SCHREIBEN ?
            SOND_ZOND_MODE_RW : SOND_ZOND_MODE_RO, &error)) {
        fehler_dialog(s, "zond kann nicht gestartet werden",
                error ? error->message : "Die Steuerdatei konnte nicht geschrieben werden.");
        goto out;
    }

    if (!sond_zond_start_pid(exe, project, s->titel, s->untertitel, &pid, &error)) {
        fehler_dialog(s, "zond konnte nicht gestartet werden",
                error ? error->message : "unbekannt");
    } else {
        //jeden Start verfolgen (auch den schreibgeschützten): das sperrt den Baum der Akte
        sond_zond_prozess_merken(lib_name, pid);
        if (zugriff == LEASE_ZUGRIFF_SCHREIBEN && akte_id > 0)
            sond_akte_lease_note_zond_started(s->lease, akte_id, pid);
        else
            g_spawn_close_pid(pid);
    }

out:
    g_clear_error(&error);
    g_free(project);
    g_free(exe);
    g_free(lib_dir);
    g_free(root);
    g_free(lib_name);
    starter_changed(s);
}

void sond_zond_starter_start(SondZondStarter *s, guint lfd_nr, guint year, gint64 akte_id) {
    g_return_if_fail(s != NULL);

    starter_start(s, lfd_nr, year, akte_id, FALSE);
}

/* ------------------------------------------------------------------ */
/*  Freigeben und Ereignisse des Hintergrundtakts                      */
/* ------------------------------------------------------------------ */

static void message_setzen(SondZondStarter *s, gint64 akte_id, const gchar *text) {
    g_free(s->message);
    s->message = g_strdup(text);
    s->message_akte = text ? akte_id : 0;
}

const gchar* sond_zond_starter_message(SondZondStarter *s, gint64 akte_id) {
    return s && s->message && s->message_akte == akte_id ? s->message : NULL;
}

static void unsynced_dialog(SondZondStarter *s, gint64 akte_id, gint offen);

/* Berechtigung der Akte zurückgeben und das Ergebnis melden.
 * ignore_unsynced: auch freigeben, wenn SeaDrive noch nicht alles hochgeladen hat */
static void release_akte(SondZondStarter *s, gint64 akte_id, gboolean ignore_unsynced) {
    GError *error = NULL;
    guint alt = 0;
    SondLeaseResult res = SOND_LEASE_ERROR;

    //läuft zond mit dem Projekt: schreibschützen und das Speichern abwarten
    if (!sond_akte_lease_zond_settle(s->lease, akte_id, 8000)) {
        fehler_dialog(s, "zond hat noch nicht gespeichert",
                "zond konnte nicht innerhalb weniger Sekunden speichern und "
                "schreibgeschützt werden. Bitte zond schließen (dabei "
                "speichern) und dann erneut freigeben.");
        starter_changed(s);

        return;
    }

    //hat SeaDrive alle Änderungen hochgeladen? (-1: nicht feststellbar)
    if (!ignore_unsynced) {
        gint offen = sond_akte_lease_unsynced(s->lease, akte_id);

        if (offen > 0) {
            unsynced_dialog(s, akte_id, offen);
            starter_changed(s);

            return;
        }
    }

    alt = sond_client_set_io_timeout(s->client, SERVER_KURZ_TIMEOUT);
    res = sond_akte_lease_release(s->lease, akte_id, &error);
    sond_client_set_io_timeout(s->client, alt);

    if (res == SOND_LEASE_ERROR) {
        fehler_dialog(s, "Freigabe nicht möglich",
                "Der Server ist nicht erreichbar. Die Berechtigung bleibt bei "
                "diesem Rechner; bitte später erneut versuchen.");
    } else if (res == SOND_LEASE_LOST) {
        fehler_dialog(s, "Berechtigung nicht mehr vorhanden",
                "Die Berechtigung gehörte diesem Rechner nicht mehr (sie wurde "
                "übernommen oder ist abgelaufen). Der lokale Eintrag wurde "
                "entfernt.");
    }
    if (res != SOND_LEASE_ERROR)
        message_setzen(s, akte_id, NULL);

    g_clear_error(&error);
    starter_changed(s);
}

static void on_release_response(GObject *source, GAsyncResult *result, gpointer user_data) {
    LeaseDlg *d = user_data;
    GError *error = NULL;
    gint button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);

    if (!error && button == 0 && !d->s->dead)
        release_akte(d->s, d->akte_id, FALSE);

    g_clear_error(&error);
    lease_dlg_free(d);
}

static void on_unsynced_response(GObject *source, GAsyncResult *result, gpointer user_data) {
    LeaseDlg *d = user_data;
    GError *error = NULL;
    gint button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);

    if (!error && button == 0 && !d->s->dead)
        release_akte(d->s, d->akte_id, TRUE);

    g_clear_error(&error);
    lease_dlg_free(d);
}

//SeaDrive hat noch nicht alles hochgeladen: warnen, auf Wunsch trotzdem freigeben
static void unsynced_dialog(SondZondStarter *s, gint64 akte_id, gint offen) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("Noch nicht alles hochgeladen");
    const char *buttons[] = { "Trotzdem freigeben", "Abbrechen", NULL };
    gchar *detail = g_strdup_printf("SeaDrive hat noch %d geänderte Datei(en) "
            "nicht hochgeladen. Gibt man die Akte jetzt frei, fehlen sie dem "
            "nächsten Bearbeiter oder erscheinen als Konfliktkopie.\n\nBesser "
            "warten, bis SeaDrive fertig ist, und dann erneut freigeben.", offen);

    gtk_alert_dialog_set_detail(dialog, detail);
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_default_button(dialog, 1);
    gtk_alert_dialog_set_cancel_button(dialog, 1);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_choose(dialog, starter_fenster(s), NULL, on_unsynced_response,
            lease_dlg_new(s, akte_id, 0, 0, NULL));

    g_free(detail);
    g_object_unref(dialog);
}

/* Rückfrage vor dem Freigeben. requested: ein anderer Rechner wünscht die
 * Übergabe (sonst: der Nutzer hat selbst "Freigeben" gedrückt) */
static void confirm_release(SondZondStarter *s, gint64 akte_id, const gchar *name,
        gboolean requested) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s",
            requested ? "Übernahme angefordert" : "Projekt freigeben?");
    const char *buttons_req[] = { "Jetzt freigeben", "Später", NULL };
    const char *buttons_own[] = { "Freigeben", "Abbrechen", NULL };
    gchar *detail = NULL;

    if (requested)
        detail = g_strdup_printf("Ihr anderer Rechner möchte die Akte %s "
                "bearbeiten.%s\n\nDie Übergabe ließ sich nicht selbständig "
                "abschließen: zond ist schreibgeschützt, aber ob SeaDrive alle "
                "Änderungen hochgeladen hat, steht nicht fest oder es dauert "
                "lange. Bitte SeaDrive prüfen und dann freigeben. (Über "
                "\"Freigeben\" bei der Akte geht das auch später.)",
                name ? name : "?",
                sond_akte_lease_zond_open(s->lease, akte_id) ?
                        " zond lief hier noch." : "");
    else
        detail = g_strdup("Danach kann ein anderer Rechner die Akte "
                "bearbeiten.\n\nLäuft zond mit dem Projekt, wird es "
                "schreibgeschützt geschaltet und speichert vorher. Bitte "
                "abwarten, bis SeaDrive alle Änderungen hochgeladen hat, sonst "
                "fehlen sie dem nächsten Bearbeiter.");

    gtk_alert_dialog_set_detail(dialog, detail);
    gtk_alert_dialog_set_buttons(dialog, requested ? buttons_req : buttons_own);
    gtk_alert_dialog_set_default_button(dialog, 1);
    gtk_alert_dialog_set_cancel_button(dialog, 1);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_choose(dialog, starter_fenster(s), NULL, on_release_response,
            lease_dlg_new(s, akte_id, 0, 0, name));

    g_free(detail);
    g_object_unref(dialog);
}

void sond_zond_starter_confirm_release(SondZondStarter *s, gint64 akte_id, const gchar *name) {
    g_return_if_fail(s != NULL);

    confirm_release(s, akte_id, name, FALSE);
}

/* Ereignisse des Hintergrundtakts (user_data: der Starter) */
void sond_zond_starter_lease_event(SondAkteLease *lease, gint64 akte_id, const gchar *name,
        SondLeaseEvent event, gpointer user_data) {
    SondZondStarter *s = user_data;

    (void) lease;

    if (!s || s->dead)
        return;

    if (event == SOND_LEASE_EVENT_TAKEOVER_REQUESTED) {
        //die Übergabe läuft selbständig (zond speichert, SeaDrive lädt hoch)
        message_setzen(s, akte_id, "Projekt: Übergabe an Ihren anderen Rechner läuft (zond "
                "speichert, SeaDrive lädt hoch) ...");
        starter_changed(s);

        return;
    }

    message_setzen(s, akte_id, NULL);

    if (event == SOND_LEASE_EVENT_HANDED_OVER || event == SOND_LEASE_EVENT_JOB_ENDED) {
        starter_changed(s);

        return;
    }

    if (event == SOND_LEASE_EVENT_HANDOVER_STUCK) {
        confirm_release(s, akte_id, name, TRUE);

        return;
    }

    {
        gchar *detail = g_strdup_printf("Die Schreibberechtigung für die Akte "
                "%s liegt nicht mehr bei diesem Rechner: sie wurde von einem "
                "anderen Rechner übernommen oder war abgelaufen.\n\nLäuft zond hier mit "
                "dem Projekt, wurde es schreibgeschützt. Änderungen seit dem "
                "letzten Speichern werden nicht gespeichert.", name ? name : "?");

        fehler_dialog(s, "Schreibberechtigung verloren", detail);
        g_free(detail);
    }
    starter_changed(s);
}
