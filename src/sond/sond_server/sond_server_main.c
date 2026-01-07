/*
 sond (sond_server_main.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026 pelo america

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

/**
 * @file server_main.c
 * @brief Hauptprogramm für den Graph-Datenbank Server
 */

#include "sond_server.h"
#include "sond_graph_db.h"
#include <signal.h>
#include <stdlib.h>

/* Globale Variablen für Signal-Handler */
static GMainLoop *main_loop = NULL;
static SondServer *server_instance = NULL;

/**
 * signal_handler:
 *
 * Behandelt SIGINT (Ctrl+C) und SIGTERM für sauberes Beenden.
 */
static void signal_handler(int signum) {
    g_print("\n");

    if (signum == SIGINT) {
        g_print("Received SIGINT (Ctrl+C), shutting down...\n");
    } else if (signum == SIGTERM) {
        g_print("Received SIGTERM, shutting down...\n");
    }

    /* Server stoppen */
    if (server_instance && sond_server_is_running(server_instance)) {
        sond_server_stop(server_instance);
    }

    /* Main loop beenden */
    if (main_loop && g_main_loop_is_running(main_loop)) {
        g_main_loop_quit(main_loop);
    }
}

/**
 * setup_signal_handlers:
 *
 * Registriert Signal-Handler für sauberes Herunterfahren.
 */
static void setup_signal_handlers(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

/**
 * print_usage:
 *
 * Zeigt Verwendungs-Informationen.
 */
static void print_usage(const char *program_name) {
    g_print("Usage: %s [OPTIONS]\n", program_name);
    g_print("\n");
    g_print("Options:\n");
    g_print("  --host HOST         MySQL host (default: localhost)\n");
    g_print("  --user USER         MySQL user (default: root)\n");
    g_print("  --password PASS     MySQL password (default: empty)\n");
    g_print("  --database DB       MySQL database (default: graphdb)\n");
    g_print("  --port PORT         Server port (default: 8080)\n");
    g_print("  --setup             Setup database (create tables & procedures)\n");
    g_print("  --help              Show this help\n");
    g_print("\n");
    g_print("Example:\n");
    g_print("  %s --host localhost --user root --password secret --database graphdb --port 8080\n", program_name);
    g_print("\n");
}

int main(int argc, char *argv[]) {
    GError *error = NULL;
    int exit_code = EXIT_SUCCESS;

    /* Default-Werte */
    const gchar *db_host = "localhost";
    const gchar *db_user = "root";
    const gchar *db_password = "";
    const gchar *db_name = "graphdb";
    guint port = 8080;
    gboolean do_setup = FALSE;

    /* Kommandozeilen-Argumente parsen */
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--help") == 0 || g_strcmp0(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (g_strcmp0(argv[i], "--host") == 0 && i + 1 < argc) {
            db_host = argv[++i];
        } else if (g_strcmp0(argv[i], "--user") == 0 && i + 1 < argc) {
            db_user = argv[++i];
        } else if (g_strcmp0(argv[i], "--password") == 0 && i + 1 < argc) {
            db_password = argv[++i];
        } else if (g_strcmp0(argv[i], "--database") == 0 && i + 1 < argc) {
            db_name = argv[++i];
        } else if (g_strcmp0(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (g_strcmp0(argv[i], "--setup") == 0) {
            do_setup = TRUE;
        } else {
            g_printerr("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    g_print("=== Graph Database Server ===\n");
    g_print("MySQL Host:     %s\n", db_host);
    g_print("MySQL User:     %s\n", db_user);
    g_print("MySQL Database: %s\n", db_name);
    g_print("Server Port:    %u\n", port);
    g_print("\n");

    /* Optional: Datenbank-Setup */
    if (do_setup) {
        g_print("Setting up database...\n");

        MYSQL *conn = mysql_init(NULL);
        if (!conn) {
            g_printerr("ERROR: mysql_init failed\n");
            return EXIT_FAILURE;
        }

        if (!mysql_real_connect(conn, db_host, db_user, db_password,
                               db_name, 0, NULL, 0)) {
            g_printerr("ERROR: MySQL connection failed: %s\n", mysql_error(conn));
            mysql_close(conn);
            return EXIT_FAILURE;
        }

        if (!sond_graph_db_setup(conn, &error)) {
            g_printerr("ERROR: Database setup failed: %s\n", error->message);
            g_clear_error(&error);
            mysql_close(conn);
            return EXIT_FAILURE;
        }

        mysql_close(conn);
        g_print("Database setup completed successfully!\n\n");
    }

    /* Signal-Handler registrieren */
    setup_signal_handlers();

    /* Server erstellen */
    g_print("Creating server...\n");
    server_instance = sond_server_new(db_host, db_user, db_password,
                                      db_name, port, &error);

    if (!server_instance) {
        g_printerr("ERROR: Failed to create server: %s\n", error->message);
        g_clear_error(&error);
        return EXIT_FAILURE;
    }

    /* Server starten */
    g_print("Starting server...\n");
    if (!sond_server_start(server_instance, &error)) {
        g_printerr("ERROR: Failed to start server: %s\n", error->message);
        g_clear_error(&error);
        g_object_unref(server_instance);
        return EXIT_FAILURE;
    }

    gchar *server_url = sond_server_get_url(server_instance);
    g_print("\n");
    g_print("✓ Server is running!\n");
    g_print("  URL: %s\n", server_url);
    g_print("\n");
    g_print("Available endpoints:\n");
    g_print("  POST   %s/node/save\n", server_url);
    g_print("  GET    %s/node/load/{id}\n", server_url);
    g_print("  DELETE %s/node/delete/{id}\n", server_url);
    g_print("  POST   %s/node/save-with-edges\n", server_url);
    g_print("\n");
    g_print("Press Ctrl+C to stop the server.\n");
    g_print("\n");
    g_free(server_url);

    /* Main Loop starten */
    main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(main_loop);

    /* Cleanup */
    g_print("\nCleaning up...\n");

    if (main_loop) {
        g_main_loop_unref(main_loop);
        main_loop = NULL;
    }

    if (server_instance) {
        g_object_unref(server_instance);
        server_instance = NULL;
    }

    g_print("Server stopped.\n");

    return exit_code;
}

/*
 * Kompilierung:
 * gcc -o sond-server server_main.c sond_server.c sond_graph_db.c \
 *     sond_graph_node.c sond_graph_edge.c sond_graph_property.c \
 *     $(pkg-config --cflags --libs glib-2.0 gobject-2.0 libsoup-3.0 json-glib-1.0) \
 *     $(mysql_config --cflags --libs)
 *
 * Verwendung:
 * ./sond-server --setup                              # Erstmaliges Setup
 * ./sond-server                                      # Server starten
 * ./sond-server --port 9000 --host 192.168.1.100   # Custom Config
 */
