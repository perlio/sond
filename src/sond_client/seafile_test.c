/*
 * Seafile Connection Test - FINAL VERSION
 *
 * - Korrekter Pipe-Name: seafile_{BASE64(username)}
 * - JSON-Response (kein GObject)
 * - Korrekte Memory-Ownership
 */

#include <stdio.h>
#include <glib.h>
#include <jansson.h>
#include "libsearpc/searpc-client.h"
#include "libsearpc/searpc-named-pipe-transport.h"

static gchar* base64_encode_simple(const gchar *input) {
    return g_base64_encode((const guchar*)input, strlen(input));
}

int main(void) {
    printf("=== Seafile Connection Test (FINAL) ===\n\n");

    const gchar *username = g_get_user_name();
    gchar *username_b64 = base64_encode_simple(username);

    /* KORREKT: seafile_{username_b64} (NICHT seafile_ext_pipe_...) */
    gchar *pipe_path = g_strdup_printf("\\\\.\\pipe\\seafile_%s", username_b64);

    printf("Username: %s\n", username);
    printf("Base64:   %s\n", username_b64);
    printf("Pipe:     %s\n\n", pipe_path);

    SearpcNamedPipeClient *pipe_client = searpc_create_named_pipe_client(pipe_path);
    if (!pipe_client) {
        printf("❌ Create failed\n");
        g_free(pipe_path);
        g_free(username_b64);
        return 1;
    }

    if (searpc_named_pipe_client_connect(pipe_client) < 0) {
        printf("❌ Connect failed\n");
        g_free(pipe_path);
        g_free(username_b64);
        g_free(pipe_client);
        return 1;
    }
    printf("✅ Verbunden!\n\n");

    SearpcClient *client = searpc_client_with_named_pipe_transport(pipe_client, "seafile-rpcserver");
    if (!client) {
        printf("❌ Transport failed\n");
        g_free(pipe_path);
        g_free(username_b64);
        g_free(pipe_client);
        return 1;
    }

    /* WICHTIG: Ab hier gehört pipe_client dem client - NICHT mehr freigeben! */

    printf("Rufe seafile_get_repo_list auf (JSON)...\n");

    GError *error = NULL;
    json_t *result = NULL;

    searpc_client_call(
        client,
        "seafile_get_repo_list",
        "json",
        0,
        &result,
        &error,
        2,
        "int", (void*)-1,
        "int", (void*)-1
    );

    if (error) {
        printf("❌ RPC-Call fehlgeschlagen: %s\n", error->message);
        g_error_free(error);
        searpc_free_client_with_pipe_transport(client);
        g_free(pipe_path);
        g_free(username_b64);
        return 1;
    }

    if (!result) {
        printf("⚠️  Keine Daten zurückgekommen\n");
    } else {
        printf("✅ RPC-Call erfolgreich!\n\n");

        if (json_is_array(result)) {
            size_t count = json_array_size(result);
            printf("Anzahl Libraries: %zu\n\n", count);

            for (size_t i = 0; i < count; i++) {
                json_t *repo = json_array_get(result, i);

                const char *id = json_string_value(json_object_get(repo, "id"));
                const char *name = json_string_value(json_object_get(repo, "name"));
                int encrypted = json_integer_value(json_object_get(repo, "encrypted"));

                printf("  %zu. %s\n", i+1, name ? name : "(kein Name)");
                printf("     ID: %s\n", id ? id : "(keine ID)");
                printf("     Encrypted: %s\n", encrypted ? "Ja" : "Nein");
                printf("\n");
            }
        } else {
            printf("Unerwartetes JSON-Format:\n");
            char *json_str = json_dumps(result, JSON_INDENT(2));
            printf("%s\n", json_str);
            free(json_str);
        }

        json_decref(result);
    }

    searpc_free_client_with_pipe_transport(client);
    /* NICHT g_free(pipe_client) - gehört dem client! */
    g_free(pipe_path);
    g_free(username_b64);

    printf("🎉 Test erfolgreich abgeschlossen!\n\n");
    printf("=== Verwendete Konfiguration ===\n");
    printf("Pipe-Pattern: \\\\.\\pipe\\seafile_{BASE64(username)}\n");
    printf("Service:      seafile-rpcserver\n");
    printf("Response:     JSON (jansson)\n");

    return 0;
}
