/*
 sond (sond_archiv_kandidaten.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_archiv_kandidaten.h"

#include <string.h>
#include <stdio.h>

#include "../../sond_graph/sond_graph_property.h"

void sond_archiv_kandidat_free(SondArchivKandidat *kandidat) {
    if (!kandidat)
        return;
    g_free(kandidat->library_name);
    g_free(kandidat->bezeichnung);
    g_free(kandidat->ablnr);
    g_free(kandidat->bis);
    g_free(kandidat->archiv_name);
    g_free(kandidat);
}

SondGraphNodeSearchCriteria* sond_archiv_suchkriterien(void) {
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();

    criteria->label = g_strdup("Akte");
    //"*": irgendein Wert; trifft jede Akte mit mindestens einem "bis"
    sond_graph_node_search_criteria_add_property_filter(criteria, "bis", "*");
    criteria->limit = 0;

    return criteria;
}

/* "JJJJ-MM-TT" -> Datum; FALSE bei ungültiger Angabe */
static gboolean datum_lesen(const gchar *text, GDate *datum) {
    guint j = 0;
    guint m = 0;
    guint t = 0;
    gchar extra = 0;

    if (!text || sscanf(text, "%4u-%2u-%2u%c", &j, &m, &t, &extra) != 3)
        return FALSE;
    if (!g_date_valid_dmy((GDateDay) t, (GDateMonth) m, (GDateYear) j))
        return FALSE;
    g_date_set_dmy(datum, (GDateDay) t, (GDateMonth) m, (GDateYear) j);

    return TRUE;
}

/* Anzahl "von" und "bis"; das letzte "bis" als Property */
static void von_bis_auswerten(SondGraphNode *node, guint *von, guint *bis,
                              SondGraphProperty **letztes_bis) {
    GPtrArray *props = sond_graph_node_get_properties(node);

    *von = 0;
    *bis = 0;
    *letztes_bis = NULL;
    for (guint i = 0; props && i < props->len; i++) {
        SondGraphProperty *prop = g_ptr_array_index(props, i);
        const gchar *key = sond_graph_property_get_key(prop);

        if (!g_strcmp0(key, "von")) {
            (*von)++;
        } else if (!g_strcmp0(key, "bis")) {
            (*bis)++;
            *letztes_bis = prop;
        }
    }
}

/* Wert einer Unter-Property des letzten "bis" (NULL, wenn es sie nicht gibt) */
static const gchar* sub_wert(SondGraphProperty *bis, const gchar *key) {
    GPtrArray *subs = bis ? sond_graph_property_get_properties(bis) : NULL;
    SondGraphProperty *sub = subs ? sond_graph_property_find(subs, key) : NULL;

    return sub ? sond_graph_property_get_first_value(sub) : NULL;
}

gchar* sond_archiv_name_bilden(const gchar *library_name, const gchar *ablnr,
                               const gchar *bis) {
    GDate datum;

    if (!library_name || !*library_name || !ablnr || !*ablnr)
        return NULL;
    if (strpbrk(library_name, "/\\") || strpbrk(ablnr, "/\\"))
        return NULL;
    g_date_clear(&datum, 1);
    if (!datum_lesen(bis, &datum))
        return NULL;

    return g_strdup_printf("%s_Abl%s_%s", library_name, ablnr, bis);
}

SondArchivKandidat* sond_archiv_kandidat_pruefen(SondGraphNode *node,
                                                 gint ueberliege_tage,
                                                 const GDate *heute,
                                                 const gchar **grund) {
    SondGraphProperty *letztes_bis = NULL;
    guint von = 0;
    guint bis_anzahl = 0;
    GDate bis_datum;
    GDate stichtag;
    GPtrArray *regnr = NULL;
    SondArchivKandidat *k = NULL;
    const gchar *bis = NULL;
    const gchar *ablnr = NULL;
    gint alter = 0;
    gchar *kurzb = NULL;
    const gchar *dummy = NULL;

    if (!grund)
        grund = &dummy;
    *grund = NULL;

    if (!node) {
        *grund = "kein Node";
        return NULL;
    }

    von_bis_auswerten(node, &von, &bis_anzahl, &letztes_bis);
    if (von == 0) {
        *grund = "nicht angelegt";
        return NULL;
    }
    if (von > bis_anzahl) {
        *grund = "aktiv";
        return NULL;
    }
    if (!letztes_bis) {
        *grund = "kein bis";
        return NULL;
    }
    if (sub_wert(letztes_bis, "archiv")) {
        *grund = "bereits archiviert";
        return NULL;
    }

    bis = sond_graph_property_get_first_value(letztes_bis);
    g_date_clear(&bis_datum, 1);
    if (!datum_lesen(bis, &bis_datum)) {
        *grund = "ungültiges bis-Datum";
        return NULL;
    }
    ablnr = sub_wert(letztes_bis, "ablnr");
    if (!ablnr || !*ablnr) {
        *grund = "keine Ablagenummer";
        return NULL;
    }

    g_date_clear(&stichtag, 1);
    if (heute) {
        stichtag = *heute;
    } else {
        GDateTime *jetzt = g_date_time_new_now_local();

        g_date_set_dmy(&stichtag, (GDateDay) g_date_time_get_day_of_month(jetzt),
                (GDateMonth) g_date_time_get_month(jetzt),
                (GDateYear) g_date_time_get_year(jetzt));
        g_date_time_unref(jetzt);
    }
    alter = (gint) g_date_days_between(&bis_datum, &stichtag);
    if (alter < ueberliege_tage) {
        *grund = "Überliegedauer noch nicht erreicht";
        return NULL;
    }

    regnr = sond_graph_node_get_property(node, "regnr");
    if (!regnr || regnr->len != 2) {
        if (regnr)
            g_ptr_array_unref(regnr);
        *grund = "keine RegNr";
        return NULL;
    }

    k = g_new0(SondArchivKandidat, 1);
    k->node_id = sond_graph_node_get_id(node);
    k->year = (guint) g_ascii_strtoull(g_ptr_array_index(regnr, 0), NULL, 10);
    k->lfd_nr = (guint) g_ascii_strtoull(g_ptr_array_index(regnr, 1), NULL, 10);
    g_ptr_array_unref(regnr);
    k->library_name = g_strdup_printf("%u-%u", k->year, k->lfd_nr);
    kurzb = sond_graph_node_get_property_string(node, "kurzb");
    k->bezeichnung = kurzb;
    k->ablnr = g_strdup(ablnr);
    k->bis = g_strdup(bis);
    k->alter_tage = alter;
    k->archiv_name = sond_archiv_name_bilden(k->library_name, k->ablnr, k->bis);
    if (!k->archiv_name) {
        sond_archiv_kandidat_free(k);
        *grund = "Archivname nicht bildbar";
        return NULL;
    }

    return k;
}

gboolean sond_archiv_ist_archiviert(SondGraphNode *node, gchar **archiv_name) {
    SondGraphProperty *letztes_bis = NULL;
    guint von = 0;
    guint bis = 0;
    const gchar *name = NULL;

    if (archiv_name)
        *archiv_name = NULL;
    if (!node)
        return FALSE;
    von_bis_auswerten(node, &von, &bis, &letztes_bis);
    name = sub_wert(letztes_bis, "archiv");
    if (!name || !*name)
        return FALSE;
    if (archiv_name)
        *archiv_name = g_strdup(name);

    return TRUE;
}

gboolean sond_archiv_markieren(SondGraphNode *node, const gchar *archiv_name) {
    SondGraphProperty *letztes_bis = NULL;
    GPtrArray *subs = NULL;
    SondGraphProperty *vorhanden = NULL;
    guint von = 0;
    guint bis = 0;

    g_return_val_if_fail(archiv_name != NULL, FALSE);
    if (!node)
        return FALSE;
    von_bis_auswerten(node, &von, &bis, &letztes_bis);
    if (!letztes_bis)
        return FALSE;

    subs = sond_graph_property_get_properties(letztes_bis);
    vorhanden = subs ? sond_graph_property_find(subs, "archiv") : NULL;
    if (vorhanden)
        sond_graph_property_set_string(vorhanden, archiv_name);
    else
        sond_graph_property_add_subproperty(letztes_bis,
                sond_graph_property_new_string("archiv", archiv_name));

    return TRUE;
}
