/*
 zond (zond_installer.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2023  pelo america

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
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib.h>

#include "../sond_file_helper.h"
#include "../sond_log_and_error.h"

#ifdef _WIN32
#include <windows.h>
#elif defined __linux__
#include <limits.h>
#endif // _WIN32

#ifndef MAX_PATH
#define MAX_PATH 4096
#endif

#include "../misc_stdlib.h"

// Wartet, bis der Prozess mit der angegebenen PID beendet ist.
// Gibt TRUE zurück, wenn der Prozess beendet wurde, FALSE bei Timeout.
static gboolean wait_for_process_exit(const char *pid_str, int timeout_seconds) {
#ifdef _WIN32
	DWORD pid = (DWORD) strtoul(pid_str, NULL, 10);
	HANDLE h_process = NULL;
	DWORD wait_result = 0;

	h_process = OpenProcess(SYNCHRONIZE, FALSE, pid);
	if (!h_process) {
		// Prozess existiert vermutlich schon nicht mehr - das ist okay
		return TRUE;
	}

	wait_result = WaitForSingleObject(h_process, timeout_seconds * 1000);
	CloseHandle(h_process);

	return (wait_result == WAIT_OBJECT_0);

#elif defined __linux__
	pid_t pid = (pid_t) strtoul(pid_str, NULL, 10);
	int elapsed = 0;

	while (elapsed < timeout_seconds) {
		if (kill(pid, 0) != 0 && errno == ESRCH)
			return TRUE; // Prozess existiert nicht mehr

		sleep(1);
		elapsed++;
	}

	return FALSE; // Timeout

#endif
}

// Loescht alle Top-Level-Eintraege in base_dir, ausser denen in skip_names
// (NULL-terminierte Liste von Namen). Sammelt Fehler, gibt Anzahl
// fehlgeschlagener Eintraege zurueck (0 = alles ok).
static int clear_base_dir_except(const char *base_dir, const char **skip_names) {
	GError *error = NULL;
	SondDir *dir = NULL;
	const gchar *name = NULL;
	int fail_count = 0;

	dir = sond_dir_open(base_dir, &error);
	if (!dir) {
		LOG_ERROR("Konnte %s nicht oeffnen: %s", base_dir, error->message);
		g_clear_error(&error);
		return 1;
	}

	while ((name = sond_dir_read_name(dir)) != NULL) {
		gboolean skip = FALSE;
		gchar *full_path = NULL;
		GStatBuf st;

		for (int i = 0; skip_names[i] != NULL; i++) {
			if (g_strcmp0(name, skip_names[i]) == 0) {
				skip = TRUE;
				break;
			}
		}
		if (skip)
			continue;

		full_path = g_build_filename(base_dir, name, NULL);

		if (sond_stat(full_path, &st, &error) != 0) {
			LOG_WARN("Konnte %s nicht stat'en: %s", full_path, error->message);
			g_clear_error(&error);
			fail_count++;
			g_free(full_path);
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			if (!sond_rmdir_r(full_path, &error)) {
				LOG_WARN("Konnte Verzeichnis %s nicht loeschen: %s",
						full_path, error->message);
				g_clear_error(&error);
				fail_count++;
			}
		} else {
			if (!sond_remove(full_path, &error)) {
				LOG_WARN("Konnte Datei %s nicht loeschen: %s",
						full_path, error->message);
				g_clear_error(&error);
				fail_count++;
			}
		}

		g_free(full_path);
	}

	sond_dir_close(dir);

	return fail_count;
}

// Verschiebt alle Top-Level-Eintraege aus vtag_dir nach base_dir,
// ausser self_exe_name (die eigene laufende .exe, die direkt in
// vtag_dir liegt und nicht verschoben werden kann).
// Sammelt Fehler, gibt Anzahl fehlgeschlagener Eintraege zurueck.
static int move_vtag_contents(const char *vtag_dir, const char *base_dir,
		const char *self_exe_name) {
	GError *error = NULL;
	SondDir *dir = NULL;
	const gchar *name = NULL;
	int fail_count = 0;

	dir = sond_dir_open(vtag_dir, &error);
	if (!dir) {
		LOG_ERROR("Konnte %s nicht oeffnen: %s", vtag_dir, error->message);
		g_clear_error(&error);
		return 1;
	}

	while ((name = sond_dir_read_name(dir)) != NULL) {
		gchar *src_path = NULL;
		gchar *dst_path = NULL;

		if (g_strcmp0(name, self_exe_name) == 0)
			continue; // eigene laufende exe - bleibt liegen

		src_path = g_build_filename(vtag_dir, name, NULL);
		dst_path = g_build_filename(base_dir, name, NULL);

		if (!sond_rename(src_path, dst_path, &error)) {
			LOG_WARN("Konnte %s nicht nach %s verschieben: %s",
					src_path, dst_path, error->message);
			g_clear_error(&error);
			fail_count++;
		}

		g_free(src_path);
		g_free(dst_path);
	}

	sond_dir_close(dir);

	return fail_count;
}

int main(int argc, char **argv) {
	int rc = 0;
	char *vtag_dir = NULL;
	char vtag_dir_tmp[MAX_PATH] = { 0 };
	char base_dir[PATH_MAX] = { 0 };
	char *vtag = NULL;
	GError *error_rem = NULL;
	int fail_count = 0;
	const char *skip_names[2] = { NULL, NULL };

	logging_init("zond_installer");
	install_crash_handler();

	//warten, bis altes zond beendet ist
	if (argv[2]) { //argv[2] ist die PID der alten zond.exe
		if (!wait_for_process_exit(argv[2], 30))
			LOG_WARN("Altes zond (PID %s) wurde nicht innerhalb von 30 Sekunden beendet - "
					"fahre trotzdem fort", argv[2]);
	}

	vtag_dir = get_base_dir(); // mit / am Ende
	if (!vtag_dir) {
		LOG_ERROR("Konnte base-dir nicht ermitteln");
		goto end;
	}

	strncpy(vtag_dir_tmp, vtag_dir, strlen(vtag_dir) - 1); // letztes Zeichen (/) abschneiden
	free(vtag_dir);

	vtag = strrchr(vtag_dir_tmp, '\\') + 1;

	strncpy(base_dir, vtag_dir_tmp, strlen(vtag_dir_tmp) - strlen(vtag));

	rc = chdir(base_dir);
	if (rc) {
		LOG_ERROR("Konnte Arbeitsverzeichnis nicht festlegen - %s",
				strerror( errno));
		goto end;
	}

	//prüfen, ob gschemas.compiled ersetzt werden muß
	if (argv[1]) //argv[1] ist die Version des Programms, das upgedated werden soll
	{ //schema wurde zuletzt geändert nach Version v0.11.2, d.h. mit v0.11.3
	  //d.h.: wenn Version < 0.11.3, dann muß geändert werden

		char *major = NULL;
		char *minor = NULL;
		char *patch = NULL;
		char tmp[PATH_MAX] = { 0 };
		char schema[PATH_MAX] = { 0 };

		major = argv[1] + 1; //"x.y.z", reicht für atoi!

		patch = strrchr(argv[1], '.') + 1;

		strncpy(tmp, argv[1], strlen(argv[1]) - strlen(patch) - 1);
		minor = strrchr(tmp, '.') + 1;

		if (atoi(major) < 0)
			goto del_schemas;
		else if (atoi(major) > 0)
			goto skip_schema;
		else {
			if (atoi(minor) < 11)
				goto del_schemas;
			else if (atoi(minor) > 11)
				goto skip_schema;
			else {
				if (atoi(patch) < 3)
					goto del_schemas;
				else
					goto skip_schema;
			}
		}

		del_schemas: strcpy(schema, vtag);
		strcat(schema, "/share/glib-2.0/schemas/gschemas.compiled");
		if (!sond_remove(schema, &error_rem)) {
			LOG_WARN("gschema.compiled konnte nicht entfernt werden: %s",
					error_rem->message);
			g_clear_error(&error_rem);
		}

	}

	skip_schema:

	//alte Installation wegraeumen (alles ausser dem aktuellen vtag-Verzeichnis)
	skip_names[0] = vtag;
	fail_count = clear_base_dir_except(base_dir, skip_names);
	if (fail_count)
		LOG_WARN("%d Eintraege konnten beim Aufraeumen von %s nicht "
				"geloescht werden", fail_count, base_dir);

	//neue Version an ihren Platz verschieben (ausser der eigenen, laufenden exe)
	fail_count = move_vtag_contents(vtag_dir_tmp, base_dir, "zond_installer.exe");
	if (fail_count)
		LOG_WARN("%d Eintraege konnten von %s nach %s nicht verschoben "
				"werden", fail_count, vtag_dir_tmp, base_dir);

	//zond neu starten
	{
		gchar *zond_exe = NULL;
		gchar *spawn_argv[3] = { NULL, NULL, NULL };
		GError *error_spawn = NULL;
		gboolean res = FALSE;

		zond_exe = g_build_filename(base_dir, "bin", "zond.exe", NULL);
		spawn_argv[0] = zond_exe;
		if (argv[3]) //argv[3] ist der Pfad der vorher offenen Projekt-Datenbank
			spawn_argv[1] = argv[3];

		res = g_spawn_async(NULL, spawn_argv, NULL, G_SPAWN_DEFAULT,
				NULL, NULL, NULL, &error_spawn);
		if (!res) {
			LOG_ERROR("Konnte %s nicht starten: %s", zond_exe,
					error_spawn->message);
			g_clear_error(&error_spawn);
		}

		g_free(zond_exe);
	}

end:
	LOG_INFO("zond_installer beendet");
	logging_cleanup();

	return 0;
}
