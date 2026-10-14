/*
 zond (sond_tmpfiles.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_tmpfiles.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#endif

#include "sond_file_helper.h"
#include "sond_log_and_error.h"

//Eine registrierte Datei wird frühestens so lange nach dem Anlegen gelöscht:
//die Anwendung, die sie öffnen soll, hat sie dann vermutlich schon geöffnet
#define TMP_GRACE_US (60 * G_USEC_PER_SEC)
#define TMP_SWEEP_SECONDS 30
#define TMP_NAME_MAX_CHARS 80

typedef struct {
	gchar *path;
	gchar *dir;
	gint64 created;
} TmpEntry;

G_LOCK_DEFINE_STATIC(tmp);
static GPtrArray *entries = NULL;
static gchar *root = NULL;
static gint counter = 0;

static void tmp_entry_free(gpointer data) {
	TmpEntry *entry = (TmpEntry *) data;

	g_free(entry->path);
	g_free(entry->dir);
	g_free(entry);

	return;
}

static gint tmp_own_pid(void) {
#ifdef G_OS_WIN32
	return (gint) GetCurrentProcessId();
#else
	return (gint) getpid();
#endif
}

static gboolean tmp_pid_alive(gint pid) {
#ifdef G_OS_WIN32
	HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
			(DWORD) pid);
	DWORD code = 0;
	gboolean alive = FALSE;

	if (!handle)
		return GetLastError() == ERROR_ACCESS_DENIED;

	if (GetExitCodeProcess(handle, &code))
		alive = (code == STILL_ACTIVE);
	CloseHandle(handle);

	return alive;
#else
	return kill((pid_t) pid, 0) == 0 || errno == EPERM;
#endif
}

//Inhalt von dir löschen, dann dir selbst; was gesperrt ist, bleibt liegen
static void tmp_remove_tree(gchar const *dir) {
	GDir *gdir = g_dir_open(dir, 0, NULL);
	gchar const *name = NULL;

	if (gdir) {
		while ((name = g_dir_read_name(gdir))) {
			gchar *path = g_build_filename(dir, name, NULL);

			if (g_file_test(path, G_FILE_TEST_IS_DIR))
				tmp_remove_tree(path);
			else
				sond_remove_once(path, NULL);
			g_free(path);
		}
		g_dir_close(gdir);
	}
	sond_rmdir_once(dir, NULL);

	return;
}

//Sitzungsordner "<pid>-<n>" im Wurzelordner; own: nur die des eigenen
//Prozesses, sonst die aller beendeten Prozesse
static void tmp_sweep_sessions(gboolean own) {
	GDir *gdir = g_dir_open(root, 0, NULL);
	gchar const *name = NULL;
	gint own_pid = tmp_own_pid();

	if (!gdir)
		return;

	while ((name = g_dir_read_name(gdir))) {
		gint pid = 0;
		gint n = 0;
		gchar *path = NULL;

		if (sscanf(name, "%d-%d", &pid, &n) != 2)
			continue;
		if (own ? pid != own_pid : (pid == own_pid || tmp_pid_alive(pid)))
			continue;

		path = g_build_filename(root, name, NULL);
		tmp_remove_tree(path);
		g_free(path);
	}
	g_dir_close(gdir);

	return;
}

//Vorgemerkte Dateien löschen, soweit nicht mehr geöffnet; force: ohne Wartezeit
static void tmp_sweep_entries(gboolean force) {
	gint64 now = g_get_monotonic_time();

	G_LOCK(tmp);
	for (guint i = entries->len; i > 0; i--) {
		TmpEntry *entry = g_ptr_array_index(entries, i - 1);

		if (!force && now - entry->created < TMP_GRACE_US)
			continue;

		if (g_file_test(entry->path, G_FILE_TEST_EXISTS)
				&& !sond_remove_once(entry->path, NULL))
			continue; //noch geöffnet, später erneut versuchen

		sond_rmdir_once(entry->dir, NULL);
		g_ptr_array_remove_index_fast(entries, i - 1);
	}
	G_UNLOCK(tmp);

	return;
}

static gboolean tmp_sweep_cb(gpointer data) {
	tmp_sweep_entries(FALSE);

	return G_SOURCE_CONTINUE;
}

void sond_tmp_cleanup(void) {
	if (!entries)
		return;

	tmp_sweep_entries(TRUE);
	tmp_sweep_sessions(TRUE);

	return;
}

static void tmp_init(void) {
	static gsize done = 0;

	if (!g_once_init_enter(&done))
		return;

	root = g_build_filename(g_get_user_data_dir(), "zond", "tmp", NULL);
	entries = g_ptr_array_new_with_free_func(tmp_entry_free);

	if (sond_mkdir_with_parents(root, NULL))
		tmp_sweep_sessions(FALSE); //Reste beendeter Läufe

	g_timeout_add_seconds(TMP_SWEEP_SECONDS, tmp_sweep_cb, NULL);
	atexit(sond_tmp_cleanup);

	g_once_init_leave(&done, 1);

	return;
}

//Name auf eine brauchbare Länge kürzen, Endung bleibt erhalten
static gchar* tmp_short_name(gchar const *name) {
	gchar *safe = sond_sanitize_filename(name);
	gchar *dot = strrchr(safe, '.');
	glong len = g_utf8_strlen(safe, -1);
	gchar *stem = NULL;
	gchar *result = NULL;

	if (len <= TMP_NAME_MAX_CHARS)
		return safe;

	if (dot && dot != safe && strlen(dot) <= 10) {
		stem = g_utf8_substring(safe, 0, TMP_NAME_MAX_CHARS - 10);
		result = g_strconcat(stem, dot, NULL);
	} else {
		stem = g_utf8_substring(safe, 0, TMP_NAME_MAX_CHARS);
		result = g_strdup(stem);
	}
	g_free(stem);
	g_free(safe);

	return result;
}

gchar* sond_tmp_new_path(gchar const *name, GError **error) {
	gchar *session = NULL;
	gchar *dir = NULL;
	gchar *safe = NULL;
	gchar *path = NULL;

	tmp_init();

	session = g_strdup_printf("%d-%d", tmp_own_pid(),
			g_atomic_int_add(&counter, 1) + 1);
	dir = g_build_filename(root, session, NULL);
	g_free(session);

	if (!sond_mkdir_with_parents(dir, error)) {
		g_free(dir);

		return NULL;
	}

	safe = tmp_short_name((name && *name) ? name : "datei");
	path = g_build_filename(dir, safe, NULL);
	g_free(safe);
	g_free(dir);

	return path;
}

void sond_tmp_register(gchar const *path) {
	TmpEntry *entry = NULL;

	g_return_if_fail(path != NULL);

	tmp_init();

	entry = g_new0(TmpEntry, 1);
	entry->path = g_strdup(path);
	entry->dir = g_path_get_dirname(path);
	entry->created = g_get_monotonic_time();

	G_LOCK(tmp);
	g_ptr_array_add(entries, entry);
	G_UNLOCK(tmp);

	return;
}

gboolean sond_tmp_remove(gchar const *path, GError **error) {
	GError *error_once = NULL;
	gboolean ok = FALSE;
	gchar *dir = NULL;

	g_return_val_if_fail(path != NULL, FALSE);

	ok = sond_remove_once(path, &error_once);
	if (!ok) {
		g_clear_error(&error_once);
		ok = sond_remove(path, error); //mit Wiederholungen
	}

	dir = g_path_get_dirname(path);
	tmp_init();
	if (g_strcmp0(dir, root) && g_str_has_prefix(dir, root))
		sond_rmdir_once(dir, NULL);
	g_free(dir);

	return ok;
}
