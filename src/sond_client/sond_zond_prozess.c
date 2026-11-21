/*
 sond (sond_zond_prozess.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_zond_prozess.h"

#include <errno.h>
#include <glib/gstdio.h>

#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <signal.h>
#endif

//Takt, in dem das Ende eines Prozesses bemerkt wird (ms)
#define TAKT_MS 1500

typedef struct {
	gchar *lib_name;
	guint pid;
	gint64 start;  //Startzeit des Prozesses (Windows: FILETIME), 0 = unbekannt
#ifdef G_OS_WIN32
	HANDLE handle;
#endif
} Prozess;

static GSList *prozesse = NULL;
static guint takt_source = 0;
static SondZondProzessFunc geaendert_func = NULL;
static gpointer geaendert_data = NULL;
static gboolean wiedergefunden = FALSE;

static void prozess_free(Prozess *p) {
#ifdef G_OS_WIN32
	if (p->handle)
		CloseHandle(p->handle);
#endif
	g_free(p->lib_name);
	g_free(p);
}

static gboolean prozess_lebt(Prozess *p) {
#ifdef G_OS_WIN32
	return p->handle && WaitForSingleObject(p->handle, 0) == WAIT_TIMEOUT;
#else
	return kill((pid_t) p->pid, 0) == 0 || errno == EPERM;
#endif
}

#ifdef G_OS_WIN32
static gint64 handle_startzeit(HANDLE handle) {
	FILETIME creation, exit_time, kernel, user;

	if (!GetProcessTimes(handle, &creation, &exit_time, &kernel, &user))
		return 0;

	return (gint64) (((guint64) creation.dwHighDateTime << 32) | creation.dwLowDateTime);
}
#endif

//Datei mit den Prozessen (neben dem Zustand der Schreibberechtigung)
static gchar* state_path(void) {
	const gchar *env = g_getenv("SOND_CLIENT_STATE");

	if (env && *env)
		return g_strconcat(env, ".zond", NULL);

	return g_build_filename(g_get_user_data_dir(), "sond", "client_zond.ini", NULL);
}

static void speichern(void) {
	GKeyFile *kf = g_key_file_new();
	g_autofree gchar *path = state_path();
	g_autofree gchar *dir = g_path_get_dirname(path);
	GError *error = NULL;

	for (GSList *l = prozesse; l; l = l->next) {
		Prozess *p = l->data;

		g_key_file_set_integer(kf, p->lib_name, "pid", (gint) p->pid);
		g_key_file_set_int64(kf, p->lib_name, "start", p->start);
	}

	g_mkdir_with_parents(dir, 0700);
	if (!g_key_file_save_to_file(kf, path, &error)) {
		g_warning("zond-Prozesse nicht gespeichert (%s): %s", path, error->message);
		g_clear_error(&error);
	}
	g_key_file_unref(kf);
}

static void melden(void) {
	if (geaendert_func)
		geaendert_func(geaendert_data);
}

static gboolean takt(gpointer data) {
	gboolean geaendert = FALSE;

	(void) data;

	for (GSList *l = prozesse; l;) {
		Prozess *p = l->data;
		GSList *next = l->next;

		if (!prozess_lebt(p)) {
			prozesse = g_slist_delete_link(prozesse, l);
			prozess_free(p);
			geaendert = TRUE;
		}
		l = next;
	}

	if (geaendert) {
		speichern();
		melden();
	}

	if (!prozesse) {
		takt_source = 0;

		return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}

static void takt_starten(void) {
	if (!takt_source && prozesse)
		takt_source = g_timeout_add(TAKT_MS, takt, NULL);
}

void sond_zond_prozess_merken(const gchar *lib_name, GPid pid) {
	Prozess *p = NULL;

	g_return_if_fail(lib_name != NULL);

	p = g_new0(Prozess, 1);
	p->lib_name = g_strdup(lib_name);
#ifdef G_OS_WIN32
	//eigene Kopie: der Aufrufer schließt sein Handle selbst
	if (!DuplicateHandle(GetCurrentProcess(), (HANDLE) pid, GetCurrentProcess(), &p->handle, 0,
			FALSE, DUPLICATE_SAME_ACCESS)) {
		g_warning("zond-Prozess nicht zu verfolgen (DuplicateHandle)");
		g_free(p->lib_name);
		g_free(p);

		return;
	}
	p->pid = GetProcessId(p->handle);
	p->start = handle_startzeit(p->handle);
#else
	p->pid = (guint) pid;
#endif

	prozesse = g_slist_prepend(prozesse, p);
	speichern();
	takt_starten();
	melden();
}

gboolean sond_zond_prozess_laeuft(const gchar *lib_name) {
	g_return_val_if_fail(lib_name != NULL, FALSE);

	for (GSList *l = prozesse; l; l = l->next) {
		Prozess *p = l->data;

		if (!g_strcmp0(p->lib_name, lib_name) && prozess_lebt(p))
			return TRUE;
	}

	return FALSE;
}

void sond_zond_prozess_wiederfinden(void) {
	GKeyFile *kf = NULL;
	g_autofree gchar *path = NULL;
	gchar **gruppen = NULL;
	gboolean gefunden = FALSE;

	if (wiedergefunden)
		return;
	wiedergefunden = TRUE;

	kf = g_key_file_new();
	path = state_path();
	if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
		g_key_file_unref(kf);

		return;
	}

	gruppen = g_key_file_get_groups(kf, NULL);
	for (gchar **g = gruppen; *g; g++) {
		guint pid = (guint) g_key_file_get_integer(kf, *g, "pid", NULL);
		gint64 start = g_key_file_get_int64(kf, *g, "start", NULL);
		Prozess *p = NULL;

		if (!pid)
			continue;

		p = g_new0(Prozess, 1);
		p->lib_name = g_strdup(*g);
		p->pid = pid;
		p->start = start;
#ifdef G_OS_WIN32
		p->handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
		//gleiche Prozess-ID, aber anderer Prozess (die ID wurde neu vergeben)?
		if (!p->handle || (start && handle_startzeit(p->handle) != start) || !prozess_lebt(p)) {
			prozess_free(p);

			continue;
		}
#else
		if (!prozess_lebt(p)) {
			prozess_free(p);

			continue;
		}
#endif
		prozesse = g_slist_prepend(prozesse, p);
		gefunden = TRUE;
	}
	g_strfreev(gruppen);
	g_key_file_unref(kf);

	//die Datei auf die noch laufenden Prozesse beschränken
	speichern();
	takt_starten();
	if (gefunden)
		melden();
}

void sond_zond_prozess_set_func(SondZondProzessFunc func, gpointer data) {
	geaendert_func = func;
	geaendert_data = data;
}
