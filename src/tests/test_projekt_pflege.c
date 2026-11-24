/*
 sond (test_projekt_pflege.c) - Akten, Beweisstücke, Unterlagen
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

/*
 * Tests der Pflegelogik (sond_projekt_pflege): Verschieben, Löschen, Einfügen, Abschnitte und
 * Transaktionsende gegen eine Projekt-DB (store, optional work) und einen Index-Kontext.
 * Dateien (E-Mail, PDF mit Anhängen) werden selbst erzeugt. Die Quelle wird eingebunden, weil
 * die Tests die vorgemerkten Zustände (pending_*) prüfen.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <gmime/gmime.h>
#include <string.h>

#include "../znd/sond_projekt_pflege.c"
#include "../znd/zond_dbase.h"
#include "../sond_process_file.h"
#include "../sond_fileparts.h"
#include "tvfm_test_support.h"

typedef struct {
	Fix *fx;
	gchar *dir;
	DBaseZond *db;
	SondIndexCtx *idx;
	SondProjektPflege *p;
} F;
static DBaseZond* k_db(gpointer d) { return ((F*) d)->db; }
static SondIndexCtx* k_idx(gpointer d) { return ((F*) d)->idx; }

static void sql(sqlite3 *s, gchar const *q) {
	gchar *e = NULL;
	if (sqlite3_exec(s, q, NULL, NULL, &e) != SQLITE_OK)
		g_error("SQL %s: %s", q, e ? e : "?");
}

static gint count(sqlite3 *s, gchar const *q) {
	sqlite3_stmt *st = NULL;
	gint n = -1;
	if (sqlite3_prepare_v2(s, q, -1, &st, NULL) != SQLITE_OK) return -1;
	if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
	sqlite3_finalize(st);
	return n;
}

static void up(F *f, gboolean index) {
	GError *err = NULL;
	gchar *sp;
	SondProjektKontext k = { k_db, k_idx, f };

	f->fx = fix_new();
	f->dir = f->fx->dir;
	schreibe_eml(f->fx, "mail.eml", "Betreff", "Text der Mail", "anhang.txt", "Inhalt des Anhangs");
	schreibe_pdf(f->fx, "emb.pdf", "bild.txt", "Eins", "bild.txt", "Zwei", NULL);
	sp = g_build_filename(f->dir, "s.ZND", NULL);
	{ ZondDBase *z = zond_dbase_new(sp, FALSE, TRUE, &err); g_assert_no_error(err); g_object_unref(z); }
	f->db = dbase_zond_new_store(sp, &err);
	g_assert_no_error(err);
	g_assert_nonnull(f->db);
	sql(zond_dbase_get_dbase(f->db->zond_dbase_store),
		"INSERT INTO knoten (ID,parent_ID,older_sibling_ID,type,link,file_part) VALUES (10,0,0,5,NULL,'dir/a.pdf');"
		"INSERT INTO knoten (ID,parent_ID,older_sibling_ID,type,link,file_part) VALUES (11,1,0,2,10,NULL);");
	g_free(sp);
	if (index) {
		gchar *ip = g_build_filename(f->dir, "i.db", NULL);
		f->idx = sond_index_ctx_new(ip, NULL, 0, 0, &err);
		g_assert_no_error(err);
		g_assert_nonnull(f->idx);
		g_free(ip);
	}
	f->p = sond_projekt_pflege_new(&k);
}

static void down(F *f) {
	sond_projekt_pflege_free(f->p);
	if (f->idx) sond_index_ctx_free(f->idx);
	dbase_zond_free(f->db);
	fix_free(f->fx);
}

static void test_check(void) {
	F f = { 0 };
	GError *err = NULL;
	DBaseZondRef ref = DBASE_ZOND_REF_NONE;

	up(&f, FALSE);
	g_assert_cmpint(sond_projekt_pflege_check_delete(f.p, "dir/a.pdf", FALSE, FALSE, NULL, &ref, &err), ==, 0);
	g_assert_cmpint(ref, !=, DBASE_ZOND_REF_NONE);
	g_assert_cmpint(sond_projekt_pflege_check_delete(f.p, "dir", TRUE, FALSE, NULL, &ref, &err), ==, 0);
	g_assert_cmpint(ref, !=, DBASE_ZOND_REF_NONE);
	g_assert_cmpint(sond_projekt_pflege_check_delete(f.p, "dir/a.pdf", TRUE, TRUE, NULL, &ref, &err), ==, 0);
	g_assert_cmpint(ref, ==, DBASE_ZOND_REF_NONE); //Container-Präfix "dir/a.pdf//": nichts darunter
	g_assert_cmpint(sond_projekt_pflege_check_delete(f.p, "andere/x.pdf", FALSE, FALSE, NULL, &ref, &err), ==, 0);
	g_assert_cmpint(ref, ==, DBASE_ZOND_REF_NONE);
	g_assert_cmpint(sond_projekt_pflege_check_delete(f.p, "ander", TRUE, FALSE, NULL, &ref, &err), ==, 0);
	g_assert_cmpint(ref, ==, DBASE_ZOND_REF_NONE);
	g_assert_no_error(err);
	down(&f);
}

static void test_ohne_index(void) {
	F f = { 0 };
	GError *err = NULL;

	up(&f, FALSE);
	g_assert_cmpint(sond_projekt_pflege_index_tx_begin(f.p, &err), ==, 0);
	g_assert_null(sond_projekt_pflege_index_tx(f.p));
	g_assert_cmpint(sond_projekt_pflege_gmessage_index_renumber(f.p, "m.eml//1", 1, FALSE, &err), ==, 0);
	sond_projekt_pflege_index_tx_end(f.p);
	g_assert_no_error(err);
	down(&f);
}

static void test_index_tx(gboolean commit) {
	F f = { 0 };
	GError *err = NULL;
	sqlite3 *s;

	up(&f, TRUE);
	s = zond_dbase_get_dbase(f.db->zond_dbase_store);

	g_assert_cmpint(sond_projekt_pflege_index_tx_begin(f.p, &err), ==, 0);
	g_assert_no_error(err);
	g_assert_nonnull(sond_projekt_pflege_index_tx(f.p));
	g_assert_cmpint(sond_projekt_pflege_index_tx_begin(f.p, &err), ==, 0); //zweimal: harmlos
	g_assert_cmpint(count(s, "SELECT count(*) FROM idx.sqlite_master;"), >, 0);

	g_assert_cmpint(dbase_zond_begin(f.db, &err), ==, 0);
	g_assert_cmpint(dbase_zond_update_path(f.db, "dir", "neu", &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_gmessage_index_renumber(f.p, "m.eml//2", 2, FALSE, &err), ==, 0);
	g_assert_no_error(err);
	if (commit) {
		g_assert_cmpint(dbase_zond_commit(f.db, &err), ==, 0);
		sond_projekt_pflege_index_tx_end(f.p);
		g_assert_cmpint(count(s, "SELECT count(*) FROM knoten WHERE file_part='neu/a.pdf';"), ==, 1);
	} else {
		sond_projekt_pflege_tx_abort(f.p);
		g_assert_cmpint(count(s, "SELECT count(*) FROM knoten WHERE file_part='dir/a.pdf';"), ==, 1);
	}
	g_assert_null(sond_projekt_pflege_index_tx(f.p));
	//idx abgehängt: Abfrage muss scheitern
	g_assert_cmpint(count(s, "SELECT count(*) FROM idx.sqlite_master;"), ==, -1 + 0 * 1);
	down(&f);
}

static void t_commit(void) { test_index_tx(TRUE); }
static void t_abort(void) { test_index_tx(FALSE); }

static gchar* idx_text(F *f, gchar const *q) {
	sqlite3 *s = NULL;
	sqlite3_stmt *st = NULL;
	gchar *r = NULL;

	g_assert_cmpint(sqlite3_open(f->idx->db_path, &s), ==, SQLITE_OK);
	if (sqlite3_prepare_v2(s, q, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW &&
			sqlite3_column_text(st, 0))
		r = g_strdup((gchar const*) sqlite3_column_text(st, 0));
	sqlite3_finalize(st);
	sqlite3_close(s);
	return r;
}

//Adressen der beiden gleichnamigen Anhänge "bild.txt" von emb.pdf (Schlüssel im Namensbaum)
static void anhang_adressen(F *f, gchar **a1, gchar **a2) {
	SondTVFMItem *pdf = datei(f->fx, "emb.pdf");
	GPtrArray *arr = NULL;
	GError *err = NULL;

	*a1 = *a2 = NULL;
	g_assert_cmpint(sond_tvfm_item_load_children(pdf, &arr, NULL, &err), ==, 0);
	g_assert_no_error(err);
	for (guint i = 0; i < arr->len; i++) {
		SondTVFMItem *k = g_ptr_array_index(arr, i);

		if (g_strcmp0(sond_tvfm_item_get_display_name(k), "bild.txt")) continue;
		if (!*a1) *a1 = sond_tvfm_item_get_filepart(k);
		else *a2 = sond_tvfm_item_get_filepart(k);
	}
	g_assert_nonnull(*a2);
	g_ptr_array_unref(arr);
	g_object_unref(pdf);
}

//Schlüssel (Teil nach "//") einer Adresse
static gchar const* adr_key(gchar const *a) {
	return strstr(a, "//") + 2;
}

static void test_mail(void) {
	F f = { 0 };
	GError *err = NULL;
	gchar *mail = g_strdup("mail.eml");
	gchar *q = NULL, *a = NULL, *b = NULL, *c = NULL;
	SondFilePart *sfp = NULL;

	up(&f, TRUE);
	sfp = sond_file_part_from_filepart(mail, &err);
	g_assert_no_error(err);
	g_assert_nonnull(sfp);

	g_assert_cmpint(sond_process_file_record_structure(f.idx, sfp, &err), ==, 0);
	g_assert_no_error(err);
	q = g_strdup_printf("SELECT total_entries FROM container_entrycount WHERE filename='%s'", mail);
	a = idx_text(&f, q);
	g_print("# Mail: entrycount=%s\n", a ? a : "(keiner)");
	g_assert_nonnull(a);

	//Mimepart-Löschen: Renummerierung verwirft die Strukturangaben der Mail ...
	{
		gchar *prefix = g_strdup_printf("%s//", mail);

		g_assert_cmpint(sond_projekt_pflege_index_tx_begin(f.p, &err), ==, 0);
		g_assert_cmpint(dbase_zond_begin(f.db, &err), ==, 0);
		g_assert_cmpint(sond_projekt_pflege_gmessage_index_renumber(f.p, prefix, 0, FALSE, &err), ==, 0);
		g_assert_no_error(err);
		g_assert_cmpint(dbase_zond_commit(f.db, &err), ==, 0);
		sond_projekt_pflege_index_tx_end(f.p);
		g_free(prefix);
	}
	b = idx_text(&f, q);
	g_assert_null(b);

	//... und das Neueinlesen stellt sie wieder her
	sond_projekt_pflege_structure_add(f.p, sfp);
	sond_projekt_pflege_structure_add(f.p, sfp); //doppelt: nur einmal
	sond_projekt_pflege_structure_refresh(f.p);
	sond_projekt_pflege_structure_clear(f.p);
	c = idx_text(&f, q);
	g_assert_cmpstr(c, ==, a);

	g_object_unref(sfp);
	g_free(q); g_free(a); g_free(b); g_free(c); g_free(mail);
	down(&f);
}

static void test_pdf_anhaenge(void) {
	F f = { 0 };
	GError *err = NULL;
	gchar *pdf = g_strdup("emb.pdf");
	gchar *fp1 = NULL, *fp2 = NULL;
	SondFilePart *sfp = NULL;
	sqlite3 *s;
	gchar *sqlq;

	up(&f, TRUE);
	anhang_adressen(&f, &fp1, &fp2);
	sfp = sond_file_part_from_filepart(fp1, &err);
	g_assert_no_error(err);
	g_assert_nonnull(sfp);
	s = zond_dbase_get_dbase(f.db->zond_dbase_store);

	sqlq = sqlite3_mprintf("INSERT INTO knoten (ID,parent_ID,older_sibling_ID,type,link,file_part) "
			"VALUES (20,0,0,5,NULL,%Q);", fp2);
	sql(s, sqlq);
	sqlite3_free(sqlq);

	//erster Anhang wird gelöscht: der zweite wird als einziger "bild.txt" eindeutig und heißt dann "bild.txt"
	g_assert_cmpint(sond_projekt_pflege_emb_sibling_changes(f.p, sfp, NULL, &err), ==, 0);
	g_assert_no_error(err);
	g_assert_nonnull(f.p->pending_sibling_pdf);
	g_assert_cmpint(f.p->pending_sibling_old->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(f.p->pending_sibling_old, 0), ==, adr_key(fp2));
	g_assert_cmpstr(g_ptr_array_index(f.p->pending_sibling_new, 0), ==, "bild.txt");

	g_assert_cmpint(sond_projekt_pflege_index_tx_begin(f.p, &err), ==, 0);
	g_assert_cmpint(dbase_zond_begin(f.db, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_emb_sibling_apply(f.p, &err), ==, 0);
	g_assert_no_error(err);
	g_assert_cmpint(dbase_zond_commit(f.db, &err), ==, 0);
	sond_projekt_pflege_index_tx_end(f.p);

	g_assert_cmpint(count(s, "SELECT count(*) FROM knoten WHERE file_part='emb.pdf//bild.txt';"), ==, 1);
	g_assert_cmpint(count(s, "SELECT count(*) FROM knoten WHERE ID=20 AND file_part<>'emb.pdf//bild.txt';"), ==, 0);
	sond_projekt_pflege_emb_sibling_clear(f.p);
	g_assert_null(f.p->pending_sibling_pdf);

	//Struktur der PDF einlesen: pdf_embedded enthält die Anhänge
	g_assert_cmpint(sond_process_file_record_structure(f.idx, sond_file_part_get_parent(sfp), &err), ==, 0);
	g_assert_no_error(err);
	{
		gchar *q = g_strdup_printf("SELECT addresses FROM pdf_embedded WHERE filename='%s'", pdf);
		gchar *r = idx_text(&f, q);

		g_assert_nonnull(r);
		g_free(r); g_free(q);
	}

	g_object_unref(sfp);
	g_free(pdf); g_free(fp1); g_free(fp2);
	down(&f);
}

static gchar* kn_path(F *f, gint id) {
	gchar *q = g_strdup_printf("SELECT file_part FROM knoten WHERE ID=%d", id);
	sqlite3_stmt *st = NULL;
	gchar *r = NULL;
	sqlite3 *s = zond_dbase_get_dbase(f->db->zond_dbase_store);

	if (sqlite3_prepare_v2(s, q, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW &&
			sqlite3_column_text(st, 0))
		r = g_strdup((gchar const*) sqlite3_column_text(st, 0));
	sqlite3_finalize(st);
	g_free(q);
	return r;
}

static void ins(F *f, gint id, gchar const *fp) {
	gchar *q = sqlite3_mprintf("INSERT INTO knoten (ID,parent_ID,older_sibling_ID,type,link,file_part) "
			"VALUES (%d,0,0,5,NULL,%Q);", id, fp);
	sql(zond_dbase_get_dbase(f->db->zond_dbase_store), q);
	sqlite3_free(q);
}

static void fin(F *f) {
	GError *err = NULL;
	g_assert_cmpint(dbase_zond_commit(f->db, &err), ==, 0);
	g_assert_no_error(err);
	sond_projekt_pflege_index_tx_end(f->p);
}

static void test_move_datei(void) {
	F f = { 0 };
	GError *err = NULL;
	SondProjektMove m = { .prefix_old = "dir/a.pdf", .prefix_new = "neu/a.pdf", .project_dir = "x", .index_to = 0 };
	gchar *p;

	up(&f, TRUE);
	g_assert_cmpint(sond_projekt_pflege_move(f.p, &m, &err), ==, 0);
	g_assert_no_error(err);
	g_assert_nonnull(sond_projekt_pflege_index_tx(f.p));
	fin(&f);
	p = kn_path(&f, 10);
	g_assert_cmpstr(p, ==, "neu/a.pdf");
	g_free(p);
	down(&f);
}

static void test_move_mail(void) {
	F f = { 0 };
	GError *err = NULL;
	gchar *p;
	//Datei x.pdf wird an Index 0 in die Mail m.eml gesetzt, vorhandene Teile rücken auf
	SondProjektMove m = { .prefix_old = "x.pdf", .prefix_new = "m.eml//alpha", .project_dir = "x",
			.index_to = 0, .into_gmessage = TRUE };
	//Teil m.eml//1 wird herausgenommen, m.eml//2 rückt auf 1
	SondProjektMove o = { .prefix_old = "m.eml//1", .prefix_new = "neu.pdf", .project_dir = "x",
			.index_to = 0, .from_gmessage = TRUE, .index_from = 1 };

	up(&f, TRUE);
	ins(&f, 30, "x.pdf");
	ins(&f, 31, "m.eml//0");
	ins(&f, 32, "m.eml//1");
	ins(&f, 33, "m.eml//2");

	g_assert_cmpint(sond_projekt_pflege_move(f.p, &m, &err), ==, 0);
	g_assert_no_error(err);
	fin(&f);
	p = kn_path(&f, 30); g_assert_cmpstr(p, ==, "m.eml//0"); g_free(p);
	p = kn_path(&f, 31); g_assert_cmpstr(p, ==, "m.eml//1"); g_free(p);
	p = kn_path(&f, 32); g_assert_cmpstr(p, ==, "m.eml//2"); g_free(p);
	p = kn_path(&f, 33); g_assert_cmpstr(p, ==, "m.eml//3"); g_free(p);

	//jetzt Teil //2 (ID 32) heraus
	o.prefix_old = "m.eml//2"; o.index_from = 2;
	g_assert_cmpint(sond_projekt_pflege_move(f.p, &o, &err), ==, 0);
	g_assert_no_error(err);
	fin(&f);
	p = kn_path(&f, 32); g_assert_cmpstr(p, ==, "neu.pdf"); g_free(p);
	p = kn_path(&f, 33); g_assert_cmpstr(p, ==, "m.eml//2"); g_free(p);
	p = kn_path(&f, 30); g_assert_cmpstr(p, ==, "m.eml//0"); g_free(p);
	down(&f);
}

static void test_move_rename_mime(void) {
	F f = { 0 };
	GError *err = NULL;
	gchar *p;
	SondProjektMove m = { .prefix_old = "m.eml//1", .prefix_new = "m.eml//1", .project_dir = "x",
			.index_to = -1, .from_gmessage = TRUE, .index_from = 1, .into_gmessage = TRUE,
			.rename_in_gmessage = TRUE };

	up(&f, TRUE);
	ins(&f, 31, "m.eml//0");
	ins(&f, 32, "m.eml//1");
	g_assert_cmpint(sond_projekt_pflege_move(f.p, &m, &err), ==, 0);
	fin(&f);
	p = kn_path(&f, 32); g_assert_cmpstr(p, ==, "m.eml//1"); g_free(p);
	p = kn_path(&f, 31); g_assert_cmpstr(p, ==, "m.eml//0"); g_free(p);
	down(&f);
}

static void test_move_pdf_anhang(void) {
	F f = { 0 };
	GError *err = NULL;
	gchar *p, *fp1 = NULL, *fp2 = NULL;
	SondFilePart *sfp = NULL;
	SondProjektMove m = { .prefix_new = "aus/bild.txt", .project_dir = "x", .index_to = 0,
			.sfp_target = NULL, .base_new = "bild.txt" };

	up(&f, TRUE);
	anhang_adressen(&f, &fp1, &fp2);
	sfp = sond_file_part_from_filepart(fp1, &err);
	g_assert_nonnull(sfp);
	m.prefix_old = fp1;
	m.sfp_emb = sfp;
	ins(&f, 40, fp1);
	ins(&f, 41, fp2);
	g_assert_cmpint(sond_projekt_pflege_move(f.p, &m, &err), ==, 0);
	g_assert_no_error(err);
	fin(&f);
	p = kn_path(&f, 40); g_assert_cmpstr(p, ==, "aus/bild.txt"); g_free(p);
	p = kn_path(&f, 41); g_assert_cmpstr(p, ==, "emb.pdf//bild.txt"); g_free(p);
	sond_projekt_pflege_emb_sibling_clear(f.p);
	g_object_unref(sfp);
	g_free(fp1); g_free(fp2);
	down(&f);
}

static void test_free_attached(void) {
	F f = { 0 };
	GError *err = NULL;
	SondProjektMove m = { .prefix_old = "dir/a.pdf", .prefix_new = "neu/a.pdf", .project_dir = "x", .index_to = 0 };

	up(&f, TRUE);
	g_assert_cmpint(sond_projekt_pflege_move(f.p, &m, &err), ==, 0);
	//Commit, aber idx bleibt angehängt und der Index-Kontext hat ggf. Statements auf der store-Verbindung
	g_assert_cmpint(dbase_zond_commit(f.db, &err), ==, 0);
	sond_index_ctx_free(f.idx);
	f.idx = NULL;
	down(&f);
}

static void test_tx_ende_ok_und_rollback(void) {
	F f = { 0 };
	GError *err = NULL;
	gchar *p;
	SondProjektMove m = { .prefix_old = "dir/a.pdf", .prefix_new = "neu/a.pdf", .project_dir = "x", .index_to = 0 };

	up(&f, TRUE);
	//Rollback
	g_assert_cmpint(sond_projekt_pflege_move(f.p, &m, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, FALSE, TRUE, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	p = kn_path(&f, 10); g_assert_cmpstr(p, ==, "dir/a.pdf"); g_free(p);
	g_assert_null(sond_projekt_pflege_index_tx(f.p));
	//Commit
	g_assert_cmpint(sond_projekt_pflege_move(f.p, &m, &err), ==, 0);
	sond_projekt_pflege_delete_merken(f.p, "dir/a.pdf");
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, TRUE, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_assert_no_error(err);
	p = kn_path(&f, 10); g_assert_cmpstr(p, ==, "neu/a.pdf"); g_free(p);
	g_assert_null(sond_projekt_pflege_index_tx(f.p));
	g_assert_null(f.p->pending_delete_path);
	down(&f);
}

static void test_tx_ende_work_only(void) {
	F f = { 0 };
	GError *err = NULL;
	ZondDBase *work;
	gchar *wp;

	up(&f, FALSE);
	wp = g_build_filename(f.dir, "w.ZND", NULL);
	work = zond_dbase_new(wp, TRUE, FALSE, &err);
	g_assert_no_error(err);
	f.db->zond_dbase_work = work;
	g_assert_cmpint(sond_projekt_pflege_tx_begin(f.p, FALSE, FALSE, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, FALSE, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_assert_no_error(err);
	g_assert_cmpint(sond_projekt_pflege_tx_begin(f.p, FALSE, FALSE, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, FALSE, FALSE, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_free(wp);
	down(&f);
}

static void test_tx_ende_commit_fehler(void) {
	F f = { 0 };
	GError *err = NULL;
	gchar *a = NULL, *b = NULL, *rep = NULL;
	GDir *d;
	gchar const *n;
	gboolean report = FALSE;

	up(&f, FALSE);
	a = g_build_filename(f.dir, "a.txt", NULL);
	b = g_build_filename(f.dir, "b.txt", NULL);
	g_assert_true(g_file_set_contents(b, "x", 1, NULL)); //Datei liegt schon am neuen Ort

	//Commit ohne offene Transaktion scheitert; physische Verschiebung a -> b wird zurückgenommen
	sond_projekt_pflege_move_merken(f.p, a, b, TRUE);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, TRUE, f.dir, &err), ==, SOND_PROJEKT_TX_FEHLER);
	g_assert_nonnull(err);
	g_clear_error(&err);
	g_assert_true(g_file_test(a, G_FILE_TEST_EXISTS));
	g_assert_false(g_file_test(b, G_FILE_TEST_EXISTS));
	g_assert_null(f.p->pending_move_old);

	//ohne physische Aktion: kritisch, Fehlerdatei im Projektverzeichnis
	sond_projekt_pflege_move_merken(f.p, "x", "y", FALSE);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, TRUE, f.dir, &err), ==, SOND_PROJEKT_TX_KRITISCH);
	g_assert_nonnull(err);
	g_clear_error(&err);
	d = g_dir_open(f.dir, 0, NULL);
	while ((n = g_dir_read_name(d)))
		if (g_str_has_prefix(n, "ZOND_FEHLER_")) {
			report = TRUE;
			rep = g_build_filename(f.dir, n, NULL);
		}
	g_dir_close(d);
	g_assert_true(report);
	{
		gchar *txt = NULL;
		g_assert_true(g_file_get_contents(rep, &txt, NULL, NULL));
		g_assert_nonnull(strstr(txt, "ZOND - Fehlerbericht"));
		g_assert_nonnull(strstr(txt, "Keine physische"));
		g_free(txt);
	}
	g_free(a); g_free(b); g_free(rep);
	down(&f);
}

static void test_collapse_nach_verschieben(void) {
	F f = { 0 };
	GError *err = NULL;
	gchar *sub = NULL, *file = NULL;
	SondProjektMove m = { .prefix_old = "dir/a.pdf", .prefix_new = "dir/a.pdf", .project_dir = "x", .index_to = 0 };

	up(&f, TRUE);
	//Projektverzeichnis mit Ordner "d", der nur noch die abgedeckte Datei "a.pdf" enthält
	sub = g_build_filename(f.dir, "d", NULL);
	g_assert_cmpint(g_mkdir(sub, 0755), ==, 0);
	file = g_build_filename(sub, "a.pdf", NULL);
	g_assert_true(g_file_set_contents(file, "x", 1, NULL));
	g_assert_true(sond_index_ctx_coverage_mark(f.idx, "d/a.pdf", 1, &err));
	g_assert_cmpint(sond_index_ctx_coverage_get(f.idx, "d"), <, 1);

	g_assert_cmpint(sond_projekt_pflege_tx_begin(f.p, TRUE, TRUE, &err), ==, 0);
	sond_projekt_pflege_delete_merken(f.p, "d/b.pdf"); //die herausgenommene, nicht indizierte Datei
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, TRUE, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_assert_no_error(err);
	g_assert_cmpint(sond_index_ctx_coverage_get(f.idx, "d"), >=, 1);
	g_assert_false(f.db->index_attached);
	(void) m;
	g_free(sub); g_free(file);
	down(&f);
}

static void test_insert(void) {
	F f = { 0 };
	GError *err = NULL;
	gchar *sub, *file;
	SondFilePart *sfp = NULL;
	gchar *q, *a, *c;

	up(&f, TRUE);
	sub = g_build_filename(f.dir, "d", NULL);
	g_assert_cmpint(g_mkdir(sub, 0755), ==, 0);
	file = g_build_filename(sub, "a.pdf", NULL);
	g_assert_true(g_file_set_contents(file, "x", 1, NULL));
	g_assert_true(sond_index_ctx_coverage_mark(f.idx, "d", 1, &err));
	g_assert_cmpint(sond_index_ctx_coverage_get(f.idx, "d/a.pdf"), >=, 1); //über den Vorfahren abgedeckt

	//neuer Inhalt unter d: Abdeckung des Ordners wird aufgelöst, das Geschwister bleibt einzeln abgedeckt
	sond_projekt_pflege_insert(f.p, "d/b.pdf", f.dir, NULL);
	sond_projekt_pflege_insert_ende(f.p, TRUE);
	g_assert_cmpint(sond_index_ctx_coverage_get(f.idx, "d"), <, 1);
	g_assert_cmpint(sond_index_ctx_coverage_get(f.idx, "d/a.pdf"), >=, 1);
	g_assert_cmpint(sond_index_ctx_coverage_get(f.idx, "d/b.pdf"), <, 1);

	//Mail als Ziel: Struktur wird nach dem Kopieren eingelesen
	sfp = sond_file_part_from_filepart("mail.eml", &err);
	g_assert_nonnull(sfp);
	sond_projekt_pflege_insert(f.p, "mail.eml//alpha", f.dir, sfp);
	q = g_strdup("SELECT total_entries FROM container_entrycount WHERE filename='mail.eml'");
	a = idx_text(&f, q);
	g_assert_null(a);
	sond_projekt_pflege_insert_ende(f.p, TRUE);
	c = idx_text(&f, q);
	g_assert_nonnull(c);
	g_free(q); g_free(c); g_object_unref(sfp);
	g_free(sub); g_free(file);
	down(&f);
}

static void test_mit_work_und_index(void) {
	F f = { 0 };
	GError *err = NULL;
	gchar *wp, *q;
	ZondDBase *work;
	SondProjektMove m = { .prefix_old = "dir/a.pdf", .prefix_new = "neu/a.pdf", .project_dir = "x", .index_to = 0 };

	up(&f, TRUE);
	wp = g_build_filename(f.dir, "w.ZND", NULL);
	work = zond_dbase_new(wp, TRUE, FALSE, &err);
	g_assert_no_error(err);
	g_assert_cmpint(zond_dbase_backup(f.db->zond_dbase_store, work, &err), ==, 0);
	f.db->zond_dbase_work = work;
	q = sqlite3_mprintf("ATTACH DATABASE %Q AS work;", wp);
	sql(zond_dbase_get_dbase(f.db->zond_dbase_store), q);
	sqlite3_free(q);

	for (gint i = 0; i < 3; i++) {
		g_assert_cmpint(sond_projekt_pflege_move(f.p, &m, &err), ==, 0);
		sond_projekt_pflege_delete_merken(f.p, "dir/b.pdf");
		g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, TRUE, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
		m.prefix_old = i % 2 ? "dir/a.pdf" : "neu/a.pdf";
		m.prefix_new = i % 2 ? "neu/a.pdf" : "dir/a.pdf";
	}
	g_assert_true(sond_index_ctx_coverage_mark(f.idx, "d", 1, &err));
	g_assert_false(f.db->index_attached);
	g_free(wp);
	down(&f);
}

static void test_delete_datei(void) {
	F f = { 0 };
	GError *err = NULL;
	gboolean dual = FALSE;
	SondProjektDelete d = { .path = "dir/a.pdf" };

	up(&f, TRUE);
	//Anker (ID 11) und FILE_PART 10: im Test ist der Anker nicht angebunden zu löschen, also erst Anker weg
	sql(zond_dbase_get_dbase(f.db->zond_dbase_store), "DELETE FROM knoten WHERE ID=11;");
	g_assert_true(sond_index_ctx_coverage_mark(f.idx, "dir/a.pdf", 1, &err));
	g_assert_cmpint(sond_projekt_pflege_delete(f.p, &d, &dual, &err), ==, 0);
	g_assert_no_error(err);
	g_assert_true(dual);
	g_assert_nonnull(sond_projekt_pflege_index_tx(f.p));
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, dual, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_assert_cmpint(count(zond_dbase_get_dbase(f.db->zond_dbase_store), "SELECT count(*) FROM knoten WHERE ID=10;"), ==, 0);
	g_assert_cmpint(sond_index_ctx_coverage_get(f.idx, "dir/a.pdf"), <, 1);
	g_assert_false(f.db->index_attached);
	down(&f);
}

static void test_delete_mimepart_und_rollback(void) {
	F f = { 0 };
	GError *err = NULL;
	gboolean dual = FALSE;
	gchar *p;
	SondProjektDelete d = { .path = "m.eml//1", .from_gmessage = TRUE, .index_from = 1 };

	up(&f, TRUE);
	ins(&f, 31, "m.eml//0");
	ins(&f, 32, "m.eml//1");
	ins(&f, 33, "m.eml//2");
	//Mimepart //1 löschen (die Zeile selbst entfernt der Baum): //2 rückt auf //1
	g_assert_cmpint(sond_projekt_pflege_delete(f.p, &d, &dual, &err), ==, 0);
	g_assert_no_error(err);
	g_assert_true(dual);
	//Rollback nimmt alles zurück
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, FALSE, dual, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	p = kn_path(&f, 33); g_assert_cmpstr(p, ==, "m.eml//2"); g_free(p);
	//noch einmal mit Commit
	g_assert_cmpint(sond_projekt_pflege_delete(f.p, &d, &dual, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, dual, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	p = kn_path(&f, 33); g_assert_cmpstr(p, ==, "m.eml//1"); g_free(p);
	down(&f);
}

static void test_delete_pdf_anhang_und_abschnitt(void) {
	F f = { 0 };
	GError *err = NULL;
	gboolean dual = TRUE;
	gchar *p;
	gchar *fp1 = NULL, *fp2 = NULL;
	SondFilePart *sfp = NULL;
	SondProjektDelete d = { 0 };
	SondProjektDelete s = { .path = "dir/a.pdf", .section = "1,2" };

	up(&f, TRUE);
	anhang_adressen(&f, &fp1, &fp2);
	sfp = sond_file_part_from_filepart(fp1, &err);
	g_assert_nonnull(sfp);
	d.path = fp1;
	d.sfp_emb = sfp;
	ins(&f, 40, fp1);
	ins(&f, 41, fp2);
	g_assert_cmpint(sond_projekt_pflege_delete(f.p, &d, &dual, &err), ==, 0);
	g_assert_no_error(err);
	g_assert_true(dual);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, dual, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	p = kn_path(&f, 41);
	g_assert_cmpstr(p, ==, "emb.pdf//bild.txt"); g_free(p);

	//Abschnitt: nur work, kein Index (hier ohne work wird dennoch begonnen: Store als Primär)
	{
		ZondDBase *work;
		gchar *wp = g_build_filename(f.dir, "w.ZND", NULL);
		work = zond_dbase_new(wp, TRUE, FALSE, &err);
		g_assert_no_error(err);
		f.db->zond_dbase_work = work;
		g_assert_cmpint(sond_projekt_pflege_delete(f.p, &s, &dual, &err), ==, 0);
		g_assert_false(dual);
		g_assert_null(sond_projekt_pflege_index_tx(f.p));
		g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, dual, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
		g_free(wp);
	}
	g_object_unref(sfp);
	g_free(fp1); g_free(fp2);
	down(&f);
}

static void test_leerer_ordner(void) {
	F f = { 0 };
	GError *err = NULL;
	gchar *sub, *a, *b;

	up(&f, TRUE);
	sub = g_build_filename(f.dir, "d", NULL);
	g_assert_cmpint(g_mkdir(sub, 0755), ==, 0);
	a = g_build_filename(sub, "a.pdf", NULL);
	b = g_build_filename(sub, "b.pdf", NULL);
	g_assert_true(g_file_set_contents(a, "x", 1, NULL));
	g_assert_true(g_file_set_contents(b, "x", 1, NULL));
	g_assert_true(sond_index_ctx_coverage_mark(f.idx, "d", 1, &err));

	//eine von zwei Dateien weg: Ordner bleibt abgedeckt
	g_assert_cmpint(g_remove(a), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_tx_begin(f.p, TRUE, TRUE, &err), ==, 0);
	sond_projekt_pflege_delete_merken(f.p, "d/a.pdf");
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, TRUE, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_assert_cmpint(sond_index_ctx_coverage_get(f.idx, "d/b.pdf"), >=, 1);

	//die letzte weg: Ordner leer, nicht mehr abgedeckt
	g_assert_cmpint(g_remove(b), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_tx_begin(f.p, TRUE, TRUE, &err), ==, 0);
	sond_projekt_pflege_delete_merken(f.p, "d/b.pdf");
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, TRUE, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_assert_cmpint(sond_index_ctx_coverage_get(f.idx, "d"), <, 1);
	g_assert_cmpint(sond_index_ctx_coverage_get(f.idx, "d/b.pdf"), <, 1);
	g_free(sub); g_free(a); g_free(b);
	down(&f);
}

static void test_anbindungen(void) {
	F f = { 0 };
	GError *err = NULL;
	gint ID = -1;
	gchar *text = NULL;
	GPtrArray *secs = NULL;
	gchar const *S = "{{1,0},{2,0}}";
	gchar *q;

	up(&f, FALSE);
	q = sqlite3_mprintf("INSERT INTO knoten (ID,parent_ID,older_sibling_ID,type,link,file_part,section,node_text) "
			"VALUES (50,10,0,5,NULL,'dir/a.pdf',%Q,'Abschnitt A');", S);
	sql(zond_dbase_get_dbase(f.db->zond_dbase_store), q);
	sqlite3_free(q);

	g_assert_cmpint(sond_projekt_pflege_section_id(f.p, "dir/a.pdf", S, &ID, &err), ==, 0);
	g_assert_no_error(err);
	g_assert_cmpint(ID, ==, 50);
	g_assert_cmpint(sond_projekt_pflege_section_id(f.p, "dir/a.pdf", "{{9,0},{9,0}}", &ID, &err), ==, 0);
	g_assert_cmpint(ID, ==, 0);

	g_assert_cmpint(sond_projekt_pflege_section_text(f.p, "dir/a.pdf", S, &text, &err), ==, 0);
	g_assert_cmpstr(text, ==, "Abschnitt A");
	g_free(text);
	g_assert_cmpint(sond_projekt_pflege_section_text(f.p, "dir/a.pdf", "{{9,0},{9,0}}", &text, &err), ==, 0);
	g_assert_nonnull(text); //aus dem Bereich selbst
	g_free(text);

	g_assert_cmpint(sond_projekt_pflege_anbindung_volltreffer(f.p, "dir/a.pdf", S, &err), ==, 1);
	g_assert_cmpint(sond_projekt_pflege_anbindung_volltreffer(f.p, "dir/b.pdf", S, &err), ==, 0);

	g_assert_cmpint(sond_projekt_pflege_sections(f.p, "dir/a.pdf", NULL, NULL, &err), ==, 1);
	g_assert_cmpint(sond_projekt_pflege_sections(f.p, "dir/a.pdf", NULL, &secs, &err), ==, 0);
	g_assert_cmpint(secs->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(secs, 0), ==, S);
	g_ptr_array_unref(secs);
	g_assert_cmpint(sond_projekt_pflege_sections(f.p, "dir/a.pdf", S, NULL, &err), ==, 0); //keine Unterabschnitte

	g_assert_cmpint(sond_projekt_pflege_section_set_text(f.p, "dir/a.pdf", S, "Neu", &ID, &err), ==, 0);
	g_assert_cmpint(ID, ==, 50);
	g_assert_cmpint(sond_projekt_pflege_section_text(f.p, "dir/a.pdf", S, &text, &err), ==, 0);
	g_assert_cmpstr(text, ==, "Neu");
	g_free(text);
	//unbekannter Abschnitt: Fehler
	g_assert_cmpint(sond_projekt_pflege_section_set_text(f.p, "dir/a.pdf", "{{9,0},{9,0}}", "x", NULL, &err), ==, -1);
	g_assert_nonnull(err);
	g_clear_error(&err);

	g_assert_cmpint(sond_projekt_pflege_section_delete(f.p, "dir/a.pdf", S, &err), ==, 0);
	g_assert_no_error(err);
	g_assert_cmpint(count(zond_dbase_get_dbase(f.db->zond_dbase_store), "SELECT count(*) FROM knoten WHERE ID=50;"), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_sections(f.p, "dir/a.pdf", NULL, NULL, &err), ==, 0);
	down(&f);
}
static void sec_ins(F *f, gint id, gchar const *sec, gchar const *text) {
	gchar *q = sqlite3_mprintf("INSERT INTO knoten (ID,parent_ID,older_sibling_ID,type,link,file_part,section,node_text) "
			"VALUES (%d,10,0,5,NULL,'dir/a.pdf',%Q,%Q);", id, sec, text);
	sql(zond_dbase_get_dbase(f->db->zond_dbase_store), q);
	sqlite3_free(q);
}

//Abschnitte ohne work (Client): Transaktion über den store
static void test_abschnitt_ohne_work(void) {
	F f = { 0 };
	GError *err = NULL;
	gchar const *S = "{{1,0},{2,0}}";
	gchar *text = NULL;
	DBaseZondRef ref = DBASE_ZOND_REF_NONE;
	gboolean dual = TRUE;
	SondProjektDelete d = { .path = "dir/a.pdf", .section = S };
	sqlite3 *s;

	up(&f, FALSE);
	g_assert_null(f.db->zond_dbase_work);
	s = zond_dbase_get_dbase(f.db->zond_dbase_store);
	sql(s, "DELETE FROM knoten WHERE ID=11;"); //keine Anbindung der ganzen Datei
	sec_ins(&f, 50, S, "Abschnitt A");

	//Text ändern: Rollback, dann Commit
	g_assert_cmpint(sond_projekt_pflege_tx_begin(f.p, FALSE, FALSE, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_section_set_text(f.p, "dir/a.pdf", S, "Neu", NULL, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, FALSE, FALSE, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_assert_cmpint(sond_projekt_pflege_section_text(f.p, "dir/a.pdf", S, &text, &err), ==, 0);
	g_assert_cmpstr(text, ==, "Abschnitt A");
	g_free(text);

	g_assert_cmpint(sond_projekt_pflege_tx_begin(f.p, FALSE, FALSE, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_section_set_text(f.p, "dir/a.pdf", S, "Neu", NULL, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, FALSE, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_assert_cmpint(sond_projekt_pflege_section_text(f.p, "dir/a.pdf", S, &text, &err), ==, 0);
	g_assert_cmpstr(text, ==, "Neu");
	g_free(text);
	g_assert_no_error(err);

	//angebundener Abschnitt: nicht löschbar
	sql(s, "INSERT INTO knoten (ID,parent_ID,older_sibling_ID,type,link,file_part) VALUES (60,1,0,2,50,NULL);");
	g_assert_cmpint(sond_projekt_pflege_check_delete(f.p, "dir/a.pdf", FALSE, FALSE, S, &ref, &err), ==, 0);
	g_assert_cmpint(ref, !=, DBASE_ZOND_REF_NONE);
	sql(s, "DELETE FROM knoten WHERE ID=60;");
	g_assert_cmpint(sond_projekt_pflege_check_delete(f.p, "dir/a.pdf", FALSE, FALSE, S, &ref, &err), ==, 0);
	g_assert_cmpint(ref, ==, DBASE_ZOND_REF_NONE);

	//löschen: Rollback, dann Commit (wie das Löschen im Baum: delete, dann section_delete)
	g_assert_cmpint(sond_projekt_pflege_delete(f.p, &d, &dual, &err), ==, 0);
	g_assert_no_error(err);
	g_assert_false(dual);
	g_assert_cmpint(sond_projekt_pflege_section_delete(f.p, "dir/a.pdf", S, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, FALSE, dual, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_assert_cmpint(count(s, "SELECT count(*) FROM knoten WHERE ID=50;"), ==, 1);

	g_assert_cmpint(sond_projekt_pflege_delete(f.p, &d, &dual, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_section_delete(f.p, "dir/a.pdf", S, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, dual, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_assert_cmpint(count(s, "SELECT count(*) FROM knoten WHERE ID=50;"), ==, 0);
	//die Datei selbst (FILE_PART 10) bleibt
	g_assert_cmpint(count(s, "SELECT count(*) FROM knoten WHERE ID=10;"), ==, 1);
	g_assert_no_error(err);
	down(&f);
}

static gint sec_pos(sqlite3 *s, gint id, gchar const *col) {
	gchar *q = g_strdup_printf("SELECT %s FROM knoten WHERE ID=%d;", col, id);
	gint n = count(s, q);
	g_free(q);
	return n;
}

//Abschnitt auflösen: Unterabschnitte rücken hoch
static void test_aufloesen(void) {
	F f = { 0 };
	GError *err = NULL;
	gboolean angebunden = TRUE;
	sqlite3 *s;
	gchar const *SA = "{{0,0},{0,9}}", *SB = "{{1,0},{1,9}}", *SC = "{{2,0},{2,9}}", *SD = "{{3,0},{3,9}}";

	up(&f, FALSE);
	s = zond_dbase_get_dbase(f.db->zond_dbase_store);
	sql(s, "DELETE FROM knoten WHERE ID=11;");
	//A (50) unter 10, darunter B (51), C (52); D (53) hinter A
	sec_ins(&f, 50, SA, "A");
	sql(s, "INSERT INTO knoten (ID,parent_ID,older_sibling_ID,type,file_part,section,node_text) VALUES (53,10,50,5,'dir/a.pdf','{{3,0},{3,9}}','D');");
	sql(s, "INSERT INTO knoten (ID,parent_ID,older_sibling_ID,type,file_part,section,node_text) VALUES (51,50,0,5,'dir/a.pdf','{{1,0},{1,9}}','B');");
	sql(s, "INSERT INTO knoten (ID,parent_ID,older_sibling_ID,type,file_part,section,node_text) VALUES (52,50,51,5,'dir/a.pdf','{{2,0},{2,9}}','C');");
	(void) SB; (void) SC; (void) SD;

	g_assert_cmpint(sond_projekt_pflege_section_angebunden(f.p, "dir/a.pdf", SA, &angebunden, &err), ==, 0);
	g_assert_false(angebunden);
	//Anbindung eines Unterabschnitts zählt nicht, die des Abschnitts selbst schon
	sql(s, "INSERT INTO knoten (ID,parent_ID,older_sibling_ID,type,link,file_part) VALUES (60,1,0,2,51,NULL);");
	g_assert_cmpint(sond_projekt_pflege_section_angebunden(f.p, "dir/a.pdf", SA, &angebunden, &err), ==, 0);
	g_assert_false(angebunden);
	sql(s, "INSERT INTO knoten (ID,parent_ID,older_sibling_ID,type,link,file_part) VALUES (61,1,60,2,50,NULL);");
	g_assert_cmpint(sond_projekt_pflege_section_angebunden(f.p, "dir/a.pdf", SA, &angebunden, &err), ==, 0);
	g_assert_true(angebunden);
	sql(s, "DELETE FROM knoten WHERE ID=61;");

	//Rollback: nichts geändert
	g_assert_cmpint(sond_projekt_pflege_tx_begin(f.p, FALSE, FALSE, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_section_aufloesen(f.p, "dir/a.pdf", SA, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, FALSE, FALSE, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_assert_cmpint(sec_pos(s, 50, "ID"), ==, 50);
	g_assert_cmpint(sec_pos(s, 51, "parent_ID"), ==, 50);

	//Commit: A weg, B und C an seiner Stelle unter 10, D dahinter; Anbindung von B bleibt
	g_assert_cmpint(sond_projekt_pflege_tx_begin(f.p, FALSE, FALSE, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_section_aufloesen(f.p, "dir/a.pdf", SA, &err), ==, 0);
	g_assert_cmpint(sond_projekt_pflege_tx_ende(f.p, TRUE, FALSE, f.dir, &err), ==, SOND_PROJEKT_TX_OK);
	g_assert_no_error(err);
	g_assert_cmpint(count(s, "SELECT count(*) FROM knoten WHERE ID=50;"), ==, 0);
	g_assert_cmpint(sec_pos(s, 51, "parent_ID"), ==, 10);
	g_assert_cmpint(sec_pos(s, 51, "older_sibling_ID"), ==, 0);
	g_assert_cmpint(sec_pos(s, 52, "parent_ID"), ==, 10);
	g_assert_cmpint(sec_pos(s, 52, "older_sibling_ID"), ==, 51);
	g_assert_cmpint(sec_pos(s, 53, "older_sibling_ID"), ==, 52);
	g_assert_cmpint(count(s, "SELECT count(*) FROM knoten WHERE ID=60 AND link=51;"), ==, 1);
	down(&f);
}

void test_projekt_pflege_register(void) {
	g_mime_init();
	g_test_add_func("/projekt_pflege/abschnitt_ohne_work", test_abschnitt_ohne_work);
	g_test_add_func("/projekt_pflege/aufloesen", test_aufloesen);
	g_test_add_func("/projekt_pflege/check", test_check);
	g_test_add_func("/projekt_pflege/ohne_index", test_ohne_index);
	g_test_add_func("/projekt_pflege/index_commit", t_commit);
	g_test_add_func("/projekt_pflege/index_abort", t_abort);
	g_test_add_func("/projekt_pflege/mail", test_mail);
	g_test_add_func("/projekt_pflege/pdf", test_pdf_anhaenge);
	g_test_add_func("/projekt_pflege/move_datei", test_move_datei);
	g_test_add_func("/projekt_pflege/anbindungen", test_anbindungen);
	g_test_add_func("/projekt_pflege/leerer_ordner", test_leerer_ordner);
	g_test_add_func("/projekt_pflege/delete_datei", test_delete_datei);
	g_test_add_func("/projekt_pflege/delete_mime", test_delete_mimepart_und_rollback);
	g_test_add_func("/projekt_pflege/delete_pdf", test_delete_pdf_anhang_und_abschnitt);
	g_test_add_func("/projekt_pflege/work_index", test_mit_work_und_index);
	g_test_add_func("/projekt_pflege/insert", test_insert);
	g_test_add_func("/projekt_pflege/collapse", test_collapse_nach_verschieben);
	g_test_add_func("/projekt_pflege/tx_ende_ok", test_tx_ende_ok_und_rollback);
	g_test_add_func("/projekt_pflege/tx_ende_work", test_tx_ende_work_only);
	g_test_add_func("/projekt_pflege/tx_ende_fehler", test_tx_ende_commit_fehler);
	g_test_add_func("/projekt_pflege/free_attached", test_free_attached);
	g_test_add_func("/projekt_pflege/move_mail", test_move_mail);
	g_test_add_func("/projekt_pflege/move_rename_mime", test_move_rename_mime);
	g_test_add_func("/projekt_pflege/move_pdf_anhang", test_move_pdf_anhang);
}
