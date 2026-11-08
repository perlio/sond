/*
 sond (test_sond_mime.c) - Akten, Beweisstücke, Unterlagen
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
 * Vierter Baustein der Test-Infrastruktur (GLib Testing). Testet die
 * reinen Tabellen-Lookup-Funktionen aus sond_mime.c (Extension <-> MIME-
 * Type). mime_guess_content_type()/_init() (echtes Content-Sniffing via
 * libmagic) bewusst NICHT getestet - das braucht eine initialisierte
 * magic_t-Datenbank und ist kein reiner, isolierter Unit-Test-Fall.
 */

#include <glib.h>
#include "../sond_mime.h"

static void
test_mime_from_extension_known(void)
{
    g_assert_cmpstr(mime_from_extension("dokument.pdf"), ==, "application/pdf");
    g_assert_cmpstr(mime_from_extension("archiv.zip"), ==, "application/zip");
    g_assert_cmpstr(mime_from_extension("mail.eml"), ==, "message/rfc822");
}

static void
test_mime_from_extension_case_insensitive(void)
{
    g_assert_cmpstr(mime_from_extension("DOKUMENT.PDF"), ==, "application/pdf");
    g_assert_cmpstr(mime_from_extension("Bild.JpG"), ==, "image/jpeg");
}

static void
test_mime_from_extension_edge_cases(void)
{
    g_assert_null(mime_from_extension(NULL));
    g_assert_null(mime_from_extension("ohne_punkt"));
    /* Punkt ist das erste Zeichen -> keine "echte" Extension. */
    g_assert_null(mime_from_extension(".hidden"));
    /* Unbekannte Extension -> NULL (anders als mime_to_extension, das auf
     * .bin zurueckfaellt - mime_from_extension hat keinen Fallback). */
    g_assert_null(mime_from_extension("datei.xyz123"));
}

static void
test_mime_to_extension_known(void)
{
    g_assert_cmpstr(mime_to_extension("application/pdf"), ==, ".pdf");
    g_assert_cmpstr(mime_to_extension("image/jpeg"), ==, ".jpg");
}

static void
test_mime_to_extension_unknown_falls_back_to_bin(void)
{
    g_assert_cmpstr(mime_to_extension("application/x-does-not-exist"), ==, ".bin");
}

static void
test_mime_to_extension_null(void)
{
    g_assert_null(mime_to_extension(NULL));
}

static void
test_mime_to_extension_ci_case_insensitive(void)
{
    g_assert_cmpstr(mime_to_extension_ci("APPLICATION/PDF"), ==, ".pdf");
    g_assert_cmpstr(mime_to_extension_ci("Image/Jpeg"), ==, ".jpg");
    g_assert_cmpstr(mime_to_extension_ci("application/x-does-not-exist"), ==, ".bin");
}

static void
test_mime_to_extension_with_params(void)
{
    g_assert_cmpstr(mime_to_extension_with_params("text/html; charset=utf-8"), ==, ".html");
    /* Trailing Whitespace vor dem Semikolon-Teil muss toleriert werden. */
    g_assert_cmpstr(mime_to_extension_with_params("text/plain ; charset=utf-8"), ==, ".txt");
    /* Ohne Semikolon: Fallback auf mime_to_extension_ci(). */
    g_assert_cmpstr(mime_to_extension_with_params("image/png"), ==, ".png");
    g_assert_null(mime_to_extension_with_params(NULL));
}

void
test_sond_mime_register(void)
{
    g_test_add_func("/sond_mime/from_extension_known", test_mime_from_extension_known);
    g_test_add_func("/sond_mime/from_extension_case_insensitive", test_mime_from_extension_case_insensitive);
    g_test_add_func("/sond_mime/from_extension_edge_cases", test_mime_from_extension_edge_cases);
    g_test_add_func("/sond_mime/to_extension_known", test_mime_to_extension_known);
    g_test_add_func("/sond_mime/to_extension_unknown_falls_back_to_bin", test_mime_to_extension_unknown_falls_back_to_bin);
    g_test_add_func("/sond_mime/to_extension_null", test_mime_to_extension_null);
    g_test_add_func("/sond_mime/to_extension_ci_case_insensitive", test_mime_to_extension_ci_case_insensitive);
    g_test_add_func("/sond_mime/to_extension_with_params", test_mime_to_extension_with_params);
}
