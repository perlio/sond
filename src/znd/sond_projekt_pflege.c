/*
 zond (sond_projekt_pflege.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_projekt_pflege.h"

#include "../sond_log_and_error.h"
#include "../sond_file_helper.h"
#include "zond_dbase.h"
#include "anbindung.h"
#include "zond_error.h"
#include "../sond_process_file.h"

struct _SondProjektPflege {
	SondProjektKontext kontext;

	/* Index-DB innerhalb der Dual-Write-Transaktion: als Schema idx an die store-Connection
	 * gehängt, index_tx ist eine Kopie des Index-Kontexts, die über diese Connection arbeitet.
	 * So gehören die Schreibzugriffe auf die Index-DB zum selben Commit wie store und work. */
	SondIndexCtx index_tx;
	gboolean index_tx_active;

	//PDFs und Mails, deren Struktur sich durch die laufende Aktion ändert (eigene Refs)
	GPtrArray *pending_structure;

	//Adressänderungen der übrigen Anhänge einer PDF (s. emb_sibling_changes)
	SondFilePart *pending_sibling_pdf;
	GPtrArray *pending_sibling_old;
	GPtrArray *pending_sibling_new;

	/* Von der Vorbereitung (vor Löschen/Verschieben) an den Abschluss (tx_ende) übergeben:
	 * - Pfad der gelöschten Datei: nach der physischen Löschung ist zu prüfen, ob das
	 *   Elternverzeichnis dadurch vollständig abgedeckt ist (Coverage zusammenfassen)
	 * - alter/neuer absoluter Pfad einer Verschiebung samt Kennzeichnung, ob sie physisch im
	 *   Dateisystem stattfand (nur dann kann ein gescheiterter Commit per Umbenennen mit
	 *   vertauschten Pfaden zurückgenommen werden)
	 * Gesetzt erst unmittelbar vor dem garantierten Erfolg der Vorbereitung, damit bei einem
	 * Fehlerweg (der Abschluss kommt dann nicht) nichts hängen bleibt. */
	gchar *pending_delete_path;
	gchar *pending_move_old;
	gchar *pending_move_new;
	gboolean pending_move_physical;
};

SondProjektPflege* sond_projekt_pflege_new(SondProjektKontext const *kontext) {
	SondProjektPflege *pflege = NULL;

	g_return_val_if_fail(kontext && kontext->dbase, NULL);

	pflege = g_new0(SondProjektPflege, 1);
	pflege->kontext = *kontext;

	return pflege;
}

void sond_projekt_pflege_free(SondProjektPflege *pflege) {
	if (!pflege)
		return;

	sond_projekt_pflege_emb_sibling_clear(pflege);
	sond_projekt_pflege_structure_clear(pflege);
	g_free(pflege->pending_delete_path);
	g_free(pflege->pending_move_old);
	g_free(pflege->pending_move_new);
	g_free(pflege);
}

static DBaseZond* pflege_dbase(SondProjektPflege *pflege) {
	return pflege->kontext.dbase(pflege->kontext.data);
}

static SondIndexCtx* pflege_index_ctx(SondProjektPflege *pflege) {
	return pflege->kontext.index_ctx ? pflege->kontext.index_ctx(pflege->kontext.data) : NULL;
}

/* ------------------------------------------------------------------------
 * Prüfen
 * --------------------------------------------------------------------- */

gint sond_projekt_pflege_check_delete(SondProjektPflege *pflege, gchar const *path,
		gboolean is_dir, gboolean container_root, gchar const *section, DBaseZondRef *ref,
		GError **error) {
	if (is_dir) {
		g_autofree gchar *path_prefix = g_strconcat(path, container_root ? "//" : "/", NULL);

		return dbase_zond_test_path(pflege_dbase(pflege), path_prefix, ref, error);
	}

	return dbase_zond_test_path_section(pflege_dbase(pflege), path, section, FALSE, ref, error);
}

/* ------------------------------------------------------------------------
 * Index in der Transaktion
 * --------------------------------------------------------------------- */

gint sond_projekt_pflege_index_tx_begin(SondProjektPflege *pflege, GError **error) {
	SondIndexCtx *index_ctx = pflege_index_ctx(pflege);
	DBaseZond *dbase_zond = pflege_dbase(pflege);

	if (!index_ctx || pflege->index_tx_active)
		return 0;

	if (dbase_zond_attach_index(dbase_zond, index_ctx->db_path, error))
		return -1;

	dbase_zond_index_ctx(dbase_zond, &pflege->index_tx, index_ctx);
	pflege->index_tx_active = TRUE;

	return 0;
}

void sond_projekt_pflege_index_tx_end(SondProjektPflege *pflege) {
	if (!pflege->index_tx_active)
		return;

	dbase_zond_detach_index(pflege_dbase(pflege));
	pflege->index_tx_active = FALSE;
}

SondIndexCtx* sond_projekt_pflege_index_tx(SondProjektPflege *pflege) {
	return pflege->index_tx_active ? &pflege->index_tx : NULL;
}

void sond_projekt_pflege_tx_abort(SondProjektPflege *pflege) {
	dbase_zond_rollback(pflege_dbase(pflege), NULL);
	sond_projekt_pflege_index_tx_end(pflege);
}

gint sond_projekt_pflege_gmessage_index_renumber(SondProjektPflege *pflege, gchar const *prefix,
		gint index, gboolean into, GError **error) {
	SondIndexCtx *index_ctx = NULL;
	gchar const *boundary = NULL;
	g_autofree gchar *mail = NULL;
	GError *idx_err = NULL;

	index_ctx = sond_projekt_pflege_index_tx(pflege);
	if (!index_ctx)
		return 0;

	if (!sond_index_ctx_update_gmessage_index(index_ctx, prefix, index, into, &idx_err))
		goto err;

	//Mail = alles vor dem letzten "//"
	boundary = g_strrstr(prefix, "//");
	mail = boundary ? g_strndup(prefix, boundary - prefix) : g_strdup(prefix);
	if (!sond_index_ctx_clear_gmessage_structure(index_ctx, mail, &idx_err))
		goto err;

	return 0;

err:
	if (error)
		*error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED, "%s: %s", __func__,
				idx_err ? idx_err->message : "?");
	g_clear_error(&idx_err);

	return -1;
}

/* ------------------------------------------------------------------------
 * Anhänge einer PDF
 * --------------------------------------------------------------------- */

void sond_projekt_pflege_emb_sibling_clear(SondProjektPflege *pflege) {
	g_clear_object(&pflege->pending_sibling_pdf);
	g_clear_pointer(&pflege->pending_sibling_old, g_ptr_array_unref);
	g_clear_pointer(&pflege->pending_sibling_new, g_ptr_array_unref);
}

gint sond_projekt_pflege_emb_sibling_changes(SondProjektPflege *pflege, SondFilePart *sfp,
		gchar const *filename_new, GError **error) {
	SondFilePart *sfp_pdf = NULL;
	GPtrArray *old = NULL;
	GPtrArray *new = NULL;

	sond_projekt_pflege_emb_sibling_clear(pflege);

	if (!sfp)
		return 0;

	sfp_pdf = sond_file_part_get_parent(sfp);
	if (!sfp_pdf || !SOND_IS_FILE_PART_PDF(sfp_pdf))
		return 0;

	if (sond_file_part_pdf_emb_address_changes(SOND_FILE_PART_PDF(sfp_pdf),
			sond_file_part_get_path(sfp), filename_new, &old, &new, error))
		return -1;

	if (old->len == 0) {
		g_ptr_array_unref(old);
		g_ptr_array_unref(new);

		return 0;
	}

	pflege->pending_sibling_pdf = g_object_ref(sfp_pdf);
	pflege->pending_sibling_old = old;
	pflege->pending_sibling_new = new;

	return 0;
}

gint sond_projekt_pflege_emb_sibling_apply(SondProjektPflege *pflege, GError **error) {
	g_autofree gchar *filepart_pdf = NULL;

	if (!pflege->pending_sibling_pdf)
		return 0;

	filepart_pdf = sond_file_part_get_filepart(pflege->pending_sibling_pdf);

	for (guint i = 0; i < pflege->pending_sibling_old->len; i++) {
		g_autofree gchar *path_old = g_strconcat(filepart_pdf, "//",
				g_ptr_array_index(pflege->pending_sibling_old, i), NULL);
		g_autofree gchar *path_new = g_strconcat(filepart_pdf, "//",
				g_ptr_array_index(pflege->pending_sibling_new, i), NULL);
		GError *idx_err = NULL;

		if (dbase_zond_update_path(pflege_dbase(pflege), path_old, path_new, error))
			return -1;

		if (sond_projekt_pflege_index_tx(pflege) &&
				!sond_index_ctx_rename_file(sond_projekt_pflege_index_tx(pflege), path_old,
						path_new, &idx_err)) {
			if (error)
				*error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
						"%s: sond_index_ctx_rename_file: %s", __func__,
						idx_err ? idx_err->message : "?");
			g_clear_error(&idx_err);

			return -1;
		}
	}

	return 0;
}

void sond_projekt_pflege_emb_sibling_adjust_opened(SondProjektPflege *pflege) {
	GPtrArray *opened = NULL;

	if (!pflege->pending_sibling_pdf)
		return;

	opened = sond_file_part_get_arr_opened_files(pflege->pending_sibling_pdf);
	for (guint i = 0; opened && i < opened->len; i++) {
		SondFilePart *child = g_ptr_array_index(opened, i);

		for (guint j = 0; j < pflege->pending_sibling_old->len; j++)
			if (!g_strcmp0(sond_file_part_get_path(child),
					g_ptr_array_index(pflege->pending_sibling_old, j))) {
				sond_file_part_set_path(child, g_ptr_array_index(pflege->pending_sibling_new, j));
				break;
			}
	}
}

/* ------------------------------------------------------------------------
 * Struktur von PDF und Mail im Index
 * --------------------------------------------------------------------- */

void sond_projekt_pflege_structure_add(SondProjektPflege *pflege, SondFilePart *container) {
	if (!container)
		return;

	if (!pflege->pending_structure)
		pflege->pending_structure = g_ptr_array_new_with_free_func(g_object_unref);
	else if (g_ptr_array_find(pflege->pending_structure, container, NULL))
		return;

	g_ptr_array_add(pflege->pending_structure, g_object_ref(container));
}

void sond_projekt_pflege_structure_clear(SondProjektPflege *pflege) {
	g_clear_pointer(&pflege->pending_structure, g_ptr_array_unref);
}

void sond_projekt_pflege_structure_refresh(SondProjektPflege *pflege) {
	SondIndexCtx *index_ctx = pflege_index_ctx(pflege);

	if (!pflege->pending_structure || !index_ctx)
		return;

	for (guint i = 0; i < pflege->pending_structure->len; i++) {
		GError *error = NULL;

		if (sond_process_file_record_structure(index_ctx,
				g_ptr_array_index(pflege->pending_structure, i), &error)) {
			LOG_WARN("%s: %s", __func__, error ? error->message : "?");
			g_clear_error(&error);
		}
	}
}

/* ------------------------------------------------------------------------
 * Verschieben, Umbenennen
 * --------------------------------------------------------------------- */

//Fehler der Index-DB (idx_err wird freigegeben) in *error übernehmen
static void set_idx_error(GError **error, gchar const *func, gchar const *what, GError **idx_err) {
	if (error)
		*error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED, "%s: %s: %s", func, what,
				*idx_err ? (*idx_err)->message : "?");
	g_clear_error(idx_err);
}

//Mimepart-Nummern hinter prefix in store/work und Index umzählen
static gint gmessage_renumber_all(SondProjektPflege *pflege, gchar const *prefix, gint index,
		gboolean into, GError **error) {
	if (dbase_zond_update_gmessage_index(pflege_dbase(pflege), prefix, index, into, error))
		return -1;

	return sond_projekt_pflege_gmessage_index_renumber(pflege, prefix, index, into, error);
}

gint sond_projekt_pflege_move(SondProjektPflege *pflege, SondProjektMove const *m, GError **error) {
	DBaseZond *dbase = pflege_dbase(pflege);

	if (sond_projekt_pflege_tx_begin(pflege, TRUE, TRUE, error))
		return -1;

	//Index-Pfad umbenennen - in derselben Transaktion wie store/work
	if (sond_projekt_pflege_index_tx(pflege)) {
		SondIndexCtx *index_tx = sond_projekt_pflege_index_tx(pflege);
		GError *idx_err = NULL;

		/* Gilt der Zielpfad (direkt oder über einen Vorfahren) schon als abgedeckt, ist das VOR dem
		 * Umbenennen aufzulösen: hierher kommt neuer, ungeprüfter Inhalt. Reihenfolge wichtig, sonst
		 * würde der gerade umbenannte, korrekte eigene Eintrag gleich wieder mitgelöscht. Nicht beim
		 * Umbenennen an Ort und Stelle (index_to -1): der Inhalt bleibt, ein abdeckender Vorfahre
		 * bliebe sonst aufgelöst. */
		if (!m->rename_in_gmessage && m->index_to != -1 &&
				!sond_index_ctx_coverage_invalidate(index_tx, m->prefix_new, m->project_dir,
				&idx_err)) {
			sond_projekt_pflege_tx_abort(pflege);
			set_idx_error(error, __func__, "sond_index_ctx_coverage_invalidate", &idx_err);

			return -1;
		}

		if (!m->rename_in_gmessage &&
				!sond_index_ctx_rename_file(index_tx, m->prefix_old, m->prefix_new, &idx_err)) {
			sond_projekt_pflege_tx_abort(pflege);
			set_idx_error(error, __func__, "sond_index_ctx_rename_file", &idx_err);

			return -1;
		}
	}

	//alle Dateien, die mit prefix_old anfangen (einschließlich der Datei selbst) umbenennen
	if (!m->rename_in_gmessage &&
			dbase_zond_update_path(dbase, m->prefix_old, m->prefix_new, error)) {
		sond_projekt_pflege_tx_abort(pflege); //Fehler von update_path bleibt erhalten

		return -1;
	}

	//aus einer Mail heraus: die nachfolgenden Nummern rücken auf (-1)
	if (m->from_gmessage && !m->rename_in_gmessage) {
		g_autofree gchar *prefix_gmessage = g_strndup(m->prefix_old,
				strlen(m->prefix_old) - strlen(strrchr(m->prefix_old, '/') + 1));

		if (gmessage_renumber_all(pflege, prefix_gmessage, m->index_from, FALSE, error)) {
			sond_projekt_pflege_tx_abort(pflege);

			return -1;
		}
	}

	//in eine Mail hinein
	if (!m->rename_in_gmessage && m->into_gmessage) {
		gint rc = 0;
		//"alpha" wieder wegnehmen
		g_autofree gchar *prefix_gmessage = g_strndup(m->prefix_new,
				strlen(m->prefix_new) - strlen(strrchr(m->prefix_new, '/') + 1));
		g_autofree gchar *path_final = NULL;

		//Nummern ab index_to rücken um 1 weiter
		if (gmessage_renumber_all(pflege, prefix_gmessage, m->index_to, TRUE, error)) {
			sond_projekt_pflege_tx_abort(pflege);

			return -1;
		}

		//Platzhalter "alpha" auf den Zielindex setzen
		path_final = g_strdup_printf("%s%d", prefix_gmessage, m->index_to);
		rc = dbase_zond_update_path(dbase, m->prefix_new, path_final, error);

		if (!rc && sond_projekt_pflege_index_tx(pflege)) {
			GError *idx_err = NULL;

			if (!sond_index_ctx_rename_file(sond_projekt_pflege_index_tx(pflege), m->prefix_new,
					path_final, &idx_err)) {
				set_idx_error(error, __func__, "sond_index_ctx_rename_file", &idx_err);
				rc = -1;
			}
		}

		if (rc) {
			sond_projekt_pflege_tx_abort(pflege);

			return -1;
		}
	}

	/* Anhang einer PDF: Adressen der übrigen Anhänge nachführen - nach dem Element selbst, damit sich
	 * alte und neue Pfade nicht überschneiden. Bleibt der Anhang in derselben PDF, ist es ein
	 * Umbenennen, sonst verlässt er sie (wie Löschen). */
	sond_projekt_pflege_emb_sibling_clear(pflege);
	if (m->sfp_emb) {
		gboolean same_pdf = sond_file_part_get_parent(m->sfp_emb) == m->sfp_target;

		if (sond_projekt_pflege_emb_sibling_changes(pflege, m->sfp_emb,
				same_pdf ? m->base_new : NULL, error) ||
				sond_projekt_pflege_emb_sibling_apply(pflege, error)) {
			sond_projekt_pflege_emb_sibling_clear(pflege);
			sond_projekt_pflege_tx_abort(pflege);

			return -1;
		}
	}

	return 0;
}

/* ------------------------------------------------------------------------
 * Transaktionsrahmen
 * --------------------------------------------------------------------- */

/* Transaktion über store und work (dual) oder nur über work? Ohne work (Client) gibt es nur den store:
 * dann läuft auch eine Änderung "nur der Anbindungen" über ihn. */
static gboolean tx_ueber_store(DBaseZond *dbase, gboolean dual) {
	return dual || !dbase->zond_dbase_work;
}

gint sond_projekt_pflege_tx_begin(SondProjektPflege *pflege, gboolean dual, gboolean with_index,
		GError **error) {
	DBaseZond *dbase = pflege_dbase(pflege);
	gint rc = 0;

	//Index-DB als Schema idx anhängen (vor der Transaktion): ihre Schreibzugriffe laufen in derselben
	if (with_index && sond_projekt_pflege_index_tx_begin(pflege, error))
		return -1;

	rc = tx_ueber_store(dbase, dual) ? dbase_zond_begin(dbase, error) :
			zond_dbase_begin(dbase->zond_dbase_work, error);
	if (rc) {
		sond_projekt_pflege_index_tx_end(pflege);

		return -1;
	}

	return 0;
}

void sond_projekt_pflege_delete_merken(SondProjektPflege *pflege, gchar const *path) {
	g_free(pflege->pending_delete_path);
	pflege->pending_delete_path = g_strdup(path);
}

void sond_projekt_pflege_move_merken(SondProjektPflege *pflege, gchar const *path_old,
		gchar const *path_new, gboolean physical) {
	g_free(pflege->pending_move_old);
	g_free(pflege->pending_move_new);
	pflege->pending_move_old = g_strdup(path_old);
	pflege->pending_move_new = g_strdup(path_new);
	pflege->pending_move_physical = path_old && path_new && physical;
}

/* Schreibt eine lesbare Fehlerdatei ZOND_FEHLER_<Zeitstempel>.txt ins Projektverzeichnis, wenn eine
 * Verschiebung/Umbenennung nicht gespeichert und (soweit physisch möglich) nicht zurückgenommen werden
 * konnte. Einziger dauerhafter Anhaltspunkt, weil die Anwendung danach das Projekt schließt.
 * path_old/path_new NULL: keine physische Änderung betroffen; msg_revert NULL: kein Rückgängigmachen
 * versucht. Best-effort: Fehler beim Schreiben werden nur geloggt. */
static void write_commit_failure_report(gchar const *project_dir, gchar const *path_old,
		gchar const *path_new, gchar const *msg_commit, gchar const *msg_revert) {
	g_autoptr(GDateTime) now = NULL;
	g_autofree gchar *timestamp = NULL;
	g_autofree gchar *filename = NULL;
	GError *error_report = NULL;
	FILE *fp = NULL;

	if (!project_dir)
		return;

	now = g_date_time_new_now_local();
	timestamp = g_date_time_format(now, "%Y%m%d_%H%M%S");
	filename = g_strdup_printf("%s/ZOND_FEHLER_%s.txt", project_dir, timestamp);

	fp = sond_fopen(filename, "w", &error_report);
	if (!fp) {
		LOG_WARN("%s: sond_fopen('%s'): %s", __func__, filename,
				error_report ? error_report->message : "?");
		g_clear_error(&error_report);

		return;
	}

	fprintf(fp, "ZOND - Fehlerbericht\n");
	fprintf(fp, "Zeitpunkt: %s\n\n", timestamp);
	fprintf(fp, "Eine Verschiebung/Umbenennung konnte nicht gespeichert werden. Das Programm wurde "
			"sicherheitshalber beendet, nachdem versucht wurde, das Projekt zu speichern und zu "
			"schließen.\n\n");

	if (path_old && path_new) {
		fprintf(fp, "Alter Pfad: %s\n", path_old);
		fprintf(fp, "Neuer Pfad: %s\n\n", path_new);
	} else
		fprintf(fp, "(Keine physische Dateisystem-Änderung betroffen.)\n\n");

	fprintf(fp, "Fehler beim Speichern:\n%s\n\n", msg_commit ? msg_commit : "unbekannt");

	if (msg_revert)
		fprintf(fp, "Fehler beim Rückgängigmachen der Verschiebung:\n%s\n", msg_revert);
	else if (path_old && path_new)
		fprintf(fp, "Ein Rückgängigmachen wurde nicht versucht.\n");

	fclose(fp);
}

//Vormerkungen verwerfen (nach jedem Abschluss)
static void pending_clear(SondProjektPflege *pflege) {
	g_clear_pointer(&pflege->pending_delete_path, g_free);
	g_clear_pointer(&pflege->pending_move_old, g_free);
	g_clear_pointer(&pflege->pending_move_new, g_free);
	pflege->pending_move_physical = FALSE;
	sond_projekt_pflege_emb_sibling_clear(pflege);
	sond_projekt_pflege_structure_clear(pflege);
}

/* Ist das Elternverzeichnis von path (echtes Dateisystemverzeichnis unterhalb von project_dir) leer?
 * Dann ist es nur im leeren Sinn "vollständig abgedeckt": die Coverage entfernen, damit es nicht grün
 * bleibt. Rückgabe TRUE, wenn das Verzeichnis leer war (der Aufrufer fasst dann nichts zusammen),
 * auch wenn das Entfernen scheitert. Verzeichnisse in Containern (mit "//"), die Projektwurzel und
 * nicht lesbare Verzeichnisse: FALSE, nichts zu tun. */
static gboolean coverage_leeres_verzeichnis(SondIndexCtx *index_ctx, gchar const *path,
		gchar const *project_dir) {
	g_autofree gchar *parent = NULL;
	g_autofree gchar *parent_abs = NULL;
	gchar const *slash = NULL;
	GDir *dir = NULL;
	gboolean leer = FALSE;
	GError *idx_err = NULL;

	if (!project_dir || strstr(path, "//"))
		return FALSE;

	slash = strrchr(path, '/');
	if (!slash)
		return FALSE;

	parent = g_strndup(path, slash - path);
	parent_abs = g_build_filename(project_dir, parent, NULL);

	dir = g_dir_open(parent_abs, 0, NULL);
	if (!dir)
		return FALSE;

	leer = g_dir_read_name(dir) == NULL;
	g_dir_close(dir);

	if (leer && !sond_index_ctx_coverage_clear(index_ctx, parent, &idx_err)) {
		LOG_WARN("%s: sond_index_ctx_coverage_clear('%s'): %s", __func__, parent,
				idx_err ? idx_err->message : "?");
		g_clear_error(&idx_err);
	}

	return leer;
}

SondProjektTxErgebnis sond_projekt_pflege_tx_ende(SondProjektPflege *pflege, gboolean suc,
		gboolean dual, gchar const *project_dir, GError **error) {
	DBaseZond *dbase = pflege_dbase(pflege);
	SondProjektTxErgebnis ergebnis = SOND_PROJEKT_TX_OK;
	GError *error_int = NULL;

	if (!suc) {
		if (tx_ueber_store(dbase, dual))
			dbase_zond_rollback(dbase, &error_int);
		else
			zond_dbase_rollback(dbase->zond_dbase_work, &error_int);

		sond_projekt_pflege_index_tx_end(pflege);
		g_clear_error(&error_int);
		pending_clear(pflege);

		return SOND_PROJEKT_TX_OK;
	}

	/* Löschung war erfolgreich: ist das Elternverzeichnis jetzt - und nur jetzt, weil der Knoten
	 * physisch weg ist - vollständig abgedeckt (die gelöschte, nicht indizierte Datei war ggf. der
	 * einzige "Lückenfüller")? Dann wieder zu einem Ordner-Eintrag zusammenfassen. Vor dem Commit,
	 * damit der Eintrag Teil derselben Transaktion ist. Ist das Verzeichnis dadurch leer geworden,
	 * verliert es stattdessen seine Coverage (ein leerer Ordner soll nicht grün bleiben). */
	if (pflege->pending_delete_path && sond_projekt_pflege_index_tx(pflege) &&
			!coverage_leeres_verzeichnis(sond_projekt_pflege_index_tx(pflege),
					pflege->pending_delete_path, project_dir)) {
		GError *collapse_error = NULL;

		if (!sond_index_ctx_coverage_try_collapse(sond_projekt_pflege_index_tx(pflege),
				pflege->pending_delete_path, project_dir, &collapse_error)) {
			LOG_WARN("%s: sond_index_ctx_coverage_try_collapse('%s'): %s", __func__,
					pflege->pending_delete_path, collapse_error ? collapse_error->message : "?");
			g_clear_error(&collapse_error);
		}
	}

	if (tx_ueber_store(dbase, dual) ? dbase_zond_commit(dbase, &error_int) :
			zond_dbase_commit(dbase->zond_dbase_work, &error_int)) {
		gboolean reverted = FALSE;
		GError *error_revert = NULL;

		sond_projekt_pflege_index_tx_end(pflege);

		//Physische Verschiebung zurücknehmen; vertauschte Pfade decken Umbenennen und Verschieben ab
		if (dual && pflege->pending_move_physical)
			reverted = sond_rename(pflege->pending_move_new, pflege->pending_move_old,
					&error_revert);

		if (!dual || reverted) {
			//zurückgenommen, keine Nachführung
			ergebnis = SOND_PROJEKT_TX_FEHLER;
		} else {
			/* Kein Zurücknehmen möglich oder gescheitert (z.B. reine Mimepart-Renummerierung ohne
			 * physische Aktion): Fehlerbericht schreiben, die Anwendung beendet sich. */
			write_commit_failure_report(project_dir,
					pflege->pending_move_physical ? pflege->pending_move_old : NULL,
					pflege->pending_move_physical ? pflege->pending_move_new : NULL,
					error_int ? error_int->message : NULL,
					error_revert ? error_revert->message : NULL);
			ergebnis = SOND_PROJEKT_TX_KRITISCH;
		}

		if (error && error_int)
			*error = g_error_copy(error_int);
		g_clear_error(&error_int);
		g_clear_error(&error_revert);
		pending_clear(pflege);

		return ergebnis;
	}

	sond_projekt_pflege_index_tx_end(pflege);

	//geöffnete Anhänge auf ihre nachgeführte Adresse setzen, Struktur geänderter PDFs/Mails neu einlesen
	sond_projekt_pflege_emb_sibling_adjust_opened(pflege);
	sond_projekt_pflege_structure_refresh(pflege);
	pending_clear(pflege);

	return SOND_PROJEKT_TX_OK;
}

/* ------------------------------------------------------------------------
 * Kopieren (Einfügen)
 * --------------------------------------------------------------------- */

void sond_projekt_pflege_insert(SondProjektPflege *pflege, gchar const *prefix_new,
		gchar const *project_dir, SondFilePart *container) {
	SondIndexCtx *index_ctx = pflege_index_ctx(pflege);
	GError *idx_err = NULL;

	if (!index_ctx)
		return;

	if (!sond_index_ctx_coverage_invalidate(index_ctx, prefix_new, project_dir, &idx_err)) {
		LOG_WARN("%s: sond_index_ctx_coverage_invalidate('%s'): %s", __func__, prefix_new,
				idx_err ? idx_err->message : "?");
		g_clear_error(&idx_err);
	}

	//Struktur der Ziel-PDF/Mail nach dem Kopieren neu einlesen
	sond_projekt_pflege_structure_clear(pflege);
	sond_projekt_pflege_structure_add(pflege, container);
}

void sond_projekt_pflege_insert_ende(SondProjektPflege *pflege, gboolean suc) {
	if (suc)
		sond_projekt_pflege_structure_refresh(pflege);
	sond_projekt_pflege_structure_clear(pflege);
}

/* ------------------------------------------------------------------------
 * Löschen
 * --------------------------------------------------------------------- */

gint sond_projekt_pflege_delete(SondProjektPflege *pflege, SondProjektDelete const *d,
		gboolean *dual, GError **error) {
	DBaseZond *dbase = pflege_dbase(pflege);
	SondIndexCtx *index_tx = NULL;
	g_autofree gchar *prefix = NULL;

	/* Nur eine Datei oder ein Verzeichnis (section NULL) betrifft auch den Index; ein Abschnitt nur
	 * work. Mimepart und Anhang einer PDF schreiben wie ein Mimepart in beide Datenbanken. */
	*dual = d->from_gmessage || d->sfp_emb || !d->section;

	if (d->from_gmessage)
		prefix = g_strndup(d->path, strlen(d->path) - strlen(strrchr(d->path, '/') + 1));

	if (sond_projekt_pflege_tx_begin(pflege, *dual, !d->section, error))
		return -1;

	index_tx = sond_projekt_pflege_index_tx(pflege);
	if (index_tx) {
		GError *idx_err = NULL;

		if (!sond_index_ctx_clear_file(index_tx, d->path, &idx_err)) {
			sond_projekt_pflege_tx_abort(pflege);
			set_idx_error(error, __func__, "sond_index_ctx_clear_file", &idx_err);

			return -1;
		}

		/* Einen eigenen coverage-Eintrag für genau diesen Pfad (oder darunter) entfernen, damit nicht
		 * später ein neuer, ganz anderer Pfad gleichen Namens fälschlich als schon abgedeckt gilt.
		 * Ein abdeckender Vorfahre bleibt unberührt: Löschen von Inhalt kann seine Aussage nicht
		 * verletzen. */
		if (!sond_index_ctx_coverage_clear(index_tx, d->path, &idx_err)) {
			sond_projekt_pflege_tx_abort(pflege);
			set_idx_error(error, __func__, "sond_index_ctx_coverage_clear", &idx_err);

			return -1;
		}
	}

	//aus einer Mail heraus: die nachfolgenden Nummern rücken auf
	if (d->from_gmessage &&
			gmessage_renumber_all(pflege, prefix, d->index_from, FALSE, error)) {
		sond_projekt_pflege_tx_abort(pflege);

		return -1;
	}

	/* Die (nicht mehr angebundene, s. check_delete) FILE_PART-Zeile zum Pfad in work und store
	 * mitlöschen, sonst bleibt sie als Müll stehen. Vor dem Nachführen der Anhang-Adressen: ein
	 * Geschwister kann danach genau die Adresse des gelöschten Anhangs tragen (bild.txt (1) wird
	 * bild.txt) und darf nicht mitgelöscht werden. */
	if (!d->section && !d->from_gmessage && dbase_zond_remove_filepart(dbase, d->path, error)) {
		sond_projekt_pflege_emb_sibling_clear(pflege);
		sond_projekt_pflege_tx_abort(pflege);

		return -1;
	}

	//Anhang einer PDF: Adressen der übrigen Anhänge nachführen
	sond_projekt_pflege_emb_sibling_clear(pflege);
	if (d->sfp_emb && (sond_projekt_pflege_emb_sibling_changes(pflege, d->sfp_emb, NULL, error) ||
			sond_projekt_pflege_emb_sibling_apply(pflege, error))) {
		sond_projekt_pflege_emb_sibling_clear(pflege);
		sond_projekt_pflege_tx_abort(pflege);

		return -1;
	}

	/* Erst jetzt, unmittelbar vor dem garantierten Erfolg, für den Abschluss vormerken: nach der
	 * physischen Löschung kann das Elternverzeichnis vollständig abgedeckt sein (die gelöschte
	 * Datei war ggf. der einzige "Lückenfüller"). Eine Löschung ist nie eine physische Verschiebung:
	 * die Vormerkung einer früheren Verschiebung verwerfen, sonst würde ein gescheiterter Commit sie
	 * versehentlich zurückdrehen. */
	sond_projekt_pflege_delete_merken(pflege,
			(pflege_index_ctx(pflege) && !d->section) ? d->path : NULL);
	sond_projekt_pflege_move_merken(pflege, NULL, NULL, FALSE);

	//Struktur der PDF/Mail nach dem Löschen neu einlesen
	sond_projekt_pflege_structure_clear(pflege);
	sond_projekt_pflege_structure_add(pflege, d->container);

	return 0;
}

/* ------------------------------------------------------------------------
 * Anbindungen und Abschnitte
 * --------------------------------------------------------------------- */

//Die Datenbank, in der Anbindungen gelesen und geändert werden: work in zond, store im Client
static ZondDBase* anbindungen_db(SondProjektPflege *pflege) {
	return dbase_zond_primary(pflege_dbase(pflege));
}

gint sond_projekt_pflege_anbindung_volltreffer(SondProjektPflege *pflege, gchar const *filepart,
		gchar const *section, GError **error) {
	//Funktion testet, ob mindestens ein Abschnitt in der Datenbank die section mindestens umfaßt
	return zond_dbase_test_path_section(anbindungen_db(pflege), filepart, section, TRUE, error);
}

gint sond_projekt_pflege_section_id(SondProjektPflege *pflege, gchar const *filepart,
		gchar const *section, gint *ID, GError **error) {
	*ID = 0; //zond_dbase_get_section() setzt die ID nur bei einem Treffer
	return zond_dbase_get_section(anbindungen_db(pflege), filepart, section, ID, error) ? -1 : 0;
}

gint sond_projekt_pflege_section_text(SondProjektPflege *pflege, gchar const *filepart,
		gchar const *section, gchar **text, GError **error) {
	gint ID = 0;

	if (sond_projekt_pflege_section_id(pflege, filepart, section, &ID, error))
		return -1;

	if (ID == 0) { //nicht in der Datenbank: Text aus dem Bereich selbst
		Anbindung anbindung = { 0 };

		anbindung_parse_file_section(section, &anbindung);
		*text = anbindung_to_human_readable(&anbindung);

		return 0;
	}

	return zond_dbase_get_node(anbindungen_db(pflege), ID, NULL, NULL, NULL, NULL, NULL, text, NULL,
			error) ? -1 : 0;
}

gint sond_projekt_pflege_section_set_text(SondProjektPflege *pflege, gchar const *filepart,
		gchar const *section, gchar const *text, gint *ID, GError **error) {
	gint ID_section = 0;

	if (sond_projekt_pflege_section_id(pflege, filepart, section, &ID_section, error))
		return -1;

	if (ID_section == 0) {
		if (error)
			*error = g_error_new(ZOND_ERROR, 0, "%s\nAbschnitt nicht gefunden", __func__);

		return -1;
	}

	if (zond_dbase_update_node_text(anbindungen_db(pflege), ID_section, text, error))
		return -1;

	if (ID)
		*ID = ID_section;

	return 0;
}

gint sond_projekt_pflege_section_delete(SondProjektPflege *pflege, gchar const *filepart,
		gchar const *section, GError **error) {
	gint ID = 0;

	if (sond_projekt_pflege_section_id(pflege, filepart, section, &ID, error))
		return -1;

	/* Ist ID selbst oder ein Abkömmling im Bestandsverzeichnis angebunden, wurde die Löschung
	 * schon vorher verhindert (check_delete): für einen angebundenen Abschnitt wird das hier nie
	 * erreicht. */
	return zond_dbase_remove_node(anbindungen_db(pflege), ID, error) ? -1 : 0;
}

gint sond_projekt_pflege_section_angebunden(SondProjektPflege *pflege, gchar const *filepart,
		gchar const *section, gboolean *angebunden, GError **error) {
	ZondDBase *db = anbindungen_db(pflege);
	gint ID = 0;
	gint anbindung = 0;
	gint kopie = 0;

	*angebunden = FALSE;

	if (sond_projekt_pflege_section_id(pflege, filepart, section, &ID, error))
		return -1;

	if (ID == 0) {
		if (error)
			*error = g_error_new(ZOND_ERROR, 0, "%s\nAbschnitt nicht gefunden", __func__);

		return -1;
	}

	if (zond_dbase_get_baum_inhalt_file_from_file_part(db, ID, &anbindung, error) ||
			zond_dbase_get_baum_auswertung_copy(db, ID, &kopie, error))
		return -1;

	*angebunden = anbindung != 0 || kopie != 0;

	return 0;
}

gint sond_projekt_pflege_section_aufloesen(SondProjektPflege *pflege, gchar const *filepart,
		gchar const *section, GError **error) {
	ZondDBase *db = anbindungen_db(pflege);
	gint ID = 0;
	gint anchor = 0;

	if (sond_projekt_pflege_section_id(pflege, filepart, section, &ID, error))
		return -1;

	if (ID == 0) {
		if (error)
			*error = g_error_new(ZOND_ERROR, 0, "%s\nAbschnitt nicht gefunden", __func__);

		return -1;
	}

	/* Kinder der Reihe nach hinter den Abschnitt setzen (immer das erste Kind, das jeweils nächste
	 * folgt hinter dem eben verschobenen), dann den leeren Abschnitt entfernen */
	anchor = ID;
	while (1) {
		gint child = 0;

		if (zond_dbase_get_first_child(db, ID, &child, error))
			return -1;

		if (!child)
			break;

		if (zond_dbase_verschieben_knoten(db, child, anchor, FALSE, error))
			return -1;

		anchor = child;
	}

	return zond_dbase_remove_node(db, ID, error) ? -1 : 0;
}

gint sond_projekt_pflege_sections(SondProjektPflege *pflege, gchar const *filepart,
		gchar const *section, GPtrArray **sections, GError **error) {
	ZondDBase *db = anbindungen_db(pflege);
	g_autoptr(GPtrArray) arr = NULL;
	gint ID = 0;
	gint child = 0;

	if (sond_projekt_pflege_section_id(pflege, filepart, section, &ID, error))
		return -1;

	if (section && ID == 0) {
		if (error)
			*error = g_error_new(ZOND_ERROR, 0, "%s\nAbschnitt nicht gefunden", __func__);

		return -1;
	}

	if (zond_dbase_get_first_child(db, ID, &child, error))
		return -1;

	if (!sections)
		return child ? 1 : 0;

	arr = g_ptr_array_new_with_free_func(g_free);
	while (child) {
		gchar *section_child = NULL;
		gint younger_sibling_id = 0;

		if (zond_dbase_get_node(db, child, NULL, NULL, NULL, &section_child, NULL, NULL, NULL,
				error))
			return -1;

		g_ptr_array_add(arr, section_child);

		if (zond_dbase_get_younger_sibling(db, child, &younger_sibling_id, error))
			return -1;

		child = younger_sibling_id;
	}

	*sections = g_ptr_array_ref(arr);

	return 0;
}
