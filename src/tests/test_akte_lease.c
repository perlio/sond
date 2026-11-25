/*
 sond (test_akte_lease.c) - Akten, Beweisstücke, Unterlagen
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

/*
 * Tests der Schreibberechtigung (Lease) einer Akte gegen einen nachgebauten Server
 * (fake_server.[ch]). Geräte und Nutzer:
 *   a1, a2: Nutzer "anna" auf zwei Rechnern, b: Nutzer "bernd".
 * Nicht abgedeckt: der Takt im Hintergrund und der Ablauf der Übergabe (GLib-Timer).
 */

#include <glib.h>
#include <glib/gstdio.h>

#include "fake_server.h"
#include "../sond_client/modules/projekt/sond_akte_lease.h"

#define AKTE 4711

//Die Lease ruft nur diese beiden Funktionen aus dem SeaDrive-Teil auf, wenn zond und Upload
//geprüft werden; die getesteten Operationen berühren sie nicht
gint sond_seadrive_count_unsynced(const gchar *dir, gint64 since_unix) {
    (void) dir;
    (void) since_unix;

    return 0;
}

typedef struct {
    FakeServer *srv;
    SondClient *ca1;
    SondClient *ca2;
    SondClient *cb;
    SondAkteLease *a1;
    SondAkteLease *a2;
    SondAkteLease *b;
    gchar *dir;
    gchar *state_a1;
    gchar *state_a2;
    gchar *state_b;
} Fix;

static SondAkteLease* neue_lease(SondClient *client, const gchar *state) {
    SondAkteLease *l = NULL;

    g_setenv("SOND_CLIENT_STATE", state, TRUE);
    l = sond_akte_lease_new(client);
    g_unsetenv("SOND_CLIENT_STATE");

    return l;
}

static Fix* fix_new(void) {
    Fix *f = g_new0(Fix, 1);

    f->srv = fake_server_new();
    f->ca1 = fake_server_client(f->srv, "anna");
    f->ca2 = fake_server_client(f->srv, "anna");
    f->cb = fake_server_client(f->srv, "bernd");
    f->dir = g_dir_make_tmp("lease-test-XXXXXX", NULL);
    g_assert_nonnull(f->dir);
    f->state_a1 = g_build_filename(f->dir, "a1.ini", NULL);
    f->state_a2 = g_build_filename(f->dir, "a2.ini", NULL);
    f->state_b = g_build_filename(f->dir, "b.ini", NULL);
    f->a1 = neue_lease(f->ca1, f->state_a1);
    f->a2 = neue_lease(f->ca2, f->state_a2);
    f->b = neue_lease(f->cb, f->state_b);

    return f;
}

static void fix_free(Fix *f) {
    sond_akte_lease_free(f->a1);
    sond_akte_lease_free(f->a2);
    sond_akte_lease_free(f->b);
    g_object_unref(f->ca1);
    g_object_unref(f->ca2);
    g_object_unref(f->cb);
    fake_server_free(f->srv);
    g_remove(f->state_a1);
    g_remove(f->state_a2);
    g_remove(f->state_b);
    g_rmdir(f->dir);
    g_free(f->state_a1);
    g_free(f->state_a2);
    g_free(f->state_b);
    g_free(f->dir);
    g_free(f);
}

//Kurzformen für die Erwartungen
static SondLeaseResult acquire(SondAkteLease *l, SondLeaseInfo *info) {
    GError *error = NULL;
    SondLeaseResult r = sond_akte_lease_acquire(l, AKTE, info, &error);

    if (r == SOND_LEASE_ERROR)
        g_assert_nonnull(error);
    g_clear_error(&error);

    return r;
}

static SondLeaseResult heartbeat(SondAkteLease *l, gboolean zond, gint up, gboolean *angefordert) {
    GError *error = NULL;
    SondLeaseResult r = sond_akte_lease_heartbeat(l, AKTE, zond, up, angefordert, &error);

    g_clear_error(&error);

    return r;
}

static SondLeaseResult release(SondAkteLease *l) {
    GError *error = NULL;
    SondLeaseResult r = sond_akte_lease_release(l, AKTE, &error);

    g_clear_error(&error);

    return r;
}

static SondLeaseResult takeover(SondAkteLease *l, gboolean force, SondLeaseInfo *info) {
    GError *error = NULL;
    SondLeaseResult r = sond_akte_lease_takeover(l, AKTE, force, info, &error);

    g_clear_error(&error);

    return r;
}

static void info_holen(SondAkteLease *l, SondLeaseInfo *info) {
    GError *error = NULL;

    g_assert_true(sond_akte_lease_get_info(l, AKTE, info, &error));
    g_assert_no_error(error);
}

/* ------------------------------------------------------------------ */

static void test_acquire(void) {
    Fix *f = fix_new();
    SondLeaseInfo info = { 0 };

    info_holen(f->a1, &info);
    g_assert_false(info.found);
    g_assert_cmpuint(fake_server_count(f->srv, "ProjektLease"), ==, 0);   //lesen legt nichts an
    sond_akte_lease_info_clear(&info);

    g_assert_cmpint(acquire(f->a1, &info), ==, SOND_LEASE_OK);
    g_assert_false(info.held);   //Stand vor der Vergabe
    sond_akte_lease_info_clear(&info);
    g_assert_cmpuint(fake_server_count(f->srv, "ProjektLease"), ==, 1);
    g_assert_true(sond_akte_lease_may_write(f->a1, AKTE));
    g_assert_cmpuint(fake_server_lock_count(f->srv), ==, 0);   //der Node-Lock ist wieder frei

    info_holen(f->a1, &info);
    g_assert_true(info.found && info.held && info.mine && info.same_user);
    g_assert_false(info.expired);
    g_assert_false(info.by_job);
    g_assert_cmpint(info.epoch, ==, 1);
    g_assert_cmpstr(info.user, ==, "anna");
    sond_akte_lease_info_clear(&info);

    //dasselbe Gerät erneuert, die Epoche bleibt
    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);
    info_holen(f->a1, &info);
    g_assert_cmpint(info.epoch, ==, 1);
    sond_akte_lease_info_clear(&info);

    //anderes Gerät desselben Nutzers: verweigert, der Halter steht in info
    g_assert_cmpint(acquire(f->a2, &info), ==, SOND_LEASE_DENIED);
    g_assert_true(info.held && !info.mine && info.same_user);
    g_assert_cmpstr(info.user, ==, "anna");
    sond_akte_lease_info_clear(&info);
    g_assert_false(sond_akte_lease_may_write(f->a2, AKTE));

    //anderer Nutzer: verweigert, nicht derselbe Nutzer
    g_assert_cmpint(acquire(f->b, &info), ==, SOND_LEASE_DENIED);
    g_assert_true(info.held && !info.same_user);
    sond_akte_lease_info_clear(&info);

    fix_free(f);
}

static void test_ablauf(void) {
    Fix *f = fix_new();
    SondLeaseInfo info = { 0 };

    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);

    //knapp vor dem Ablauf: noch verweigert
    fake_server_advance(f->srv, SOND_AKTE_LEASE_SECONDS - 10);
    g_assert_cmpint(acquire(f->b, NULL), ==, SOND_LEASE_DENIED);

    //darüber hinaus: abgelaufen, frei vergebbar
    fake_server_advance(f->srv, 20);
    info_holen(f->b, &info);
    g_assert_true(info.held && info.expired);
    sond_akte_lease_info_clear(&info);

    g_assert_cmpint(acquire(f->b, NULL), ==, SOND_LEASE_OK);
    info_holen(f->b, &info);
    g_assert_true(info.mine);
    g_assert_cmpint(info.epoch, ==, 2);
    sond_akte_lease_info_clear(&info);

    //der alte Halter merkt es beim nächsten Lebenszeichen und verliert den lokalen Zustand
    g_assert_true(sond_akte_lease_may_write(f->a1, AKTE));
    g_assert_cmpint(heartbeat(f->a1, FALSE, 0, NULL), ==, SOND_LEASE_LOST);
    g_assert_false(sond_akte_lease_may_write(f->a1, AKTE));
    g_assert_cmpint(heartbeat(f->b, FALSE, 0, NULL), ==, SOND_LEASE_OK);

    fix_free(f);
}

static void test_ablauf_eigene(void) {
    Fix *f = fix_new();

    //abgelaufen, aber niemand sonst hat sie genommen: das Lebenszeichen erneuert sie
    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);
    fake_server_advance(f->srv, SOND_AKTE_LEASE_SECONDS + 3600);
    g_assert_cmpint(heartbeat(f->a1, FALSE, 0, NULL), ==, SOND_LEASE_OK);
    g_assert_cmpint(acquire(f->a2, NULL), ==, SOND_LEASE_DENIED);

    fix_free(f);
}

static void test_release(void) {
    Fix *f = fix_new();
    SondLeaseInfo info = { 0 };

    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);

    //wer sie nicht hält, kann sie nicht freigeben
    g_assert_cmpint(release(f->a2), ==, SOND_LEASE_LOST);
    info_holen(f->a1, &info);
    g_assert_true(info.mine);
    sond_akte_lease_info_clear(&info);

    g_assert_cmpint(release(f->a1), ==, SOND_LEASE_OK);
    g_assert_false(sond_akte_lease_may_write(f->a1, AKTE));
    info_holen(f->a2, &info);
    g_assert_true(info.found);
    g_assert_false(info.held);
    sond_akte_lease_info_clear(&info);
    g_assert_cmpint(release(f->a1), ==, SOND_LEASE_LOST);   //zweites Mal: nichts mehr da

    //danach nimmt sie ein anderes Gerät, die Epoche steigt
    g_assert_cmpint(acquire(f->a2, NULL), ==, SOND_LEASE_OK);
    info_holen(f->a2, &info);
    g_assert_cmpint(info.epoch, ==, 2);
    sond_akte_lease_info_clear(&info);

    fix_free(f);
}

static void test_heartbeat_und_uebergabewunsch(void) {
    Fix *f = fix_new();
    SondLeaseInfo info = { 0 };
    gboolean angefordert = TRUE;

    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);

    g_assert_cmpint(heartbeat(f->a1, FALSE, 0, &angefordert), ==, SOND_LEASE_OK);
    g_assert_false(angefordert);

    //anderer Nutzer darf nichts anfordern
    g_assert_cmpint(takeover(f->b, FALSE, NULL), ==, SOND_LEASE_NOT_ALLOWED);

    //derselbe Nutzer auf dem zweiten Rechner fordert an
    g_assert_cmpint(takeover(f->a2, FALSE, &info), ==, SOND_LEASE_REQUESTED);
    g_assert_true(info.takeover_requested && info.takeover_by_me);
    sond_akte_lease_info_clear(&info);

    //der Halter sieht den Wunsch und meldet zond und offene Uploads
    g_assert_cmpint(heartbeat(f->a1, TRUE, 3, &angefordert), ==, SOND_LEASE_OK);
    g_assert_true(angefordert);

    info_holen(f->a2, &info);
    g_assert_true(info.zond_open);
    g_assert_cmpint(info.pending_up, ==, 3);
    g_assert_true(info.takeover_requested && info.takeover_by_me);
    g_assert_true(info.idle_seconds >= 0);
    sond_akte_lease_info_clear(&info);

    //Halter gibt frei, der Anforderer holt sie; der Wunsch ist damit erledigt
    g_assert_cmpint(release(f->a1), ==, SOND_LEASE_OK);
    g_assert_cmpint(acquire(f->a2, NULL), ==, SOND_LEASE_OK);
    info_holen(f->a2, &info);
    g_assert_true(info.mine);
    g_assert_false(info.takeover_requested);
    g_assert_false(info.zond_open);
    g_assert_cmpint(info.pending_up, ==, -1);
    sond_akte_lease_info_clear(&info);

    fix_free(f);
}

static void test_takeover_hart(void) {
    Fix *f = fix_new();
    SondLeaseInfo info = { 0 };

    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);

    g_assert_cmpint(takeover(f->b, TRUE, NULL), ==, SOND_LEASE_NOT_ALLOWED);
    g_assert_true(sond_akte_lease_may_write(f->a1, AKTE));

    g_assert_cmpint(takeover(f->a2, TRUE, NULL), ==, SOND_LEASE_OK);
    g_assert_true(sond_akte_lease_may_write(f->a2, AKTE));
    info_holen(f->a2, &info);
    g_assert_true(info.mine);
    g_assert_cmpint(info.epoch, ==, 2);
    sond_akte_lease_info_clear(&info);

    //der alte Halter verliert beim nächsten Lebenszeichen
    g_assert_cmpint(heartbeat(f->a1, FALSE, 0, NULL), ==, SOND_LEASE_LOST);
    g_assert_false(sond_akte_lease_may_write(f->a1, AKTE));

    fix_free(f);
}

static void test_takeover_frei(void) {
    Fix *f = fix_new();
    SondLeaseInfo info = { 0 };

    //noch nie vergeben (legt den Knoten an) und frei: einfach erteilt, auch von einem anderen Nutzer
    g_assert_cmpint(takeover(f->b, FALSE, NULL), ==, SOND_LEASE_OK);
    g_assert_true(sond_akte_lease_may_write(f->b, AKTE));
    g_assert_cmpint(release(f->b), ==, SOND_LEASE_OK);

    //das eigene Gerät: erneuert ohne Epochenwechsel
    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);
    info_holen(f->a1, &info);
    g_assert_cmpint(info.epoch, ==, 2);   //nach bernd
    sond_akte_lease_info_clear(&info);
    g_assert_cmpint(takeover(f->a1, TRUE, NULL), ==, SOND_LEASE_OK);
    info_holen(f->a1, &info);
    g_assert_cmpint(info.epoch, ==, 2);
    sond_akte_lease_info_clear(&info);

    //abgelaufen: auch ein anderer Nutzer bekommt sie
    fake_server_advance(f->srv, SOND_AKTE_LEASE_SECONDS + 1);
    g_assert_cmpint(takeover(f->b, FALSE, NULL), ==, SOND_LEASE_OK);

    fix_free(f);
}

static void test_index_job(void) {
    Fix *f = fix_new();
    SondLeaseInfo info = { 0 };

    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);

    //nur wer die Lease hält, kann sie dem Job übergeben
    g_assert_cmpint(sond_akte_lease_handover_to_job(f->a2, AKTE, "tok1", NULL), ==, SOND_LEASE_LOST);
    g_assert_cmpint(sond_akte_lease_handover_to_job(f->a1, AKTE, "tok1", NULL), ==, SOND_LEASE_OK);
    g_assert_false(sond_akte_lease_may_write(f->a1, AKTE));
    g_assert_cmpint(heartbeat(f->a1, FALSE, 0, NULL), ==, SOND_LEASE_LOST);

    info_holen(f->a2, &info);
    g_assert_true(info.held && info.by_job && !info.mine);
    g_assert_cmpstr(info.job_token, ==, "tok1");
    g_assert_cmpstr(info.user, ==, "anna");   //der Nutzer bleibt eingetragen
    g_assert_cmpint(info.epoch, ==, 2);
    sond_akte_lease_info_clear(&info);

    //während des Jobs bekommt niemand die Lease
    g_assert_cmpint(acquire(f->a2, &info), ==, SOND_LEASE_DENIED);
    g_assert_true(info.by_job);
    sond_akte_lease_info_clear(&info);

    fix_free(f);
}

static void test_index_job_zurueckholen(void) {
    Fix *f = fix_new();
    SondLeaseInfo info = { 0 };
    GError *error = NULL;

    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);
    g_assert_cmpint(sond_akte_lease_handover_to_job(f->a1, AKTE, "tok1", NULL), ==, SOND_LEASE_OK);

    //falsche Kennung: gehört keinem (passenden) Job
    g_assert_cmpint(sond_akte_lease_reclaim_from_job(f->a2, AKTE, "falsch", TRUE, &error), ==,
            SOND_LEASE_DENIED);
    g_clear_error(&error);

    //anderer Nutzer darf sie nicht an sich nehmen
    g_assert_cmpint(sond_akte_lease_reclaim_from_job(f->b, AKTE, "tok1", TRUE, &error), ==,
            SOND_LEASE_NOT_ALLOWED);
    g_clear_error(&error);
    g_assert_false(sond_akte_lease_may_write(f->b, AKTE));

    //der Nutzer holt sie auf einem Rechner zurück (auch auf einem anderen als dem übergebenden)
    g_assert_cmpint(sond_akte_lease_reclaim_from_job(f->a2, AKTE, "tok1", TRUE, &error), ==,
            SOND_LEASE_OK);
    g_clear_error(&error);
    g_assert_true(sond_akte_lease_may_write(f->a2, AKTE));
    info_holen(f->a2, &info);
    g_assert_true(info.mine && !info.by_job);
    g_assert_null(info.job_token);
    g_assert_cmpint(info.epoch, ==, 3);   //1, Übergabe 2, Rückholung 3
    sond_akte_lease_info_clear(&info);

    //ein weiteres Zurückholen ohne Job: nichts zu tun
    g_assert_cmpint(sond_akte_lease_reclaim_from_job(f->a2, AKTE, NULL, TRUE, &error), ==,
            SOND_LEASE_DENIED);
    g_clear_error(&error);

    fix_free(f);
}

static void test_index_job_frei(void) {
    Fix *f = fix_new();
    SondLeaseInfo info = { 0 };
    GError *error = NULL;

    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);
    g_assert_cmpint(sond_akte_lease_handover_to_job(f->a1, AKTE, "tok2", NULL), ==, SOND_LEASE_OK);

    //verwaist: freigeben (to_me FALSE) darf auch ein anderer Nutzer, ohne Kennung zu prüfen
    g_assert_cmpint(sond_akte_lease_reclaim_from_job(f->b, AKTE, NULL, FALSE, &error), ==,
            SOND_LEASE_OK);
    g_clear_error(&error);
    info_holen(f->b, &info);
    g_assert_true(info.found);
    g_assert_false(info.held);
    g_assert_false(info.by_job);
    g_assert_cmpint(info.epoch, ==, 3);
    sond_akte_lease_info_clear(&info);

    g_assert_cmpint(acquire(f->b, NULL), ==, SOND_LEASE_OK);

    fix_free(f);
}

static void test_lokaler_zustand(void) {
    Fix *f = fix_new();
    SondAkteLease *neu = NULL;

    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);
    sond_akte_lease_set_name(f->a1, AKTE, "2026-1");
    g_assert_cmpint(sond_akte_lease_find_by_name(f->a1, "2026-1"), ==, AKTE);
    g_assert_cmpint(sond_akte_lease_find_by_name(f->a1, "2026-2"), ==, 0);
    g_assert_cmpstr(sond_akte_lease_get_name(f->a1, AKTE), ==, "2026-1");

    //ein neuer Start des Clients liest den Zustand aus der Datei
    neu = neue_lease(f->ca1, f->state_a1);
    g_assert_cmpstr(sond_akte_lease_get_device_id(neu), ==, sond_akte_lease_get_device_id(f->a1));
    g_assert_true(sond_akte_lease_may_write(neu, AKTE));
    g_assert_cmpint(sond_akte_lease_find_by_name(neu, "2026-1"), ==, AKTE);
    g_assert_cmpint(heartbeat(neu, FALSE, 0, NULL), ==, SOND_LEASE_OK);   //auch dem Server nach wie vor bekannt

    //die Geräte haben verschiedene Kennungen
    g_assert_cmpstr(sond_akte_lease_get_device_id(f->a1), !=, sond_akte_lease_get_device_id(f->a2));

    //nach der Freigabe ist der Zustand auch dort weg
    g_assert_cmpint(release(neu), ==, SOND_LEASE_OK);
    sond_akte_lease_free(neu);
    neu = neue_lease(f->ca1, f->state_a1);
    g_assert_false(sond_akte_lease_may_write(neu, AKTE));
    g_assert_cmpint(sond_akte_lease_find_by_name(neu, "2026-1"), ==, 0);
    sond_akte_lease_free(neu);

    fix_free(f);
}

static SondGraphNode* lease_knoten(gint64 akte) {
    SondGraphNode *n = sond_graph_node_new();
    gchar *id = g_strdup_printf("%" G_GINT64_FORMAT, akte);

    sond_graph_node_set_label(n, "ProjektLease");
    sond_graph_node_set_property_string(n, "akte", id);
    sond_graph_node_set_property_string(n, "device", "");
    sond_graph_node_set_property_string(n, "epoch", "0");
    sond_graph_node_set_property_string(n, "seen", "0");
    g_free(id);

    return n;
}

static void test_dubletten(void) {
    Fix *f = fix_new();
    SondGraphNode *n1 = lease_knoten(AKTE);
    SondGraphNode *n2 = lease_knoten(AKTE);
    SondGraphNode *fremd = lease_knoten(AKTE + 1);
    gint64 id1 = fake_server_add_node(f->srv, n1);
    gint64 id2 = fake_server_add_node(f->srv, n2);
    gchar *dev1 = NULL;
    gchar *dev2 = NULL;
    gchar *fremd_dev = NULL;
    gint64 id_fremd = fake_server_add_node(f->srv, fremd);

    //bei Dubletten arbeitet die Lease mit dem Knoten der kleinsten ID; andere Akten bleiben unberührt
    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);
    dev1 = fake_server_node_prop(f->srv, id1, "device");
    dev2 = fake_server_node_prop(f->srv, id2, "device");
    fremd_dev = fake_server_node_prop(f->srv, id_fremd, "device");
    g_assert_cmpstr(dev1, ==, sond_akte_lease_get_device_id(f->a1));
    g_assert_cmpstr(dev2, ==, "");
    g_assert_cmpstr(fremd_dev, ==, "");

    g_free(dev1);
    g_free(dev2);
    g_free(fremd_dev);
    g_object_unref(n1);
    g_object_unref(n2);
    g_object_unref(fremd);
    fix_free(f);
}

static void test_fehler(void) {
    Fix *f = fix_new();
    SondLeaseInfo info = { 0 };
    GError *error = NULL;

    //Server ohne Zeitangabe: Fehler, nichts wird vergeben, der Lock bleibt nicht hängen
    fake_server_set_no_time(f->srv, TRUE);
    g_assert_cmpint(sond_akte_lease_acquire(f->a1, AKTE, NULL, &error), ==, SOND_LEASE_ERROR);
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_false(sond_akte_lease_may_write(f->a1, AKTE));
    g_assert_cmpuint(fake_server_lock_count(f->srv), ==, 0);
    fake_server_set_no_time(f->srv, FALSE);

    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);

    //Server nicht erreichbar: Fehler, der lokale Zustand bleibt (Offline-Arbeit)
    fake_server_set_offline(f->srv, TRUE);
    g_assert_cmpint(heartbeat(f->a1, FALSE, 0, NULL), ==, SOND_LEASE_ERROR);
    g_assert_cmpint(release(f->a1), ==, SOND_LEASE_ERROR);
    g_assert_cmpint(acquire(f->a2, NULL), ==, SOND_LEASE_ERROR);
    g_assert_false(sond_akte_lease_get_info(f->a1, AKTE, &info, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    sond_akte_lease_info_clear(&info);
    g_assert_true(sond_akte_lease_may_write(f->a1, AKTE));

    //wieder erreichbar: das Lebenszeichen klappt, die Berechtigung ist unverändert
    fake_server_set_offline(f->srv, FALSE);
    g_assert_cmpint(heartbeat(f->a1, FALSE, 0, NULL), ==, SOND_LEASE_OK);

    fix_free(f);
}

//Ein hängengebliebener Node-Lock eines anderen wird nach der Wartezeit gebrochen (8 s: langsamer Test)
static void test_haengender_lock(void) {
    Fix *f = NULL;
    gint64 id = 0;

    if (!g_test_slow()) {
        g_test_skip("wartet 8 Sekunden (mit -m slow)");

        return;
    }

    f = fix_new();
    g_assert_cmpint(acquire(f->a1, NULL), ==, SOND_LEASE_OK);
    id = fake_server_first_id(f->srv, "ProjektLease");
    fake_server_hold_lock(f->srv, id, "fremd");

    g_assert_cmpint(heartbeat(f->a1, FALSE, 0, NULL), ==, SOND_LEASE_OK);
    g_assert_cmpuint(fake_server_lock_count(f->srv), ==, 0);

    fix_free(f);
}

void test_akte_lease_register(void) {
    g_test_add_func("/akte_lease/acquire", test_acquire);
    g_test_add_func("/akte_lease/ablauf", test_ablauf);
    g_test_add_func("/akte_lease/ablauf_eigene", test_ablauf_eigene);
    g_test_add_func("/akte_lease/release", test_release);
    g_test_add_func("/akte_lease/heartbeat_uebergabewunsch", test_heartbeat_und_uebergabewunsch);
    g_test_add_func("/akte_lease/takeover_hart", test_takeover_hart);
    g_test_add_func("/akte_lease/takeover_frei", test_takeover_frei);
    g_test_add_func("/akte_lease/index_job", test_index_job);
    g_test_add_func("/akte_lease/index_job_zurueckholen", test_index_job_zurueckholen);
    g_test_add_func("/akte_lease/index_job_frei", test_index_job_frei);
    g_test_add_func("/akte_lease/lokaler_zustand", test_lokaler_zustand);
    g_test_add_func("/akte_lease/dubletten", test_dubletten);
    g_test_add_func("/akte_lease/fehler", test_fehler);
    g_test_add_func("/akte_lease/haengender_lock", test_haengender_lock);
}
