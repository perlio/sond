/*
 sond (fake_server.h) - Akten, Beweisstücke, Unterlagen
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
 * Nachgebauter Graph-Server für Tests des Clients (ohne Netz): Knoten im Speicher, Node-Locks,
 * eine steuerbare Serverzeit. Stellt den Typ SondClient und die Client-Funktionen bereit, die die
 * Lease benutzt (search_nodes, save_node, delete_node, lock_node[_timed], unlock_node,
 * force_unlock_node, get_user_id, is_logged_in, set_io_timeout). Mehrere Clients (Geräte, Nutzer)
 * teilen sich einen Server. Statt sond_client.c linken.
 */

#ifndef FAKE_SERVER_H
#define FAKE_SERVER_H

#include "../sond_client/sond_client.h"
#include "../sond_graph/sond_graph_node.h"

typedef struct _FakeServer FakeServer;

FakeServer* fake_server_new(void);
void fake_server_free(FakeServer *srv);

/* Neuer Client dieses Servers für den Nutzer (transfer full) */
SondClient* fake_server_client(FakeServer *srv, const gchar *user);

/* Serverzeit (Unix-Sekunden) lesen und vorstellen */
gint64 fake_server_now(FakeServer *srv);
void fake_server_advance(FakeServer *srv, gint64 seconds);

/* Der Server antwortet nicht (alle Aufrufe scheitern) */
void fake_server_set_offline(FakeServer *srv, gboolean offline);

/* lock_node_timed liefert keine Serverzeit (alter Server) */
void fake_server_set_no_time(FakeServer *srv, gboolean no_time);

/* Knoten direkt anlegen (ohne Lock); liefert die vergebene ID */
gint64 fake_server_add_node(FakeServer *srv, SondGraphNode *node);

/* Eigenschaft eines Knotens (g_free) oder NULL, wenn es den Knoten nicht gibt */
gchar* fake_server_node_prop(FakeServer *srv, gint64 node_id, const gchar *key);

/* Anzahl der Knoten mit dem Label */
guint fake_server_count(FakeServer *srv, const gchar *label);

/* ID des ersten (kleinsten) Knotens mit dem Label, 0 wenn keiner */
gint64 fake_server_first_id(FakeServer *srv, const gchar *label);

/* Anzahl der gehaltenen Node-Locks */
guint fake_server_lock_count(FakeServer *srv);

/* Ein Node-Lock für einen fremden Nutzer setzen (simuliert eine hängende Sperre) */
void fake_server_hold_lock(FakeServer *srv, gint64 node_id, const gchar *user);

#endif /* FAKE_SERVER_H */
