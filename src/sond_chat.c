/*
 sond (sond_chat.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_chat.h"

#include <glib.h>
#include <gio/gio.h>
#include <string.h>

/* SOND_WITH_EMBEDDINGS gate hier bewußt wiederverwendet (nicht ein eigenes
 * SOND_WITH_CHAT o.ä.): das Makro steht faktisch für "llama.cpp ist
 * eingebunden", nicht ausschließlich für Embeddings - Chat-Modell und
 * Embedding-Modell teilen sich dieselbe Bibliothek/denselben Build-Schalter
 * (siehe Makefile, Ziel "zond"). */
#ifdef SOND_WITH_EMBEDDINGS
#include <llama.h>
#include <ggml-backend.h>
#endif

#define SOND_CHAT_DEFAULT_N_CTX     8192
#define SOND_CHAT_MAX_CONTEXT_HITS  8
#define SOND_CHAT_MAX_NEW_TOKENS    800

#ifdef SOND_WITH_EMBEDDINGS
/* sond_llama_ensure_backends:
 *
 * Dieselbe Notlösung wie in sond_index.c (dort ausführlicher kommentiert):
 * ggml_backend_load_all() (von llama_backend_init() aufgerufen, solange
 * noch kein Backend registriert ist) durchsucht nur das Verzeichnis der
 * .exe und das Arbeitsverzeichnis, nicht PATH - in der MSYS2/UCRT64-
 * Entwicklungsumgebung liegen ggml-base.dll/ggml-cpu-*.dll aber unter
 * /ucrt64/bin. Deshalb hier PATH selbst absuchen und darüber laden.
 * Bewußt dupliziert statt gemeinsam mit sond_index.c genutzt - beide
 * Module sind absichtlich unabhängig voneinander (eigener llama.cpp-
 * Kontext, kein gemeinsamer Zustand). */
static void
sond_llama_ensure_backends(void) {
    if (ggml_backend_reg_count() > 0)
        return;

    gchar const *path_env = g_getenv("PATH");
    if (!path_env)
        return;

    gchar **dirs = g_strsplit(path_env, G_SEARCHPATH_SEPARATOR_S, -1);
    for (gint i = 0; dirs[i] && ggml_backend_reg_count() == 0; i++) {
        gchar *probe = g_build_filename(dirs[i], "ggml-base.dll", NULL);
        if (g_file_test(probe, G_FILE_TEST_EXISTS))
            ggml_backend_load_all_from_path(dirs[i]);
        g_free(probe);
    }
    g_strfreev(dirs);
}
#endif

struct _SondChatCtx {
#ifdef SOND_WITH_EMBEDDINGS
    gpointer llama_model; /* struct llama_model*   - opak */
    gpointer llama_ctx;   /* struct llama_context* - opak */
#endif
    gint n_ctx;
};

static gchar const *SOND_CHAT_SYSTEM_PROMPT =
    "Du bist ein Assistent, der ausschließlich auf Basis der vom Nutzer "
    "mitgelieferten Textauszüge aus Aktendokumenten antwortet. Wenn die "
    "Auszüge die Frage nicht beantworten, sage das ausdrücklich (\"Dazu "
    "finde ich in den vorliegenden Auszügen keine Angabe.\"), anstatt zu "
    "raten oder Wissen von außerhalb der Auszüge zu verwenden. Belege Deine "
    "Antwort möglichst mit der Nummer des jeweiligen Auszugs in eckigen "
    "Klammern, z.B. [2].";

SondChatCtx* sond_chat_ctx_new(gchar const *model_path, gint n_ctx, GError **error) {
    g_return_val_if_fail(model_path != NULL, NULL);

#ifndef SOND_WITH_EMBEDDINGS
    (void) n_ctx;
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
            "sond_chat_ctx_new: ohne SOND_WITH_EMBEDDINGS (llama.cpp) gebaut");
    return NULL;
#else
    SondChatCtx *ctx = g_new0(SondChatCtx, 1);
    ctx->n_ctx = (n_ctx > 0) ? n_ctx : SOND_CHAT_DEFAULT_N_CTX;

    llama_backend_init(); /* idempotent, auch wenn schon für das
                            * Embedding-Modell aufgerufen */
    sond_llama_ensure_backends();

    struct llama_model_params model_params = llama_model_default_params();
    /* Test ergab: 37/37 Layer vollständig auf die iGPU ausgelagert, aber nur
     * ~1,3-1,4 Token/s bei der Generierung - anders als beim Embedding-
     * Modell (ein Batch-Rechendurchgang pro Chunk, dafür ist eine GPU gut
     * geeignet) läuft Chat-Generierung zwangsläufig Token für Token; bei
     * so einer Einzelschritt-Verarbeitung dominieren Overhead/Speicher-
     * bandbreite, und die bescheidene iGPU kann da langsamer sein als die
     * CPU (12 Threads). Deshalb hier (anders als beim Embedding-Modell in
     * sond_index.c) bewußt CPU-only zum Vergleich. */
    model_params.n_gpu_layers = 0;

    ctx->llama_model = llama_model_load_from_file(model_path, model_params);
    if (!ctx->llama_model) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "sond_chat_ctx_new: llama_model_load_from_file '%s' fehlgeschlagen",
                model_path);
        sond_chat_ctx_free(ctx);
        return NULL;
    }

    struct llama_context_params ctx_params = llama_context_default_params();
    ctx_params.embeddings = FALSE;
    ctx_params.n_ctx      = ctx->n_ctx;
    /* n_batch = n_ctx, damit ein einzelner Prompt (Frage + Fundstellen) in
     * einem Rutsch dekodiert werden kann - llama_decode() teilt einen
     * größeren Batch nicht automatisch in mehrere Durchgänge auf, das
     * müßte der Aufrufer sonst selbst übernehmen. */
    ctx_params.n_batch    = ctx->n_ctx;
    /* Wie beim Embedding-Modell (sond_index.c): Standardwert ist ein
     * konservativer Festwert (4) statt der tatsächlichen Kernzahl - auf
     * CPU-only-Systemen bringt das spürbar Geschwindigkeit. */
    {
        gint n_cpu = g_get_num_processors();
        ctx_params.n_threads       = n_cpu;
        ctx_params.n_threads_batch = n_cpu;
    }

    ctx->llama_ctx = llama_init_from_model(
            (struct llama_model*) ctx->llama_model, ctx_params);
    if (!ctx->llama_ctx) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "sond_chat_ctx_new: llama_init_from_model fehlgeschlagen");
        sond_chat_ctx_free(ctx);
        return NULL;
    }

    return ctx;
#endif
}

void sond_chat_ctx_free(SondChatCtx *ctx) {
    if (!ctx) return;

#ifdef SOND_WITH_EMBEDDINGS
    if (ctx->llama_ctx)
        llama_free((struct llama_context*) ctx->llama_ctx);
    if (ctx->llama_model)
        llama_model_free((struct llama_model*) ctx->llama_model);
#endif

    g_free(ctx);
}

gchar* sond_chat_answer(SondChatCtx *ctx, gchar const *question,
                         GPtrArray *hits, GError **error) {
    g_return_val_if_fail(ctx      != NULL, NULL);
    g_return_val_if_fail(question != NULL, NULL);

#ifndef SOND_WITH_EMBEDDINGS
    (void) hits;
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
            "sond_chat_answer: ohne SOND_WITH_EMBEDDINGS (llama.cpp) gebaut");
    return NULL;
#else
    struct llama_model        *model = (struct llama_model*)   ctx->llama_model;
    struct llama_context       *lctx = (struct llama_context*) ctx->llama_ctx;
    const struct llama_vocab  *vocab = llama_model_get_vocab(model);

    /* --- Prompt aus Frage + Fundstellen bauen --- */
    GString *user_prompt = g_string_new(NULL);
    g_string_append_printf(user_prompt, "Frage: %s\n\n", question);

    if (hits && hits->len > 0) {
        guint n = MIN(hits->len, SOND_CHAT_MAX_CONTEXT_HITS);

        g_string_append(user_prompt, "Auszüge aus den Aktendokumenten:\n\n");
        for (guint i = 0; i < n; i++) {
            SondIndexHit *hit = g_ptr_array_index(hits, i);
            gchar *page_str = (hit->page_nr >= 0)
                    ? g_strdup_printf(", Seite %d", hit->page_nr + 1)
                    : g_strdup("");

            g_string_append_printf(user_prompt, "[%u] (Datei: %s%s): %s\n\n",
                    i + 1, hit->filename ? hit->filename : "?", page_str,
                    hit->snippet ? hit->snippet : "");
            g_free(page_str);
        }
    } else
        g_string_append(user_prompt, "(keine Fundstellen)\n");

    /* --- Chat-Template des Modells anwenden (Rollenformat je nach Modell
     * unterschiedlich, z.B. ChatML bei Qwen) - Fallback auf einfache
     * Verkettung, falls das Modell kein/ein nicht unterstütztes Template
     * mitbringt (llama_chat_apply_template kennt nur eine vordefinierte
     * Liste, keinen allgemeinen Jinja-Parser). --- */
    struct llama_chat_message messages[2] = {
        { "system", SOND_CHAT_SYSTEM_PROMPT },
        { "user",   user_prompt->str }
    };

    gchar const *tmpl = llama_model_chat_template(model, NULL);
    gchar *formatted   = NULL;
    gint   n_formatted = 0;

    if (tmpl) {
        gint buf_size = 2 * (gint) (strlen(SOND_CHAT_SYSTEM_PROMPT) + user_prompt->len) + 256;
        formatted = g_malloc(buf_size);
        n_formatted = llama_chat_apply_template(tmpl, messages, 2, TRUE, formatted, buf_size);

        if (n_formatted > buf_size) {
            formatted = g_realloc(formatted, n_formatted + 1);
            n_formatted = llama_chat_apply_template(tmpl, messages, 2, TRUE, formatted, n_formatted + 1);
        }

        if (n_formatted < 0) {
            g_free(formatted);
            formatted = NULL;
        }
    }

    if (!formatted) {
        formatted = g_strdup_printf("%s\n\n%s\n", SOND_CHAT_SYSTEM_PROMPT, user_prompt->str);
        n_formatted = (gint) strlen(formatted);
    }

    g_string_free(user_prompt, TRUE);

    /* --- Tokenisieren --- */
    gint n_tokens_max = ctx->n_ctx;
    llama_token *tokens = g_new(llama_token, n_tokens_max);

    /* add_special=TRUE: jeder Aufruf ist ein frischer Einzel-Durchgang
     * ohne mehrturnigen Gesprächsverlauf, entsprechend "erste Nachricht"
     * (BOS soll mit hinzugefügt werden). parse_special=TRUE: die vom
     * Chat-Template eingefügten Sondertoken (z.B. <|im_start|>) sollen als
     * echte Sondertoken erkannt werden, nicht als Klartext. */
    gint n_tokens = llama_tokenize(vocab, formatted, n_formatted,
            tokens, n_tokens_max, TRUE, TRUE);

    if (n_tokens < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "sond_chat_answer: Prompt zu lang für den Kontext (max. %d Token) - "
                "weniger Fundstellen verwenden oder Kontextgröße erhöhen.",
                n_tokens_max);
        g_free(tokens);
        g_free(formatted);
        return NULL;
    }

    g_free(formatted);

    /* --- Prompt verarbeiten --- */
    llama_memory_clear(llama_get_memory(lctx), TRUE); /* frischer Durchgang,
            kein mehrturniger Verlauf */

    /* Diagnose: Zeit für Prompt-Verarbeitung (ein Rechendurchgang über den
     * ganzen Prompt, analog zu compute_embedding()) getrennt von der
     * anschließenden Token-für-Token-Generierung messen. */
    gint64 t_prompt0 = g_get_monotonic_time();

    llama_batch batch = llama_batch_get_one(tokens, n_tokens);
    if (llama_decode(lctx, batch) != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "sond_chat_answer: llama_decode (Prompt) fehlgeschlagen");
        g_free(tokens);
        return NULL;
    }
    g_free(tokens);

    g_warning("sond_chat_answer: Prompt (%d Token) verarbeitet in %.2fs",
            n_tokens, (gdouble)(g_get_monotonic_time() - t_prompt0) / 1e6);

    /* --- Sampler-Kette: niedrige Temperatur, da wortgetreue, belegte
     * Antworten wichtiger sind als kreative Vielfalt (Grounded-QA). --- */
    struct llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    struct llama_sampler *smpl = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.2f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    /* --- Generierungsschleife --- */
    GString *answer = g_string_new(NULL);
    gint64   t_gen0  = g_get_monotonic_time();

    for (gint n_generated = 0; n_generated < SOND_CHAT_MAX_NEW_TOKENS; n_generated++) {
        llama_token new_token = llama_sampler_sample(smpl, lctx, -1);
        llama_sampler_accept(smpl, new_token);

        if (llama_vocab_is_eog(vocab, new_token))
            break;

        gchar piece_buf[256];
        gint  piece_len = llama_token_to_piece(vocab, new_token,
                piece_buf, sizeof(piece_buf), 0, TRUE);
        if (piece_len > 0)
            g_string_append_len(answer, piece_buf, piece_len);

        /* Diagnose: alle 20 Token Zwischenstand melden, damit auf der
         * Konsole sichtbar ist, ob/wie schnell die Generierung
         * voranschreitet (bisher gab es dafür gar keine Rückmeldung -
         * ein einfach nur langsamer Lauf sah dadurch wie ein Hänger aus). */
        if ((n_generated % 20) == 0)
            g_warning("sond_chat_answer: %d Token generiert (%.2fs, %.2f Token/s)",
                    n_generated + 1,
                    (gdouble)(g_get_monotonic_time() - t_gen0) / 1e6,
                    (n_generated + 1) / ((gdouble)(g_get_monotonic_time() - t_gen0) / 1e6));

        llama_token   next_token_arr[1] = { new_token };
        llama_batch   next_batch = llama_batch_get_one(next_token_arr, 1);
        if (llama_decode(lctx, next_batch) != 0)
            break; /* Kontext voll o.ä. - bisher Generiertes trotzdem zurückgeben */
    }

    llama_sampler_free(smpl);

    return g_string_free(answer, FALSE); /* Inhalt bleibt erhalten, Ownership geht an Aufrufer */
#endif
}
