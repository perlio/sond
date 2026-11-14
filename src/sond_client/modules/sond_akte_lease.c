/*
 sond (sond_akte_lease.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_akte_lease.h"

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>

#include "../../sond_graph/sond_graph_node.h"
#include "../../sond_graph/sond_graph_db.h"
#include "../../sond_zond_ctl.h"
#include "../../sond_seadrive_sync.h"

#define LEASE_LABEL "ProjektLease"
#define LEASE_LOCK_REASON "Lease"

//So lange wird auf einen fremden Node-Lock des Lease-Knotens gewartet, bevor er
//als hängengeblieben gilt und gebrochen wird (eine Operation dauert normal
//deutlich unter einer Sekunde)
#define MUTEX_WAIT_MS 8000
#define MUTEX_POLL_MS 250

//Toleranz für eine rückwärts gestellte lokale Uhr
#define CLOCK_BACK_TOLERANCE 300

typedef struct {
    gint64 epoch;
    gint64 node_id; //Lease-Knoten
    gint64 last_ok; //lokale Zeit (Unix) des letzten erfolgreichen Abgleichs
    gchar *name;    //Library-Name der Akte (nullable), um sie offline zu finden
    gchar *project; //Pfad der Projektdatei (nullable), für die Verständigung mit zond
    gint64 since;   //lokale Zeit (Unix), ab der dieses Gerät die Berechtigung hält
    gboolean notified; //Übergabewunsch schon gemeldet (nur zur Laufzeit)
    gboolean handover; //Übergabe läuft: warten auf Speichern und Upload (Laufzeit)
    guint handover_elapsed; //Sekunden seit Beginn der Übergabe
    guint clean_checks; //aufeinanderfolgende Prüfungen "alles gespeichert und hochgeladen"
} LocalLease;

static void local_lease_free(gpointer data) {
    LocalLease *ll = data;

    if (!ll)
        return;
    g_free(ll->name);
    g_free(ll->project);
    g_free(ll);
}

//Takt der Lebenszeichen, Zeitgrenze je Lauf und größte Pause bei Fehlern (in Takten)
#define HEARTBEAT_INTERVAL 30
#define HEARTBEAT_TIMEOUT 4
#define HEARTBEAT_BACKOFF_MAX 10

typedef struct {
    struct _SondAkteLease *lease;
    gint64 akte_id;
    GPid pid;
    guint source;
} ZondProc;

struct _SondAkteLease {
    SondClient *client; //nicht besessen
    gchar *device_id;
    gchar *device_name;
    gchar *state_path;
    GHashTable *local; //gint64* akte_id -> LocalLease*

    SondLeaseEventFunc event_func;
    gpointer event_data;
    guint heartbeat_source;
    guint handover_source; //schneller Takt während einer Übergabe
    guint failures;  //aufeinanderfolgende Verbindungsfehler
    guint backoff;   //noch auszulassende Takte
    GSList *procs;   //ZondProc*
};

/* ------------------------------------------------------------------ */
/*  Lokaler Zustand                                                    */
/* ------------------------------------------------------------------ */

static gchar* default_state_path(void) {
    const gchar *env = g_getenv("SOND_CLIENT_STATE");

    if (env && *env)
        return g_strdup(env);

    return g_build_filename(g_get_user_data_dir(), "sond", "client_state.ini",
            NULL);
}

static void state_save(SondAkteLease *lease) {
    GKeyFile *kf = g_key_file_new();
    GHashTableIter iter;
    gpointer key = NULL;
    gpointer value = NULL;
    gchar *dir = NULL;
    GError *error = NULL;

    g_key_file_set_string(kf, "device", "id", lease->device_id);

    g_hash_table_iter_init(&iter, lease->local);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        LocalLease *ll = value;
        gchar *group = g_strdup_printf("lease_%" G_GINT64_FORMAT,
                *(gint64*) key);

        g_key_file_set_int64(kf, group, "epoch", ll->epoch);
        g_key_file_set_int64(kf, group, "node", ll->node_id);
        g_key_file_set_int64(kf, group, "last_ok", ll->last_ok);
        g_key_file_set_int64(kf, group, "since", ll->since);
        if (ll->name)
            g_key_file_set_string(kf, group, "name", ll->name);
        if (ll->project)
            g_key_file_set_string(kf, group, "project", ll->project);
        g_free(group);
    }

    dir = g_path_get_dirname(lease->state_path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    if (!g_key_file_save_to_file(kf, lease->state_path, &error)) {
        g_warning("Lease-Zustand nicht gespeichert (%s): %s",
                lease->state_path, error->message);
        g_error_free(error);
    }

    g_key_file_free(kf);
}

static void state_load(SondAkteLease *lease) {
    GKeyFile *kf = g_key_file_new();

    if (g_key_file_load_from_file(kf, lease->state_path, G_KEY_FILE_NONE,
            NULL)) {
        gchar **groups = g_key_file_get_groups(kf, NULL);

        lease->device_id = g_key_file_get_string(kf, "device", "id", NULL);

        for (gchar **g = groups; *g; g++) {
            gint64 akte_id = 0;
            LocalLease *ll = NULL;
            gint64 *key = NULL;

            if (!g_str_has_prefix(*g, "lease_"))
                continue;
            akte_id = g_ascii_strtoll(*g + 6, NULL, 10);
            if (akte_id <= 0)
                continue;

            ll = g_new0(LocalLease, 1);
            ll->epoch = g_key_file_get_int64(kf, *g, "epoch", NULL);
            ll->node_id = g_key_file_get_int64(kf, *g, "node", NULL);
            ll->last_ok = g_key_file_get_int64(kf, *g, "last_ok", NULL);
            ll->since = g_key_file_get_int64(kf, *g, "since", NULL);
            ll->name = g_key_file_get_string(kf, *g, "name", NULL);
            ll->project = g_key_file_get_string(kf, *g, "project", NULL);

            key = g_new(gint64, 1);
            *key = akte_id;
            g_hash_table_replace(lease->local, key, ll);
        }
        g_strfreev(groups);
    }

    g_key_file_free(kf);
}

static LocalLease* local_get(SondAkteLease *lease, gint64 akte_id) {
    return g_hash_table_lookup(lease->local, &akte_id);
}

static void local_set(SondAkteLease *lease, gint64 akte_id, gint64 epoch,
        gint64 node_id) {
    gint64 *key = g_new(gint64, 1);
    LocalLease *ll = g_new0(LocalLease, 1);
    LocalLease *old = local_get(lease, akte_id);

    *key = akte_id;
    ll->epoch = epoch;
    ll->node_id = node_id;
    ll->last_ok = g_get_real_time() / G_USEC_PER_SEC;
    //Beginn der Berechtigung bleibt bei Erneuerung (gleiche Epoche) erhalten
    ll->since = (old && old->epoch == epoch && old->since > 0) ? old->since :
            ll->last_ok;
    ll->name = old ? g_strdup(old->name) : NULL; //Name bleibt bei Erneuerung
    ll->project = old ? g_strdup(old->project) : NULL;
    g_hash_table_replace(lease->local, key, ll);
    state_save(lease);
}

void sond_akte_lease_set_name(SondAkteLease *lease, gint64 akte_id,
        const gchar *name) {
    LocalLease *ll = NULL;

    g_return_if_fail(lease != NULL);

    ll = local_get(lease, akte_id);
    if (!ll || g_strcmp0(ll->name, name) == 0)
        return;

    g_free(ll->name);
    ll->name = g_strdup(name);
    state_save(lease);
}

gint64 sond_akte_lease_find_by_name(SondAkteLease *lease, const gchar *name) {
    GHashTableIter iter;
    gpointer key = NULL;
    gpointer value = NULL;

    g_return_val_if_fail(lease != NULL, 0);

    if (!name)
        return 0;

    g_hash_table_iter_init(&iter, lease->local);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (g_strcmp0(((LocalLease*) value)->name, name) == 0)
            return *(gint64*) key;
    }

    return 0;
}

static void local_touch(SondAkteLease *lease, gint64 akte_id) {
    LocalLease *ll = local_get(lease, akte_id);

    if (!ll)
        return;
    ll->last_ok = g_get_real_time() / G_USEC_PER_SEC;
    state_save(lease);
}

static void local_remove(SondAkteLease *lease, gint64 akte_id) {
    if (g_hash_table_remove(lease->local, &akte_id))
        state_save(lease);
}

SondAkteLease* sond_akte_lease_new(SondClient *client) {
    SondAkteLease *lease = NULL;

    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);

    lease = g_new0(SondAkteLease, 1);
    lease->client = client;
    lease->state_path = default_state_path();
    lease->local = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free,
            local_lease_free);

    state_load(lease);

    if (!lease->device_id || !*lease->device_id) {
        g_free(lease->device_id);
        lease->device_id = g_uuid_string_random();
        state_save(lease);
    }

    lease->device_name = g_strdup(g_get_host_name());

    return lease;
}

void sond_akte_lease_free(SondAkteLease *lease) {
    if (!lease)
        return;

    if (lease->heartbeat_source)
        g_source_remove(lease->heartbeat_source);
    if (lease->handover_source)
        g_source_remove(lease->handover_source);

    for (GSList *l = lease->procs; l; l = l->next) {
        ZondProc *zp = l->data;

        g_source_remove(zp->source);
        g_spawn_close_pid(zp->pid);
        g_free(zp);
    }
    g_slist_free(lease->procs);

    g_hash_table_destroy(lease->local);
    g_free(lease->device_id);
    g_free(lease->device_name);
    g_free(lease->state_path);
    g_free(lease);
}

const gchar* sond_akte_lease_get_device_id(SondAkteLease *lease) {
    g_return_val_if_fail(lease != NULL, NULL);

    return lease->device_id;
}

const gchar* sond_akte_lease_get_device_name(SondAkteLease *lease) {
    g_return_val_if_fail(lease != NULL, NULL);

    return lease->device_name;
}

gboolean sond_akte_lease_may_write(SondAkteLease *lease, gint64 akte_id) {
    LocalLease *ll = NULL;
    gint64 diff = 0;

    g_return_val_if_fail(lease != NULL, FALSE);

    ll = local_get(lease, akte_id);
    if (!ll)
        return FALSE;

    diff = g_get_real_time() / G_USEC_PER_SEC - ll->last_ok;

    return diff > -CLOCK_BACK_TOLERANCE && diff < SOND_AKTE_LEASE_SECONDS;
}

/* ------------------------------------------------------------------ */
/*  Hilfen: Eigenschaften des Lease-Knotens, Serverzeit                */
/* ------------------------------------------------------------------ */

//Serverzeit "JJJJ-MM-TT HH:MM:SS" als Sekunden einer Wanduhr ohne Zeitzone.
//Nur Differenzen sind sinnvoll; alle Zeiten kommen von derselben Uhr.
static gboolean parse_server_time(const gchar *text, gint64 *seconds) {
    gint y, mo, d, h, mi, s;
    GDateTime *dt = NULL;

    if (!text || sscanf(text, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi,
            &s) != 6)
        return FALSE;

    dt = g_date_time_new_utc(y, mo, d, h, mi, (gdouble) s);
    if (!dt)
        return FALSE;
    *seconds = g_date_time_to_unix(dt);
    g_date_time_unref(dt);

    return TRUE;
}

//Wert oder "" (nie NULL), mit g_free() freizugeben
static gchar* prop_get(SondGraphNode *node, const gchar *key) {
    gchar *value = sond_graph_node_get_property_string(node, key);

    return value ? value : g_strdup("");
}

static gint64 prop_get_int(SondGraphNode *node, const gchar *key,
        gint64 fallback) {
    gchar *value = sond_graph_node_get_property_string(node, key);
    gint64 result = fallback;

    if (value && *value)
        result = g_ascii_strtoll(value, NULL, 10);
    g_free(value);

    return result;
}

static void prop_set_int(SondGraphNode *node, const gchar *key, gint64 value) {
    gchar *text = g_strdup_printf("%" G_GINT64_FORMAT, value);

    sond_graph_node_set_property_string(node, key, text);
    g_free(text);
}

/* ------------------------------------------------------------------ */
/*  Lease-Knoten suchen und anlegen                                    */
/* ------------------------------------------------------------------ */

//Sucht den Lease-Knoten der Akte (bei Dubletten den mit der kleinsten ID).
//*node: neue Referenz oder NULL, wenn es keinen gibt. FALSE nur bei Fehler.
static gboolean find_lease_node(SondAkteLease *lease, gint64 akte_id,
        SondGraphNode **node, GError **error) {
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    gchar *id_str = g_strdup_printf("%" G_GINT64_FORMAT, akte_id);
    GPtrArray *nodes = NULL;
    SondGraphNode *best = NULL;

    *node = NULL;

    criteria->label = g_strdup(LEASE_LABEL);
    sond_graph_node_search_criteria_add_property_filter(criteria, "akte", id_str);
    criteria->limit = 0;

    nodes = sond_client_search_nodes(lease->client, criteria, error);
    sond_graph_node_search_criteria_free(criteria);

    if (!nodes) {
        g_free(id_str);
        return FALSE;
    }

    for (guint i = 0; i < nodes->len; i++) {
        SondGraphNode *candidate = g_ptr_array_index(nodes, i);
        gchar *akte = sond_graph_node_get_property_string(candidate, "akte");
        gboolean passt = (g_strcmp0(akte, id_str) == 0);

        g_free(akte);
        //der Filter kann mit Mustern arbeiten, deshalb genau nachprüfen
        if (!passt)
            continue;
        if (!best || sond_graph_node_get_id(candidate) <
                sond_graph_node_get_id(best))
            best = candidate;
    }

    if (best)
        *node = g_object_ref(best);

    g_ptr_array_unref(nodes);
    g_free(id_str);

    return TRUE;
}

//Legt den Lease-Knoten an. Entstehen durch gleichzeitiges Anlegen mehrere,
//gewinnt der mit der kleinsten ID; die eigene Dublette wird wieder gelöscht.
static gboolean create_lease_node(SondAkteLease *lease, gint64 akte_id,
        SondGraphNode **node, GError **error) {
    SondGraphNode *mine = sond_graph_node_new();
    gint64 mine_id = 0;
    SondGraphNode *best = NULL;

    sond_graph_node_set_label(mine, LEASE_LABEL);
    prop_set_int(mine, "akte", akte_id);
    sond_graph_node_set_property_string(mine, "device", "");
    sond_graph_node_set_property_string(mine, "device_name", "");
    sond_graph_node_set_property_string(mine, "user", "");
    sond_graph_node_set_property_string(mine, "epoch", "0");
    sond_graph_node_set_property_string(mine, "seen", "0");
    sond_graph_node_set_property_string(mine, "zond_open", "0");
    sond_graph_node_set_property_string(mine, "pending_up", "-1");
    sond_graph_node_set_property_string(mine, "takeover_by", "");

    if (!sond_client_save_node(lease->client, mine, error)) {
        g_object_unref(mine);
        return FALSE;
    }
    mine_id = sond_graph_node_get_id(mine);

    if (!find_lease_node(lease, akte_id, &best, error)) {
        g_object_unref(mine);
        return FALSE;
    }

    if (best && sond_graph_node_get_id(best) != mine_id) {
        GError *ignored = NULL;

        //Dublette: eigenen Knoten entfernen (Löschen verlangt den Lock)
        if (sond_client_lock_node(lease->client, mine_id, LEASE_LOCK_REASON,
                &ignored)) {
            sond_client_delete_node(lease->client, mine, &ignored);
        }
        g_clear_error(&ignored);
        g_object_unref(mine);
        *node = best;

        return TRUE;
    }

    if (best)
        g_object_unref(best);
    *node = mine;

    return TRUE;
}

/* ------------------------------------------------------------------ */
/*  Gegenseitiger Ausschluss und Transaktion                           */
/* ------------------------------------------------------------------ */

//Node-Lock des Lease-Knotens; liefert die Serverzeit der Sperre.
static gboolean mutex_lock(SondAkteLease *lease, gint64 node_id,
        gchar **server_time, GError **error) {
    gint waited = 0;
    gboolean broken = FALSE;

    for (;;) {
        GError *err = NULL;
        gchar *time_str = NULL;
        gchar *by = NULL;
        gchar *at = NULL;
        gboolean conflict = FALSE;

        if (sond_client_lock_node_timed(lease->client, node_id,
                LEASE_LOCK_REASON, &time_str, &by, &at, &err)) {
            g_free(by);
            g_free(at);
            *server_time = time_str;

            return TRUE;
        }

        conflict = (by != NULL || at != NULL);
        g_free(by);
        g_free(at);
        g_free(time_str);

        if (!conflict) {
            g_propagate_error(error, err);

            return FALSE;
        }
        g_clear_error(&err);

        if (waited >= MUTEX_WAIT_MS) {
            GError *ignored = NULL;

            if (broken) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY,
                        "Der Lease-Knoten ist gesperrt und liess sich nicht "
                        "freigeben");

                return FALSE;
            }

            //hängengebliebener Lock (Abbruch zwischen Lock und Unlock)
            sond_client_force_unlock_node(lease->client, node_id, &ignored);
            g_clear_error(&ignored);
            broken = TRUE;

            continue;
        }

        g_usleep(MUTEX_POLL_MS * 1000);
        waited += MUTEX_POLL_MS;
    }
}

static void mutex_unlock(SondAkteLease *lease, gint64 node_id) {
    GError *ignored = NULL;

    sond_client_unlock_node(lease->client, node_id, &ignored);
    g_clear_error(&ignored);
}

typedef struct {
    SondAkteLease *lease;
    gint64 akte_id;
    SondGraphNode *node; //unter dem Lock frisch geladen
    gint64 now;          //Serverzeit
    gboolean dirty;      //Knoten wird nach der Entscheidung gespeichert
} LeaseTxn;

typedef SondLeaseResult (*TxnFunc)(LeaseTxn *txn, gpointer user_data);

//Führt fn unter dem Node-Lock des Lease-Knotens aus. Fehlt der Knoten:
//mit create wird er angelegt, sonst *found = FALSE und fn läuft nicht.
static gboolean lease_run(SondAkteLease *lease, gint64 akte_id, gboolean create,
        TxnFunc fn, gpointer user_data, SondLeaseResult *result,
        gboolean *found, GError **error) {
    SondGraphNode *node = NULL;
    gint64 node_id = 0;
    gchar *server_time = NULL;
    LeaseTxn txn = { 0 };

    *found = TRUE;

    if (!find_lease_node(lease, akte_id, &node, error))
        return FALSE;

    if (!node) {
        if (!create) {
            *found = FALSE;

            return TRUE;
        }
        if (!create_lease_node(lease, akte_id, &node, error))
            return FALSE;
    }

    node_id = sond_graph_node_get_id(node);
    g_object_unref(node);
    node = NULL;

    if (!mutex_lock(lease, node_id, &server_time, error))
        return FALSE;

    //frisch laden, erst jetzt ist der Stand verbindlich
    if (!find_lease_node(lease, akte_id, &node, error) || !node) {
        if (error && !*error)
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                    "Der Lease-Knoten der Akte %" G_GINT64_FORMAT
                    " ist verschwunden", akte_id);
        mutex_unlock(lease, node_id);
        g_free(server_time);

        return FALSE;
    }

    if (!parse_server_time(server_time, &txn.now)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Der Server liefert keine Zeitangabe (Server zu alt?)");
        g_object_unref(node);
        mutex_unlock(lease, node_id);
        g_free(server_time);

        return FALSE;
    }
    g_free(server_time);

    txn.lease = lease;
    txn.akte_id = akte_id;
    txn.node = node;

    *result = fn(&txn, user_data);

    if (txn.dirty) {
        //das Lebenszeichen ändert den Knoten in jedem Fall
        prop_set_int(node, "seen", txn.now);

        if (!sond_client_save_node(lease->client, node, error)) {
            g_object_unref(node);
            mutex_unlock(lease, node_id);

            return FALSE;
        }
    }

    g_object_unref(node);
    mutex_unlock(lease, node_id);

    return TRUE;
}

/* ------------------------------------------------------------------ */
/*  Stand lesen                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    gchar *device;
    gchar *device_name;
    gchar *user;
    gchar *takeover_by;
    gint64 epoch;
    gint64 seen;
    gboolean zond_open;
    gint pending_up;
    gboolean held;
    gboolean mine;
    gboolean same_user;
    gboolean expired;
} LeaseState;

static void state_clear(LeaseState *st) {
    g_free(st->device);
    g_free(st->device_name);
    g_free(st->user);
    g_free(st->takeover_by);
    memset(st, 0, sizeof(LeaseState));
}

static void state_read(LeaseTxn *txn, LeaseState *st) {
    const gchar *own_user = sond_client_get_user_id(txn->lease->client);

    memset(st, 0, sizeof(LeaseState));

    st->device = prop_get(txn->node, "device");
    st->device_name = prop_get(txn->node, "device_name");
    st->user = prop_get(txn->node, "user");
    st->takeover_by = prop_get(txn->node, "takeover_by");
    st->epoch = prop_get_int(txn->node, "epoch", 0);
    st->seen = prop_get_int(txn->node, "seen", 0);
    st->zond_open = prop_get_int(txn->node, "zond_open", 0) != 0;
    st->pending_up = (gint) prop_get_int(txn->node, "pending_up", -1);

    st->held = (*st->device != '\0');
    st->mine = st->held && g_strcmp0(st->device, txn->lease->device_id) == 0;
    st->same_user = st->held && own_user && g_strcmp0(st->user, own_user) == 0;
    st->expired = st->held &&
            (st->seen <= 0 || txn->now - st->seen > SOND_AKTE_LEASE_SECONDS);
}

void sond_akte_lease_info_clear(SondLeaseInfo *info) {
    if (!info)
        return;

    g_free(info->user);
    g_free(info->device_name);
    memset(info, 0, sizeof(SondLeaseInfo));
}

static void info_fill(SondLeaseInfo *info, LeaseTxn *txn, LeaseState *st) {
    if (!info)
        return;

    sond_akte_lease_info_clear(info);

    info->found = TRUE;
    info->held = st->held;
    info->mine = st->mine;
    info->same_user = st->same_user;
    info->expired = st->expired;
    info->takeover_requested = (*st->takeover_by != '\0');
    info->takeover_by_me = info->takeover_requested &&
            g_strcmp0(st->takeover_by, txn->lease->device_id) == 0;
    info->zond_open = st->zond_open;
    info->pending_up = st->pending_up;
    info->idle_seconds = (st->seen > 0) ? txn->now - st->seen : -1;
    info->epoch = st->epoch;
    info->user = g_strdup(st->user);
    info->device_name = g_strdup(st->device_name);
}

/* ------------------------------------------------------------------ */
/*  Entscheidungen                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    gboolean force;
    gboolean zond_open;
    gint pending_up;
    gboolean takeover_requested; //Ausgabe von heartbeat
    gint64 granted_epoch;        //Ausgabe: Epoche nach einer Vergabe
    gint64 node_id;              //Ausgabe: Lease-Knoten nach einer Vergabe
    SondLeaseInfo *info;
} TxnArgs;

//Lease an dieses Gerät vergeben (Epoche steigt, außer beim gleichen Gerät)
static void grant(LeaseTxn *txn, LeaseState *st, TxnArgs *args) {
    const gchar *own_user = sond_client_get_user_id(txn->lease->client);
    gint64 epoch = st->mine ? st->epoch : st->epoch + 1;

    sond_graph_node_set_property_string(txn->node, "device",
            txn->lease->device_id);
    sond_graph_node_set_property_string(txn->node, "device_name",
            txn->lease->device_name ? txn->lease->device_name : "");
    sond_graph_node_set_property_string(txn->node, "user",
            own_user ? own_user : "");
    prop_set_int(txn->node, "epoch", epoch);
    sond_graph_node_set_property_string(txn->node, "takeover_by", "");
    if (!st->mine) {
        sond_graph_node_set_property_string(txn->node, "zond_open", "0");
        sond_graph_node_set_property_string(txn->node, "pending_up", "-1");
    }

    txn->dirty = TRUE;
    args->granted_epoch = epoch;
    args->node_id = sond_graph_node_get_id(txn->node);
}

static SondLeaseResult txn_acquire(LeaseTxn *txn, gpointer user_data) {
    TxnArgs *args = user_data;
    LeaseState st;
    SondLeaseResult result = SOND_LEASE_DENIED;

    state_read(txn, &st);
    info_fill(args->info, txn, &st);

    if (!st.held || st.mine || st.expired) {
        grant(txn, &st, args);
        result = SOND_LEASE_OK;
    }

    state_clear(&st);

    return result;
}

static SondLeaseResult txn_takeover(LeaseTxn *txn, gpointer user_data) {
    TxnArgs *args = user_data;
    LeaseState st;
    SondLeaseResult result = SOND_LEASE_NOT_ALLOWED;

    state_read(txn, &st);
    info_fill(args->info, txn, &st);

    if (!st.held || st.mine || st.expired) {
        grant(txn, &st, args);
        result = SOND_LEASE_OK;
    } else if (!st.same_user) {
        result = SOND_LEASE_NOT_ALLOWED;
    } else if (args->force) {
        grant(txn, &st, args);
        result = SOND_LEASE_OK;
    } else {
        sond_graph_node_set_property_string(txn->node, "takeover_by",
                txn->lease->device_id);
        txn->dirty = TRUE;
        if (args->info) {
            args->info->takeover_requested = TRUE;
            args->info->takeover_by_me = TRUE;
        }
        result = SOND_LEASE_REQUESTED;
    }

    state_clear(&st);

    return result;
}

//Gehört die Lease laut Server noch diesem Gerät (mit der Epoche, die wir
//kennen)? Sonst wurde sie übernommen oder ist abgelaufen und neu vergeben.
static gboolean still_mine(LeaseTxn *txn, LeaseState *st) {
    LocalLease *ll = local_get(txn->lease, txn->akte_id);

    return st->mine && ll && ll->epoch == st->epoch;
}

static SondLeaseResult txn_heartbeat(LeaseTxn *txn, gpointer user_data) {
    TxnArgs *args = user_data;
    LeaseState st;
    SondLeaseResult result = SOND_LEASE_LOST;

    state_read(txn, &st);

    if (still_mine(txn, &st)) {
        sond_graph_node_set_property_string(txn->node, "zond_open",
                args->zond_open ? "1" : "0");
        prop_set_int(txn->node, "pending_up", args->pending_up);
        txn->dirty = TRUE;
        args->takeover_requested = (*st.takeover_by != '\0');
        result = SOND_LEASE_OK;
    }

    state_clear(&st);

    return result;
}

static SondLeaseResult txn_release(LeaseTxn *txn, gpointer user_data) {
    LeaseState st;
    SondLeaseResult result = SOND_LEASE_LOST;

    (void) user_data;

    state_read(txn, &st);

    if (still_mine(txn, &st)) {
        sond_graph_node_set_property_string(txn->node, "device", "");
        sond_graph_node_set_property_string(txn->node, "device_name", "");
        sond_graph_node_set_property_string(txn->node, "takeover_by", "");
        sond_graph_node_set_property_string(txn->node, "zond_open", "0");
        txn->dirty = TRUE;
        result = SOND_LEASE_OK;
    }

    state_clear(&st);

    return result;
}

static SondLeaseResult txn_info(LeaseTxn *txn, gpointer user_data) {
    TxnArgs *args = user_data;
    LeaseState st;

    state_read(txn, &st);
    info_fill(args->info, txn, &st);
    state_clear(&st);

    return SOND_LEASE_OK;
}

/* ------------------------------------------------------------------ */
/*  Öffentliche Operationen                                            */
/* ------------------------------------------------------------------ */

gboolean sond_akte_lease_get_info(SondAkteLease *lease, gint64 akte_id,
        SondLeaseInfo *info, GError **error) {
    TxnArgs args = { 0 };
    SondLeaseResult result = SOND_LEASE_OK;
    gboolean found = TRUE;

    g_return_val_if_fail(lease != NULL && info != NULL, FALSE);
    g_return_val_if_fail(akte_id > 0, FALSE);

    sond_akte_lease_info_clear(info);
    args.info = info;

    return lease_run(lease, akte_id, FALSE, txn_info, &args, &result, &found,
            error);
}

//Gemeinsamer Teil von acquire und takeover: bei OK lokalen Zustand setzen
static SondLeaseResult acquire_or_takeover(SondAkteLease *lease, gint64 akte_id,
        TxnFunc fn, gboolean force, SondLeaseInfo *info, GError **error) {
    TxnArgs args = { 0 };
    SondLeaseResult result = SOND_LEASE_DENIED;
    gboolean found = TRUE;

    args.force = force;
    args.info = info;

    if (info)
        sond_akte_lease_info_clear(info);

    if (!lease_run(lease, akte_id, TRUE, fn, &args, &result, &found, error))
        return SOND_LEASE_ERROR;

    if (result == SOND_LEASE_OK)
        local_set(lease, akte_id, args.granted_epoch, args.node_id);

    return result;
}

SondLeaseResult sond_akte_lease_acquire(SondAkteLease *lease, gint64 akte_id,
        SondLeaseInfo *info, GError **error) {
    g_return_val_if_fail(lease != NULL, SOND_LEASE_ERROR);
    g_return_val_if_fail(akte_id > 0, SOND_LEASE_ERROR);

    return acquire_or_takeover(lease, akte_id, txn_acquire, FALSE, info, error);
}

SondLeaseResult sond_akte_lease_takeover(SondAkteLease *lease, gint64 akte_id,
        gboolean force, SondLeaseInfo *info, GError **error) {
    g_return_val_if_fail(lease != NULL, SOND_LEASE_ERROR);
    g_return_val_if_fail(akte_id > 0, SOND_LEASE_ERROR);

    return acquire_or_takeover(lease, akte_id, txn_takeover, force, info, error);
}

SondLeaseResult sond_akte_lease_heartbeat(SondAkteLease *lease, gint64 akte_id,
        gboolean zond_open, gint pending_up, gboolean *takeover_requested,
        GError **error) {
    TxnArgs args = { 0 };
    SondLeaseResult result = SOND_LEASE_LOST;
    gboolean found = TRUE;

    g_return_val_if_fail(lease != NULL, SOND_LEASE_ERROR);
    g_return_val_if_fail(akte_id > 0, SOND_LEASE_ERROR);

    if (takeover_requested)
        *takeover_requested = FALSE;

    args.zond_open = zond_open;
    args.pending_up = pending_up;

    //ohne lokalen Zustand kann die Lease nicht unsere sein
    if (!local_get(lease, akte_id))
        return SOND_LEASE_LOST;

    if (!lease_run(lease, akte_id, FALSE, txn_heartbeat, &args, &result, &found,
            error))
        return SOND_LEASE_ERROR;

    if (!found)
        result = SOND_LEASE_LOST;

    if (result == SOND_LEASE_OK) {
        local_touch(lease, akte_id);
        if (takeover_requested)
            *takeover_requested = args.takeover_requested;
    } else {
        local_remove(lease, akte_id);
    }

    return result;
}

SondLeaseResult sond_akte_lease_release(SondAkteLease *lease, gint64 akte_id,
        GError **error) {
    TxnArgs args = { 0 };
    SondLeaseResult result = SOND_LEASE_LOST;
    gboolean found = TRUE;

    g_return_val_if_fail(lease != NULL, SOND_LEASE_ERROR);
    g_return_val_if_fail(akte_id > 0, SOND_LEASE_ERROR);

    if (!local_get(lease, akte_id))
        return SOND_LEASE_LOST;

    if (!lease_run(lease, akte_id, FALSE, txn_release, &args, &result, &found,
            error))
        return SOND_LEASE_ERROR;

    if (!found)
        result = SOND_LEASE_LOST;

    //freigegeben oder gar nicht mehr unsere: lokal jedenfalls nichts mehr halten
    local_remove(lease, akte_id);

    return result;
}

/* ------------------------------------------------------------------ */
/*  zond-Prozesse                                                      */
/* ------------------------------------------------------------------ */

static void zond_exited(GPid pid, gint status, gpointer data) {
    ZondProc *zp = data;

    (void) status;

    zp->lease->procs = g_slist_remove(zp->lease->procs, zp);
    g_spawn_close_pid(pid);
    g_free(zp);
}

void sond_akte_lease_note_zond_started(SondAkteLease *lease, gint64 akte_id,
        GPid pid) {
    ZondProc *zp = NULL;

    g_return_if_fail(lease != NULL);

    zp = g_new0(ZondProc, 1);
    zp->lease = lease;
    zp->akte_id = akte_id;
    zp->pid = pid;
    zp->source = g_child_watch_add(pid, zond_exited, zp);
    lease->procs = g_slist_prepend(lease->procs, zp);
}

gboolean sond_akte_lease_zond_open(SondAkteLease *lease, gint64 akte_id) {
    LocalLease *ll = NULL;

    g_return_val_if_fail(lease != NULL, FALSE);

    //zond meldet sich selbst (Steuerdatei): verlässlicher als der Prozess, denn
    //zond ist eine Einzelinstanz-Anwendung
    ll = local_get(lease, akte_id);
    if (ll && ll->project) {
        SondZondCtlState st;
        gboolean running = FALSE;

        sond_zond_ctl_read_state(ll->project, &st);
        running = st.running;
        sond_zond_ctl_state_clear(&st);
        if (running)
            return TRUE;
    }

    for (GSList *l = lease->procs; l; l = l->next) {
        if (((ZondProc*) l->data)->akte_id == akte_id)
            return TRUE;
    }

    return FALSE;
}

void sond_akte_lease_note_project(SondAkteLease *lease, gint64 akte_id,
        const gchar *project_path) {
    LocalLease *ll = NULL;

    g_return_if_fail(lease != NULL);

    ll = local_get(lease, akte_id);
    if (!ll || g_strcmp0(ll->project, project_path) == 0)
        return;

    g_free(ll->project);
    ll->project = g_strdup(project_path);
    state_save(lease);
}

gint sond_akte_lease_unsynced(SondAkteLease *lease, gint64 akte_id) {
    LocalLease *ll = NULL;
    gchar *dir = NULL;
    gint count = -1;

    g_return_val_if_fail(lease != NULL, -1);

    ll = local_get(lease, akte_id);
    if (!ll || !ll->project || ll->since <= 0)
        return -1;

    dir = g_path_get_dirname(ll->project);
    count = sond_seadrive_count_unsynced(dir, ll->since);
    g_free(dir);

    return count;
}

gboolean sond_akte_lease_set_zond_mode(SondAkteLease *lease, gint64 akte_id,
        const gchar *mode, GError **error) {
    LocalLease *ll = NULL;

    g_return_val_if_fail(lease != NULL && mode != NULL, FALSE);

    ll = local_get(lease, akte_id);
    if (!ll || !ll->project)
        return TRUE; //kein Projekt bekannt: nichts zu tun

    return sond_zond_ctl_set_mode(ll->project, mode, error);
}

gboolean sond_akte_lease_zond_settle(SondAkteLease *lease, gint64 akte_id,
        guint timeout_ms) {
    LocalLease *ll = NULL;
    guint waited = 0;

    g_return_val_if_fail(lease != NULL, FALSE);

    ll = local_get(lease, akte_id);
    if (!ll || !ll->project)
        return TRUE; //kein Projekt bekannt: zond kann nicht gestartet worden sein

    sond_zond_ctl_set_mode(ll->project, SOND_ZOND_MODE_RO, NULL);

    for (;;) {
        SondZondCtlState st;
        gboolean settled = FALSE;

        sond_zond_ctl_read_state(ll->project, &st);
        //nicht offen, oder schreibgeschützt und alles gespeichert
        settled = !st.running || (g_strcmp0(st.mode, SOND_ZOND_MODE_RO) == 0 &&
                !st.changed);
        sond_zond_ctl_state_clear(&st);

        if (settled)
            return TRUE;
        if (waited >= timeout_ms)
            return FALSE;

        //die Oberfläche weiterlaufen lassen, während zond speichert
        for (guint i = 0; i < 10; i++)
            g_main_context_iteration(NULL, FALSE);
        g_usleep(300 * 1000);
        waited += 300;
    }
}

const gchar* sond_akte_lease_get_name(SondAkteLease *lease, gint64 akte_id) {
    LocalLease *ll = NULL;

    g_return_val_if_fail(lease != NULL, NULL);

    ll = local_get(lease, akte_id);

    return ll ? ll->name : NULL;
}

/* ------------------------------------------------------------------ */
/*  Lebenszeichen im Hintergrund                                       */
/* ------------------------------------------------------------------ */

void sond_akte_lease_set_event_handler(SondAkteLease *lease,
        SondLeaseEventFunc func, gpointer user_data) {
    g_return_if_fail(lease != NULL);

    lease->event_func = func;
    lease->event_data = user_data;
}

static void notify_event(SondAkteLease *lease, gint64 akte_id,
        const gchar *name, SondLeaseEvent event) {
    if (lease->event_func)
        lease->event_func(lease, akte_id, name, event, lease->event_data);
}

/* ---- Übergabe: warten, bis zond gespeichert und SeaDrive hochgeladen hat ---- */

#define HANDOVER_INTERVAL 5
#define HANDOVER_STUCK_SECONDS 150
#define HANDOVER_CLEAN_CHECKS 2

static gboolean handover_active(SondAkteLease *lease) {
    GHashTableIter iter;
    gpointer value = NULL;

    g_hash_table_iter_init(&iter, lease->local);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        if (((LocalLease*) value)->handover)
            return TRUE;
    }

    return FALSE;
}

static gboolean handover_tick(gpointer data) {
    SondAkteLease *lease = data;
    GArray *ids = g_array_new(FALSE, FALSE, sizeof(gint64));
    GHashTableIter iter;
    gpointer key = NULL;
    gpointer value = NULL;

    g_hash_table_iter_init(&iter, lease->local);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (((LocalLease*) value)->handover)
            g_array_append_val(ids, *(gint64*) key);
    }

    for (guint i = 0; i < ids->len; i++) {
        gint64 akte_id = g_array_index(ids, gint64, i);
        LocalLease *ll = local_get(lease, akte_id);
        gchar *name = NULL;
        gboolean settled = TRUE;
        gint pending = -1;

        if (!ll || !ll->handover)
            continue;

        name = g_strdup(ll->name);
        ll->handover_elapsed += HANDOVER_INTERVAL;

        if (ll->project) {
            SondZondCtlState st;

            sond_zond_ctl_read_state(ll->project, &st);
            //zond nicht offen, oder schreibgeschützt und alles gespeichert
            settled = !st.running || (g_strcmp0(st.mode, SOND_ZOND_MODE_RO) == 0 &&
                    !st.changed);
            sond_zond_ctl_state_clear(&st);

            if (settled)
                pending = sond_akte_lease_unsynced(lease, akte_id);
        }

        if (settled && pending == 0)
            ll->clean_checks++;
        else
            ll->clean_checks = 0;

        if (ll->clean_checks >= HANDOVER_CLEAN_CHECKS) {
            GError *error = NULL;
            guint old_timeout = sond_client_set_io_timeout(lease->client,
                    HEARTBEAT_TIMEOUT);
            SondLeaseResult res = sond_akte_lease_release(lease, akte_id, &error);

            sond_client_set_io_timeout(lease->client, old_timeout);
            g_clear_error(&error);

            if (res != SOND_LEASE_ERROR)   //freigegeben (Eintrag ist weg)
                notify_event(lease, akte_id, name, SOND_LEASE_EVENT_HANDED_OVER);
            //bei einem Verbindungsfehler im nächsten Takt erneut versuchen
        } else if ((settled && pending < 0) ||
                ll->handover_elapsed >= HANDOVER_STUCK_SECONDS) {
            //nicht selbständig zu klären: der Nutzer entscheidet
            ll->handover = FALSE;
            notify_event(lease, akte_id, name, SOND_LEASE_EVENT_HANDOVER_STUCK);
        }

        g_free(name);
    }

    g_array_free(ids, TRUE);

    if (handover_active(lease))
        return G_SOURCE_CONTINUE;

    lease->handover_source = 0;

    return G_SOURCE_REMOVE;
}

static gboolean heartbeat_tick(gpointer data) {
    SondAkteLease *lease = data;
    GArray *ids = NULL;
    GHashTableIter iter;
    gpointer key = NULL;
    gpointer value = NULL;
    guint old_timeout = 0;

    if (g_hash_table_size(lease->local) == 0)
        return G_SOURCE_CONTINUE;

    if (!sond_client_is_logged_in(lease->client))
        return G_SOURCE_CONTINUE;

    if (lease->backoff > 0) {
        lease->backoff--;

        return G_SOURCE_CONTINUE;
    }

    //Schlüssel vorab kopieren: Ereignisse können den lokalen Zustand ändern
    ids = g_array_new(FALSE, FALSE, sizeof(gint64));
    g_hash_table_iter_init(&iter, lease->local);
    while (g_hash_table_iter_next(&iter, &key, &value))
        g_array_append_val(ids, *(gint64*) key);

    old_timeout = sond_client_set_io_timeout(lease->client, HEARTBEAT_TIMEOUT);

    for (guint i = 0; i < ids->len; i++) {
        gint64 akte_id = g_array_index(ids, gint64, i);
        LocalLease *ll = local_get(lease, akte_id);
        gchar *name = NULL;
        gchar *project = NULL;
        gboolean requested = FALSE;
        GError *error = NULL;
        SondLeaseResult res = SOND_LEASE_ERROR;

        if (!ll)
            continue;
        name = g_strdup(ll->name);
        project = g_strdup(ll->project); //bei LOST ist der lokale Eintrag weg

        res = sond_akte_lease_heartbeat(lease, akte_id,
                sond_akte_lease_zond_open(lease, akte_id), -1, &requested,
                &error);
        g_clear_error(&error);

        if (res == SOND_LEASE_ERROR) {
            //Server nicht erreichbar: andere Akten lohnen den Versuch nicht
            lease->failures++;
            lease->backoff = MIN(1u << MIN(lease->failures - 1, 4u),
                    (guint) HEARTBEAT_BACKOFF_MAX);
            g_free(name);
            g_free(project);
            break;
        }
        lease->failures = 0;

        if (res == SOND_LEASE_OK) {
            ll = local_get(lease, akte_id);
            if (ll && requested && !ll->notified) {
                ll->notified = TRUE;
                ll->handover = TRUE;
                ll->handover_elapsed = 0;
                ll->clean_checks = 0;
                if (!lease->handover_source)
                    lease->handover_source = g_timeout_add_seconds(
                            HANDOVER_INTERVAL, handover_tick, lease);
                //zond speichert und wird schreibgeschützt, damit der andere
                //Rechner übernehmen kann
                if (project)
                    sond_zond_ctl_set_mode(project, SOND_ZOND_MODE_RO, NULL);
                notify_event(lease, akte_id, name,
                        SOND_LEASE_EVENT_TAKEOVER_REQUESTED);
            } else if (ll && !requested) {
                ll->notified = FALSE;
                ll->handover = FALSE;
            }
        } else if (res == SOND_LEASE_LOST) {
            //nicht mehr speichern: die Projektdatei gehört dem neuen Halter
            if (project)
                sond_zond_ctl_set_mode(project, SOND_ZOND_MODE_LOST, NULL);
            notify_event(lease, akte_id, name, SOND_LEASE_EVENT_LOST);
        }

        g_free(name);
        g_free(project);
    }

    sond_client_set_io_timeout(lease->client, old_timeout);
    g_array_free(ids, TRUE);

    return G_SOURCE_CONTINUE;
}

void sond_akte_lease_start_heartbeat(SondAkteLease *lease) {
    g_return_if_fail(lease != NULL);

    if (lease->heartbeat_source)
        return;

    lease->heartbeat_source = g_timeout_add_seconds(HEARTBEAT_INTERVAL,
            heartbeat_tick, lease);
}
