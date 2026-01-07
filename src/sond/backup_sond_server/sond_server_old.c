#include <libgen.h>         // dirname
#include <unistd.h>         // readlink
#include <gio/gio.h>
#include <stdio.h>
#include <mariadb/mysql.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __linux__
#include <linux/limits.h>   // PATH_MAX
#endif // __linux__

#include "../../misc_stdlib.h"
#include "../../sond_database.h"

#include "../backup_sond_server/sond_server_old.h"
#include "../backup_sond_server/sond_server_akte.h"

typedef struct _Cred {
	gchar *user;
	gchar *password;
} Cred;

static void free_cred(Cred *cred) {
	g_free(cred->user);
	g_free(cred->password);

	return;
}

static void sond_server_unlock_ID(SondServer *sond_server, gint auth,
		gchar *params, gchar **omessage) {
	gint ID_entity = 0;
	Lock lock = { 0 };

	ID_entity = atoi(params);

	g_mutex_lock(&sond_server->mutex_arr_locks);
	for (gint i = 0; i < sond_server->arr_locks->len; i++) {
		lock = g_array_index(sond_server->arr_locks, Lock, i);

		if (lock.ID_entity == ID_entity) {
			g_array_remove_index_fast(sond_server->arr_locks, i);
			break;
		}
	}
	g_mutex_unlock(&sond_server->mutex_arr_locks);

	*omessage = g_strdup("OK");

	return;
}

Lock sond_server_has_lock(SondServer *sond_server, gint ID_entity) {
	Lock lock = { 0 };

	g_mutex_lock(&sond_server->mutex_arr_locks);
	for (gint i = 0; i < sond_server->arr_locks->len; i++) {
		lock = g_array_index(sond_server->arr_locks, Lock, i);

		if (lock.ID_entity == ID_entity) {
			g_mutex_unlock(&sond_server->mutex_arr_locks);
			return lock;
		}
	}

	g_mutex_unlock(&sond_server->mutex_arr_locks);

	lock.ID_entity = 0;

	return lock;
}

gint sond_server_get_lock(SondServer *sond_server, gint ID_entity, gint auth,
		gboolean force, const gchar **user) {
	Lock lock = { 0 };
	Cred cred = { 0 };

	g_mutex_lock(&sond_server->mutex_arr_locks);
	for (gint i = 0; i < sond_server->arr_locks->len; i++) {
		lock = g_array_index(sond_server->arr_locks, Lock, i);

		if (lock.ID_entity == ID_entity) {
			if (force) {
				if (lock.index != auth) {
					g_array_remove_index_fast(sond_server->arr_locks, i);
					break;
				}
			} else {
				if (user)
					*user = lock.user;
				g_mutex_unlock(&sond_server->mutex_arr_locks);

				return 1;
			}
		}
	}

	lock.ID_entity = ID_entity;

	g_mutex_lock(&sond_server->mutex_arr_creds);
	cred = g_array_index(sond_server->arr_creds, Cred, auth);
	lock.user = cred.user;
	g_mutex_unlock(&sond_server->mutex_arr_creds);

	g_array_append_val(sond_server->arr_locks, lock);

	g_mutex_unlock(&sond_server->mutex_arr_locks);

	return 0;
}

static void sond_server_lock_ID(SondServer *sond_server, gint auth,
		gchar *params, gchar **omessage) {
	gint ID_entity = 0;

	ID_entity = atoi(params);
	sond_server_get_lock(sond_server, ID_entity, auth, TRUE, NULL);

	*omessage = g_strdup("OK");

	return;
}

gchar*
sond_server_seafile_get_auth_token(SondServer *sond_server, const gchar *user,
		const gchar *password, gchar **errmsg) {
	SoupSession *soup_session = NULL;
	SoupMessage *soup_message = NULL;
	gchar *url_text = NULL;
	gchar *body_text = NULL;
	GBytes *body = NULL;
	GBytes *response = NULL;
	JsonParser *parser = NULL;
	JsonNode *node = NULL;
	GError *error = NULL;
	gchar *auth_token = NULL;

	url_text = g_strdup_printf("%s/api2/auth-token/", sond_server->seafile_url);
	soup_session = soup_session_new();
	soup_message = soup_message_new(SOUP_METHOD_POST, url_text);
	g_free(url_text);

	body_text = g_strdup_printf("username=%s&password=%s", user, password);
	body = g_bytes_new(body_text, strlen(body_text));
	g_free(body_text);
	soup_message_set_request_body_from_bytes(soup_message,
			"application/x-www-form-urlencoded", body);
	g_bytes_unref(body);

	response = soup_session_send_and_read(soup_session, soup_message, NULL,
			&error);
	g_object_unref(soup_message);
	g_object_unref(soup_session);
	if (error) {
		if (errmsg)
			*errmsg = g_strconcat("Keine Antwort vom SeafileServer\n\n"
					"Bei Aufruf soup_session_send_and_read:\n", error->message,
					NULL);
		g_error_free(error);

		return NULL;
	}

	parser = json_parser_new();
	if (!json_parser_load_from_data(parser, g_bytes_get_data(response, NULL),
			-1, &error)) {
		if (errmsg)
			*errmsg = g_strconcat(
					"Antwort vom SeafileServer nicht im json-Format\n\n"
							"Bei Aufruf json_parser_load_from_data:\n",
					error->message, "\n\nEmpfangene Nachricht:\n",
					g_bytes_get_data(response, NULL), NULL);
		g_error_free(error);

		g_object_unref(parser);
		g_bytes_unref(response);

		return NULL;
	}

	node = json_parser_get_root(parser);
	if (JSON_NODE_HOLDS_OBJECT(node)) {
		JsonObject *object = NULL;

		object = json_node_get_object(node);

		if (json_object_has_member(object, "token"))
			auth_token = g_strdup(
					json_object_get_string_member(object, "token"));
		else {
			if (errmsg)
				*errmsg =
						g_strconcat(
								"Antwort vom SeafileServer\n\n"
										"json hat kein member " "token" "\n\nEmpfangene Nachricht:\n",
								g_bytes_get_data(response, NULL), NULL);

			g_object_unref(parser);
			g_bytes_unref(response);

			return NULL;
		}
	} else {
		if (errmsg)
			*errmsg = g_strconcat("Antwort vom SeafileServer\n\n"
					"json ist kein object\n\nEmpfangene Nachricht:\n",
					g_bytes_get_data(response, NULL), NULL);

		g_object_unref(parser);
		g_bytes_unref(response);

		return NULL;
	}

	g_object_unref(parser);
	g_bytes_unref(response);

	return auth_token;
}

static gint get_auth_level(SondServer *sond_server, const gchar *user,
		const gchar *password) {
	Cred cred = { 0 };
	gchar *errmsg = NULL;
	gint i = 0;

	//mutex user
	g_mutex_lock(&sond_server->mutex_arr_creds);
	for (i = 0; i < sond_server->arr_creds->len; i++) {
		cred = g_array_index(sond_server->arr_creds, Cred, i);
		if (!g_strcmp0(cred.user, user))
			break;
	}
	g_mutex_unlock(&sond_server->mutex_arr_creds);

	if (i == sond_server->arr_creds->len) {
		gchar *auth_token = NULL;

		auth_token = sond_server_seafile_get_auth_token(sond_server, user,
				password, &errmsg);
		if (!auth_token) {
			g_warning("User konnte nicht legitimiert werden:\n%s", errmsg);
			g_free(errmsg);

			return -1;
		}

		//brauchen wir nicht
		g_free(auth_token);

		g_mutex_lock(&sond_server->mutex_arr_creds);
		if (i < sond_server->arr_creds->len) {
			g_free(
					(((Cred*) (void*) (sond_server->arr_creds)->data)[i]).password);
			(((Cred*) (void*) (sond_server->arr_creds)->data)[i]).password =
					g_strdup(password);
		} else {
			Cred cred = { 0 };

			cred.user = g_strdup(user);
			cred.password = g_strdup(password);

			g_array_append_val(sond_server->arr_creds, cred);
		}
		g_mutex_unlock(&sond_server->mutex_arr_creds);
	}

	return i;
}

static gint process_imessage(SondServer *sond_server, gchar **imessage_strv,
		gchar **omessage) {
	gint auth = 0;

	auth = get_auth_level(sond_server, imessage_strv[0], imessage_strv[1]);
	if (auth < 0) {
		*omessage = g_strdup("ERROR *** AUTHENTICATION FAILED");
		return 0;
	}

	if (!g_strcmp0(imessage_strv[2], "PING"))
		*omessage = g_strdup("PONG");
	else if (!g_strcmp0(imessage_strv[2], "SHUTDOWN")) {
		*omessage = g_strdup("SONDSERVER_OK");

		return 1;
	} else if (!g_strcmp0(imessage_strv[2], "AKTE_SCHREIBEN"))
		sond_server_akte_schreiben(sond_server, auth, imessage_strv[3],
				omessage);
	else if (!g_strcmp0(imessage_strv[2], "AKTE_HOLEN"))
		sond_server_akte_holen(sond_server, auth, imessage_strv[3], omessage);
	else if (!g_strcmp0(imessage_strv[2], "GET_LOCK"))
		sond_server_lock_ID(sond_server, auth, imessage_strv[3], omessage);
	else if (!g_strcmp0(imessage_strv[2], "UNLOCK"))
		sond_server_unlock_ID(sond_server, auth, imessage_strv[3], omessage);
	else if (!g_strcmp0(imessage_strv[2], "AKTE_SUCHEN"))
		sond_server_akte_suchen(sond_server, imessage_strv[3], omessage);

	else {
		*omessage = g_strdup("ERROR *** UNBEKANNTER BEFEHL");
	}

	return 0;
}