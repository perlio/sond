/*
 sond (sond_chat.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SRC_SOND_CHAT_H_
#define SRC_SOND_CHAT_H_

#include <glib.h>

#include "sond_index.h"

G_BEGIN_DECLS

/**
 * SondChatCtx:
 *
 * Eigener llama.cpp-Kontext für das Chat-/Generierungsmodell - bewußt
 * getrennt vom Embedding-Modell in SondIndexCtx (unterschiedliche Aufgabe,
 * unterschiedliche Modellgröße, unabhängig austauschbar). Läuft rein lokal,
 * keine Netzwerkverbindung.
 */
typedef struct _SondChatCtx SondChatCtx;

/**
 * sond_chat_ctx_new:
 * @model_path: Pfad zum GGUF-Chat-Modell
 * @n_ctx:      Kontextgröße in Token (<=0: Standardwert 8192)
 * @error:      GError
 *
 * Lädt das Chat-Modell. Anders als sond_index_ctx_new() (wo ein fehlendes
 * Embedding-Modell die gesamte Projekt-Infrastruktur nicht scheitern lassen
 * darf) wird hier ein echter Fehler zurückgegeben, wenn das Modell nicht
 * geladen werden kann - der Aufrufer (das Chat-Fenster) entscheidet dann,
 * ob/wie er das dem Nutzer meldet.
 *
 * Returns: (transfer full) neuer SondChatCtx, oder NULL bei Fehler.
 */
SondChatCtx* sond_chat_ctx_new(gchar const *model_path, gint n_ctx, GError **error);

/**
 * sond_chat_ctx_free:
 * Schließt den llama.cpp-Kontext und gibt alle Ressourcen frei.
 */
void sond_chat_ctx_free(SondChatCtx *ctx);

/**
 * sond_chat_answer:
 * @ctx:      SondChatCtx
 * @question: die vom Nutzer gestellte Frage
 * @hits:     GPtrArray von SondIndexHit* (z.B. aus
 *            sond_index_semantic_search()) - deren Text wird als
 *            Fundstellen/Kontext in den Prompt eingebaut. Es werden
 *            höchstens die ersten SOND_CHAT_MAX_CONTEXT_HITS Einträge
 *            verwendet (Reihenfolge = Priorität, z.B. nach score sortiert).
 * @error:    GError
 *
 * Baut aus @question und den Texten der @hits einen Prompt (über das im
 * Modell hinterlegte Chat-Template, falls vorhanden) und weist das Modell
 * an, ausschließlich auf Basis der Fundstellen zu antworten - sonst
 * ausdrücklich zu sagen, daß sich dazu nichts in den Auszügen findet,
 * statt zu raten. Rein lokale Berechnung, keine Netzwerkverbindung.
 *
 * Returns: (transfer full) neu allozierter Antworttext (mit g_free()
 *          freizugeben), oder NULL bei Fehler.
 */
gchar* sond_chat_answer(SondChatCtx *ctx, gchar const *question,
                         GPtrArray *hits, GError **error);

G_END_DECLS

#endif /* SRC_SOND_CHAT_H_ */
