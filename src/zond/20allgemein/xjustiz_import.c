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

#include <zip.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "../../sond_log_and_error.h"
#include "../../sond_file_helper.h"
#include "../../misc.h"

#include "../zond_init.h"
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
 * ZIP-ZUGRIFF
 * ========================================================================== */

/* Öffnet eine ZIP-Datei vom Datenträger - Windows-Long-Path-sicher über
 * sond_fopen(), analog zum bestehenden Muster in
 * sond_file_part_zip_open_archive() (sond_fileparts.c, Fall "Filesystem,
 * nur lesend"). */
static zip_t* xjustiz_open_zip(gchar const *disk_path, GError **error) {
	FILE *f = NULL;
	zip_error_t zip_error = { 0 };
	zip_source_t *src = NULL;
	zip_t *archive = NULL;

	f = sond_fopen(disk_path, "rb", error);
	if (!f)
		return NULL;

	zip_error_init(&zip_error);
	src = zip_source_filep_create(f, 0, -1, &zip_error);
	if (!src) {
		fclose(f);
		g_set_error(error, ZOND_ERROR, 0, "%s\nzip_source_filep_create: %s",
				__func__, zip_error_strerror(&zip_error));
		zip_error_fini(&zip_error);
		return NULL;
	}

	archive = zip_open_from_source(src, ZIP_RDONLY, &zip_error);
	if (!archive) {
		zip_source_free(src);
		g_set_error(error, ZOND_ERROR, 0, "%s\nzip_open_from_source: %s",
				__func__, zip_error_strerror(&zip_error));
		zip_error_fini(&zip_error);
		return NULL;
	}
	zip_error_fini(&zip_error);

	return archive;
}

/* Sucht im Archiv (auf beliebiger Verzeichnistiefe) einen Eintrag, dessen
 * Basename "xjustiz_nachricht.xml" lautet (Groß-/Kleinschreibung egal). */
static gchar* xjustiz_find_nachricht_entry(zip_t *archive) {
	zip_int64_t n = zip_get_num_entries(archive, 0);

	for (zip_int64_t i = 0; i < n; i++) {
		gchar const *name = zip_get_name(archive, i, 0);
		gchar *base = NULL;
		gboolean match = FALSE;

		if (!name)
			continue;

		base = g_path_get_basename(name);
		match = !g_ascii_strcasecmp(base, "xjustiz_nachricht.xml");
		g_free(base);

		if (match)
			return g_strdup(name);
	}

	return NULL;
}

/* Liest einen Eintrag vollständig in den Speicher - Muster wie
 * extract_from_zip() in sond_text_extract.c. */
static gchar* xjustiz_read_entry(zip_t *archive, gchar const *entry_name,
		gsize *out_len, GError **error) {
	struct zip_stat st;
	zip_file_t *zf = NULL;
	gchar *content = NULL;
	zip_int64_t bytes_read = 0;

	zip_stat_init(&st);
	if (zip_stat(archive, entry_name, 0, &st) != 0) {
		g_set_error(error, ZOND_ERROR, 0, "%s\nzip_stat('%s'): %s", __func__,
				entry_name, zip_strerror(archive));
		return NULL;
	}

	zf = zip_fopen(archive, entry_name, 0);
	if (!zf) {
		g_set_error(error, ZOND_ERROR, 0, "%s\nzip_fopen('%s'): %s", __func__,
				entry_name, zip_strerror(archive));
		return NULL;
	}

	content = g_malloc(st.size + 1);
	bytes_read = zip_fread(zf, content, st.size);
	zip_fclose(zf);

	if (bytes_read < 0 || (zip_uint64_t) bytes_read != st.size) {
		g_free(content);
		g_set_error(error, ZOND_ERROR, 0, "%s\nFehler beim Lesen von '%s'",
				__func__, entry_name);
		return NULL;
	}

	content[bytes_read] = '\0';
	if (out_len)
		*out_len = (gsize) bytes_read;

	return content;
}

/* Löst einen aus der XML stammenden Dateinamen (kann ein blanker Basename
 * oder ein relativer Pfad innerhalb der ZIP sein) auf einen tatsächlich im
 * Archiv vorhandenen Eintragsnamen auf: erst exakter Treffer, sonst
 * Suche über den Basename (nur bei genau einem Treffer eindeutig). */
static gchar* xjustiz_resolve_zip_entry(zip_t *archive,
		gchar const *dateiname, GError **error) {
	gchar *base_wanted = NULL;
	gchar *base_wanted_cf = NULL;
	zip_int64_t n = 0;
	gchar *found = NULL;
	gint matches = 0;

	if (zip_name_locate(archive, dateiname, 0) >= 0)
		return g_strdup(dateiname);

	base_wanted = g_path_get_basename(dateiname);
	base_wanted_cf = g_utf8_casefold(base_wanted, -1);
	g_free(base_wanted);

	n = zip_get_num_entries(archive, 0);
	for (zip_int64_t i = 0; i < n; i++) {
		gchar const *name = zip_get_name(archive, i, 0);
		gchar *base = NULL;
		gchar *base_cf = NULL;

		if (!name)
			continue;

		base = g_path_get_basename(name);
		base_cf = g_utf8_casefold(base, -1);
		g_free(base);

		if (!g_strcmp0(base_cf, base_wanted_cf)) {
			matches++;
			g_free(found);
			found = g_strdup(name);
		}
		g_free(base_cf);
	}
	g_free(base_wanted_cf);

	if (matches == 1)
		return found;

	g_free(found);
	if (matches == 0)
		g_set_error(error, ZOND_ERROR, 0, "Datei '%s' nicht im Archiv gefunden",
				dateiname);
	else
		g_set_error(error, ZOND_ERROR, 0,
				"Dateiname '%s' im Archiv nicht eindeutig", dateiname);
	return NULL;
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
 * unabhängig von der genauen Verschachtelung. */
static GPtrArray* xjustiz_parse_nachricht(gchar const *xml, gsize len,
		gchar **out_produktname, gchar **out_zeitpunkt, GError **error) {
	xmlDocPtr doc = NULL;
	xmlXPathContextPtr ctx = NULL;
	xmlXPathObjectPtr xpath_dok = NULL;
	GPtrArray *arr = NULL;

	if (out_produktname)
		*out_produktname = NULL;
	if (out_zeitpunkt)
		*out_zeitpunkt = NULL;

	doc = xmlReadMemory(xml, (int) len, "xjustiz_nachricht.xml", NULL,
			XML_PARSE_NOBLANKS | XML_PARSE_NONET);
	if (!doc) {
		g_set_error(error, ZOND_ERROR, 0,
				"%s\nxjustiz_nachricht.xml konnte nicht geparst werden",
				__func__);
		return NULL;
	}

	ctx = xmlXPathNewContext(doc);
	if (!ctx) {
		xmlFreeDoc(doc);
		g_set_error(error, ZOND_ERROR, 0, "%s\nxmlXPathNewContext fehlgeschlagen",
				__func__);
		return NULL;
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
 * ZIP-DATEI IM PROJEKTVERZEICHNIS BESTIMMEN
 * ========================================================================== */

/* Öffnet IMMER den Dateiauswahl-Dialog (kein automatisches Suchen/Raten
 * im Projektverzeichnis - Nutzer-Entscheidung 22.09.2026) und liefert
 * einen zu zond->project_dir RELATIVEN Pfad zurück (nie einen absoluten!):
 * "filepart"-Strings sind in zond durchgängig relativ zu project_dir zu
 * verstehen (path_root-Konvention, s. sond_fileparts.h sowie
 * project.c/project_open() -> sond_treeviewfm_set_root(...,
 * zond->project_dir, ...)) - Code, der später aus einem filepart wieder
 * einen echten Datenträgerpfad macht, stellt project_dir selbst voran.
 * Ein hier zurückgegebener ABSOLUTER Pfad führte deshalb dort zu doppelt
 * vorangestelltem project_dir (Nutzer-Fund 22.09.2026, s. auch
 * xjustiz_import()).
 *
 * NULL ohne *error gesetzt bedeutet: Nutzer hat Dialog abgebrochen. */
static gchar* xjustiz_choose_zip(Projekt *zond, GError **error) {
	gchar *abs_path = NULL;
	gsize prefix_len = 0;
	gchar *result = NULL;

	abs_path = filename_oeffnen(GTK_WINDOW(zond->app_window), zond->project_dir);
	if (!abs_path)
		return NULL; //Abbruch

	//absoluten Pfad aus dem Dateiauswahl-Dialog in einen zu project_dir
	//relativen Pfad umwandeln (s. Funktionskopf)
	prefix_len = strlen(zond->project_dir);
	if (!g_str_has_prefix(abs_path, zond->project_dir)
			|| (abs_path[prefix_len] != '/' && abs_path[prefix_len] != '\0')) {
		g_set_error(error, ZOND_ERROR, 0,
				"Die gewählte Datei liegt nicht im Projektverzeichnis ('%s').",
				zond->project_dir);
		g_free(abs_path);
		return NULL;
	}

	result = g_strdup(abs_path + prefix_len
			+ (abs_path[prefix_len] == '/' ? 1 : 0));
	g_free(abs_path);
	return result;
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
	gchar *zip_path_rel = NULL; //relativ zu zond->project_dir - wird als
			//filepart in der DB gespeichert (s. xjustiz_choose_zip())
	gchar *zip_path_abs = NULL; //nur für den eigenen Datenträgerzugriff
	zip_t *archive = NULL;
	gchar *nachricht_entry = NULL;
	gchar *xml_content = NULL;
	gsize xml_len = 0;
	GPtrArray *arr_dok = NULL;
	GPtrArray *nicht_gefunden = NULL;
	gchar *produktname = NULL;
	gchar *zeitpunkt_roh = NULL;
	gchar *zeitpunkt = NULL;
	gchar *struktur_label = NULL;
	gint struktur_id = 0;

	if (n_angebunden)
		*n_angebunden = 0;
	if (n_vorhanden)
		*n_vorhanden = 0;
	if (arr_nicht_gefunden)
		*arr_nicht_gefunden = NULL;

	if (zond->baum_active != BAUM_INHALT) {
		g_set_error(error, ZOND_ERROR, 0,
				"Bitte zuerst im Bestandsverzeichnis die Zielposition "
				"markieren (wie beim normalen Einfügen).");
		return -1;
	}

	rc = zond_treeview_get_anchor(zond, &child, &iter_cursor, &iter_anchor,
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

	zip_path_rel = xjustiz_choose_zip(zond, error);
	if (!zip_path_rel)
		return (error && *error) ? -1 : 1; //1: Dateiauswahl abgebrochen

	//absoluter Pfad NUR für zip_open() u.ä. (eigener Datenträgerzugriff) -
	//im filepart (weiter unten, DB) wird bewußt zip_path_rel verwendet,
	//s. ausführlichen Kommentar an xjustiz_choose_zip().
	zip_path_abs = g_strconcat(zond->project_dir, "/", zip_path_rel, NULL);

	archive = xjustiz_open_zip(zip_path_abs, error);
	if (!archive) {
		g_free(zip_path_rel);
		g_free(zip_path_abs);
		return -1;
	}

	nachricht_entry = xjustiz_find_nachricht_entry(archive);
	if (!nachricht_entry) {
		zip_close(archive);
		g_set_error(error, ZOND_ERROR, 0,
				"In '%s' wurde keine xjustiz_nachricht.xml gefunden.",
				zip_path_abs);
		g_free(zip_path_rel);
		g_free(zip_path_abs);
		return -1;
	}

	xml_content = xjustiz_read_entry(archive, nachricht_entry, &xml_len,
			error);
	g_free(nachricht_entry);
	if (!xml_content) {
		zip_close(archive);
		g_free(zip_path_rel);
		g_free(zip_path_abs);
		return -1;
	}

	arr_dok = xjustiz_parse_nachricht(xml_content, xml_len, &produktname,
			&zeitpunkt_roh, error);
	g_free(xml_content);
	if (!arr_dok) {
		g_free(produktname);
		g_free(zeitpunkt_roh);
		zip_close(archive);
		g_free(zip_path_rel);
		g_free(zip_path_abs);
		return -1;
	}

	if (arr_dok->len == 0) {
		g_free(produktname);
		g_free(zeitpunkt_roh);
		g_ptr_array_unref(arr_dok);
		zip_close(archive);
		g_free(zip_path_rel);
		g_free(zip_path_abs);
		g_set_error(error, ZOND_ERROR, 0,
				"xjustiz_nachricht.xml enthält keine referenzierten "
				"PDF-Dokumente.");
		return -1;
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
		g_ptr_array_unref(nicht_gefunden);
		g_ptr_array_unref(arr_dok);
		zip_close(archive);
		g_free(zip_path_rel);
		g_free(zip_path_abs);
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
		g_ptr_array_unref(nicht_gefunden);
		g_ptr_array_unref(arr_dok);
		zip_close(archive);
		g_free(zip_path_rel);
		g_free(zip_path_abs);
		return -1;
	}

	anchor_id = struktur_id;
	child = TRUE;

	/* Pro Dokument: Fehler (Datei nicht auflösbar, DB-Fehler) werden - wie
	 * beim normalen Anbinden (zond_treeview_anbinden_rekursiv,
	 * zond_treeview.c) - nicht als Abbruch der gesamten Operation
	 * behandelt, sondern einzeln vermerkt; die übrigen Dokumente werden
	 * trotzdem angebunden. */
	for (guint i = 0; i < arr_dok->len; i++) {
		XJustizDatei *d = g_ptr_array_index(arr_dok, i);
		gchar *zip_entry = NULL;
		gchar *filepart = NULL;
		gint ID_file_part = 0;
		gint baum_inhalt_file = 0;
		gint new_node_id = 0;
		GError *local_error = NULL;

		zip_entry = xjustiz_resolve_zip_entry(archive, d->dateiname,
				&local_error);
		if (!zip_entry) {
			g_clear_error(&local_error);
			g_ptr_array_add(nicht_gefunden, g_strdup(d->dateiname));
			continue;
		}

		filepart = g_strconcat(zip_path_rel, "//", zip_entry, NULL);
		g_free(zip_entry);

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

	zip_close(archive);
	g_free(zip_path_rel);
	g_free(zip_path_abs);
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
