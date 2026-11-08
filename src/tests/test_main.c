/*
 sond (test_main.c) - Akten, Beweisstücke, Unterlagen
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
 * Zentrales main() fuer alle GLib-Testing-Unit-Tests (Makefile-Target
 * "test"). Jede test_*.c-Datei bringt nur noch eine *_register()-Funktion
 * mit (kein eigenes main() mehr) - hier eintragen, wenn eine neue
 * Test-Datei hinzukommt.
 */

#include <glib.h>

void test_sond_graph_property_register(void);
void test_sond_graph_node_register(void);
void test_sond_graph_db_filter_register(void);
void test_sond_mime_register(void);
void test_sond_text_extract_register(void);

int
main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    test_sond_graph_property_register();
    test_sond_graph_node_register();
    test_sond_graph_db_filter_register();
    test_sond_mime_register();
    test_sond_text_extract_register();

    return g_test_run();
}
