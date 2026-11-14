/*
 sond (sond_zond_ctl.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_zond_ctl.h"

#include <string.h>
#include <glib/gstdio.h>

//Ein Zustand gilt als frisch, wenn er jünger ist (Sekunden)
#define STATE_MAX_AGE 10

static gchar* ctl_dir(void) {
    const gchar *env = g_getenv("SOND_ZOND_CTL_DIR");

    if (env && *env)
        return g_strdup(env);

    return g_build_filename(g_get_user_data_dir(), "sond", "zond_ctl", NULL);
}

//Kennung des Projekts: Pfad vereinheitlicht (Schrägstriche, Kleinschreibung)
static gchar* ctl_id(const gchar *project_path) {
    gchar *norm = g_strdup(project_path);
    gchar *lower = NULL;
    gchar *sum = NULL;
    gchar *id = NULL;

    g_strdelimit(norm, "\\", '/');
    lower = g_utf8_strdown(norm, -1);
    sum = g_compute_checksum_for_string(G_CHECKSUM_SHA256, lower, -1);
    id = g_strndup(sum, 24);

    g_free(norm);
    g_free(lower);
    g_free(sum);

    return id;
}

static gchar* ctl_file(const gchar *project_path, const gchar *suffix) {
    gchar *dir = ctl_dir();
    gchar *id = ctl_id(project_path);
    gchar *name = g_strdup_printf("%s.%s", id, suffix);
    gchar *path = g_build_filename(dir, name, NULL);

    g_free(dir);
    g_free(id);
    g_free(name);

    return path;
}

static gboolean write_file(const gchar *path, const gchar *content,
        GError **error) {
    gchar *dir = g_path_get_dirname(path);
    gboolean ok = FALSE;

    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    ok = g_file_set_contents(path, content, -1, error);

    return ok;
}

gboolean sond_zond_ctl_set_mode(const gchar *project_path, const gchar *mode,
        GError **error) {
    gchar *path = NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail(project_path != NULL && mode != NULL, FALSE);

    path = ctl_file(project_path, "mode");
    ok = write_file(path, mode, error);
    g_free(path);

    return ok;
}

gchar* sond_zond_ctl_get_mode(const gchar *project_path) {
    gchar *path = NULL;
    gchar *content = NULL;

    g_return_val_if_fail(project_path != NULL, NULL);

    path = ctl_file(project_path, "mode");
    if (!g_file_get_contents(path, &content, NULL, NULL)) {
        g_free(path);

        return NULL;
    }
    g_free(path);

    g_strstrip(content);
    if (strcmp(content, SOND_ZOND_MODE_RW) != 0 &&
            strcmp(content, SOND_ZOND_MODE_RO) != 0 &&
            strcmp(content, SOND_ZOND_MODE_LOST) != 0) {
        g_free(content);

        return NULL;
    }

    return content;
}

void sond_zond_ctl_write_state(const gchar *project_path, const gchar *mode,
        gboolean changed) {
    gchar *path = NULL;
    gchar *content = NULL;

    g_return_if_fail(project_path != NULL && mode != NULL);

    path = ctl_file(project_path, "state");
    content = g_strdup_printf("mode=%s\nchanged=%d\nt=%" G_GINT64_FORMAT "\n",
            mode, changed ? 1 : 0, g_get_real_time() / G_USEC_PER_SEC);
    write_file(path, content, NULL);

    g_free(content);
    g_free(path);
}

void sond_zond_ctl_clear_state(const gchar *project_path) {
    gchar *path = NULL;

    g_return_if_fail(project_path != NULL);

    path = ctl_file(project_path, "state");
    g_remove(path);
    g_free(path);
}

void sond_zond_ctl_state_clear(SondZondCtlState *state) {
    if (!state)
        return;

    g_free(state->mode);
    memset(state, 0, sizeof(SondZondCtlState));
}

void sond_zond_ctl_read_state(const gchar *project_path, SondZondCtlState *state) {
    gchar *path = NULL;
    gchar *content = NULL;
    gchar **lines = NULL;
    gint64 t = 0;

    g_return_if_fail(project_path != NULL && state != NULL);

    memset(state, 0, sizeof(SondZondCtlState));

    path = ctl_file(project_path, "state");
    if (!g_file_get_contents(path, &content, NULL, NULL)) {
        g_free(path);

        return;
    }
    g_free(path);

    lines = g_strsplit(content, "\n", -1);
    for (gchar **l = lines; *l; l++) {
        if (g_str_has_prefix(*l, "mode="))
            state->mode = g_strdup(*l + 5);
        else if (g_str_has_prefix(*l, "changed="))
            state->changed = (g_ascii_strtoll(*l + 8, NULL, 10) != 0);
        else if (g_str_has_prefix(*l, "t="))
            t = g_ascii_strtoll(*l + 2, NULL, 10);
    }
    g_strfreev(lines);
    g_free(content);

    state->running = (t > 0 && g_get_real_time() / G_USEC_PER_SEC - t <=
            STATE_MAX_AGE && state->mode != NULL);
}
