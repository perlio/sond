/*
 sond (sond_zond_start.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_zond_start.h"

#include <string.h>

#ifdef G_OS_WIN32
#include <windows.h>
#define ZOND_PROGRAM "zond.exe"
#else
#define ZOND_PROGRAM "zond"
#endif

gchar* sond_zond_project_file(const gchar *library_dir, const gchar *library_name) {
    gchar *safe = g_strdup(library_name);
    gchar *file = NULL;
    gchar *path = NULL;

    //Pfadtrenner im Namen, wie beim Anlegen der Projektdatei durch den Server
    g_strdelimit(safe, "/\\", '_');
    file = g_strdup_printf(".%s.ZND", safe);
    path = g_build_filename(library_dir, file, NULL);
    g_free(file);
    g_free(safe);

    return path;
}

/* Verzeichnis des laufenden Programms oder NULL */
static gchar* own_directory(void) {
#ifdef G_OS_WIN32
    wchar_t buf[MAX_PATH * 2];
    DWORD len = GetModuleFileNameW(NULL, buf, G_N_ELEMENTS(buf));
    gchar *utf8 = NULL;
    gchar *dir = NULL;

    if (len == 0 || len >= G_N_ELEMENTS(buf))
        return NULL;
    utf8 = g_utf16_to_utf8((const gunichar2*) buf, (glong) len, NULL, NULL, NULL);
    if (!utf8)
        return NULL;
    dir = g_path_get_dirname(utf8);
    g_free(utf8);

    return dir;
#else
    return NULL;
#endif
}

gchar* sond_zond_find_exe(const gchar *configured, GError **error) {
    gchar *dir = NULL;
    gchar *cand = NULL;

    if (configured && *configured) {
        if (g_file_test(configured, G_FILE_TEST_IS_REGULAR))
            return g_strdup(configured);

        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                "zond nicht gefunden: '%s' (Einstellung [zond] exe)",
                configured);

        return NULL;
    }

    dir = own_directory();
    if (dir) {
        cand = g_build_filename(dir, ZOND_PROGRAM, NULL);
        g_free(dir);
        if (g_file_test(cand, G_FILE_TEST_IS_REGULAR))
            return cand;
        g_free(cand);
    }

    cand = g_find_program_in_path(ZOND_PROGRAM);
    if (cand)
        return cand;

    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
            "zond wurde nicht gefunden. Bitte in der Konfiguration des "
            "Clients im Abschnitt [zond] den Schlüssel exe auf den Pfad zu "
            "%s setzen.", ZOND_PROGRAM);

    return NULL;
}

gboolean sond_zond_start_pid(const gchar *exe, const gchar *project_file,
        GPid *pid, GError **error) {
    gchar *work_dir = NULL;
    gchar *argv[3] = { NULL, NULL, NULL };
    gboolean ok = FALSE;

    g_return_val_if_fail(exe != NULL && project_file != NULL, FALSE);

    //Offline liegt die Datei nur vor, wenn SeaDrive sie schon geladen hatte
    if (!g_file_test(project_file, G_FILE_TEST_EXISTS)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                "Die Projektdatei '%s' ist nicht erreichbar. Ist SeaDrive "
                "verbunden, und wurde die Akte schon einmal geöffnet?",
                project_file);

        return FALSE;
    }

    work_dir = g_path_get_dirname(exe);
    argv[0] = (gchar*) exe;
    argv[1] = (gchar*) project_file;

    ok = g_spawn_async(work_dir, argv, NULL,
            pid ? G_SPAWN_DO_NOT_REAP_CHILD : G_SPAWN_DEFAULT, NULL, NULL, pid,
            error);
    g_free(work_dir);

    return ok;
}

gboolean sond_zond_start(const gchar *exe, const gchar *project_file,
        GError **error) {
    return sond_zond_start_pid(exe, project_file, NULL, error);
}
