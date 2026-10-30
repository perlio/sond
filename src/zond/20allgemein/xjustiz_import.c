/*
 zond (xjustiz_import.c) - Akten, Beweisstücke, Unterlagen
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

/* s. ausführlichen Kommentar in xjustiz_import.h. */

#include <string.h>
#include <math.h>

#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "../../sond_log_and_error.h"
#include "../../sond_file_helper.h"
#include "../../sond_fileparts.h"
#include "../../misc.h"
#include "../../sond_seadrive.h"
#include "../../sond_treeviewfm.h"

#include "../zond_init.h"
#include "../10init/app_window.h"
#include "../zond_dbase.h"
#include "../zond_treeview.h"

#include "project.h"
#include "xjustiz_import.h"

/* ============================================================================
 * HILFSFUNKTIONEN - STRINGS
 * ========================================================================== */

static gboolean str_has_suffix_ci(gchar const *s, gchar const *suffix) {
	gsize len_s = strlen(s);
	gsize len_suffix = strlen(suffix);

	if (len_s < len_suffix)
		return FALSE;

	return g_ascii_strcasecmp(s + (len_s - len_suffix), suffix) == 0;
}

/* ============================================================================
 * BAUM-STRUKTUREN
 * ========================================================================== */

typedef struct {
	gchar *dateiname; //roher Dateiname/-pfad laut XML; NULL bei ist_luecke
	gchar *anzeigename; //Label laut XML (ggf. Fallback aus Dateiname)
	gboolean hat_seiten; //ersteSeitennummer/letzteSeitennummer verwertbar
	gdouble erste; //ersteSeitennummer (Dezimalwerte: eingeschobene Blätter)
	gdouble letzte; //letzteSeitennummer
	gint seiten; //Seitenzahl laut Seitennummern (nur mit hat_seiten)
	gboolean ist_luecke; //kein Dokument: Platzhalter "Bl. x fehlt"
} XJustizDatei;

static void xjustiz_datei_free(gpointer data) {
	XJustizDatei *d = data;

	if (!d)
		return;

	g_free(d->dateiname);
	g_free(d->anzeigename);
	g_free(d);
}

/* Ein Strukturpunkt - entspricht genau EINEM <akte>- oder <teilakte>-
 * Element der Nachricht, das in seinem Teilbaum mindestens ein Dokument
 * enthält (leere Zweige, insbesondere reine "Verweis auf: ..."-
 * Platzhalter auf eine andere Akte, werden beim Aufbau schon ausgelassen -
 * s. xjustiz_build_akte()/xjustiz_build_teilakte() - und erscheinen hier
 * nie). 'dateien': Dokumente, die DIREKT an diesem Knoten hängen (laut
 * Analyse eines realen Datensatzes schließt sich das mit 'children' aktuell
 * gegenseitig aus - eine Akte/Teilakte hat entweder nur Dokumente oder nur
 * weitere Teilakten -, der Code geht aber bewußt vom allgemeineren Fall
 * aus, daß auch beides gleichzeitig vorkommen könnte). */
typedef struct _XJustizNode XJustizNode;
struct _XJustizNode {
	gchar *label;
	GPtrArray *dateien; //XJustizDatei*
	GPtrArray *children; //XJustizNode*
	gint seiten; //Summe der Seiten im Teilbaum (s. xjustiz_node_finalize())
};

static void xjustiz_node_free(gpointer data) {
	XJustizNode *n = data;

	if (!n)
		return;

	g_free(n->label);
	if (n->dateien)
		g_ptr_array_unref(n->dateien);
	if (n->children)
		g_ptr_array_unref(n->children);
	g_free(n);
}

/* ============================================================================
 * XML-AUSWERTUNG (xjustiz_nachricht.xml)
 * ========================================================================== */

static gchar* xjustiz_node_text(xmlNodePtr node) {
	xmlChar *content = NULL;
	gchar *text = NULL;

	content = xmlNodeGetContent(node);
	if (!content)
		return NULL;

	text = g_strdup((gchar const*) content);
	xmlFree(content);

	g_strstrip(text);
	if (!text[0]) {
		g_free(text);
		return NULL;
	}

	return text;
}

/* Formatiert einen XML xs:dateTime-Wert (z.B. aus nachrichtenkopf/
 * erstellungszeitpunkt, etwa "2026-08-05T08:07:16.2265062+02:00") kompakt
 * als "JJJJMMTT" (z.B. "20260805") für die Verwendung als Namensbestandteil
 * des obersten Strukturpunkts (s. xjustiz_import()). Bei Parsierungsfehler
 * wird der Rohwert unverändert zurückgegeben - robuster als ein hartes
 * Scheitern für einen reinen Namensbestandteil. NULL rein -> NULL raus.
 * Rückgabe muß immer freigegeben werden. */
static gchar* xjustiz_format_datum_kompakt(gchar const *roh) {
	GDateTime *dt = NULL;
	gchar *formatted = NULL;

	if (!roh)
		return NULL;

	dt = g_date_time_new_from_iso8601(roh, NULL);
	if (!dt)
		return g_strdup(roh);

	formatted = g_date_time_format(dt, "%Y%m%d");
	g_date_time_unref(dt);

	return formatted ? formatted : g_strdup(roh);
}

/* Sucht unter 'parent' das erste DIREKTE Kind-Element mit lokalem Namen
 * 'name' (Namespace-Präfix wird von libxml2 bereits abgetrennt und steckt
 * in node->ns, nicht in node->name - der Vergleich ist daher implizit
 * namespace-präfix-unabhängig, analog zu local-name() in XPath). NULL,
 * wenn 'parent' NULL ist oder kein solches Kind hat. */
static xmlNodePtr xjustiz_find_child(xmlNodePtr parent, gchar const *name) {
	xmlNodePtr c;

	if (!parent)
		return NULL;

	for (c = parent->children; c; c = c->next)
		if (c->type == XML_ELEMENT_NODE && c->name
				&& strcmp((gchar const*) c->name, name) == 0)
			return c;

	return NULL;
}

/* Wie xjustiz_find_child(), liefert aber direkt den (getrimmten) Textinhalt
 * des gefundenen Kindes - NULL, wenn das Kind fehlt oder leer ist. Rückgabe
 * muß freigegeben werden. */
static gchar* xjustiz_child_text(xmlNodePtr parent, gchar const *name) {
	xmlNodePtr c = xjustiz_find_child(parent, name);

	return c ? xjustiz_node_text(c) : NULL;
}

/* Kontext der Auswertung: XPath-Kontext sowie Verwaltung der <akte>-
 * Elemente für das Ersetzen von "Verweis auf: ..."-Platzhaltern durch die
 * referenzierte Akte (s. xjustiz_platzhalter_ziel()). Alle xmlNodePtr sind
 * nur geborgt (gehören dem xmlDoc). */
typedef struct {
	xmlXPathContextPtr xpath;
	GHashTable *akten; //Aktenzeichen (g_free) -> <akte>
	GHashTable *referenziert; //<akte> -> Ziel mindestens eines Platzhalters
	GHashTable *verbraucht; //<akte> -> schon als Strukturpunkt gebaut
} XJustizCtx;

/* Liest eine Seitennummer (Dezimalwert möglich, z.B. "318.1" für ein
 * eingeschobenes Blatt) aus dem <dokument> (Kontextknoten von ctx->xpath).
 * FALSE, wenn das Element fehlt oder keine Zahl enthält. */
static gboolean xjustiz_dok_seitennummer(XJustizCtx *ctx, gchar const *expr,
		gdouble *out) {
	xmlXPathObjectPtr xp = xmlXPathEvalExpression((xmlChar const*) expr,
			ctx->xpath);
	gchar *text = NULL;
	gchar *end = NULL;
	gboolean ok = FALSE;

	if (xp && xp->nodesetval && xp->nodesetval->nodeNr > 0)
		text = xjustiz_node_text(xp->nodesetval->nodeTab[0]);
	if (xp)
		xmlXPathFreeObject(xp);

	if (text) {
		*out = g_ascii_strtod(text, &end);
		ok = end && end != text && *end == '\0';
		g_free(text);
	}

	return ok;
}

/* Wertet EIN <dokument>-Element aus und hängt jede darin referenzierte
 * PDF-Datei (datei/dateiname, gefiltert auf ".pdf") mit zugehörigem
 * anzeigename (Fallback: Dateiname ohne Endung) an 'out_dateien' an.
 * Sucht bewußt per lokalem Elementnamen UND ohne feste Verschachtelungs-
 * tiefe (".//" via XPath) für anzeigename/datei/dateiname innerhalb des
 * dokument-Knotens, da diese Tiefe laut Spezifikation je Fachmodul/Version
 * variieren kann - anders als die STRUKTURELLE Navigation akte/teilakte/
 * dokument selbst, die exakt der (anhand eines realen Datensatzes
 * analysierten) Verschachtelung folgt, s. xjustiz_process_inhalt(). */
static void xjustiz_collect_dokument(xmlNodePtr dok_node,
		XJustizCtx *ctx, GPtrArray *out_dateien) {
	xmlXPathObjectPtr xpath_anzeige = NULL;
	xmlXPathObjectPtr xpath_datei = NULL;
	gchar *anzeigename = NULL;
	gint n_pdf_in_dok = 0;
	gdouble erste = 0;
	gdouble letzte = 0;
	gboolean hat_erste = FALSE;
	gboolean hat_letzte = FALSE;

	xmlXPathSetContextNode(dok_node, ctx->xpath);

	xpath_anzeige = xmlXPathEvalExpression(
			(xmlChar const*) ".//*[local-name()='anzeigename']", ctx->xpath);
	if (xpath_anzeige && xpath_anzeige->nodesetval
			&& xpath_anzeige->nodesetval->nodeNr > 0)
		anzeigename = xjustiz_node_text(
				xpath_anzeige->nodesetval->nodeTab[0]);
	if (xpath_anzeige)
		xmlXPathFreeObject(xpath_anzeige);

	hat_erste = xjustiz_dok_seitennummer(ctx,
			".//*[local-name()='ersteSeitennummer']", &erste);
	hat_letzte = xjustiz_dok_seitennummer(ctx,
			".//*[local-name()='letzteSeitennummer']", &letzte);

	xpath_datei = xmlXPathEvalExpression(
			(xmlChar const*) ".//*[local-name()='datei']"
					"/*[local-name()='dateiname']", ctx->xpath);

	if (xpath_datei && xpath_datei->nodesetval) {
		for (gint j = 0; j < xpath_datei->nodesetval->nodeNr; j++) {
			gchar *dateiname = xjustiz_node_text(
					xpath_datei->nodesetval->nodeTab[j]);
			XJustizDatei *d = NULL;

			if (!dateiname)
				continue;

			if (!str_has_suffix_ci(dateiname, ".pdf")) {
				g_free(dateiname);
				continue;
			}

			n_pdf_in_dok++;

			d = g_new0(XJustizDatei, 1);
			d->dateiname = dateiname; //Ownership übernommen

			//Seitennummern gelten für das Dokument, nicht für einzelne
			//Dateien daraus - bei mehreren PDFs nur der ersten zuordnen
			if (n_pdf_in_dok == 1 && hat_erste && hat_letzte
					&& letzte >= erste) {
				d->hat_seiten = TRUE;
				d->erste = erste;
				d->letzte = letzte;
				d->seiten = (erste == letzte) ? 1 :
						(gint) (letzte - erste + 0.5) + 1;
			}

			if (anzeigename)
				d->anzeigename = (n_pdf_in_dok > 1) ?
						g_strdup_printf("%s (%d)", anzeigename,
								n_pdf_in_dok) : g_strdup(anzeigename);
			else {
				//Fallback: Dateiname ohne Endung
				gchar *base = g_path_get_basename(d->dateiname);
				gchar *dot = strrchr(base, '.');

				if (dot)
					*dot = '\0';
				d->anzeigename = base;
			}

			g_ptr_array_add(out_dateien, d);
		}
	}
	if (xpath_datei)
		xmlXPathFreeObject(xpath_datei);

	g_free(anzeigename);
}

static XJustizNode* xjustiz_build_teilakte(xmlNodePtr teilakte,
		XJustizCtx *ctx);
static XJustizNode* xjustiz_build_akte(xmlNodePtr akte, XJustizCtx *ctx);
static gchar* xjustiz_akte_aktenzeichen(xmlNodePtr fsd);

/* Ist 'teilakte' ein Platzhalter (kein einziges Kindelement unter inhalt,
 * z.B. "Verweis auf: 50 Gs 3928/25") mit einem Aktenzeichen, das zu einer
 * <akte> der Nachricht passt, liefert das diese <akte>, sonst NULL. Die
 * Hauptakte verweist so auf ihre Beiakten; verglichen wird das Aktenzeichen
 * des Platzhalters (dasselbe Feld wie bei der Akte selbst), nicht dessen
 * Anzeigename. */
static xmlNodePtr xjustiz_platzhalter_ziel(XJustizCtx *ctx,
		xmlNodePtr teilakte) {
	xmlNodePtr fsd = xjustiz_find_child(teilakte,
			"xjustiz.fachspezifischeDaten");
	xmlNodePtr inhalt = fsd ? xjustiz_find_child(fsd, "inhalt") : NULL;
	xmlNodePtr ziel = NULL;
	gchar *az = NULL;

	if (!fsd)
		return NULL;

	for (xmlNodePtr c = inhalt ? inhalt->children : NULL; c; c = c->next)
		if (c->type == XML_ELEMENT_NODE)
			return NULL; //hat Inhalt: kein Platzhalter

	az = xjustiz_akte_aktenzeichen(fsd);
	if (az)
		ziel = g_hash_table_lookup(ctx->akten, az);
	g_free(az);

	return ziel;
}

/* Ergänzt Seitenangaben im Namen des Strukturpunkts und fügt Lücken-
 * Platzhalter ein; ersetzt node->label. Die Seitenzahl ist die Summe der
 * Seiten der Dokumente (laut Seitennummern) plus die der Unterpunkte.
 * Enthält der Knoten nur Dokumente und sind sie fortlaufend paginiert,
 * kommt der Blattbereich dazu: "Name - Bl. x - y (N Seiten)", und wo die
 * Nummern springen, ein Platzhalter "Bl. x - y fehlt" zwischen den
 * Dokumenten; den Dokumenten selbst wird ihre Blattzahl vorangestellt
 * ("x-y: Name", bei einem Blatt "x: Name"). Nicht paginiert (Nummern
 * fallen zurück oder jedes Dokument beginnt bei 1, wie in Vorakten) gibt
 * es nur "Name (N Seiten)" und unveränderte Dokumentnamen, und für
 * Knoten ohne verwertbare Seitennummern bleibt der Name unverändert. */
static void xjustiz_node_finalize(XJustizNode *node) {
	gboolean rueckgang = FALSE;
	gboolean alle_eins = TRUE;
	gint n_mit = 0;
	gint seiten = 0;
	gdouble prev = 0;
	gdouble erste_gesamt = 0;
	gdouble letzte_gesamt = 0;
	gchar *label = NULL;
	guint i;

	for (i = 0; i < node->dateien->len; i++) {
		XJustizDatei *d = g_ptr_array_index(node->dateien, i);

		if (!d->hat_seiten)
			continue;

		seiten += d->seiten;
		if (n_mit == 0)
			erste_gesamt = d->erste;
		else if (d->erste <= prev)
			rueckgang = TRUE;
		if (d->erste != 1.0)
			alle_eins = FALSE;
		prev = d->letzte;
		letzte_gesamt = d->letzte;
		n_mit++;
	}
	for (i = 0; i < node->children->len; i++)
		seiten += ((XJustizNode*) g_ptr_array_index(node->children, i))->seiten;
	node->seiten = seiten;

	if (n_mit > 0 && !rueckgang && !alle_eins && node->children->len == 0) {
		GPtrArray *alt = node->dateien;
		GPtrArray *neu = g_ptr_array_new_with_free_func(xjustiz_datei_free);
		gboolean hat_prev = FALSE;
		gchar von_s[G_ASCII_DTOSTR_BUF_SIZE];
		gchar bis_s[G_ASCII_DTOSTR_BUF_SIZE];

		g_ptr_array_set_free_func(alt, NULL); //Zeiger wandern nach 'neu'
		for (i = 0; i < alt->len; i++) {
			XJustizDatei *d = g_ptr_array_index(alt, i);

			if (d->hat_seiten) {
				if (hat_prev && d->erste - prev > 1.0) {
					gdouble von = floor(prev) + 1.0;
					gdouble bis = ceil(d->erste) - 1.0;

					if (von <= bis) {
						XJustizDatei *l = g_new0(XJustizDatei, 1);

						g_ascii_formatd(von_s, sizeof(von_s), "%g", von);
						g_ascii_formatd(bis_s, sizeof(bis_s), "%g", bis);
						l->ist_luecke = TRUE;
						l->anzeigename = (von == bis) ?
								g_strdup_printf("Bl. %s fehlt", von_s) :
								g_strdup_printf("Bl. %s - %s fehlt", von_s,
										bis_s);
						g_ptr_array_add(neu, l);
					}
				}
				prev = d->letzte;
				hat_prev = TRUE;

				//Blattzahl(en) vor den Namen des Dokuments: "x-y: Name",
				//bei nur einem Blatt "x: Name"
				{
					gchar e_s[G_ASCII_DTOSTR_BUF_SIZE];
					gchar l_s[G_ASCII_DTOSTR_BUF_SIZE];
					gchar *mit_blatt = NULL;

					g_ascii_formatd(e_s, sizeof(e_s), "%g", d->erste);
					g_ascii_formatd(l_s, sizeof(l_s), "%g", d->letzte);
					mit_blatt = (d->erste == d->letzte) ?
							g_strdup_printf("%s: %s", e_s, d->anzeigename) :
							g_strdup_printf("%s-%s: %s", e_s, l_s,
									d->anzeigename);
					g_free(d->anzeigename);
					d->anzeigename = mit_blatt;
				}
			}
			g_ptr_array_add(neu, d);
		}
		g_ptr_array_unref(alt);
		node->dateien = neu;

		g_ascii_formatd(von_s, sizeof(von_s), "%g", erste_gesamt);
		g_ascii_formatd(bis_s, sizeof(bis_s), "%g", letzte_gesamt);
		label = g_strdup_printf("%s - Bl. %s - %s (%d %s)", node->label,
				von_s, bis_s, seiten, (seiten == 1) ? "Seite" : "Seiten");
	}
	else if (seiten > 0)
		label = g_strdup_printf("%s (%d %s)", node->label, seiten,
				(seiten == 1) ? "Seite" : "Seiten");

	if (label) {
		g_free(node->label);
		node->label = label;
	}
}

/* Verarbeitet <inhalt> (direktes Kind von xjustiz.fachspezifischeDaten
 * einer akte oder teilakte): <dokument>-Kinder werden direkt ausgewertet
 * und in 'out_dateien' gesammelt, <teilakte>-Kinder rekursiv zu
 * Strukturpunkten gebaut. Ein Unter-Zweig ohne jedes Dokument (z.B. ein
 * reiner "Verweis auf: ..."-Platzhalter auf eine andere Akte, s. Kommentar
 * in xjustiz_import.h) liefert aus xjustiz_build_teilakte() NULL und wird
 * hier dadurch automatisch komplett ausgelassen - kein leerer Ordner im
 * Baum. Rückgabe: GPtrArray von XJustizNode* (nie NULL, ggf. leer). */
static GPtrArray* xjustiz_process_inhalt(xmlNodePtr inhalt,
		XJustizCtx *ctx, GPtrArray *out_dateien) {
	GPtrArray *children = g_ptr_array_new_with_free_func(xjustiz_node_free);
	xmlNodePtr c;

	if (!inhalt)
		return children;

	for (c = inhalt->children; c; c = c->next) {
		if (c->type != XML_ELEMENT_NODE || !c->name)
			continue;

		if (strcmp((gchar const*) c->name, "dokument") == 0)
			xjustiz_collect_dokument(c, ctx, out_dateien);
		else if (strcmp((gchar const*) c->name, "teilakte") == 0) {
			XJustizNode *node = xjustiz_build_teilakte(c, ctx);

			if (node)
				g_ptr_array_add(children, node);
		}
	}

	return children;
}

/* Baut aus einem <teilakte>-Element rekursiv einen Strukturpunkt - NULL,
 * wenn im gesamten Teilbaum kein einziges Dokument liegt (leerer
 * Platzhalter-Zweig). Benennung: anzeigename (bei einer teilakte laut
 * Analyse eines realen Datensatzes IMMER vorhanden) - als Absicherung
 * trotzdem ein Fallback für den (praktisch nicht erwarteten) Fall, daß es
 * doch einmal fehlt. */
static XJustizNode* xjustiz_build_teilakte(xmlNodePtr teilakte,
		XJustizCtx *ctx) {
	xmlNodePtr fsd = xjustiz_find_child(teilakte,
			"xjustiz.fachspezifischeDaten");
	xmlNodePtr inhalt = fsd ? xjustiz_find_child(fsd, "inhalt") : NULL;
	xmlNodePtr ziel = xjustiz_platzhalter_ziel(ctx, teilakte);
	gchar *anzeigename = NULL;
	GPtrArray *dateien = NULL;
	GPtrArray *children = NULL;
	XJustizNode *node = NULL;

	//Platzhalter auf eine (noch nicht eingebaute) Akte: stattdessen diese
	//Akte hier einfügen, "Verweis auf: ..." entfällt dabei
	if (ziel && !g_hash_table_contains(ctx->verbraucht, ziel))
		return xjustiz_build_akte(ziel, ctx);

	anzeigename = fsd ? xjustiz_child_text(fsd, "anzeigename") : NULL;
	dateien = g_ptr_array_new_with_free_func(xjustiz_datei_free);
	children = xjustiz_process_inhalt(inhalt, ctx, dateien);

	if (dateien->len == 0 && children->len == 0) {
		g_ptr_array_unref(dateien);
		g_ptr_array_unref(children);
		g_free(anzeigename);
		return NULL;
	}

	node = g_new0(XJustizNode, 1);
	node->label = anzeigename ? anzeigename : g_strdup("(ohne Bezeichnung)");
	node->dateien = dateien;
	node->children = children;
	xjustiz_node_finalize(node);

	return node;
}

/* Ermittelt das Aktenzeichen einer akte - akte/xjustiz.fachspezifischeDaten/
 * aktenzeichen/auswahl_aktenzeichen/aktenzeichen.freitext, robust gegenüber
 * einer fehlenden auswahl_aktenzeichen-Zwischenebene (für die
 * Benennung von Akten ohne anzeigename, s. xjustiz_build_akte() - NICHT zu
 * verwechseln mit nachrichtenkopf/absender/aktenzeichen, s.
 * xjustiz_parse_nachrichtenkopf(), das ist ein eigenes, flaches Element). */
static gchar* xjustiz_akte_aktenzeichen(xmlNodePtr fsd) {
	xmlNodePtr az = xjustiz_find_child(fsd, "aktenzeichen");
	xmlNodePtr auswahl = NULL;

	if (!az)
		return NULL;

	auswahl = xjustiz_find_child(az, "auswahl_aktenzeichen");

	return xjustiz_child_text(auswahl ? auswahl : az, "aktenzeichen.freitext");
}

/* Baut aus einem <akte>-Element rekursiv einen Strukturpunkt - NULL, wenn
 * im gesamten Teilbaum kein einziges Dokument liegt. Benennung:
 * anzeigename, falls vorhanden (bei der Hauptakte laut Analyse eines realen
 * Datensatzes der Fall) - sonst das eigene Aktenzeichen (bei Beiakten der
 * Fall - sie haben kein anzeigename, aber immer ein eigenes, von der
 * Hauptakte verschiedenes Aktenzeichen), als letzter Fallback schlicht
 * "Akte" (praktisch nicht erwartet). */
static XJustizNode* xjustiz_build_akte(xmlNodePtr akte, XJustizCtx *ctx) {
	xmlNodePtr fsd = xjustiz_find_child(akte, "xjustiz.fachspezifischeDaten");
	xmlNodePtr inhalt = fsd ? xjustiz_find_child(fsd, "inhalt") : NULL;
	gchar *anzeigename = fsd ? xjustiz_child_text(fsd, "anzeigename") : NULL;
	GPtrArray *dateien = NULL;
	GPtrArray *children = NULL;
	XJustizNode *node = NULL;
	gchar *label = NULL;

	//vor dem Abstieg markieren: verhindert Doppeleinbau und Endlosschleifen
	//bei zirkulären Verweisen
	g_hash_table_add(ctx->verbraucht, akte);

	dateien = g_ptr_array_new_with_free_func(xjustiz_datei_free);
	children = xjustiz_process_inhalt(inhalt, ctx, dateien);

	if (dateien->len == 0 && children->len == 0) {
		g_ptr_array_unref(dateien);
		g_ptr_array_unref(children);
		g_free(anzeigename);
		return NULL;
	}

	if (anzeigename)
		label = anzeigename; //Ownership übernommen
	else {
		gchar *aktenzeichen = fsd ? xjustiz_akte_aktenzeichen(fsd) : NULL;

		label = aktenzeichen ? aktenzeichen : g_strdup("Akte");
	}

	node = g_new0(XJustizNode, 1);
	node->label = label;
	node->dateien = dateien;
	node->children = children;
	xjustiz_node_finalize(node);

	return node;
}

/* Liefert aus dem (ersten) Element "nachrichtenkopf" dessen
 * erstellungszeitpunkt (kompakt formatiert, s.
 * xjustiz_format_datum_kompakt()) und das Aktenzeichen des Absenders
 * (nachrichtenkopf/absender/aktenzeichen - beide direkte, einfache
 * Textkinder ohne weitere Verschachtelung, laut Analyse eines realen
 * Datensatzes) - für die Benennung des obersten Strukturpunkts in
 * xjustiz_import(). Beide *out-Parameter werden auf NULL gesetzt, wenn das
 * jeweilige Element fehlt oder "nachrichtenkopf" selbst nicht gefunden
 * wird (beides laut Spezifikation zulässig). */
static void xjustiz_parse_nachrichtenkopf(xmlXPathContextPtr ctx,
		gchar **out_datum, gchar **out_aktenzeichen) {
	xmlXPathObjectPtr xpath_nk = NULL;
	xmlNodePtr nk = NULL;

	*out_datum = NULL;
	*out_aktenzeichen = NULL;

	xpath_nk = xmlXPathEvalExpression(
			(xmlChar const*) "//*[local-name()='nachrichtenkopf']", ctx);
	if (xpath_nk && xpath_nk->nodesetval && xpath_nk->nodesetval->nodeNr > 0)
		nk = xpath_nk->nodesetval->nodeTab[0];

	if (nk) {
		gchar *zeitpunkt_roh = xjustiz_child_text(nk, "erstellungszeitpunkt");
		xmlNodePtr absender = xjustiz_find_child(nk, "absender");

		*out_datum = xjustiz_format_datum_kompakt(zeitpunkt_roh);
		g_free(zeitpunkt_roh);

		if (absender)
			*out_aktenzeichen = xjustiz_child_text(absender, "aktenzeichen");
	}

	if (xpath_nk)
		xmlXPathFreeObject(xpath_nk);
}

/* Wertet die xjustiz_nachricht.xml aus: baut rekursiv einen Baum aus
 * XJustizNode-Strukturpunkten, der die tatsächliche Akte/Teilakte/
 * Dokument-Hierarchie der Nachricht nachbildet (s. ausführlichen Kommentar
 * in xjustiz_import.h) - ein Strukturpunkt pro <akte> bzw. <teilakte>-
 * Element, SOFERN in dessen Teilbaum mindestens ein Dokument liegt (leere
 * Platzhalter-Zweige werden komplett ausgelassen, s.
 * xjustiz_build_akte()/xjustiz_build_teilakte()).
 *
 * Rückgabe: GPtrArray von XJustizNode* (eine pro <akte> unterhalb von
 * schriftgutobjekte, die mindestens ein Dokument enthält) - niemals NULL
 * bei Erfolg (ggf. mit len==0, wenn gar keine Dokumente referenziert sind).
 * NULL bei Fehler (*error gesetzt).
 *
 * *out_datum, *out_aktenzeichen (beide optional - NULL-Pointer werden
 * übergangen): für die Benennung des obersten, in xjustiz_import() neu
 * angelegten Strukturpunkts, s. xjustiz_parse_nachrichtenkopf().
 *
 * *out_is_xjustiz (optional, NULL-Pointer wird übergangen): TRUE, wenn die
 * XML mindestens ein Element "schriftgutobjekte" enthält - unabhängig
 * davon, ob darin referenzierte PDF-Dokumente gefunden wurden. FALSE
 * bedeutet: die markierte Datei ist erkennbar KEIN XJustiz-Datensatz. */
static GPtrArray* xjustiz_parse_nachricht(gchar const *xml, gsize len,
		gchar **out_datum, gchar **out_aktenzeichen,
		gboolean *out_is_xjustiz, GError **error) {
	xmlDocPtr doc = NULL;
	xmlXPathContextPtr ctx = NULL;
	xmlXPathObjectPtr xpath_sgo = NULL;
	GPtrArray *arr_akten = NULL;

	if (out_datum)
		*out_datum = NULL;
	if (out_aktenzeichen)
		*out_aktenzeichen = NULL;
	if (out_is_xjustiz)
		*out_is_xjustiz = FALSE;

	doc = xmlReadMemory(xml, (int) len, "xjustiz_nachricht.xml", NULL,
			XML_PARSE_NOBLANKS | XML_PARSE_NONET);
	if (!doc) {
		g_set_error(error, ZOND_ERROR, 0,
				"%s\nDatei konnte nicht als XML gelesen werden", __func__);
		return NULL;
	}

	ctx = xmlXPathNewContext(doc);
	if (!ctx) {
		xmlFreeDoc(doc);
		g_set_error(error, ZOND_ERROR, 0, "%s\nxmlXPathNewContext fehlgeschlagen",
				__func__);
		return NULL;
	}

	xpath_sgo = xmlXPathEvalExpression(
			(xmlChar const*) "//*[local-name()='schriftgutobjekte']", ctx);
	if (out_is_xjustiz)
		*out_is_xjustiz = xpath_sgo && xpath_sgo->nodesetval
				&& xpath_sgo->nodesetval->nodeNr > 0;

	arr_akten = g_ptr_array_new_with_free_func(xjustiz_node_free);

	if (xpath_sgo && xpath_sgo->nodesetval && xpath_sgo->nodesetval->nodeNr > 0) {
		//schriftgutobjekte enthält laut Analyse eines realen Datensatzes
		//eine FLACHE Liste von <akte>-Elementen (Haupt- und Beiakten
		//nicht verschachtelt, sondern über aktenreferenzen verknüpft -
		//s. Kommentar in xjustiz_import.h) - hier daher bewußt nur die
		//direkten <akte>-Kinder, nicht rekursiv gesucht.
		xmlNodePtr sgo = xpath_sgo->nodesetval->nodeTab[0];
		xmlNodePtr c;
		XJustizCtx xctx = { ctx, g_hash_table_new_full(g_str_hash,
				g_str_equal, g_free, NULL), g_hash_table_new(g_direct_hash,
				g_direct_equal), g_hash_table_new(g_direct_hash,
				g_direct_equal) };
		xmlXPathObjectPtr xpath_ta = NULL;

		//Akten nach Aktenzeichen erfassen und die Ziele von Platzhaltern
		//vorab ermitteln - damit hängt das Ergebnis nicht von der
		//Reihenfolge der Akten in der Nachricht ab
		for (c = sgo->children; c; c = c->next) {
			if (c->type == XML_ELEMENT_NODE && c->name
					&& strcmp((gchar const*) c->name, "akte") == 0) {
				xmlNodePtr fsd = xjustiz_find_child(c,
						"xjustiz.fachspezifischeDaten");
				gchar *az = fsd ? xjustiz_akte_aktenzeichen(fsd) : NULL;

				if (az && !g_hash_table_contains(xctx.akten, az))
					g_hash_table_insert(xctx.akten, az, c);
				else
					g_free(az);
			}
		}

		xpath_ta = xmlXPathEvalExpression(
				(xmlChar const*) "//*[local-name()='teilakte']", ctx);
		if (xpath_ta && xpath_ta->nodesetval)
			for (gint i = 0; i < xpath_ta->nodesetval->nodeNr; i++) {
				xmlNodePtr ziel = xjustiz_platzhalter_ziel(&xctx,
						xpath_ta->nodesetval->nodeTab[i]);

				if (ziel)
					g_hash_table_add(xctx.referenziert, ziel);
			}
		if (xpath_ta)
			xmlXPathFreeObject(xpath_ta);

		//1. Durchlauf: Akten, auf die kein Platzhalter verweist (werden
		//dort eingebaut). 2. Durchlauf: Rest - Akten, die doch nicht über
		//einen Platzhalter erreicht wurden (z.B. zirkuläre Verweise),
		//damit keine Dokumente verloren gehen.
		for (gint durchlauf = 1; durchlauf <= 2; durchlauf++)
			for (c = sgo->children; c; c = c->next) {
				XJustizNode *node = NULL;

				if (c->type != XML_ELEMENT_NODE || !c->name
						|| strcmp((gchar const*) c->name, "akte") != 0)
					continue;
				if (g_hash_table_contains(xctx.verbraucht, c))
					continue;
				if (durchlauf == 1 && g_hash_table_contains(xctx.referenziert,
						c))
					continue;

				node = xjustiz_build_akte(c, &xctx);
				if (node)
					g_ptr_array_add(arr_akten, node);
			}

		g_hash_table_unref(xctx.akten);
		g_hash_table_unref(xctx.referenziert);
		g_hash_table_unref(xctx.verbraucht);
	}
	if (xpath_sgo)
		xmlXPathFreeObject(xpath_sgo);

	if (out_datum || out_aktenzeichen) {
		gchar *datum = NULL;
		gchar *aktenzeichen = NULL;

		xjustiz_parse_nachrichtenkopf(ctx, &datum, &aktenzeichen);
		if (out_datum)
			*out_datum = datum;
		else
			g_free(datum);
		if (out_aktenzeichen)
			*out_aktenzeichen = aktenzeichen;
		else
			g_free(aktenzeichen);
	}

	xmlXPathFreeContext(ctx);
	xmlFreeDoc(doc);

	return arr_akten;
}

/* Sammelt rekursiv ALLE Dokument-Dateien aus dem Teilbaum von 'node' in
 * 'out' - für das Vorab-Laden nicht geladener SeaDrive-Dateien (s.
 * xjustiz_dateien_laden()), wo unabhängig von der Akte/Teilakte-Hierarchie
 * die komplette flache Liste aller referenzierten PDFs benötigt wird.
 * 'out' enthält nur GEBORGTE XJustizDatei*-Zeiger (Ownership bleibt bei den
 * XJustizNode-Bäumen) - der Aufrufer darf 'out' daher nur mit einem
 * einfachen g_ptr_array_new() (ohne Free-Func) anlegen und mit
 * g_ptr_array_unref() wieder freigeben, ohne die Einträge selbst
 * anzurühren. */
static void xjustiz_collect_alle_dateien(XJustizNode *node, GPtrArray *out) {
	guint i;

	for (i = 0; i < node->dateien->len; i++) {
		XJustizDatei *d = g_ptr_array_index(node->dateien, i);

		if (!d->ist_luecke)
			g_ptr_array_add(out, d);
	}
	for (i = 0; i < node->children->len; i++)
		xjustiz_collect_alle_dateien(g_ptr_array_index(node->children, i),
				out);
}

/* ============================================================================
 * ÖFFENTLICHE FUNKTION
 * ========================================================================== */

/* Lädt nicht geladene SeaDrive-Dateien vorab (Fortschrittsfenster mit
 * Abbrechen): mit sfp_xml die oberste Datei der Nachricht, sonst die in
 * arr_dok referenzierten PDFs (FLACHE Liste, s.
 * xjustiz_collect_alle_dateien()) im Projektverzeichnis (dir_prefix:
 * Verzeichnisanteil der Nachricht). FALSE: Abbruch oder Download
 * gescheitert (gemeldet). */
static gboolean xjustiz_dateien_laden(Projekt *zond, SondFilePart *sfp_xml,
		GPtrArray *arr_dok, gchar const *dir_prefix) {
	gchar const *root_dir = NULL;
	GPtrArray *pfade = NULL;
	gboolean ok = TRUE;

	if (!zond->treeview[BAUM_FS])
		return TRUE;
	root_dir = sond_treeviewfm_get_root(SOND_TREEVIEWFM(zond->treeview[BAUM_FS]));
	if (!root_dir)
		return TRUE;

	pfade = g_ptr_array_new_with_free_func(g_free);
	if (sfp_xml) {
		SondFilePart *sfp_root = sfp_xml;
		SondFilePart *sfp_up = NULL;

		while ((sfp_up = sond_file_part_get_parent(sfp_root)))
			sfp_root = sfp_up;
		if (sond_file_part_get_path(sfp_root))
			g_ptr_array_add(pfade, g_strconcat(root_dir, "/",
					sond_file_part_get_path(sfp_root), NULL));
	}
	else {
		for (guint i = 0; arr_dok && i < arr_dok->len; i++) {
			XJustizDatei *d = g_ptr_array_index(arr_dok, i);

			g_ptr_array_add(pfade, (dir_prefix && dir_prefix[0] != '\0') ?
					g_strconcat(root_dir, "/", dir_prefix, "/", d->dateiname,
					NULL) : g_strconcat(root_dir, "/", d->dateiname, NULL));
		}
	}

	if (pfade->len > 0)
		ok = sond_seadrive_load_sync(GTK_WINDOW(zond->app_window), pfade);
	g_ptr_array_unref(pfade);

	return ok;
}

/* Bindet EIN referenziertes PDF-Dokument an (anchor_id/child wie bei
 * zond_dbase_insert_node) - sond_file_part_create() als Geschwister von
 * xjustiz_nachricht.xml, s. ausführlichen Kommentar in xjustiz_import.h.
 * Fehler (Datei nicht auflösbar, DB-Fehler) werden - wie beim normalen
 * Anbinden (zond_treeview_anbinden_rekursiv, zond_treeview.c) - nicht als
 * Abbruch der gesamten Operation behandelt, sondern einzeln in
 * 'nicht_gefunden' vermerkt.
 *
 * *out_new_id: 0, wenn KEIN neuer Knoten angelegt wurde (Datei nicht
 * gefunden ODER schon anderswo angebunden - in *n_vorhanden gezählt);
 * sonst die ID des neuen Anker-Knotens, für den Aufrufer, um den nächsten
 * Geschwisterknoten daran anzuhängen. */
static void xjustiz_insert_datei(Projekt *zond, XJustizDatei *d,
		gint anchor_id, gboolean child, SondFilePart *sfp_parent_xml,
		gchar const *dir_prefix, gint *n_angebunden, gint *n_vorhanden,
		GPtrArray *nicht_gefunden, gint *out_new_id) {
	gchar *sibling_path = NULL;
	SondFilePart *sfp_pdf = NULL;
	gchar *filepart = NULL;
	gint ID_file_part = 0;
	gint baum_inhalt_file = 0;
	gint new_node_id = 0;
	GError *local_error = NULL;
	gint rc;

	*out_new_id = 0;

	sibling_path = (dir_prefix[0] != '\0') ?
			g_strconcat(dir_prefix, "/", d->dateiname, NULL) :
			g_strdup(d->dateiname);

	sfp_pdf = sond_file_part_create(sfp_parent_xml, sibling_path,
			&local_error);
	g_free(sibling_path);
	if (!sfp_pdf) {
		g_clear_error(&local_error);
		g_ptr_array_add(nicht_gefunden, g_strdup(d->dateiname));
		return;
	}

	filepart = sond_file_part_get_filepart(sfp_pdf);
	g_object_unref(sfp_pdf);

	rc = zond_dbase_get_section(zond->dbase_zond->zond_dbase_work,
			filepart, NULL, &ID_file_part, &local_error);
	if (rc) {
		g_free(filepart);
		g_clear_error(&local_error);
		g_ptr_array_add(nicht_gefunden, g_strdup(d->dateiname));
		return;
	}

	if (!ID_file_part) {
		//Datei noch nicht in zond_dbase - anlegen, Label = anzeigename
		rc = zond_treeview_insert_file_part_in_db(zond, filepart,
				d->anzeigename, "pdf", &ID_file_part, &local_error);
		if (rc) {
			g_free(filepart);
			g_clear_error(&local_error);
			g_ptr_array_add(nicht_gefunden, g_strdup(d->dateiname));
			return;
		}
	} else {
		//schon in zond_dbase - ggf. schon angebunden?
		rc = zond_dbase_find_baum_inhalt_file(
				zond->dbase_zond->zond_dbase_work, ID_file_part,
				&baum_inhalt_file, NULL, NULL, &local_error);
		if (rc) {
			g_free(filepart);
			g_clear_error(&local_error);
			g_ptr_array_add(nicht_gefunden, g_strdup(d->dateiname));
			return;
		}
		if (baum_inhalt_file) {
			g_free(filepart);
			if (n_vorhanden)
				(*n_vorhanden)++;
			return; //schon angebunden: Anker nicht vorrücken
		}
	}
	g_free(filepart);

	//Anker-Knoten: bewußt ohne node_text/icon_name, s. Erklärung an
	//zond_treeview_leaf_anbinden() (zond_treeview.c)
	new_node_id = zond_dbase_insert_node(
			zond->dbase_zond->zond_dbase_work, anchor_id, child,
			ZOND_DBASE_TYPE_BAUM_INHALT_FILE, ID_file_part, NULL, NULL,
			NULL, NULL, NULL, &local_error);
	if (new_node_id == -1) {
		g_clear_error(&local_error);
		g_ptr_array_add(nicht_gefunden, g_strdup(d->dateiname));
		return;
	}

	if (n_angebunden)
		(*n_angebunden)++;

	*out_new_id = new_node_id;
}

/* Legt für 'node' einen Strukturpunkt an (anchor_id/child wie bei
 * zond_dbase_insert_node) und hängt darunter - als Geschwister, in dieser
 * Reihenfolge - erst die direkt an 'node' hängenden Dokumente, dann die
 * Unter-Strukturpunkte (rekursiv) ein. Bildet damit die Akte/Teilakte/
 * Dokument-Hierarchie 1:1 nach, s. ausführlichen Kommentar in
 * xjustiz_import.h.
 *
 * *out_struktur_id (optional): ID des hier angelegten Strukturpunkts - für
 * den Aufrufer, um anschließend WEITERE Geschwister von 'node' selbst
 * anzuhängen (anchor_id=*out_struktur_id, child=FALSE) - unabhängig davon,
 * wie tief/komplex der unter 'node' neu aufgebaute Teilbaum ist.
 *
 * Rückgabe: 0 Erfolg (auch bei einzelnen fehlgeschlagenen Dokumenten, s.
 * xjustiz_insert_datei()), -1 bei einem Datenbankfehler beim Anlegen DIESES
 * Strukturpunkts selbst (*error gesetzt - Abbruch der gesamten Operation,
 * wie auch beim obersten Strukturpunkt in xjustiz_import()). */
static gint xjustiz_insert_node(Projekt *zond, XJustizNode *node,
		gint anchor_id, gboolean child, SondFilePart *sfp_parent_xml,
		gchar const *dir_prefix, gint *n_angebunden, gint *n_vorhanden,
		GPtrArray *nicht_gefunden, gint *out_struktur_id, GError **error) {
	gint struktur_id;
	gint inner_anchor;
	gboolean inner_child;
	guint i;

	struktur_id = zond_dbase_insert_node(zond->dbase_zond->zond_dbase_work,
			anchor_id, child, ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL,
			zond->icon[ICON_ORDNER].icon_name, node->label, NULL, error);
	if (struktur_id == -1)
		return -1;

	inner_anchor = struktur_id;
	inner_child = TRUE;

	for (i = 0; i < node->dateien->len; i++) {
		XJustizDatei *d = g_ptr_array_index(node->dateien, i);
		gint new_id = 0;

		if (d->ist_luecke) {
			//fehlende Blätter: Strukturpunkt mit Fehlersymbol
			gint luecke_id = zond_dbase_insert_node(
					zond->dbase_zond->zond_dbase_work, inner_anchor,
					inner_child, ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL,
					zond->icon[ICON_WICHTIG].icon_name, d->anzeigename, NULL,
					error);

			if (luecke_id == -1)
				return -1;
			inner_anchor = luecke_id;
			inner_child = FALSE;
			continue;
		}

		xjustiz_insert_datei(zond, d, inner_anchor, inner_child,
				sfp_parent_xml, dir_prefix, n_angebunden, n_vorhanden,
				nicht_gefunden, &new_id);
		if (new_id) {
			inner_anchor = new_id;
			inner_child = FALSE;
		}
	}

	for (i = 0; i < node->children->len; i++) {
		XJustizNode *sub = g_ptr_array_index(node->children, i);
		gint sub_struktur_id = 0;

		if (xjustiz_insert_node(zond, sub, inner_anchor, inner_child,
				sfp_parent_xml, dir_prefix, n_angebunden, n_vorhanden,
				nicht_gefunden, &sub_struktur_id, error))
			return -1;

		inner_anchor = sub_struktur_id;
		inner_child = FALSE;
	}

	if (out_struktur_id)
		*out_struktur_id = struktur_id;

	return 0;
}

gint xjustiz_import(Projekt *zond, gboolean child, gint *n_angebunden,
		gint *n_vorhanden, GPtrArray **arr_nicht_gefunden, GError **error) {
	gint rc = 0;
	GtkTreeIter iter_cursor = { 0 };
	GtkTreeIter iter_anchor = { 0 };
	gint anchor_id = 0;
	gboolean in_link = FALSE;
	SondFilePart *sfp_xml = NULL;
	SondFilePart *sfp_parent_xml = NULL; //geborgte Referenz (gehört sfp_xml)
	gchar const *xml_path = NULL;
	gchar *dir_prefix = NULL; //Verzeichnisanteil von xml_path (ggf. leer)
	GBytes *xml_bytes = NULL;
	gconstpointer xml_data = NULL;
	gsize xml_len = 0;
	GPtrArray *arr_top = NULL; //XJustizNode* - je ein Strukturpunkt pro akte
	GPtrArray *nicht_gefunden = NULL;
	gchar *datum = NULL;
	gchar *aktenzeichen = NULL;
	gchar *struktur_label = NULL;
	gint struktur_id = 0;
	gboolean is_xjustiz = FALSE;

	if (n_angebunden)
		*n_angebunden = 0;
	if (n_vorhanden)
		*n_vorhanden = 0;
	if (arr_nicht_gefunden)
		*arr_nicht_gefunden = NULL;

	if (zond_baum_aktuell(zond) != BAUM_INHALT) {
		g_set_error(error, ZOND_ERROR, 0,
				"Bitte zuerst im Bestandsverzeichnis die Zielposition "
				"markieren (wie beim normalen Einfügen).");
		return -1;
	}

	rc = zond_treeview_get_anchor(zond, zond_baum_aktuell(zond), &child, &iter_cursor, &iter_anchor,
			&anchor_id, &in_link, error);
	if (rc)
		return -1;
	if (in_link) {
		g_set_error(error, ZOND_ERROR, 0,
				"Zielposition liegt in einem Link - bitte eine andere "
				"Position im Bestandsverzeichnis markieren.");
		return -1;
	}
	if (!anchor_id) {
		g_set_error(error, ZOND_ERROR, 0,
				"Keine gültige Zielposition im Bestandsverzeichnis gefunden.");
		return -1;
	}

	//Ausgangspunkt ist die xjustiz_nachricht.xml, ausgewählt über den
	//container-bewussten Dialog filepart_oeffnen() (project.c/.h) - kann,
	//anders als der normale GTK-Dateiauswahldialog, auch in eine noch nicht
	//entpackte ZIP hineinsehen. Eine Vorauswahl im Dateiverzeichnis (BAUM_FS)
	//wäre nicht praktikabel, da diese Selektion verloren geht, sobald im
	//Bestandsverzeichnis die Zielposition markiert wird.
	sfp_xml = filepart_oeffnen(zond, error);
	if (!sfp_xml)
		return (error && *error) ? -1 : 1; //1: Dialog abgebrochen

	//nicht geladene SeaDrive-Datei: erst laden (Abbruch oder Fehlschlag: still
	//beenden, der Fehler ist gemeldet)
	if (!xjustiz_dateien_laden(zond, sfp_xml, NULL, NULL)) {
		g_object_unref(sfp_xml);

		return 1;
	}

	//Lesen über die sond_file_part-Maschinerie statt eigener libzip-
	//Aufrufe - deckt Dateisystem/ZIP/etc. einheitlich und Windows-
	//Long-Path-sicher ab (sond_file_helper.h-Wrapper stecken bereits in
	//sond_file_part_get_bytes(), s. sond_fileparts.c).
	xml_bytes = sond_file_part_get_bytes(sfp_xml, error);
	if (!xml_bytes) {
		g_object_unref(sfp_xml);
		return -1;
	}

	xml_data = g_bytes_get_data(xml_bytes, &xml_len);

	arr_top = xjustiz_parse_nachricht((gchar const*) xml_data, xml_len,
			&datum, &aktenzeichen, &is_xjustiz, error);
	g_bytes_unref(xml_bytes);
	if (!arr_top) {
		g_free(datum);
		g_free(aktenzeichen);
		g_object_unref(sfp_xml);
		return -1;
	}

	//Prüfung, ob xjustiz-Datensatz, sonst Meldung - statt
	//stillschweigend 0 Dokumente anzubinden.
	if (!is_xjustiz) {
		g_free(datum);
		g_free(aktenzeichen);
		g_ptr_array_unref(arr_top);
		g_object_unref(sfp_xml);
		g_set_error(error, ZOND_ERROR, 0,
				"Die markierte Datei ist kein XJustiz-Datensatz (kein "
				"Element 'schriftgutobjekte' gefunden).");
		return -1;
	}

	if (arr_top->len == 0) {
		g_free(datum);
		g_free(aktenzeichen);
		g_ptr_array_unref(arr_top);
		g_object_unref(sfp_xml);
		g_set_error(error, ZOND_ERROR, 0,
				"xjustiz_nachricht.xml enthält keine referenzierten "
				"PDF-Dokumente.");
		return -1;
	}

	/* Verzeichnis/Container von xjustiz_nachricht.xml bestimmen - die im
	 * gleichen Verzeichnis/Container befindlichen Dateien laut
	 * xml-Datensatz werden angebunden. Die
	 * PDF-Dokumente werden weiter unten als GESCHWISTER von
	 * xjustiz_nachricht.xml gesucht: sfp_parent_xml (Elternteil von
	 * sfp_xml - NULL, wenn xjustiz_nachricht.xml direkt im Projekt-
	 * verzeichnis bzw. direkt an der ZIP-Wurzel liegt) legt den Container
	 * fest, dir_prefix (Verzeichnisanteil von sond_file_part_get_path(
	 * sfp_xml) VOR dem letzten '/', bzw. leer) die Position darin -
	 * sond_file_part_create(sfp_parent_xml, dir_prefix + "/" + dateiname)
	 * findet so pro referenzierter PDF-Datei denselben Ort, unabhängig
	 * davon, ob es sich um einen Dateisystem-Unterordner oder ein
	 * ZIP-Unterverzeichnis handelt - EINE einheitliche Auflösung statt
	 * getrennter Dateisystem-/ZIP-Logik. */
	sfp_parent_xml = sond_file_part_get_parent(sfp_xml);
	xml_path = sond_file_part_get_path(sfp_xml);
	{
		gchar *last_slash = strrchr(xml_path, '/');

		dir_prefix = last_slash ?
				g_strndup(xml_path, last_slash - xml_path) : g_strdup("");
	}

	//die referenzierten PDFs im Projektverzeichnis (nicht in einem Container,
	//dessen Datei schon geladen ist) vorab laden: sie werden unten zur
	//Typerkennung gelesen und würden sonst als "nicht gefunden" gemeldet.
	//arr_top ist ein Baum - für das Vorab-Laden wird eine flache, nur
	//geborgte Liste aller Dokumente benötigt, s.
	//xjustiz_collect_alle_dateien().
	if (!sfp_parent_xml) {
		GPtrArray *flach = g_ptr_array_new(); //nur geborgte Zeiger
		gboolean ok;

		for (guint i = 0; i < arr_top->len; i++)
			xjustiz_collect_alle_dateien(g_ptr_array_index(arr_top, i),
					flach);
		ok = xjustiz_dateien_laden(zond, NULL, flach, dir_prefix);
		g_ptr_array_unref(flach);

		if (!ok) {
			g_free(datum);
			g_free(aktenzeichen);
			g_free(dir_prefix);
			g_ptr_array_unref(arr_top);
			g_object_unref(sfp_xml);

			return 1;
		}
	}

	/* Benennung des gleich anzulegenden obersten Strukturpunkts (s.u.): aus
	 * dem Nachrichtenkopf, Erstellungszeitpunkt (kompakt, z.B. "20260805")
	 * und Aktenzeichen des Absenders (z.B. "51 KLs 11/26"), getrennt durch
	 * " Az. ". Beide Felder laut Spezifikation optional - Fallback auf das
	 * jeweils vorhandene Feld, oder falls beide fehlen ein fester
	 * Platzhalter statt einer leeren Beschriftung. */
	if (datum && aktenzeichen)
		struktur_label = g_strdup_printf("%s Az. %s", datum, aktenzeichen);
	else if (aktenzeichen)
		struktur_label = g_strdup_printf("Az. %s", aktenzeichen);
	else if (datum)
		struktur_label = g_strdup(datum);
	else
		struktur_label = g_strdup("XJustiz-Import");
	g_free(datum);
	g_free(aktenzeichen);

	nicht_gefunden = g_ptr_array_new_with_free_func(g_free);

	rc = zond_dbase_begin(zond->dbase_zond->zond_dbase_work, error);
	if (rc) {
		g_free(struktur_label);
		g_free(dir_prefix);
		g_ptr_array_unref(nicht_gefunden);
		g_ptr_array_unref(arr_top);
		g_object_unref(sfp_xml);
		return -1;
	}

	/* In das Bestandsverzeichnis wird an der gewählten Stelle zunächst EIN
	 * oberster Strukturpunkt eingefügt; darunter wird anschließend die
	 * komplette Akte/Teilakte/Dokument-Hierarchie der Nachricht rekursiv
	 * nachgebildet (xjustiz_insert_node(), je ein Strukturpunkt pro akte/
	 * teilakte mit Inhalt, Dokumente als deren unmittelbare Kinder). */
	struktur_id = zond_dbase_insert_node(zond->dbase_zond->zond_dbase_work,
			anchor_id, child, ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL,
			zond->icon[ICON_ORDNER].icon_name, struktur_label, NULL, error);
	g_free(struktur_label);
	if (struktur_id == -1) {
		zond_dbase_rollback(zond->dbase_zond->zond_dbase_work, error);
		g_free(dir_prefix);
		g_ptr_array_unref(nicht_gefunden);
		g_ptr_array_unref(arr_top);
		g_object_unref(sfp_xml);
		return -1;
	}

	anchor_id = struktur_id;
	child = TRUE;

	/* Pro Akte (oberste Ebene des Baums arr_top): rekursiv als Geschwister
	 * unter dem soeben angelegten obersten Strukturpunkt einfügen. sfp_xml
	 * (und damit sfp_parent_xml) muß bis zum Ende am Leben bleiben, s.
	 * Variablendeklaration oben. */
	for (guint i = 0; i < arr_top->len; i++) {
		XJustizNode *akte_node = g_ptr_array_index(arr_top, i);
		gint sub_struktur_id = 0;

		rc = xjustiz_insert_node(zond, akte_node, anchor_id, child,
				sfp_parent_xml, dir_prefix, n_angebunden, n_vorhanden,
				nicht_gefunden, &sub_struktur_id, error);
		if (rc) {
			zond_dbase_rollback(zond->dbase_zond->zond_dbase_work, NULL);
			g_free(dir_prefix);
			g_ptr_array_unref(nicht_gefunden);
			g_ptr_array_unref(arr_top);
			g_object_unref(sfp_xml);
			return -1;
		}

		anchor_id = sub_struktur_id;
		child = FALSE; //weitere Akten als Geschwister anhängen
	}

	g_free(dir_prefix);
	g_object_unref(sfp_xml);
	g_ptr_array_unref(arr_top);

	rc = zond_dbase_commit(zond->dbase_zond->zond_dbase_work, error);
	if (rc) {
		g_ptr_array_unref(nicht_gefunden);
		return -1;
	}

	rc = project_load_trees(zond, error);
	if (rc) {
		g_ptr_array_unref(nicht_gefunden);
		return -1;
	}

	if (arr_nicht_gefunden)
		*arr_nicht_gefunden = nicht_gefunden;
	else
		g_ptr_array_unref(nicht_gefunden);

	return 0;
}
