/*
 sond (sond_akte_name.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_akte_name.h"

#include <string.h>

static gboolean needs_escape(guchar c) {
    return c < 0x20 || c == 0x7f || c == '%' || c == '/' || c == '\\' ||
            c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|' || c == '~';
}

gchar* sond_akte_name_escape(const gchar *wert) {
    gchar *trimmed = NULL;
    GString *out = NULL;
    const gchar *p = NULL;

    if (!wert)
        return NULL;

    trimmed = g_strstrip(g_strdup(wert));
    if (!*trimmed || !g_utf8_validate(trimmed, -1, NULL)) {
        g_free(trimmed);

        return NULL;
    }

    out = g_string_new(NULL);
    for (p = trimmed; *p; p = g_utf8_next_char(p)) {
        gsize len = (gsize) (g_utf8_next_char(p) - p);
        GString *unit = g_string_new(NULL);

        if (len == 1 && needs_escape((guchar) *p))
            g_string_append_printf(unit, "%%%02X", (guchar) *p);
        else
            g_string_append_len(unit, p, (gssize) len);

        //zu lang: an der Zeichengrenze abbrechen
        if (out->len + unit->len > SOND_AKTE_NAME_MAX) {
            g_string_free(unit, TRUE);
            break;
        }
        g_string_append_len(out, unit->str, (gssize) unit->len);
        g_string_free(unit, TRUE);
    }
    g_free(trimmed);

    //Punkt oder Leerzeichen am Ende sind unter Windows nicht erlaubt
    if (out->len > 0 && (out->str[out->len - 1] == '.' ||
            out->str[out->len - 1] == ' ')) {
        gchar last = out->str[out->len - 1];

        g_string_truncate(out, out->len - 1);
        g_string_append_printf(out, "%%%02X", (guchar) last);
    }

    if (out->len == 0) {
        g_string_free(out, TRUE);

        return NULL;
    }

    return g_string_free(out, FALSE);
}

static gint hex_value(gchar c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    return -1;
}

gchar* sond_akte_name_unescape(const gchar *dateiname) {
    GString *out = NULL;

    g_return_val_if_fail(dateiname != NULL, NULL);

    out = g_string_new(NULL);
    for (const gchar *p = dateiname; *p; p++) {
        if (*p == '%' && hex_value(p[1]) >= 0 && hex_value(p[2]) >= 0) {
            g_string_append_c(out, (gchar) (hex_value(p[1]) * 16 + hex_value(p[2])));
            p += 2;
        } else
            g_string_append_c(out, *p);
    }

    return g_string_free(out, FALSE);
}

gchar* sond_akte_name_make(gchar art, const gchar *wert) {
    gchar *kodiert = sond_akte_name_escape(wert);
    gchar *name = NULL;

    if (!kodiert)
        return NULL;

    name = g_strdup_printf("%c~%s", art, kodiert);
    g_free(kodiert);

    return name;
}

gchar sond_akte_name_parse(const gchar *dateiname, gchar **wert,
        gboolean *neues_format) {
    gchar art = SOND_AKTE_NAME_BEZEICHNUNG;
    const gchar *rest = dateiname;
    gboolean neu = FALSE;

    g_return_val_if_fail(dateiname != NULL, art);

    if ((dateiname[0] == SOND_AKTE_NAME_BEZEICHNUNG ||
            dateiname[0] == SOND_AKTE_NAME_GEGENSTAND) && dateiname[1] == '~') {
        art = dateiname[0];
        rest = dateiname + 2;
        neu = TRUE;
    }

    if (wert)
        *wert = sond_akte_name_unescape(rest);
    if (neues_format)
        *neues_format = neu;

    return art;
}
