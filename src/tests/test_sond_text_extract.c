/*
 sond (test_sond_text_extract.c) - Akten, Beweisstücke, Unterlagen
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
 * Fuenfter Baustein der Test-Infrastruktur (GLib Testing). Testet
 * sond_text_extract_plain()/sond_text_extract_html() - deterministisch
 * (gegebene Bytes -> gegebener Text), aber mit echten, "haarigen" Faellen:
 * CRLF-Normalisierung, BOM-Erkennung (UTF-8/UTF-16), windows-1252-Fallback,
 * <meta charset>-Erkennung, Script/Style-Ausschluss, Block-Element-Umbrueche.
 */

#include <glib.h>
#include <string.h>
#include "../sond_text_extract.h"

static void
test_plain_empty_input(void)
{
    GPtrArray *segs = sond_text_extract_plain(NULL, 0);
    g_assert_nonnull(segs);
    g_assert_cmpuint(segs->len, ==, 0);
    g_ptr_array_unref(segs);

    segs = sond_text_extract_plain((guchar const *) "egal", 0);
    g_assert_cmpuint(segs->len, ==, 0);
    g_ptr_array_unref(segs);
}

static void
test_plain_crlf_normalized(void)
{
    const gchar *input = "line1\r\nline2\r\nline3";
    GPtrArray *segs = sond_text_extract_plain((guchar const *) input, strlen(input));

    g_assert_cmpuint(segs->len, ==, 1);
    SondTextSegment *seg = g_ptr_array_index(segs, 0);
    g_assert_cmpstr(seg->text, ==, "line1\nline2\nline3");
    g_assert_cmpint(seg->page_nr, ==, -1);
    g_assert_cmpint(seg->char_pos, ==, 0);

    g_ptr_array_unref(segs);
}

static void
test_plain_bare_lf_untouched(void)
{
    const gchar *input = "a\nb\nc";
    GPtrArray *segs = sond_text_extract_plain((guchar const *) input, strlen(input));

    SondTextSegment *seg = g_ptr_array_index(segs, 0);
    g_assert_cmpstr(seg->text, ==, "a\nb\nc");

    g_ptr_array_unref(segs);
}

static void
test_plain_utf8_bom_stripped(void)
{
    const guchar input[] = { 0xEF, 0xBB, 0xBF, 'h', 'i' };
    GPtrArray *segs = sond_text_extract_plain(input, sizeof(input));

    SondTextSegment *seg = g_ptr_array_index(segs, 0);
    g_assert_cmpstr(seg->text, ==, "hi");

    g_ptr_array_unref(segs);
}

static void
test_plain_utf16le_bom_converted(void)
{
    /* UTF-16LE BOM (FF FE) + "hi" als UTF-16LE Codeunits. */
    const guchar input[] = { 0xFF, 0xFE, 'h', 0x00, 'i', 0x00 };
    GPtrArray *segs = sond_text_extract_plain(input, sizeof(input));

    SondTextSegment *seg = g_ptr_array_index(segs, 0);
    g_assert_cmpstr(seg->text, ==, "hi");

    g_ptr_array_unref(segs);
}

static void
test_plain_windows1252_fallback(void)
{
    /* 0xE9 ist in windows-1252 'é', als einzelnes Byte aber kein gueltiges
     * UTF-8 -> muss ueber den windows-1252-Fallback zu UTF-8 "é" (C3 A9)
     * konvertiert werden. */
    const guchar input[] = { 'C', 'a', 'f', 0xE9 };
    GPtrArray *segs = sond_text_extract_plain(input, sizeof(input));

    g_assert_cmpuint(segs->len, ==, 1);
    SondTextSegment *seg = g_ptr_array_index(segs, 0);
    g_assert_cmpstr(seg->text, ==, "Caf\xC3\xA9");

    g_ptr_array_unref(segs);
}

static void
test_html_empty_input(void)
{
    GPtrArray *segs = sond_text_extract_html(NULL, 0);
    g_assert_cmpuint(segs->len, ==, 0);
    g_ptr_array_unref(segs);
}

static void
test_html_basic_extraction(void)
{
    const gchar *html = "<html><body>Hello <b>World</b></body></html>";
    GPtrArray *segs = sond_text_extract_html((guchar const *) html, strlen(html));

    g_assert_cmpuint(segs->len, ==, 1);
    SondTextSegment *seg = g_ptr_array_index(segs, 0);
    g_assert_cmpstr(seg->text, ==, "Hello World");

    g_ptr_array_unref(segs);
}

static void
test_html_script_and_style_excluded(void)
{
    const gchar *html =
        "<html><head><style>body{color:red}</style></head>"
        "<body><script>var x = 1;</script>Visible</body></html>";
    GPtrArray *segs = sond_text_extract_html((guchar const *) html, strlen(html));

    SondTextSegment *seg = g_ptr_array_index(segs, 0);
    g_assert_cmpstr(seg->text, ==, "Visible");

    g_ptr_array_unref(segs);
}

static void
test_html_paragraph_breaks(void)
{
    const gchar *html = "<html><body><p>First</p><p>Second</p></body></html>";
    GPtrArray *segs = sond_text_extract_html((guchar const *) html, strlen(html));

    SondTextSegment *seg = g_ptr_array_index(segs, 0);
    g_assert_cmpstr(seg->text, ==, "First\nSecond\n");

    g_ptr_array_unref(segs);
}

static void
test_html_br_becomes_newline(void)
{
    const gchar *html = "<html><body><p>Line1<br>Line2</p></body></html>";
    GPtrArray *segs = sond_text_extract_html((guchar const *) html, strlen(html));

    SondTextSegment *seg = g_ptr_array_index(segs, 0);
    g_assert_cmpstr(seg->text, ==, "Line1\nLine2\n");

    g_ptr_array_unref(segs);
}

static void
test_html_meta_charset_windows1252(void)
{
    /* 0xE9 ('é' in windows-1252) direkt im Body, Charset per <meta>
     * deklariert statt per UTF-8-Validitaet erkannt. */
    const guchar html[] =
        "<html><head><meta charset=\"windows-1252\"></head>"
        "<body>Caf\xE9</body></html>";
    GPtrArray *segs = sond_text_extract_html(html, sizeof(html) - 1 /* kein NUL mitzaehlen */);

    g_assert_cmpuint(segs->len, ==, 1);
    SondTextSegment *seg = g_ptr_array_index(segs, 0);
    g_assert_cmpstr(seg->text, ==, "Caf\xC3\xA9");

    g_ptr_array_unref(segs);
}

void
test_sond_text_extract_register(void)
{
    g_test_add_func("/sond_text_extract/plain_empty_input", test_plain_empty_input);
    g_test_add_func("/sond_text_extract/plain_crlf_normalized", test_plain_crlf_normalized);
    g_test_add_func("/sond_text_extract/plain_bare_lf_untouched", test_plain_bare_lf_untouched);
    g_test_add_func("/sond_text_extract/plain_utf8_bom_stripped", test_plain_utf8_bom_stripped);
    g_test_add_func("/sond_text_extract/plain_utf16le_bom_converted", test_plain_utf16le_bom_converted);
    g_test_add_func("/sond_text_extract/plain_windows1252_fallback", test_plain_windows1252_fallback);
    g_test_add_func("/sond_text_extract/html_empty_input", test_html_empty_input);
    g_test_add_func("/sond_text_extract/html_basic_extraction", test_html_basic_extraction);
    g_test_add_func("/sond_text_extract/html_script_and_style_excluded", test_html_script_and_style_excluded);
    g_test_add_func("/sond_text_extract/html_paragraph_breaks", test_html_paragraph_breaks);
    g_test_add_func("/sond_text_extract/html_br_becomes_newline", test_html_br_becomes_newline);
    g_test_add_func("/sond_text_extract/html_meta_charset_windows1252", test_html_meta_charset_windows1252);
}
