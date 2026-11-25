/*
 sond (sond_akten_suche.h) - Akten, Beweisstücke, Unterlagen
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

/**
 * @file sond_akten_suche.h
 * @brief Suchfeld für Akten (Oberfläche und Logik), von mehreren Modulen genutzt
 *
 * Das Feld nimmt eine Registernummer ("23/26", "2326", "126") oder einen Suchtext. Eine
 * Suche gilt online und offline gleich: Zuerst wird der Server befragt (mit Anmeldung,
 * kurzer Zeitgrenze und Pause nach einem Fehler); antwortet er, ist seine Antwort
 * maßgeblich. Nur wenn er nicht erreichbar ist, gilt die lokale Liste aus dem SeaDrive-
 * Laufwerk (Bezeichnung aus .sond/akte oder Merkliste). Ein einziger Treffer wird gleich
 * gemeldet, bei mehreren erscheint eine Trefferliste; ohne Treffer und ohne Antwort des
 * Servers wird gefragt, ob erneut gesucht werden soll.
 *
 * Dazu gehört der Stil der Eingabefelder: grün, wenn sie bedienbar sind, sonst grau.
 */

#ifndef SOND_AKTEN_SUCHE_H
#define SOND_AKTEN_SUCHE_H

#include <gtk/gtk.h>

#include "../sond_client.h"
#include "../../sond_graph/sond_graph_node.h"

G_BEGIN_DECLS

/* Wie weit der Server an einer Suche beteiligt war */
typedef enum {
    SOND_SUCHE_SERVER_NICHT_BEFRAGT,   /* keine Sitzung: es wurde nur lokal gesucht */
    SOND_SUCHE_SERVER_OK,              /* hat geantwortet (auch "nicht gefunden") */
    SOND_SUCHE_SERVER_FEHLER           /* nicht erreichbar oder Fehler */
} SondSucheServer;

/* Ein Treffer der Suche */
typedef struct {
    SondGraphNode *node;    /* (nullable) nur, wenn der Server die Akte lieferte */
    guint lfd_nr;
    guint year;
    gchar *regnr_display;   /* Format: "23/26" */
    gchar *kurzbezeichnung; /* (nullable) */
    gchar *gegenstand;      /* (nullable) unterscheidet gleiche Bezeichnungen */
    gboolean lokal;         /* Library im SeaDrive vorhanden */
} SondAkteTreffer;

/* Eine Akte wurde gewählt (einziger Treffer oder Wahl in der Liste). Der Treffer gilt nur
 * während des Aufrufs. */
typedef void (*SondAkteGewaehltFunc)(const SondAkteTreffer *treffer, SondSucheServer server,
        gpointer user_data);

/* Die Registernummer ist nirgends bekannt, der Server hat geantwortet (eine neue Akte
 * anlegen?). Ohne diesen Rückruf zeigt das Feld "Keine Ergebnisse". */
typedef void (*SondAkteUnbekanntFunc)(guint lfd_nr, guint year, gpointer user_data);

/* Legt das Suchfeld an (ein GtkEntry; Text und Fokus über die üblichen GtkEditable- und
 * GtkWidget-Funktionen) */
GtkWidget* sond_akten_suche_new(SondClient *client);

void sond_akten_suche_set_funcs(GtkWidget *suche, SondAkteGewaehltFunc gewaehlt,
        SondAkteUnbekanntFunc unbekannt, gpointer user_data);

/* Bedienbar und grün (TRUE) oder gesperrt und grau (FALSE) */
void sond_akten_suche_set_aktiv(GtkWidget *suche, gboolean aktiv);

/* Trägt die zuletzt verwendete Registernummer als Vorschlag ein, wenn das Feld bedienbar ist und
 * leer ist oder noch den letzten Vorschlag enthält. Gewählt wird nichts; Enter bleibt nötig. */
void sond_akten_suche_vorschlagen(GtkWidget *suche);

/* Fokus auf das Feld, sofern es bedienbar ist */
void sond_akten_suche_fokus(GtkWidget *suche);

/* Akten-Knoten zur Registernummer vom Server (nur Lesen, kein Lock); G_IO_ERROR_NOT_FOUND,
 * wenn es die Akte nicht gibt */
SondGraphNode* sond_akten_suche_node_by_regnr(SondClient *client, guint lfd_nr, guint year,
        GError **error);

/* Stil der Eingabefelder: beim Anlegen eines Moduls ref, beim Zerstören unref (der CSS-
 * Provider ist global am Display registriert). apply: "widget-active" (grün) oder
 * "widget-inactive" (grau) setzen. */
void sond_akten_suche_stil_ref(void);
void sond_akten_suche_stil_unref(void);
void sond_akten_suche_stil_apply(GtkWidget *widget, gboolean active);

G_END_DECLS

#endif /* SOND_AKTEN_SUCHE_H */
