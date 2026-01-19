/*
 * Seafile Connection Test
 *
 * Testet ob Verbindung zum Seafile-Client möglich ist
 */

#include <stdio.h>
#include <glib.h>
#include "libsearpc/searpc-client.h"
#include "libsearpc/searpc-named-pipe-transport.h"

/**
 * test_seafile_connection:
 *
 * Versucht mit Seafile-Client zu verbinden und Repo-Liste abzurufen
 *
 * Returns: TRUE wenn erfolgreich
 */
gboolean test_seafile_connection(void) {
    GError *error = NULL;
    SearpcNamedPipeClient *pipe_client = NULL;
    SearpcClient *client = NULL;

    /* Windows Named Pipe: \\.\pipe\seafile_pipe_<username> */
    const gchar *username = g_get_user_name();
    gchar *pipe_path = g_strdup_printf("\\\\.\\pipe\\seafile_pipe_%s", username);

    printf("Versuche Verbindung zu: %s\n", pipe_path);

    /* Named Pipe Client erstellen */
    pipe_client = searpc_create_named_pipe_client(pipe_path);

    if (!pipe_client) {
        printf("❌ Konnte Named Pipe Client nicht erstellen: %s\n", pipe_path);
        g_free(pipe_path);
        return FALSE;
    }

    /* Verbinden */
    if (searpc_named_pipe_client_connect(pipe_client) < 0) {
        printf("❌ Konnte nicht verbinden zu: %s\n", pipe_path);
        printf("   Seafile-Client läuft vermutlich nicht oder nutzt anderen Pipe-Namen\n");
        g_free(pipe_path);
        g_free(pipe_client);
        return FALSE;
    }

    printf("✅ Verbindung hergestellt!\n");
    g_free(pipe_path);

    /* SearpcClient mit Transport erstellen */
    client = searpc_client_with_named_pipe_transport(pipe_client, "seafile-rpcserver");

    if (!client) {
        printf("❌ Konnte RPC-Client nicht erstellen\n");
        g_free(pipe_client);
        return FALSE;
    }

    /* Test-RPC-Call: Hole Repo-Liste */
    printf("Rufe seafile_get_repo_list auf...\n");

    GList *repos = NULL;
    searpc_client_call(
        client,
        "seafile_get_repo_list",
        "objlist",
        0, // GType (wird ignoriert für objlist)
        &repos,
        &error,
        2,
        "int", (void*)-1,  // offset
        "int", (void*)-1   // limit
    );

    if (error) {
        printf("❌ RPC-Call fehlgeschlagen: %s\n", error->message);
        g_error_free(error);
        searpc_free_client_with_pipe_transport(client);
        g_free(pipe_client);
        return FALSE;
    }

    if (!repos) {
        printf("⚠️  Keine Repos gefunden (oder Seafile hat keine Libraries)\n");
    } else {
        printf("✅ RPC-Call erfolgreich! Anzahl Repos: %u\n", g_list_length(repos));

        /* Erste 5 Repos ausgeben */
        guint count = 0;
        for (GList *l = repos; l != NULL && count < 5; l = l->next, count++) {
            GObject *repo = l->data;
            gchar *id = NULL;
            gchar *name = NULL;

            g_object_get(repo, "id", &id, "name", &name, NULL);
            printf("   - %s: %s\n", name ? name : "(kein Name)", id ? id : "(keine ID)");

            g_free(id);
            g_free(name);
        }

        g_list_free_full(repos, g_object_unref);
    }

    searpc_free_client_with_pipe_transport(client);
    g_free(pipe_client);

    printf("\n✅ Test erfolgreich abgeschlossen!\n");
    return TRUE;
}

/* Test-Main (optional) */
int main(void) {
    printf("=== Seafile Connection Test ===\n\n");

    if (test_seafile_connection()) {
        printf("\n🎉 Seafile-Client ist erreichbar!\n");
        return 0;
    } else {
        printf("\n❌ Seafile-Client nicht erreichbar\n");
        printf("\nMögliche Ursachen:\n");
        printf("- Seafile-Client läuft nicht\n");
        printf("- Falscher Pipe-Name\n");
        printf("\nPrüfen Sie welche Pipes existieren mit PowerShell:\n");
        printf("  Get-ChildItem \\\\.\\pipe\\ | Where-Object {$_.Name -like \"*sea*\"}\n");
        printf("\nAlternative Pipe-Namen zum Testen:\n");
        printf("  - \\\\.\\pipe\\seafile\n");
        printf("  - \\\\.\\pipe\\seafile_client\n");
        printf("  - \\\\.\\pipe\\seadrive\n");
        return 1;
    }
}
