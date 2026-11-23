/*
 zond (viewer_host.c) - Akten, Beweisstücke, Unterlagen
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

/* zond als Host der Viewer: hängt sich an die Signale der PdfViewerSession
 * und steuert Anbindungen, Projekt-Datenbank und Volltextindex bei. */

#include "viewer_host.h"

#include <sqlite3.h>
#include <gtk/gtk.h>

#include "../sond_log_and_error.h"
#include "../sond_fileparts.h"
#include "../sond_index.h"
#include "../sond_process_file.h"
#include "../seadrive/sond_seadrive_gtk3.h"

#include "zond_init.h"
#include "../znd/zond_dbase.h"
#include "../znd/dbase_zond.h"
#include "zond_pdf_document.h"
#include "anbindung_zaehlung.h"
#include "../znd/anbindung.h"
#include "20allgemein/project.h"
#include "20allgemein/ziele.h"
#include "40viewer/viewer.h"
#include "40viewer/document.h"
#include "40viewer/viewer_save.h"

typedef struct {
	Projekt *zond;
	gboolean changed; //Projekt vor dem Speichern geändert?
	SondIndexCtx index_tx;
	gboolean index_tx_active;
} ViewerHost;

static gint viewer_host_anbindung_int(ZondDBase* zond_dbase,
		PdfDocumentPage* pdf_document_page, GError** error) {
	gint rc = 0;
	GArray* arr_sections = NULL;
	gchar* filepart = NULL;

	filepart = sond_file_part_get_filepart(SOND_FILE_PART(
			zond_pdf_document_get_sfp_pdf(pdf_document_page->document)));

	rc = zond_dbase_get_arr_sections(zond_dbase, filepart, &arr_sections, error);
	g_free(filepart);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	for (gint u = 0; u < arr_sections->len; u++) {
		Section section = { 0, };
		Anbindung anbindung = { 0, };

		section = g_array_index(arr_sections, Section, u);
		anbindung_parse_file_section(section.section, &anbindung);

		anbindung_aktualisieren(pdf_document_page->document, &anbindung);

		if ((pdf_document_page->page_akt == anbindung.von.seite) ||
				(!anbindung_is_pdf_punkt(anbindung) &&
				pdf_document_page->page_akt == anbindung.bis.seite)) {
			g_array_unref(arr_sections);

			return 1;
		}
	}

	g_array_unref(arr_sections);

	return 0;
}

static gint viewer_host_anbindung(Projekt *zond, GPtrArray *arr_document_page,
		GError **error) {
	for (gint i = 0; i < arr_document_page->len; i++) {
		PdfDocumentPage *pdf_document_page = NULL;
		gint rc = 0;

		pdf_document_page = g_ptr_array_index(arr_document_page, i);

		rc = viewer_host_anbindung_int(zond->proj.dbase_zond->zond_dbase_work,
				pdf_document_page, error);
		if (rc) {
			if (rc == -1) g_prefix_error(error, "%s\n", __func__);

			return (rc == -1) ? -1 : 1;
		}

		rc = viewer_host_anbindung_int(zond->proj.dbase_zond->zond_dbase_store,
				pdf_document_page, error);
		if (rc) {
			if (rc == -1) g_prefix_error(error, "%s\n", __func__);

			return (rc == -1) ? -1 : 2;
		}
	}

	return 0;
}

/* Sections müssen für main und work UNABHÄNGIG gelesen und neu berechnet
 * werden (nicht: einmal berechnen, in beide schreiben) - store und work
 * können für dieselbe Zeile unterschiedliche Ausgangswerte haben, da work
 * laufend fortgeschrieben, store aber nur bei project_save() komplett
 * nachgezogen wird. Läuft innerhalb der Transaktion von dbase_zond_begin(). */
static gint viewer_host_update_section_schema(sqlite3 *db, gchar const *schema,
		DisplayedDocument *dd, GError **error) {
	gint rc = 0;
	gchar *sql = NULL;
	sqlite3_stmt *stmt = NULL;
	SondFilePartPDF *sfp_pdf = NULL;
	gchar *filepart = NULL;
	g_autoptr(GArray) arr_sections = NULL;

	sfp_pdf = zond_pdf_document_get_sfp_pdf(dd->zpdfd_part->zond_pdf_document);
	filepart = sond_file_part_get_filepart(SOND_FILE_PART(sfp_pdf));

	sql = g_strdup_printf("SELECT ID, section FROM %s.knoten WHERE file_part=?1;",
			schema);
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	g_free(sql);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
					"%s (%s): %s", __func__, schema, sqlite3_errmsg(db));
		g_free(filepart);
		return -1;
	}
	sqlite3_bind_text(stmt, 1, filepart, -1, SQLITE_TRANSIENT);
	g_free(filepart);

	arr_sections = g_array_new(FALSE, FALSE, sizeof(Section));
	g_array_set_clear_func(arr_sections, (GDestroyNotify) section_free);

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		Section section = { 0 };

		section.ID = sqlite3_column_int(stmt, 0);
		section.section = g_strdup((gchar const*) sqlite3_column_text(stmt, 1));

		//wenn section == NULL, dann brauch es nicht gespeichert zu werden,
		//ist ja die ganze PDF-Datei - keine Anpassung nötig
		if (section.section)
			g_array_append_val(arr_sections, section);
	}
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
					"%s (%s): %s", __func__, schema, sqlite3_errmsg(db));
		return -1;
	}

	for (guint i = 0; i < arr_sections->len; i++) {
		Section section = g_array_index(arr_sections, Section, i);
		Anbindung anbindung_int = { 0 };
		gchar *section_new = NULL;
		gchar *sql_update = NULL;
		sqlite3_stmt *stmt_update = NULL;

		anbindung_parse_file_section(section.section, &anbindung_int);
		anbindung_aktualisieren(dd->zpdfd_part->zond_pdf_document, &anbindung_int);
		// Recalculate changes that will be removed during save
		anbindung_korrigieren(dd->zpdfd_part, &anbindung_int);
		anbindung_build_file_section(anbindung_int, &section_new);

		sql_update = g_strdup_printf("UPDATE %s.knoten SET section=?2 WHERE ID=?1;",
				schema);
		rc = sqlite3_prepare_v2(db, sql_update, -1, &stmt_update, NULL);
		g_free(sql_update);
		if (rc != SQLITE_OK) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schema, sqlite3_errmsg(db));
			g_free(section_new);
			return -1;
		}

		sqlite3_bind_int(stmt_update, 1, section.ID);
		sqlite3_bind_text(stmt_update, 2, section_new, -1, SQLITE_TRANSIENT);
		g_free(section_new);

		rc = sqlite3_step(stmt_update);
		sqlite3_finalize(stmt_update);
		if (rc != SQLITE_DONE) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schema, sqlite3_errmsg(db));
			return -1;
		}
	}

	return 0;
}

static gint viewer_host_update_sections(DBaseZond* dbase_zond,
		DisplayedDocument* dd, GError** error) {
	sqlite3 *db = zond_dbase_get_dbase(dbase_zond->zond_dbase_store);
	gint rc = 0;

	rc = viewer_host_update_section_schema(db, "main", dd, error);
	if (rc)
		return -1;

	rc = viewer_host_update_section_schema(db, "work", dd, error);
	if (rc)
		return -1;

	return 0;
}

/* Vor dem physischen Speichern (viewer_do_save_dd()) den FTS-Index für
 * diese Datei auf den nach dem Speichern gültigen Stand bringen:
 * - Seiten mit einem JOURNAL_TYPE_OCR-Eintrag: Inhalt ändert sich (neue
 *   versteckte Textebene) - nur diese eine Seite verwerfen, kein
 *   pauschales Verwerfen der ganzen Datei. Zusätzlich eine eventuell
 *   bestehende coverage-Abdeckung invalidieren.
 * - Seiten mit einem JOURNAL_TYPE_PAGES_INSERTED-Eintrag: neuer, noch nie
 *   geprüfter Seiteninhalt - kein clear_page nötig, aber ebenfalls eine
 *   eventuell bestehende coverage-Abdeckung invalidieren (s.u.).
 * - Seiten, die gelöscht werden (pdfp->deleted): ebenfalls verwerfen.
 * - Überlebende Seiten, deren Nummer sich durch anderswo gelöschte/
 *   eingefügte Seiten verschiebt: in chunks/pages umnummerieren (Inhalt
 *   bleibt erhalten). Wiederverwendet dieselbe, für Anbindungen bereits
 *   bewährte Korrektur wie dbase_zond_update_section_dbase() - eine
 *   einzelne Seite ist dafür einfach eine entartete Anbindung
 *   {seite,0}-{seite,EOP} ("ganze Seite").
 * Muss VOR viewer_do_save_dd() laufen (genau wie dbase_zond_update_
 * sections()), weil die dort ablaufende Kompaktierung (pdfp->deleted/
 * inserted-Flags werden gelöscht/page_akt neu gesetzt) genau das ist,
 * was anbindung_korrigieren() auswertet - danach sind die Flags weg. */
//Seitenzahl in der Zählung der Datei auf der Platte (vor dem Speichern): so zählt der Index
static gint viewer_host_page_platte(ZondPdfDocument *zpdfd, gint page_live) {
	Anbindung anbindung = { { page_live, 0 }, { page_live, EOP } };

	anbindung_get_orig(zpdfd, &anbindung);

	return anbindung.von.seite;
}

static void viewer_host_update_index_for_save(Projekt *zond, DisplayedDocument *dd,
		SondIndexCtx *index_ctx) {
	gchar *filename = NULL;
	GArray *arr_journal = NULL;
	GArray *pages = NULL;
	GError *error = NULL;
	GArray *old_nrs = NULL;
	GArray *new_nrs = NULL;

	if (!index_ctx)
		return;

	filename = sond_file_part_get_filepart(
			SOND_FILE_PART(zond_pdf_document_get_sfp_pdf(
					dd->zpdfd_part->zond_pdf_document)));

	/* OCR: Inhalt der betroffenen Seite ändert sich - verwerfen, UND
	 * (nur hier, nicht bei reinem Löschen/Umnumerieren weiter unten - dort
	 * ändert sich der Inhalt überlebender Seiten nicht) eine eventuell
	 * bestehende coverage-Abdeckung dieser Datei (oder eines Vorfahre-
	 * Verzeichnisses) invalidieren, weil clear_page() gerade Chunks ohne
	 * Ersatz entfernt hat - ohne Invalidierung würde eine spätere
	 * Abdeckungs-Prüfung fälschlich "vollständig indiziert" melden.
	 *
	 * PAGES_INSERTED: hier kommt neuer, noch nie geprüfter Seiteninhalt
	 * hinzu - kein clear_page nötig (dafür gibt es ja noch keinen
	 * Index-Eintrag), aber dieselbe coverage-Invalidierung wie bei OCR:
	 * eine bestehende Abdeckungs-Aussage ("alles hier ist mindestens
	 * Modus M") wird durch die neue, ungeprüfte Seite ebenso verletzt. */
	{
		GArray *inserted_pages = g_array_new(FALSE, FALSE, sizeof(gint));

		arr_journal = zond_pdf_document_get_arr_journal(dd->zpdfd_part->zond_pdf_document);
		for (guint u = 0; u < arr_journal->len; u++) {
			JournalEntry entry = g_array_index(arr_journal, JournalEntry, u);

			if (entry.type != JOURNAL_TYPE_OCR &&
					entry.type != JOURNAL_TYPE_PAGES_INSERTED)
				continue;
			if (!document_entry_in_dd(&entry, dd->zpdfd_part))
				continue;

			if (entry.type == JOURNAL_TYPE_OCR) {
				if (!sond_index_ctx_clear_page(index_ctx, filename,
						viewer_host_page_platte(dd->zpdfd_part->zond_pdf_document,
								entry.pdf_document_page->page_akt), &error)) {
					LOG_WARN("%s\n", error->message);
					g_clear_error(&error);
				}

				if (!sond_index_ctx_coverage_invalidate(index_ctx, filename,
						zond->proj.project_dir, &error)) {
					LOG_WARN("%s\n", error->message);
					g_clear_error(&error);
				}
			} else { /* JOURNAL_TYPE_PAGES_INSERTED - Sammeln, s.u.: einmal
			          * für die ganze Datei behandeln, nicht pro Eintrag,
			          * damit bei mehreren Einfüge-Vorgängen keine bereits
			          * "geretteten" Seiten fälschlich wieder mit erfasst
			          * werden. */
				gint first = entry.pdf_document_page->page_akt;
				gint count = entry.pages_inserted.count;

				for (gint p = first; p < first + count; p++)
					g_array_append_val(inserted_pages, p);
			}
		}

		if (inserted_pages->len > 0) {
			/* Vor dem Verwerfen eines eventuell bestehenden Datei-Eintrags
			 * erst den Seiten-Fortschritt der ÜBRIGEN (weder gelöschten
			 * noch neu eingefügten) Seiten retten. ACHTUNG: nicht einfach
			 * "0..Seitenzahl minus eingefügte", weil gelöschte Seiten als
			 * stabile "Karteileichen" im Seiten-Array stehen bleiben (keine
			 * Kompaktierung mehr) - die rohe Seitenzahl zählt sie mit. */
			GPtrArray *arr_pages = zond_pdf_document_get_arr_pages(
					dd->zpdfd_part->zond_pdf_document);
			GArray *pages_to_write = g_array_new(FALSE, FALSE, sizeof(gint));

			for (guint i = 0; i < arr_pages->len; i++) {
				PdfDocumentPage *pdfp = g_ptr_array_index(arr_pages, i);
				gint page_nr = (gint) i;
				gboolean is_new = FALSE;

				if (!pdfp || pdfp->deleted)
					continue;

				for (guint u = 0; u < inserted_pages->len; u++) {
					if (g_array_index(inserted_pages, gint, u) == page_nr) {
						is_new = TRUE;
						break;
					}
				}
				if (is_new)
					continue;

				//in den Index kommt die Zählung der Datei auf der Platte
				page_nr = viewer_host_page_platte(dd->zpdfd_part->zond_pdf_document, page_nr);
				g_array_append_val(pages_to_write, page_nr);
			}

			if (!sond_index_ctx_coverage_expand_to_pages(index_ctx, filename,
					(gint const*) pages_to_write->data, pages_to_write->len,
					&error)) {
				LOG_WARN("%s\n", error->message);
				g_clear_error(&error);
			}
			g_array_unref(pages_to_write);

			if (!sond_index_ctx_coverage_invalidate(index_ctx, filename,
					zond->proj.project_dir, &error)) {
				LOG_WARN("%s\n", error->message);
				g_clear_error(&error);
			}
		}

		g_array_unref(inserted_pages);
	}

	/* Gelöschte Seiten verwerfen, überlebende ggf. umnummerieren - über
	 * ALLE aktuell im Index geführten Seiten dieser Datei (nicht nur den
	 * Bereich dieses dd), da anbindung_korrigieren() ohnehin nur Seiten
	 * berücksichtigt, die tatsächlich betroffen sind. */
	pages = sond_index_ctx_get_pages_for_file(index_ctx, filename);
	old_nrs = g_array_new(FALSE, FALSE, sizeof(gint));
	new_nrs = g_array_new(FALSE, FALSE, sizeof(gint));

	for (guint u = 0; u < pages->len; u++) {
		gint page_nr = g_array_index(pages, gint, u);
		Anbindung anbindung = { { page_nr, 0 }, { page_nr, EOP } };
		PdfDocumentPage *pdfp = NULL;
		gint page_live = 0;

		if (page_nr < 0) /* Nicht-PDF-Konvention (page_nr == -1) betrifft hier nicht */
			continue;

		/* Der Index zählt wie die Datei auf der Platte, das Seiten-Array live (mit gelöschten
		 * Seiten als Karteileichen): erst umrechnen, dann nachsehen, dann zurück auf die
		 * Zählung nach dem Speichern. */
		anbindung_aktualisieren(dd->zpdfd_part->zond_pdf_document, &anbindung);
		page_live = anbindung.von.seite;

		/* Verteidigung gegen g_ptr_array_index() ohne Bounds-Check:
		 * sollte der Index eine Seitenzahl kennen, die es (mehr) gar
		 * nicht gibt, verwerfen statt out-of-bounds zuzugreifen. */
		if (page_live >= zond_pdf_document_get_number_of_pages(
				dd->zpdfd_part->zond_pdf_document)) {
			if (!sond_index_ctx_clear_page(index_ctx, filename, page_nr, &error)) {
				LOG_WARN("%s\n", error->message);
				g_clear_error(&error);
			}
			continue;
		}

		pdfp = zond_pdf_document_get_pdf_document_page(
				dd->zpdfd_part->zond_pdf_document, page_live);

		if (!pdfp || pdfp->deleted) {
			if (!sond_index_ctx_clear_page(index_ctx, filename, page_nr, &error)) {
				LOG_WARN("%s\n", error->message);
				g_clear_error(&error);
			}
			continue;
		}

		anbindung_korrigieren(dd->zpdfd_part, &anbindung);

		if (anbindung.von.seite != page_nr) {
			g_array_append_val(old_nrs, page_nr);
			g_array_append_val(new_nrs, anbindung.von.seite);
		}
	}

	if (old_nrs->len > 0) {
		if (!sond_index_ctx_renumber_pages(index_ctx, filename,
				(gint const*) old_nrs->data, (gint const*) new_nrs->data,
				old_nrs->len, &error)) {
			LOG_WARN("%s\n", error->message);
			g_clear_error(&error);
		}
	}

	/* Nach dem Renumerieren prüfen, ob jetzt ALLE nach dem Speichern in der Datei stehenden
	 * (nicht gelöschten) Seiten individuell indiziert sind (keine Lücke mehr, z.B. weil gerade die
	 * letzte, bisher nicht indizierte Seite gelöscht wurde) - falls ja, zur Datei-Ebene
	 * kollabieren (Mindestmodus-Prinzip wie bei coverage_try_collapse), statt die pages-Tabelle
	 * dauerhaft fein-granular stehen zu lassen.
	 * Der Index zählt wie die Datei nach dem Speichern, das Seiten-Array live: gelöschte Seiten
	 * bleiben als stabile "Karteileichen" stehen, eingefügte Seiten außerhalb dieses Teils sind
	 * noch nicht in der Datei. Deshalb wird hier nach der Zählung der Datei durchgezählt. */
	{
		GPtrArray *arr_pages = zond_pdf_document_get_arr_pages(
				dd->zpdfd_part->zond_pdf_document);
		GArray *pages_now = sond_index_ctx_get_pages_for_file(index_ctx, filename);
		g_autofree gboolean *erwartet = g_new0(gboolean, arr_pages->len ? arr_pages->len : 1);
		Anbindung teil = { 0 };
		gboolean complete = TRUE;
		gint min_mode = G_MAXINT;
		gint n_platte = 0; //Seiten in der Datei nach dem Speichern
		gint n_erwartet = 0; //davon nicht gelöscht: müssen im Index stehen

		zpdfd_part_get_anbindung(dd->zpdfd_part, &teil);

		for (guint i = 0; i < arr_pages->len; i++) {
			PdfDocumentPage *pdfp = g_ptr_array_index(arr_pages, i);
			gboolean in_teil = (gint) i >= teil.von.seite && (gint) i <= teil.bis.seite;

			if (pdfp && ((pdfp->deleted && (pdfp->on_disk_deleted || in_teil)) ||
					(pdfp->inserted && !in_teil)))
				continue; //steht nach dem Speichern nicht in der Datei

			if (pdfp && pdfp->deleted) { //anderer Teil, noch nicht gespeichert: bleibt, ohne Eintrag
				n_platte++;
				continue;
			}

			erwartet[n_platte++] = TRUE;
			n_erwartet++;
		}

		/* file_pagecount (coalescing-unabhängige Gesamtseitenzahl, s.
		 * sond_index.h) auf den aktuellen Stand bringen - unabhängig
		 * davon, ob unten überhaupt (wieder) auf Datei-Ebene kollabiert
		 * werden kann. Sonst bliebe nach Seiten-Einfügen/-Löschen eine
		 * veraltete Zahl stehen, die z.B. sond_index_ctx_delete_index()
		 * beim Rekonstruieren der übrigen Seiten fälschlich zugrunde
		 * legen würde. */
		if (!sond_index_ctx_set_page_count(index_ctx, filename, n_erwartet, &error)) {
			LOG_WARN("%s\n", error->message);
			g_clear_error(&error);
		}

		for (guint u = 0; u < pages_now->len && complete; u++) {
			gint page_nr = g_array_index(pages_now, gint, u);
			gint mode;

			if (page_nr < 0 || page_nr >= n_platte || !erwartet[page_nr]) {
				complete = FALSE;
				break;
			}

			mode = sond_index_ctx_get_page_ocr_mode(index_ctx, filename, page_nr);
			if (mode < 0) {
				complete = FALSE;
				break;
			}
			if (mode < min_mode)
				min_mode = mode;
		}

		//jede erwartete Seite steht genau einmal im Index
		if (n_erwartet == 0 || pages_now->len != (guint) n_erwartet)
			complete = FALSE;

		if (complete) {
			if (!sond_index_ctx_coverage_mark(index_ctx, filename, min_mode, &error)) {
				LOG_WARN("%s\n", error->message);
				g_clear_error(&error);
			} else if (zond->proj.project_dir) {
				if (!sond_index_ctx_coverage_try_collapse(index_ctx, filename,
						zond->proj.project_dir, &error)) {
					LOG_WARN("%s\n", error->message);
					g_clear_error(&error);
				}
			}
		}
		g_array_unref(pages_now);
	}

	g_array_unref(old_nrs);
	g_array_unref(new_nrs);
	g_array_unref(pages);
	g_free(filename);

	return;
}

//nicht geladene SeaDrive-Datei laden (Abbruch: 1)
static gint viewer_host_cb_load_file(PdfViewerSession *session,
		gpointer parent, gchar const *path, gpointer data) {
	return sond_seadrive_load_file_sync(GTK_WINDOW(parent), path) ? 0 : 1;
}

static gint viewer_host_cb_anbindung_requested(PdfViewerSession *session,
		PdfViewer *pv, GError **error, gpointer data) {
	ViewerHost *host = data;
	gint rc = 0;

	rc = zond_anbindung_erzeugen(host->zond, pv, error);
	if (rc)
		return -1;

	gtk_window_present(GTK_WINDOW(host->zond->win.app_window));

	return 0;
}

static gint viewer_host_cb_pages_deleting(PdfViewerSession *session,
		PdfViewer *pv, GPtrArray *arr_document_page, GError **error,
		gpointer data) {
	ViewerHost *host = data;
	gint rc = 0;

	//Abfrage, ob Anbindung mit Seite verknüpft
	rc = viewer_host_anbindung(host->zond, arr_document_page, error);
	if (rc == -1)
		return -1;

	if (rc == 1) {
		g_set_error(error, VIEWER_ERROR, 0, "Seiten enthalten Anbindungen - \n"
				"Löschen nicht zulässig");

		return 1;
	}

	if (rc == 2) {
		g_set_error(error, VIEWER_ERROR, 0,
				"Gespeichertes Projekt: Seiten enthalten Anbindungen\n"
				"Zunächst Projekt speichern");

		return 1;
	}

	return 0;
}

static void viewer_host_cb_save_started(PdfViewerSession *session,
		PdfViewer *pv, gpointer data) {
	ViewerHost *host = data;

	//Projekt-Zustand (geändert oder nicht) zwischenspeichern
	host->changed = host->zond->proj.dbase_zond->changed;

	return;
}

static gint viewer_host_cb_save_prepare(PdfViewerSession *session,
		PdfViewer *pv, DisplayedDocument *dd, GError **error, gpointer data) {
	ViewerHost *host = data;
	Projekt *zond = host->zond;
	gint rc = 0;

	host->index_tx_active = FALSE;

	//Index-DB als Schema idx anhängen (vor der Transaktion): ihre
	//Änderungen liegen dann im selben Commit wie store/work und werden
	//mit zurückgerollt, wenn das Speichern scheitert
	if (zond->proj.wctx && zond->proj.wctx->index_ctx) {
		rc = dbase_zond_attach_index(zond->proj.dbase_zond,
				zond->proj.wctx->index_ctx->db_path, error);
		if (rc) {
			g_prefix_error(error, "%s\n", __func__);

			return -1;
		}
		dbase_zond_index_ctx(zond->proj.dbase_zond, &host->index_tx,
				zond->proj.wctx->index_ctx);
		host->index_tx_active = TRUE;
	}

	rc = dbase_zond_begin(zond->proj.dbase_zond, error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);
		if (host->index_tx_active)
			dbase_zond_detach_index(zond->proj.dbase_zond);

		return -1;
	}

	//FTS-Index für diese Datei auf den nach dem Speichern gültigen
	//Stand bringen (Seiten verwerfen/umnummerieren) - muss vor dem
	//physischen Speichern laufen, s. Kommentar an der Funktion.
	viewer_host_update_index_for_save(zond, dd,
			host->index_tx_active ? &host->index_tx : NULL);

	//Anbindungen in db anpassen
	rc = viewer_host_update_sections(zond->proj.dbase_zond, dd, error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		dbase_zond_rollback(zond->proj.dbase_zond, error);
		if (host->index_tx_active)
			dbase_zond_detach_index(zond->proj.dbase_zond);

		return -1;
	}

	return 0;
}

static gint viewer_host_cb_save_finished(PdfViewerSession *session,
		PdfViewer *pv, DisplayedDocument *dd, gboolean ok, GError **error,
		gpointer data) {
	ViewerHost *host = data;
	Projekt *zond = host->zond;
	gint rc = 0;

	if (!ok) {
		dbase_zond_rollback(zond->proj.dbase_zond, error);
		if (host->index_tx_active)
			dbase_zond_detach_index(zond->proj.dbase_zond);

		return 0;
	}

	rc = dbase_zond_commit(zond->proj.dbase_zond, error);
	if (host->index_tx_active)
		dbase_zond_detach_index(zond->proj.dbase_zond);

	return rc ? -1 : 0;
}

static void viewer_host_cb_save_done(PdfViewerSession *session, PdfViewer *pv,
		gpointer data) {
	ViewerHost *host = data;

	//ggf. zurücksetzen
	if (!host->changed)
		project_reset_changed(host->zond, FALSE);

	return;
}

void viewer_host_connect(Projekt *zond, PdfViewerSession *session) {
	ViewerHost *host = g_malloc0(sizeof(ViewerHost));

	host->zond = zond;
	g_object_set_data_full(G_OBJECT(session), "zond-viewer-host", host,
			g_free);

	g_signal_connect(session, "load-file",
			G_CALLBACK(viewer_host_cb_load_file), host);
	g_signal_connect(session, "anbindung-requested",
			G_CALLBACK(viewer_host_cb_anbindung_requested), host);
	g_signal_connect(session, "pages-deleting",
			G_CALLBACK(viewer_host_cb_pages_deleting), host);
	g_signal_connect(session, "save-started",
			G_CALLBACK(viewer_host_cb_save_started), host);
	g_signal_connect(session, "save-prepare",
			G_CALLBACK(viewer_host_cb_save_prepare), host);
	g_signal_connect(session, "save-finished",
			G_CALLBACK(viewer_host_cb_save_finished), host);
	g_signal_connect(session, "save-done",
			G_CALLBACK(viewer_host_cb_save_done), host);

	return;
}
