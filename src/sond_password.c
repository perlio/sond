/*
 sond (sond_password.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2020  pelo america

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

#include <glib.h>
#include <string.h>

#include "sond_password.h"

/* Schlüssel: 'E' bzw. 'C' (Rolle) gefolgt vom Filepart-String. Wert: das
 * Passwort. Zugriff nur mit gehaltenem mutex. */
static GMutex mutex;
static GHashTable *table = NULL;

//Überschreiben so, dass der Compiler es nicht als überflüssig entfernt
static void wipe(gchar *s) {
	volatile guchar *p = (volatile guchar*) s;
	gsize len = 0;

	if (!s)
		return;

	len = strlen(s);
	for (gsize i = 0; i < len; i++)
		p[i] = 0;
}

void sond_password_free(gchar *password) {
	wipe(password);
	g_free(password);
}

static void value_free(gpointer p) {
	sond_password_free((gchar*) p);
}

static GHashTable* table_get(void) {
	if (!table)
		table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
				value_free);

	return table;
}

static gchar* key_make(gchar const *filepart, SondPasswordRole role) {
	return g_strdup_printf("%c%s", (role == SOND_PASSWORD_ENTRY) ? 'E' : 'C',
			filepart);
}

//Filepart des umgebenden Archivs ("a.zip" für "a.zip//x.pdf"), sonst NULL
static gchar* parent_of(gchar const *filepart) {
	gchar const *sep = g_strrstr(filepart, "//");

	return sep ? g_strndup(filepart, sep - filepart) : NULL;
}

//Abfrage beim Nutzer, s. sond_password.h
static SondPasswordPromptFunc prompt_func = NULL;
static gpointer prompt_data = NULL;
static GThread *prompt_thread = NULL;

//Aktion über mehrere Dateien (nur im Hauptthread benutzt)
static gint action_depth = 0;
static gboolean action_skip_all = FALSE;
static gboolean action_cancelled = FALSE;
static SondPromptResult last_result = SOND_PROMPT_OK;

void sond_password_set_prompt_func(SondPasswordPromptFunc func,
		gpointer user_data) {
	prompt_func = func;
	prompt_data = user_data;
	prompt_thread = g_thread_self();
}

gboolean sond_password_can_prompt(void) {
	return prompt_func && g_thread_self() == prompt_thread;
}

void sond_password_action_begin(void) {
	if (action_depth++ == 0) {
		action_skip_all = FALSE;
		action_cancelled = FALSE;
		last_result = SOND_PROMPT_OK;
	}
}

void sond_password_action_end(void) {
	if (action_depth > 0 && --action_depth == 0) {
		action_skip_all = FALSE;
		action_cancelled = FALSE;
	}
}

SondPromptResult sond_password_last_result(void) {
	return last_result;
}

gchar* sond_password_prompt(gchar const *what, gboolean wrong) {
	gchar *password = NULL;
	SondPromptResult res = SOND_PROMPT_OK;

	if (!sond_password_can_prompt())
		return NULL;

	if (action_depth > 0 && action_cancelled) {
		last_result = SOND_PROMPT_CANCEL;

		return NULL;
	}
	if (action_depth > 0 && action_skip_all) {
		last_result = SOND_PROMPT_SKIP;

		return NULL;
	}

	res = prompt_func(what, wrong, action_depth > 0, &password, prompt_data);
	last_result = res;

	if (res == SOND_PROMPT_OK)
		return password;

	sond_password_free(password);
	if (action_depth > 0) {
		if (res == SOND_PROMPT_SKIP_ALL)
			action_skip_all = TRUE;
		else if (res == SOND_PROMPT_CANCEL)
			action_cancelled = TRUE;
	}

	return NULL;
}

void sond_password_set(gchar const *filepart, SondPasswordRole role,
		gchar const *password) {
	g_return_if_fail(filepart);

	g_mutex_lock(&mutex);
	if (password && *password)
		g_hash_table_insert(table_get(), key_make(filepart, role),
				g_strdup(password));
	else if (table) {
		gchar *key = key_make(filepart, role);

		g_hash_table_remove(table, key);
		g_free(key);
	}
	g_mutex_unlock(&mutex);
}

gchar* sond_password_get(gchar const *filepart, SondPasswordRole role) {
	gchar *key = NULL;
	gchar *password = NULL;

	g_return_val_if_fail(filepart, NULL);

	key = key_make(filepart, role);
	g_mutex_lock(&mutex);
	if (table)
		password = g_strdup(g_hash_table_lookup(table, key));
	g_mutex_unlock(&mutex);
	g_free(key);

	return password;
}

static void candidate_add(GPtrArray *arr, gchar const *password) {
	if (!password)
		return;

	for (guint i = 0; i < arr->len; i++)
		if (!g_strcmp0(g_ptr_array_index(arr, i), password))
			return;

	g_ptr_array_add(arr, g_strdup(password));
}

GPtrArray* sond_password_candidates_entry(gchar const *filepart) {
	GPtrArray *arr = g_ptr_array_new_with_free_func(value_free);
	gchar *key = NULL;
	gchar *parent = NULL;

	g_return_val_if_fail(filepart, arr);

	key = key_make(filepart, SOND_PASSWORD_ENTRY);
	parent = parent_of(filepart);

	g_mutex_lock(&mutex);
	if (table) {
		candidate_add(arr, g_hash_table_lookup(table, key));

		if (parent) {
			gchar *key_default = key_make(parent, SOND_PASSWORD_CONTENT);
			gchar *prefix = g_strdup_printf("E%s//", parent);
			GHashTableIter iter = { 0 };
			gpointer k = NULL;
			gpointer v = NULL;

			candidate_add(arr, g_hash_table_lookup(table, key_default));

			//Einträge desselben Archivs, nicht eines darin liegenden
			g_hash_table_iter_init(&iter, table);
			while (g_hash_table_iter_next(&iter, &k, &v))
				if (g_str_has_prefix((gchar const*) k, prefix) &&
						!strstr((gchar const*) k + strlen(prefix), "//"))
					candidate_add(arr, (gchar const*) v);

			g_free(key_default);
			g_free(prefix);
		}
	}
	g_mutex_unlock(&mutex);

	g_free(key);
	g_free(parent);

	return arr;
}

void sond_password_array_free(GPtrArray *arr) {
	if (arr)
		g_ptr_array_unref(arr);
}

//TRUE, wenn der Pfad im Schlüssel (ohne Rollenzeichen) prefix selbst oder darunter ist
static gboolean key_under_prefix(gchar const *key, gchar const *prefix,
		gsize len) {
	gchar const *path = key + 1;

	return !strncmp(path, prefix, len) && (path[len] == '\0' || path[len] == '/');
}

void sond_password_rename_prefix(gchar const *prefix, gchar const *new_prefix) {
	GHashTableIter iter = { 0 };
	gpointer k = NULL;
	gpointer v = NULL;
	GPtrArray *keys_new = NULL;
	GPtrArray *values = NULL;
	gsize len = 0;
	guint n = 0;

	g_return_if_fail(prefix && new_prefix);

	len = strlen(prefix);

	g_mutex_lock(&mutex);
	if (!table) {
		g_mutex_unlock(&mutex);

		return;
	}

	keys_new = g_ptr_array_new_with_free_func(g_free);
	values = g_ptr_array_new();

	//erst herauslösen, dann neu einfügen: neue Schlüssel nicht mit alten mischen
	g_hash_table_iter_init(&iter, table);
	while (g_hash_table_iter_next(&iter, &k, &v)) {
		gchar const *key = (gchar const*) k;

		if (!key_under_prefix(key, prefix, len))
			continue;

		g_ptr_array_add(keys_new, g_strdup_printf("%c%s%s", key[0], new_prefix,
				key + 1 + len));
		g_ptr_array_add(values, v);
		g_hash_table_iter_steal(&iter);
		g_free(k); //alter Schlüssel, Wert geht in values über
	}

	n = keys_new->len; //steal_index() verkleinert das Array
	for (guint i = 0; i < n; i++)
		g_hash_table_insert(table, g_ptr_array_steal_index(keys_new, 0),
				g_ptr_array_index(values, i));
	g_mutex_unlock(&mutex);

	g_ptr_array_unref(keys_new);
	g_ptr_array_unref(values);
}

void sond_password_forget_prefix(gchar const *prefix) {
	GHashTableIter iter = { 0 };
	gpointer k = NULL;
	gpointer v = NULL;
	gsize len = 0;

	g_return_if_fail(prefix);

	len = strlen(prefix);

	g_mutex_lock(&mutex);
	if (table) {
		g_hash_table_iter_init(&iter, table);
		while (g_hash_table_iter_next(&iter, &k, &v))
			if (key_under_prefix((gchar const*) k, prefix, len))
				g_hash_table_iter_remove(&iter);
	}
	g_mutex_unlock(&mutex);
}

void sond_password_clear(void) {
	g_mutex_lock(&mutex);
	if (table)
		g_hash_table_remove_all(table);
	g_mutex_unlock(&mutex);
}
