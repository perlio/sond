/*
 sond (fake_server.c) - Akten, Beweisstücke, Unterlagen
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

#include "fake_server.h"

#include <string.h>

#include "../sond_graph/sond_graph_db.h"

typedef struct {
    gchar *cid;   /* Kennung des Clients, der den Lock hält */
    gchar *user;
} LockInfo;

struct _FakeServer {
    GHashTable *nodes;   /* gint64* -> SondGraphNode* */
    GHashTable *locks;   /* gint64* -> LockInfo* */
    gint64 next_id;
    gint64 now;
    gboolean offline;
    gboolean no_time;
    guint clients;
};

struct _SondClient {
    GObject parent_instance;
    FakeServer *srv;
    gchar *user;
    gchar *cid;
};

G_DEFINE_TYPE(SondClient, sond_client, G_TYPE_OBJECT)

GQuark sond_client_error_quark(void) {
    return g_quark_from_static_string("sond-client-error");
}

static void sond_client_finalize(GObject *object) {
    SondClient *c = SOND_CLIENT(object);

    g_free(c->user);
    g_free(c->cid);
    G_OBJECT_CLASS(sond_client_parent_class)->finalize(object);
}

static void sond_client_class_init(SondClientClass *klass) {
    G_OBJECT_CLASS(klass)->finalize = sond_client_finalize;
}

static void sond_client_init(SondClient *self) {
    (void) self;
}

static void lock_info_free(gpointer data) {
    LockInfo *l = data;

    g_free(l->cid);
    g_free(l->user);
    g_free(l);
}

static gint64* id_key(gint64 id) {
    gint64 *k = g_new(gint64, 1);

    *k = id;

    return k;
}

FakeServer* fake_server_new(void) {
    FakeServer *srv = g_new0(FakeServer, 1);

    srv->nodes = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, g_object_unref);
    srv->locks = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, lock_info_free);
    srv->next_id = 1;
    srv->now = 1700000000;   //2023-11-14 22:13:20 UTC; nur Differenzen zählen

    return srv;
}

void fake_server_free(FakeServer *srv) {
    if (!srv)
        return;

    g_hash_table_destroy(srv->nodes);
    g_hash_table_destroy(srv->locks);
    g_free(srv);
}

SondClient* fake_server_client(FakeServer *srv, const gchar *user) {
    SondClient *c = g_object_new(SOND_TYPE_CLIENT, NULL);

    c->srv = srv;
    c->user = g_strdup(user);
    c->cid = g_strdup_printf("client-%u", ++srv->clients);

    return c;
}

gint64 fake_server_now(FakeServer *srv) {
    return srv->now;
}

void fake_server_advance(FakeServer *srv, gint64 seconds) {
    srv->now += seconds;
}

void fake_server_set_offline(FakeServer *srv, gboolean offline) {
    srv->offline = offline;
}

void fake_server_set_no_time(FakeServer *srv, gboolean no_time) {
    srv->no_time = no_time;
}

//Kopie über JSON, damit Änderungen des Aufrufers den Speicher nicht berühren
static SondGraphNode* copy_node(SondGraphNode *node) {
    gchar *json = sond_graph_node_to_json(node);
    SondGraphNode *copy = sond_graph_node_from_json(json, NULL);

    g_free(json);

    return copy;
}

gint64 fake_server_add_node(FakeServer *srv, SondGraphNode *node) {
    SondGraphNode *copy = copy_node(node);
    gint64 id = srv->next_id++;

    sond_graph_node_set_id(copy, id);
    sond_graph_node_set_id(node, id);
    g_hash_table_replace(srv->nodes, id_key(id), copy);

    return id;
}

gchar* fake_server_node_prop(FakeServer *srv, gint64 node_id, const gchar *key) {
    SondGraphNode *node = g_hash_table_lookup(srv->nodes, &node_id);

    return node ? sond_graph_node_get_property_string(node, key) : NULL;
}

static gint compare_ids(gconstpointer a, gconstpointer b) {
    gint64 x = *(const gint64*) a;
    gint64 y = *(const gint64*) b;

    return x < y ? -1 : (x > y ? 1 : 0);
}

//IDs aller Knoten, aufsteigend (g_free auf das Array, nicht auf die Elemente)
static GArray* sorted_ids(FakeServer *srv) {
    GArray *ids = g_array_new(FALSE, FALSE, sizeof(gint64));
    GHashTableIter it;
    gpointer key = NULL;

    g_hash_table_iter_init(&it, srv->nodes);
    while (g_hash_table_iter_next(&it, &key, NULL))
        g_array_append_val(ids, *(gint64*) key);
    g_array_sort(ids, compare_ids);

    return ids;
}

guint fake_server_count(FakeServer *srv, const gchar *label) {
    GHashTableIter it;
    gpointer value = NULL;
    guint n = 0;

    g_hash_table_iter_init(&it, srv->nodes);
    while (g_hash_table_iter_next(&it, NULL, &value))
        if (!g_strcmp0(sond_graph_node_get_label(value), label))
            n++;

    return n;
}

gint64 fake_server_first_id(FakeServer *srv, const gchar *label) {
    GArray *ids = sorted_ids(srv);
    gint64 result = 0;

    for (guint i = 0; i < ids->len && !result; i++) {
        gint64 id = g_array_index(ids, gint64, i);
        SondGraphNode *node = g_hash_table_lookup(srv->nodes, &id);

        if (!g_strcmp0(sond_graph_node_get_label(node), label))
            result = id;
    }
    g_array_free(ids, TRUE);

    return result;
}

guint fake_server_lock_count(FakeServer *srv) {
    return g_hash_table_size(srv->locks);
}

void fake_server_hold_lock(FakeServer *srv, gint64 node_id, const gchar *user) {
    LockInfo *l = g_new0(LockInfo, 1);

    l->cid = g_strdup("fremd");
    l->user = g_strdup(user);
    g_hash_table_replace(srv->locks, id_key(node_id), l);
}

/* ------------------------------------------------------------------ */
/*  Client-Funktionen                                                  */
/* ------------------------------------------------------------------ */

static gboolean offline_error(SondClient *c, GError **error) {
    if (!c->srv->offline)
        return FALSE;

    g_set_error(error, G_IO_ERROR, G_IO_ERROR_HOST_UNREACHABLE, "Server nicht erreichbar");

    return TRUE;
}

static gchar* time_string(FakeServer *srv) {
    GDateTime *dt = g_date_time_new_from_unix_utc(srv->now);
    gchar *s = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");

    g_date_time_unref(dt);

    return s;
}

const gchar* sond_client_get_user_id(SondClient *client) {
    return client->user;
}

gboolean sond_client_is_logged_in(SondClient *client) {
    (void) client;

    return TRUE;
}

guint sond_client_set_io_timeout(SondClient *client, guint seconds) {
    (void) client;
    (void) seconds;

    return 0;
}

static gboolean lock_impl(SondClient *c, gint64 node_id, gchar **out_time, gchar **out_by,
        gchar **out_at, GError **error) {
    FakeServer *srv = c->srv;
    LockInfo *held = NULL;

    if (offline_error(c, error))
        return FALSE;

    if (!g_hash_table_contains(srv->nodes, &node_id)) {
        g_set_error(error, SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_GONE, "Knoten %" G_GINT64_FORMAT
                " nicht gefunden", node_id);

        return FALSE;
    }

    held = g_hash_table_lookup(srv->locks, &node_id);
    if (held && g_strcmp0(held->cid, c->cid)) {
        if (out_by)
            *out_by = g_strdup(held->user);
        if (out_at) {
            *out_at = time_string(srv);
        }
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY, "gesperrt von %s", held->user);

        return FALSE;
    }

    if (!held) {
        LockInfo *l = g_new0(LockInfo, 1);

        l->cid = g_strdup(c->cid);
        l->user = g_strdup(c->user);
        g_hash_table_replace(srv->locks, id_key(node_id), l);
    }
    if (out_time)
        *out_time = srv->no_time ? NULL : time_string(srv);

    return TRUE;
}

gboolean sond_client_lock_node(SondClient *client, gint64 node_id, const gchar *lock_reason,
        GError **error) {
    (void) lock_reason;

    return lock_impl(client, node_id, NULL, NULL, NULL, error);
}

gboolean sond_client_lock_node_timed(SondClient *client, gint64 node_id, const gchar *lock_reason,
        gchar **out_server_time, gchar **out_locked_by, gchar **out_locked_at, GError **error) {
    (void) lock_reason;

    return lock_impl(client, node_id, out_server_time, out_locked_by, out_locked_at, error);
}

gboolean sond_client_unlock_node(SondClient *client, gint64 node_id, GError **error) {
    LockInfo *held = NULL;

    if (offline_error(client, error))
        return FALSE;

    held = g_hash_table_lookup(client->srv->locks, &node_id);
    if (held && !g_strcmp0(held->cid, client->cid))
        g_hash_table_remove(client->srv->locks, &node_id);

    return TRUE;
}

gboolean sond_client_force_unlock_node(SondClient *client, gint64 node_id, GError **error) {
    if (offline_error(client, error))
        return FALSE;

    g_hash_table_remove(client->srv->locks, &node_id);

    return TRUE;
}

static gboolean has_lock(SondClient *c, gint64 node_id) {
    LockInfo *held = g_hash_table_lookup(c->srv->locks, &node_id);

    return held && !g_strcmp0(held->cid, c->cid);
}

gboolean sond_client_save_node(SondClient *client, gpointer node_ptr, GError **error) {
    FakeServer *srv = client->srv;
    SondGraphNode *node = node_ptr;
    gint64 id = sond_graph_node_get_id(node);

    if (offline_error(client, error))
        return FALSE;

    if (id <= 0) {
        fake_server_add_node(srv, node);

        return TRUE;
    }

    if (!g_hash_table_contains(srv->nodes, &id)) {
        g_set_error(error, SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_GONE, "Knoten gelöscht");

        return FALSE;
    }
    if (!has_lock(client, id)) {
        g_set_error(error, SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_LOCK_NOT_HELD,
                "Der Lock liegt nicht bei diesem Client");

        return FALSE;
    }

    g_hash_table_replace(srv->nodes, id_key(id), copy_node(node));

    return TRUE;
}

gboolean sond_client_delete_node(SondClient *client, gpointer node_ptr, GError **error) {
    gint64 id = sond_graph_node_get_id(node_ptr);

    if (offline_error(client, error))
        return FALSE;

    if (!has_lock(client, id)) {
        g_set_error(error, SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_LOCK_NOT_HELD,
                "Der Lock liegt nicht bei diesem Client");

        return FALSE;
    }

    g_hash_table_remove(client->srv->nodes, &id);
    g_hash_table_remove(client->srv->locks, &id);

    return TRUE;
}

//Passt der Filter auf eine Eigenschaft des Knotens?
static gboolean filter_passt(SondGraphNode *node, SondPropertyFilter *f) {
    GPtrArray *values = sond_graph_node_get_property(node, f->key);
    gboolean passt = FALSE;

    if (!values)
        return FALSE;

    for (guint i = 0; i < values->len && !passt; i++) {
        if (f->array_index >= 0 && (guint) f->array_index != i)
            continue;
        passt = g_pattern_match_simple(f->value, g_ptr_array_index(values, i));
    }
    g_ptr_array_unref(values);

    return passt;
}

GPtrArray* sond_client_search_nodes(SondClient *client, gpointer criteria_ptr, GError **error) {
    SondGraphNodeSearchCriteria *criteria = criteria_ptr;
    GPtrArray *result = NULL;
    GArray *ids = NULL;

    if (offline_error(client, error))
        return NULL;

    result = g_ptr_array_new_with_free_func(g_object_unref);
    ids = sorted_ids(client->srv);

    for (guint i = 0; i < ids->len; i++) {
        gint64 id = g_array_index(ids, gint64, i);
        SondGraphNode *node = g_hash_table_lookup(client->srv->nodes, &id);
        gboolean passt = TRUE;

        if (criteria->label && g_strcmp0(criteria->label, sond_graph_node_get_label(node)))
            passt = FALSE;
        for (guint k = 0; passt && criteria->property_filters && k < criteria->property_filters->len; k++)
            passt = filter_passt(node, g_ptr_array_index(criteria->property_filters, k));

        if (passt) {
            g_ptr_array_add(result, copy_node(node));
            if (criteria->limit && result->len >= criteria->limit)
                break;
        }
    }
    g_array_free(ids, TRUE);

    return result;
}
