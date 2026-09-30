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

#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "../../sond_log_and_error.h"
#include "../../sond_file_helper.h"
#include "../../sond_fileparts.h"
#include "../../misc.h"

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
 * XML-AUSWERTUNG (xjustiz_nachricht.xml)
 * ========================================================================== */

typedef struct {
	gchar *dateiname; //roher Dateiname/-pfad laut XML
	gchar *anzeigename; //Label laut XML (ggf. Fallback aus Dateiname)
} XJustizDatei;

static void xjustiz_datei_free(gpointer data) {
	XJustizDatei *d = data;

	if (!d)
		return;

	g_free(d->dateiname);
	g_free(d->anzeigename);
	g_free(d);
}

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

/* Formatiert einen XML xs:dateTime-Wert (z.B. aus erstellungszeitpunkt,
 * etwa "2024-02-22T11:23:51.210+01:00") menschenlesbar ("TT.MM.JJJJ
 * hh:mm") für die Verwendung als Namensbestandteil (s. xjustiz_import()).
 * Bei Parsierungsfehler wird der Rohwert unverändert zurückgegeben -
 * robuster als ein hartes Scheitern für einen reinen Namensbestandteil.
 * NULL rein -> NULL raus. Rückgabe muß immer freigegeben werden. */
static gchar* xjustiz_format_zeitpunkt(gchar const *roh) {
	GDateTime *dt = NULL;
	gchar *formatted = NULL;

	if (!roh)
		return NULL;

	dt = g_date_time_new_from_iso8601(roh, NULL);
	if (!dt)
		return g_strdup(roh);

	formatted = g_date_time_format(dt, "%d.%m.%Y %H:%M");
	g_date_time_unref(dt);

	return formatted ? formatted : g_strdup(roh);
}

/* Wertet die xjustiz_nachricht.xml aus: liefert für jedes referenzierte
 * PDF-Dokument (schriftgutobjekte/dokument/datei/dateiname, gefiltert auf
 * ".pdf") ein XJustizDatei-Element mit dem zugehörigen anzeigename
 * (Fallback: Dateiname ohne Endung, falls anzeigename fehlt).
 *
 * Sucht bewußt per local-name() statt über registriertes Namespace-Präfix
 * (Namespace ist laut Spezifikation "http://www.xjustiz.de", Präfix aber
 * nicht einheitlich - manche Nachrichten nutzen "xjustiz:", andere "tns:"
 * o.ä.) - robust gegenüber Präfix-Varianten. Ebenso ".//" (statt fixer
 * Verschachtelungstiefe) für datei/anzeigename innerhalb eines dokument-
 * Knotens, da die Tiefe je Fachmodul variieren kann (s. Kommentar in
 * xjustiz_import.h).
 *
 * *out_produktname, *out_zeitpunkt (beide optional - NULL-Pointer werden
 * übergangen): Name des Produkts (laut Spezifikation unter grunddaten/
 * herstellerinformation/nameDesProdukts) und Erstellungszeitpunkt (laut
 * Spezifikation direkt unter nachrichtenkopf/erstellungszeitpunkt, roher
 * XML-Wert, noch nicht formatiert - s. xjustiz_format_zeitpunkt()) für die
 * Benennung des in xjustiz_import() neu angelegten Strukturpunkts. Beide
 * Felder sind laut Spezifikation optional - bleiben dann NULL, statt
 * lokal per fixem Pfad zu suchen (der je nach Fachmodul/Version abweichen
 * kann) wird bewußt dieselbe "//"+local-name()-Suche wie oben verwendet,
 * unabhängig von der genauen Verschachtelung.
 *
 * *out_is_xjustiz (23.09.2026, Nutzer-Vorgabe "Prüfung, ob xjustiz-
 * Datensatz, sonst Meldung" - optional, NULL-Pointer wird übergangen):
 * TRUE, wenn die XML mindestens ein Element "schriftgutobjekte" enthält -
 * unabhängig davon, ob darin referenzierte PDF-Dokumente gefunden wurden
 * (ein XJustiz-Datensatz ohne Dokumente ist laut Spezifikation zulässig
 * und bleibt dann einfach ein leeres arr - s. Aufrufer). FALSE bedeutet:
 * die markierte Datei ist erkennbar KEIN XJustiz-Datensatz. */
static GPtrArray* xjustiz_parse_nachricht(gchar const *xml, gsize len,
		gchar **out_produktname, gchar **out_zeitpunkt,
		gboolean *out_is_xjustiz, GError **error) {
	xmlDocPtr doc = NULL;
	xmlXPathContextPtr ctx = NULL;
	xmlXPathObjectPtr xpath_dok = NULL;
	GPtrArray *arr = NULL;

	if (out_produktname)
		*out_produktname = NULL;
	if (out_zeitpunkt)
		*out_zeitpunkt = NULL;
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

	if (out_is_xjustiz) {
		xmlXPathObjectPtr xpath_sgo = xmlXPathEvalExpression(
				(xmlChar const*) "//*[local-name()='schriftgutobjekte']", ctx);

		*out_is_xjustiz = xpath_sgo && xpath_sgo->nodesetval
				&& xpath_sgo->nodesetval->nodeNr > 0;
		if (xpath_sgo)
			xmlXPathFreeObject(xpath_sgo);
	}

	arr = g_ptr_array_new_with_free_func(xjustiz_datei_free);

	xpath_dok = xmlXPathEvalExpression(
			(xmlChar const*) "//*[local-name()='schriftgutobjekte']"
					"/*[local-name()='dokument']", ctx);
	if (!xpath_dok || !xpath_dok->nodesetval || xpath_dok->nodesetval->nodeNr == 0) {
		if (xpath_dok)
			xmlXPathFreeObject(xpath_dok);
		//Fallback, falls die Verschachtelung abweicht (z.B. anderes
		//Fachmodul) - direkt nach <dokument> suchen
		xpath_dok = xmlXPathEvalExpression(
				(xmlChar const*) "//*[local-name()='dokument']", ctx);
	}

	if (xpath_dok && xpath_dok->nodesetval) {
		for (gint i = 0; i < xpath_dok->nodesetval->nodeNr; i++) {
			xmlNodePtr dok_node = xpath_dok->nodesetval->nodeTab[i];
			xmlXPathObjectPtr xpath_anzeige = NULL;
			xmlXPathObjectPtr xpath_datei = NULL;
			gchar *anzeigename = NULL;
			gint n_pdf_in_dok = 0;

			xmlXPathSetContextNode(dok_node, ctx);

			xpath_anzeige = xmlXPathEvalExpression(
					(xmlChar const*) ".//*[local-name()='anzeigename']", ctx);
			if (xpath_anzeige && xpath_anzeige->nodesetval
					&& xpath_anzeige->nodesetval->nodeNr > 0)
				anzeigename = xjustiz_node_text(
						xpath_anzeige->nodesetval->nodeTab[0]);
			if (xpath_anzeige)
				xmlXPathFreeObject(xpath_anzeige);

			xpath_datei = xmlXPathEvalExpression(
					(xmlChar const*) ".//*[local-name()='datei']"
							"/*[local-name()='dateiname']", ctx);

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

					g_ptr_array_add(arr, d);
				}
			}
			if (xpath_datei)
				xmlXPathFreeObject(xpath_datei);

			g_free(anzeigename);
		}
	}
	if (xpath_dok)
		xmlXPathFreeObject(xpath_dok);

	if (out_produktname) {
		xmlXPathObjectPtr xpath_prod = xmlXPathEvalExpression(
				(xmlChar const*) "//*[local-name()='nameDesProdukts']", ctx);

		if (xpath_prod && xpath_prod->nodesetval
				&& xpath_prod->nodesetval->nodeNr > 0)
			*out_produktname = xjustiz_node_text(
					xpath_prod->nodesetval->nodeTab[0]);
		if (xpath_prod)
			xmlXPathFreeObject(xpath_prod);
	}

	if (out_zeitpunkt) {
		xmlXPathObjectPtr xpath_zeit = xmlXPathEvalExpression(
				(xmlChar const*) "//*[local-name()='erstellungszeitpunkt']",
				ctx);

		if (xpath_zeit && xpath_zeit->nodesetval
				&& xpath_zeit->nodesetval->nodeNr > 0)
			*out_zeitpunkt = xjustiz_node_text(
					xpath_zeit->nodesetval->nodeTab[0]);
		if (xpath_zeit)
			xmlXPathFreeObject(xpath_zeit);
	}

	xmlXPathFreeContext(ctx);
	xmlFreeDoc(doc);

	return arr;
}

/* ============================================================================
 * ÖFFENTLICHE FUNKTION
 * ========================================================================== */

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
	GPtrArray *arr_dok = NULL;
	GPtrArray *nicht_gefunden = NULL;
	gchar *produktname = NULL;
	gchar *zeitpunkt_roh = NULL;
	gchar *zeitpunkt = NULL;
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

	//Nutzer-Vorgabe (23.09.2026): Ausgangspunkt ist die xjustiz_nachricht.xml,
	//ausgewählt über den container-bewussten Dialog filepart_oeffnen()
	//(project.c/.h) - kann, anders als der normale GTK-Dateiauswahldialog,
	//auch in eine noch nicht entpackte ZIP hineinsehen. Ursprünglich war
	//statt dessen die Selektion im Dateiverzeichnis (BAUM_FS) vorgesehen -
	//das erwies sich aber als nicht praktikabel, da diese Selektion
	//verloren geht, sobald im Bestandsverzeichnis die Zielposition markiert
	//wird (Nutzer-Fund 23.09.2026).
	sfp_xml = filepart_oeffnen(zond, error);
	if (!sfp_xml)
		return (error && *error) ? -1 : 1; //1: Dialog abgebrochen

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

	arr_dok = xjustiz_parse_nachricht((gchar const*) xml_data, xml_len,
			&produktname, &zeitpunkt_roh, &is_xjustiz, error);
	g_bytes_unref(xml_bytes);
	if (!arr_dok) {
		g_free(produktname);
		g_free(zeitpunkt_roh);
		g_object_unref(sfp_xml);
		return -1;
	}

	//Nutzer-Vorgabe (23.09.2026): "Prüfung, ob xjustiz-Datensatz, sonst
	//Meldung" - statt stillschweigend 0 Dokumente anzubinden.
	if (!is_xjustiz) {
		g_free(produktname);
		g_free(zeitpunkt_roh);
		g_ptr_array_unref(arr_dok);
		g_object_unref(sfp_xml);
		g_set_error(error, ZOND_ERROR, 0,
				"Die markierte Datei ist kein XJustiz-Datensatz (kein "
				"Element 'schriftgutobjekte' gefunden).");
		return -1;
	}

	if (arr_dok->len == 0) {
		g_free(produktname);
		g_free(zeitpunkt_roh);
		g_ptr_array_unref(arr_dok);
		g_object_unref(sfp_xml);
		g_set_error(error, ZOND_ERROR, 0,
				"xjustiz_nachricht.xml enthält keine referenzierten "
				"PDF-Dokumente.");
		return -1;
	}

	/* Verzeichnis/Container von xjustiz_nachricht.xml bestimmen - Nutzer-
	 * Vorgabe (23.09.2026): "die im gleichen Verzeichnis/Container
	 * befindlichen Dateien lt. xml-Datensatz [werden] angebunden". Die
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

	/* Benennung des gleich anzulegenden Strukturpunkts (s.u.) - Nutzer-
	 * Vorgabe (22.09.2026): "Zur Benennung: Aus dem Nachrichtenkopf: Name
	 * des Produkts und Erstellungszeitpunkt." Beide Felder laut
	 * Spezifikation optional - Fallback auf das jeweils vorhandene Feld,
	 * oder falls beide fehlen ein fester Platzhalter statt einer leeren
	 * Beschriftung. */
	zeitpunkt = xjustiz_format_zeitpunkt(zeitpunkt_roh);
	g_free(zeitpunkt_roh);

	if (produktname && zeitpunkt)
		struktur_label = g_strdup_printf("%s %s", produktname, zeitpunkt);
	else if (produktname)
		struktur_label = g_strdup(produktname);
	else if (zeitpunkt)
		struktur_label = g_strdup(zeitpunkt);
	else
		struktur_label = g_strdup("XJustiz-Import");
	g_free(produktname);
	g_free(zeitpunkt);

	nicht_gefunden = g_ptr_array_new_with_free_func(g_free);

	rc = zond_dbase_begin(zond->dbase_zond->zond_dbase_work, error);
	if (rc) {
		g_free(struktur_label);
		g_free(dir_prefix);
		g_ptr_array_unref(nicht_gefunden);
		g_ptr_array_unref(arr_dok);
		g_object_unref(sfp_xml);
		return -1;
	}

	/* Nutzer-Vorgabe (22.09.2026): "daß in das Bestandsverzeichnis an der
	 * gewählten Stelle ein Strukturpunkt eingefügt wird, in den die
	 * einzelnen Dateien eingefügt werden" - statt die Dokumente direkt an
	 * der markierten Stelle anzubinden, wird hier zuerst EIN neuer
	 * Strukturpunkt dort eingefügt; anchor_id/child werden anschließend so
	 * umgebogen, daß alle folgenden Dokumente als dessen Kinder (erstes
	 * Dokument) bzw. dessen Geschwister (weitere Dokumente, wie schon
	 * bisher) landen. */
	struktur_id = zond_dbase_insert_node(zond->dbase_zond->zond_dbase_work,
			anchor_id, child, ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL,
			zond->icon[ICON_ORDNER].icon_name, struktur_label, NULL, error);
	g_free(struktur_label);
	if (struktur_id == -1) {
		zond_dbase_rollback(zond->dbase_zond->zond_dbase_work, error);
		g_free(dir_prefix);
		g_ptr_array_unref(nicht_gefunden);
		g_ptr_array_unref(arr_dok);
		g_object_unref(sfp_xml);
		return -1;
	}

	anchor_id = struktur_id;
	child = TRUE;

	/* Pro Dokument: Fehler (Datei nicht auflösbar, DB-Fehler) werden - wie
	 * beim normalen Anbinden (zond_treeview_anbinden_rekursiv,
	 * zond_treeview.c) - nicht als Abbruch der gesamten Operation
	 * behandelt, sondern einzeln vermerkt; die übrigen Dokumente werden
	 * trotzdem angebunden. sfp_xml (und damit sfp_parent_xml) muß bis zum
	 * Ende der Schleife am Leben bleiben, s. Variablendeklaration oben. */
	for (guint i = 0; i < arr_dok->len; i++) {
		XJustizDatei *d = g_ptr_array_index(arr_dok, i);
		gchar *sibling_path = NULL;
		SondFilePart *sfp_pdf = NULL;
		gchar *filepart = NULL;
		gint ID_file_part = 0;
		gint baum_inhalt_file = 0;
		gint new_node_id = 0;
		GError *local_error = NULL;

		sibling_path = (dir_prefix[0] != '\0') ?
				g_strconcat(dir_prefix, "/", d->dateiname, NULL) :
				g_strdup(d->dateiname);

		sfp_pdf = sond_file_part_create(sfp_parent_xml, sibling_path,
				&local_error);
		g_free(sibling_path);
		if (!sfp_pdf) {
			g_clear_error(&local_error);
			g_ptr_array_add(nicht_gefunden, g_strdup(d->dateiname));
			continue;
		}

		filepart = sond_file_part_get_filepart(sfp_pdf);
		g_object_unref(sfp_pdf);

		rc = zond_dbase_get_section(zond->dbase_zond->zond_dbase_work,
				filepart, NULL, &ID_file_part, &local_error);
		if (rc) {
			g_free(filepart);
			g_clear_error(&local_error);
			g_ptr_array_add(nicht_gefunden, g_strdup(d->dateiname));
			continue;
		}

		if (!ID_file_part) {
			//Datei noch nicht in zond_dbase - anlegen, Label = anzeigename
			rc = zond_treeview_insert_file_part_in_db(zond, filepart,
					d->anzeigename, "pdf", &ID_file_part, &local_error);
			if (rc) {
				g_free(filepart);
				g_clear_error(&local_error);
				g_ptr_array_add(nicht_gefunden, g_strdup(d->dateiname));
				continue;
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
				continue;
			}
			if (baum_inhalt_file) {
				g_free(filepart);
				if (n_vorhanden)
					(*n_vorhanden)++;
				continue; //schon angebunden: Anker nicht vorrücken
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
			continue;
		}

		anchor_id = new_node_id;
		child = FALSE; //weitere Dokumente als Geschwister anhängen

		if (n_angebunden)
			(*n_angebunden)++;
	}

	g_free(dir_prefix);
	g_object_unref(sfp_xml);
	g_ptr_array_unref(arr_dok);

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
