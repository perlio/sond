/*
 sond (test_tvfm_status.c) - Akten, Beweisstücke, Unterlagen
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
 * Tests der Anzeige-Entscheidungen des Dateibaums (tvfm/sond_tvfm_status.c) und der
 * SeaDrive-Hilfen (tvfm/sond_tvfm_load.c): Badges (SeaDrive, Index, Anhang, Schloss), Texte,
 * Indexstatus aus der Index-DB. Den SeaDrive-Zustand gibt es nur unter Windows; einen
 * nicht geladenen Platzhalter kann ein Test dort nicht erzeugen (das Attribut setzt nur der
 * Cloud-Anbieter), der Zustand kommt deshalb in den Badge-Tests von einer nachgebauten Quelle.
 * "Gepinnt" lässt sich als Dateiattribut setzen und wird gegen das Dateisystem geprüft.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include "../sond_index.h"
#include "../tvfm/sond_tvfm_load.h"
#include "../tvfm/sond_tvfm_item_private.h"
#include "../tvfm/sond_tvfm_status.h"
#include "tvfm_test_support.h"

//ein einzelnes Zeichen erkennt libmagic nicht als Text
#define TESTTEXT "Ein kurzer Text, damit der Inhalt als Text erkannt wird."
//Bytes, die libmagic als Binärdaten erkennt (nicht indizierbar), unabhängig von der Endung
#define BINAER "\x01\x02\x03\x04\xfe\xff\x80\x81\x82\x01\x02\x03\x04\x05\x06\x07"

/* ---------------------------------------------------------------------------------------- */
/* Texte                                                                                    */

static void test_texte_seadrive(void) {
	g_assert_null(sond_tvfm_badge_seadrive_text(SOND_SEADRIVE_BADGE_NONE));
	g_assert_nonnull(sond_tvfm_badge_seadrive_text(SOND_SEADRIVE_BADGE_OFFLINE));
	g_assert_nonnull(sond_tvfm_badge_seadrive_text(SOND_SEADRIVE_BADGE_PENDING));
	g_assert_nonnull(sond_tvfm_badge_seadrive_text(SOND_SEADRIVE_BADGE_PINNED));
	g_assert_cmpstr(sond_tvfm_badge_seadrive_text(SOND_SEADRIVE_BADGE_OFFLINE), !=,
			sond_tvfm_badge_seadrive_text(SOND_SEADRIVE_BADGE_PINNED));
	g_assert_cmpstr(sond_tvfm_badge_seadrive_text(SOND_SEADRIVE_BADGE_PENDING), !=,
			sond_tvfm_badge_seadrive_text(SOND_SEADRIVE_BADGE_PINNED));
}

static void test_texte_ordner(void) {
	g_assert_null(sond_tvfm_badge_dir_status_text(SOND_SEADRIVE_DIR_STATUS_NONE));
	g_assert_nonnull(sond_tvfm_badge_dir_status_text(SOND_SEADRIVE_DIR_STATUS_FULL_OFFLINE));
	g_assert_nonnull(sond_tvfm_badge_dir_status_text(SOND_SEADRIVE_DIR_STATUS_FULL_HYDRATED_PINNED));
	g_assert_nonnull(sond_tvfm_badge_dir_status_text(SOND_SEADRIVE_DIR_STATUS_MIXED));
	g_assert_cmpstr(sond_tvfm_badge_dir_status_text(SOND_SEADRIVE_DIR_STATUS_FULL_OFFLINE), !=,
			sond_tvfm_badge_dir_status_text(SOND_SEADRIVE_DIR_STATUS_MIXED));
}

/* ---------------------------------------------------------------------------------------- */
/* Zustand aus den Dateiattributen                                                          */

static void test_live_nicht_vorhandene_datei(void) {
	Fix *f = fix_new();
	gchar *p = g_build_filename(f->dir, "gibt_es_nicht.txt", NULL);

	g_assert_cmpint(sond_tvfm_badge_seadrive_live(p), ==, SOND_SEADRIVE_BADGE_NONE);
	g_free(p);
	fix_free(f);
}

static void test_live_normale_datei(void) {
	Fix *f = fix_new();
	gchar *p = g_build_filename(f->dir, "a.txt", NULL);

	schreibe(f, "a.txt", TESTTEXT);
	g_assert_cmpint(sond_tvfm_badge_seadrive_live(p), ==, SOND_SEADRIVE_BADGE_NONE);
	g_free(p);
	fix_free(f);
}

#ifdef G_OS_WIN32
static void test_live_gepinnt(void) {
	Fix *f = fix_new();
	gchar *p = g_build_filename(f->dir, "a.txt", NULL);
	wchar_t *wp = NULL;

	schreibe(f, "a.txt", TESTTEXT);
	wp = (wchar_t*) g_utf8_to_utf16(p, -1, NULL, NULL, NULL);
	g_assert_nonnull(wp);

	//FILE_ATTRIBUTE_PINNED: eine geladene, dauerhaft gehaltene Datei
	g_assert_true(SetFileAttributesW(wp, 0x00080000));
	g_assert_cmpint(sond_tvfm_badge_seadrive_live(p), ==, SOND_SEADRIVE_BADGE_PINNED);

	SetFileAttributesW(wp, FILE_ATTRIBUTE_NORMAL);
	g_assert_cmpint(sond_tvfm_badge_seadrive_live(p), ==, SOND_SEADRIVE_BADGE_NONE);

	g_free(wp);
	g_free(p);
	fix_free(f);
}
#endif

/* ---------------------------------------------------------------------------------------- */
/* Indizierbarkeit                                                                          */

static void test_indizierbar(void) {
	Fix *f = fix_new();
	SondTVFMItem *pdf = NULL;
	SondTVFMItem *txt = NULL;
	SondTVFMItem *eml = NULL;
	SondTVFMItem *db = NULL;

	schreibe_pdf_seiten(f, "a.pdf", 1);
	schreibe(f, "b.txt", TESTTEXT);
	schreibe_eml(f, "c.eml", "Betreff", "Text", "anhang.txt", "Anhang");
	schreibe(f, "d.db", BINAER);

	pdf = datei(f, "a.pdf");
	txt = datei(f, "b.txt");
	eml = datei(f, "c.eml");
	db = datei(f, "d.db");

	g_assert_true(sond_tvfm_file_part_indexable(sond_tvfm_item_get_priv(pdf)->sond_file_part, "a.pdf"));
	g_assert_true(sond_tvfm_file_part_indexable(sond_tvfm_item_get_priv(txt)->sond_file_part, "b.txt"));
	g_assert_true(sond_tvfm_file_part_indexable(sond_tvfm_item_get_priv(eml)->sond_file_part, "c.eml"));
	g_assert_false(sond_tvfm_file_part_indexable(sond_tvfm_item_get_priv(db)->sond_file_part, "d.db"));

	g_object_unref(pdf);
	g_object_unref(txt);
	g_object_unref(eml);
	g_object_unref(db);
	fix_free(f);
}

/* ---------------------------------------------------------------------------------------- */
/* Badges                                                                                   */

typedef struct {
	guint n_file;
	guint n_dir;
	guint n_index;
	gchar *letzter_pfad;
	SondSeadriveBadge file_badge;
	SondSeadriveDirStatus dir_status;
	SondIndexStatus index_status;
	SondTVFMItem *letztes_item;
} Quelle;

static SondSeadriveBadge quelle_file(gpointer data, gchar const *full_path) {
	Quelle *q = data;

	q->n_file++;
	g_free(q->letzter_pfad);
	q->letzter_pfad = g_strdup(full_path);

	return q->file_badge;
}

static SondSeadriveDirStatus quelle_dir(gpointer data, gchar const *full_path) {
	Quelle *q = data;

	q->n_dir++;
	g_free(q->letzter_pfad);
	q->letzter_pfad = g_strdup(full_path);

	return q->dir_status;
}

static SondIndexStatus quelle_index(gpointer data, SondTVFMItem *item) {
	Quelle *q = data;

	q->n_index++;
	q->letztes_item = item;

	return q->index_status;
}

static SondTVFMBadgeSources const quellen = { quelle_file, quelle_dir, quelle_index };

static void test_badges_ohne_quellen(void) {
	Fix *f = fix_new();
	SondTVFMItem *txt = NULL;
	SondTVFMBadges b;

	schreibe(f, "a.txt", TESTTEXT);
	txt = datei(f, "a.txt");

	memset(&b, 0xff, sizeof(b));
	sond_tvfm_item_get_badges(txt, (SondTVFMOwner*) f->owner, NULL, NULL, &b);
	g_assert_cmpint(b.seadrive, ==, SOND_SEADRIVE_BADGE_NONE);
	g_assert_cmpint(b.dir_status, ==, SOND_SEADRIVE_DIR_STATUS_NONE);
	g_assert_cmpint(b.index, ==, SOND_INDEX_STATUS_NONE);
	g_assert_false(b.attachment);
	g_assert_cmpint(b.lock, ==, SOND_LOCK_NONE);

	//ohne Eigentümer ebenso
	memset(&b, 0xff, sizeof(b));
	sond_tvfm_item_get_badges(txt, NULL, NULL, NULL, &b);
	g_assert_cmpint(b.seadrive, ==, SOND_SEADRIVE_BADGE_NONE);

	g_object_unref(txt);
	fix_free(f);
}

static void test_badges_index(void) {
	Fix *f = fix_new();
	SondTVFMItem *txt = NULL;
	Quelle q = { 0 };
	SondTVFMBadges b;

	schreibe(f, "a.txt", TESTTEXT);
	txt = datei(f, "a.txt");

	q.index_status = SOND_INDEX_STATUS_PARTIAL;
	sond_tvfm_item_get_badges(txt, (SondTVFMOwner*) f->owner, &quellen, &q, &b);
	g_assert_cmpuint(q.n_index, ==, 1);
	g_assert_true(q.letztes_item == txt);
	g_assert_cmpint(b.index, ==, SOND_INDEX_STATUS_PARTIAL);

	//unabhängig von SeaDrive: auch ohne SeaDrive-Wurzel wird der Index gefragt, SeaDrive nicht
	g_assert_cmpuint(q.n_file, ==, 0);
	g_assert_cmpuint(q.n_dir, ==, 0);

	g_free(q.letzter_pfad);
	g_object_unref(txt);
	fix_free(f);
}

static void test_badges_anhang_einer_mail(void) {
	Fix *f = fix_new();
	SondTVFMItem *mail = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;
	SondTVFMBadges b;

	schreibe_eml(f, "mail.eml", "Betreff", "Text der Mail", "anhang.txt", "Inhalt des Anhangs");
	mail = datei(f, "mail.eml");
	g_assert_cmpint(sond_tvfm_item_load_children(mail, &arr, NULL, &error), ==, 0);
	g_assert_no_error(error);

	sond_tvfm_item_get_badges(kind(arr, "anhang.txt"), (SondTVFMOwner*) f->owner, NULL, NULL, &b);
	g_assert_true(b.attachment);

	//die Nachricht selbst ist kein Anhang
	sond_tvfm_item_get_badges(mail, (SondTVFMOwner*) f->owner, NULL, NULL, &b);
	g_assert_false(b.attachment);

	g_ptr_array_unref(arr);
	g_object_unref(mail);
	fix_free(f);
}

static void test_badges_schloss(void) {
	Fix *f = fix_new();
	SondTVFMItem *z = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;
	SondTVFMBadges b;

	schreibe_zip_passwort(f, "arch.zip", "offen.txt", "Offen", NULL, "geheim.txt", "Geheim", "pw1", NULL);
	z = datei(f, "arch.zip");
	g_assert_cmpint(sond_tvfm_item_load_children(z, &arr, NULL, &error), ==, 0);
	g_assert_no_error(error);

	sond_tvfm_item_get_badges(kind(arr, "geheim.txt"), (SondTVFMOwner*) f->owner, NULL, NULL, &b);
	g_assert_cmpint(b.lock, ==, SOND_LOCK_LOCKED);
	sond_tvfm_item_get_badges(kind(arr, "offen.txt"), (SondTVFMOwner*) f->owner, NULL, NULL, &b);
	g_assert_cmpint(b.lock, ==, SOND_LOCK_NONE);

	g_ptr_array_unref(arr);
	g_object_unref(z);
	fix_free(f);
}

static void test_badges_kein_seadrive_pfad(void) {
	Fix *f = fix_new();
	SondTVFMItem *txt = NULL;
	SondTVFMItem *dir = NULL;
	Quelle q = { 0 };
	SondTVFMBadges b;

	schreibe(f, "sub/a.txt", TESTTEXT);
	txt = datei(f, "sub/a.txt");
	dir = verzeichnis(f, "sub");
	q.file_badge = SOND_SEADRIVE_BADGE_OFFLINE;
	q.dir_status = SOND_SEADRIVE_DIR_STATUS_MIXED;

	//Wurzel nicht in SeaDrive: die SeaDrive-Quellen werden nicht gefragt
	f->owner->seadrive = FALSE;
	sond_tvfm_item_get_badges(txt, (SondTVFMOwner*) f->owner, &quellen, &q, &b);
	g_assert_cmpint(b.seadrive, ==, SOND_SEADRIVE_BADGE_NONE);
	sond_tvfm_item_get_badges(dir, (SondTVFMOwner*) f->owner, &quellen, &q, &b);
	g_assert_cmpint(b.seadrive, ==, SOND_SEADRIVE_BADGE_NONE);
	g_assert_cmpint(b.dir_status, ==, SOND_SEADRIVE_DIR_STATUS_NONE);
	g_assert_cmpuint(q.n_file, ==, 0);
	g_assert_cmpuint(q.n_dir, ==, 0);

	g_free(q.letzter_pfad);
	g_object_unref(txt);
	g_object_unref(dir);
	fix_free(f);
}

static void test_badges_seadrive_datei(void) {
	Fix *f = fix_new();
	SondTVFMItem *txt = NULL;
	Quelle q = { 0 };
	SondTVFMBadges b;
	gchar *erwartet = NULL;

	schreibe(f, "sub/a.txt", TESTTEXT);
	txt = datei(f, "sub/a.txt");
	q.file_badge = SOND_SEADRIVE_BADGE_OFFLINE;
	f->owner->seadrive = TRUE;

	sond_tvfm_item_get_badges(txt, (SondTVFMOwner*) f->owner, &quellen, &q, &b);
#ifdef G_OS_WIN32
	//der Zustand der Datei kommt aus der Quelle des Baums, mit vollem Pfad
	erwartet = g_strconcat(f->dir, "/", "sub/a.txt", NULL);
	g_assert_cmpint(b.seadrive, ==, SOND_SEADRIVE_BADGE_OFFLINE);
	g_assert_cmpuint(q.n_file, ==, 1);
	g_assert_cmpstr(q.letzter_pfad, ==, erwartet);
	g_assert_cmpuint(q.n_dir, ==, 0);
#else
	//außerhalb von Windows gibt es keinen SeaDrive-Zustand
	g_assert_cmpint(b.seadrive, ==, SOND_SEADRIVE_BADGE_NONE);
	g_assert_cmpuint(q.n_file, ==, 0);
#endif

	g_free(erwartet);
	g_free(q.letzter_pfad);
	g_object_unref(txt);
	fix_free(f);
}

static void test_badges_seadrive_container_erbt_von_der_datei(void) {
	Fix *f = fix_new();
	SondTVFMItem *z = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;
	Quelle q = { 0 };
	SondTVFMBadges b;

	schreibe_zip(f, "sub/arch.zip", "ordner/", NULL, "ordner/x.txt", TESTTEXT, "y.txt", TESTTEXT, NULL);
	z = datei(f, "sub/arch.zip");
	q.file_badge = SOND_SEADRIVE_BADGE_PINNED;
	f->owner->seadrive = TRUE;
	g_assert_cmpint(sond_tvfm_item_load_children(z, &arr, NULL, &error), ==, 0);
	g_assert_no_error(error);

	//ein Eintrag im ZIP zeigt den Zustand der ZIP-Datei
	sond_tvfm_item_get_badges(kind(arr, "y.txt"), (SondTVFMOwner*) f->owner, &quellen, &q, &b);
#ifdef G_OS_WIN32
	{
		gchar *erwartet = g_strconcat(f->dir, "/", "sub/arch.zip", NULL);

		g_assert_cmpint(b.seadrive, ==, SOND_SEADRIVE_BADGE_PINNED);
		g_assert_cmpstr(q.letzter_pfad, ==, erwartet);
		g_free(erwartet);
	}

	//ebenso ein Ordner im Archiv: sein Zustand ist der der Datei, kein Ordnerstatus
	q.n_file = 0;
	q.n_dir = 0;
	b.seadrive = SOND_SEADRIVE_BADGE_NONE;
	sond_tvfm_item_get_badges(kind(arr, "ordner"), (SondTVFMOwner*) f->owner, &quellen, &q, &b);
	g_assert_cmpuint(q.n_file, ==, 0);
#else
	g_assert_cmpint(b.seadrive, ==, SOND_SEADRIVE_BADGE_NONE);
#endif

	g_free(q.letzter_pfad);
	g_ptr_array_unref(arr);
	g_object_unref(z);
	fix_free(f);
}

static void test_badges_seadrive_ordner(void) {
	Fix *f = fix_new();
	SondTVFMItem *dir = NULL;
	Quelle q = { 0 };
	SondTVFMBadges b;

	schreibe(f, "sub/a.txt", TESTTEXT);
	dir = verzeichnis(f, "sub");
	q.dir_status = SOND_SEADRIVE_DIR_STATUS_MIXED;
	f->owner->seadrive = TRUE;

	sond_tvfm_item_get_badges(dir, (SondTVFMOwner*) f->owner, &quellen, &q, &b);
#ifdef G_OS_WIN32
	//der Ordner selbst ist weder gepinnt noch offline: der Status des Teilbaums gilt
	g_assert_cmpint(b.seadrive, ==, SOND_SEADRIVE_BADGE_NONE);
	g_assert_cmpint(b.dir_status, ==, SOND_SEADRIVE_DIR_STATUS_MIXED);
	g_assert_cmpuint(q.n_dir, ==, 1);
	g_assert_cmpuint(q.n_file, ==, 0);
#else
	g_assert_cmpint(b.dir_status, ==, SOND_SEADRIVE_DIR_STATUS_NONE);
	g_assert_cmpuint(q.n_dir, ==, 0);
#endif

	g_free(q.letzter_pfad);
	g_object_unref(dir);
	fix_free(f);
}

#ifdef G_OS_WIN32
static void test_badges_seadrive_ordner_gepinnt(void) {
	Fix *f = fix_new();
	SondTVFMItem *dir = NULL;
	Quelle q = { 0 };
	SondTVFMBadges b;
	gchar *p = NULL;
	wchar_t *wp = NULL;

	schreibe(f, "sub/a.txt", TESTTEXT);
	dir = verzeichnis(f, "sub");
	q.dir_status = SOND_SEADRIVE_DIR_STATUS_MIXED;
	f->owner->seadrive = TRUE;
	p = g_build_filename(f->dir, "sub", NULL);
	wp = (wchar_t*) g_utf8_to_utf16(p, -1, NULL, NULL, NULL);

	//ist der Ordner selbst gepinnt, zeigt er das, nicht den Status des Teilbaums
	g_assert_true(SetFileAttributesW(wp, 0x00080000 | FILE_ATTRIBUTE_DIRECTORY));
	sond_tvfm_item_get_badges(dir, (SondTVFMOwner*) f->owner, &quellen, &q, &b);
	if (b.seadrive == SOND_SEADRIVE_BADGE_PINNED) {
		g_assert_cmpint(b.dir_status, ==, SOND_SEADRIVE_DIR_STATUS_NONE);
		g_assert_cmpuint(q.n_dir, ==, 0);
	}
	else
		g_test_incomplete("Verzeichnis lässt sich nicht als gepinnt kennzeichnen");

	SetFileAttributesW(wp, FILE_ATTRIBUTE_DIRECTORY);
	g_free(wp);
	g_free(p);
	g_free(q.letzter_pfad);
	g_object_unref(dir);
	fix_free(f);
}
#endif

/* ---------------------------------------------------------------------------------------- */
/* Indexstatus                                                                              */

typedef struct {
	Fix *fix;
	SondIndexCtx *ctx;
} Idx;

static Idx* idx_new(void) {
	Idx *i = g_new0(Idx, 1);
	GError *error = NULL;
	gchar *pfad = NULL;

	i->fix = fix_new();
	pfad = g_build_filename(i->fix->dir, "index.db", NULL);
	i->ctx = sond_index_ctx_new(pfad, NULL, 0, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(i->ctx);
	g_free(pfad);

	return i;
}

static void idx_free(Idx *i) {
	sond_index_ctx_free(i->ctx);
	fix_free(i->fix);
	g_free(i);
}

static void markiere(Idx *i, gchar const *pfad) {
	GError *error = NULL;

	g_assert_true(sond_index_ctx_coverage_mark(i->ctx, pfad, 1, &error));
	g_assert_no_error(error);
}

static void test_indexstatus_datei(void) {
	Idx *i = idx_new();
	SondTVFMItem *txt = NULL;

	schreibe(i->fix, "a.txt", TESTTEXT);
	txt = datei(i->fix, "a.txt");

	//kein Kontext, nicht indiziert, indiziert
	g_assert_cmpint(sond_tvfm_item_index_status(txt, NULL), ==, SOND_INDEX_STATUS_NONE);
	g_assert_cmpint(sond_tvfm_item_index_status(txt, i->ctx), ==, SOND_INDEX_STATUS_NONE);
	markiere(i, "a.txt");
	g_assert_cmpint(sond_tvfm_item_index_status(txt, i->ctx), ==, SOND_INDEX_STATUS_FULL);

	g_object_unref(txt);
	idx_free(i);
}

static void test_indexstatus_nicht_indizierbar(void) {
	Idx *i = idx_new();
	SondTVFMItem *db = NULL;

	schreibe(i->fix, "d.db", BINAER);
	db = datei(i->fix, "d.db");

	//ein Dateityp, der nie indiziert wird, zeigt nie "nicht indiziert" an, auch nicht abgedeckt
	markiere(i, "d.db");
	g_assert_cmpint(sond_tvfm_item_index_status(db, i->ctx), ==, SOND_INDEX_STATUS_NONE);

	g_object_unref(db);
	idx_free(i);
}

static void test_indexstatus_ordner(void) {
	Idx *i = idx_new();
	SondTVFMItem *dir = NULL;

	schreibe(i->fix, "d/a.txt", TESTTEXT);
	schreibe(i->fix, "d/b.txt", TESTTEXT);
	dir = verzeichnis(i->fix, "d");

	g_assert_cmpint(sond_tvfm_item_index_status(dir, i->ctx), ==, SOND_INDEX_STATUS_NONE);
	markiere(i, "d/a.txt");
	g_assert_cmpint(sond_tvfm_item_index_status(dir, i->ctx), ==, SOND_INDEX_STATUS_PARTIAL);
	markiere(i, "d");
	g_assert_cmpint(sond_tvfm_item_index_status(dir, i->ctx), ==, SOND_INDEX_STATUS_FULL);

	g_object_unref(dir);
	idx_free(i);
}

static void test_indexstatus_eintrag_im_zip(void) {
	Idx *i = idx_new();
	SondTVFMItem *z = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;

	schreibe_zip(i->fix, "arch.zip", "ordner/", NULL, "ordner/x.txt", TESTTEXT, "y.txt", TESTTEXT, NULL);
	z = datei(i->fix, "arch.zip");
	g_assert_cmpint(sond_tvfm_item_load_children(z, &arr, NULL, &error), ==, 0);
	g_assert_no_error(error);

	g_assert_cmpint(sond_tvfm_item_index_status(kind(arr, "y.txt"), i->ctx), ==,
			SOND_INDEX_STATUS_NONE);
	markiere(i, "arch.zip//y.txt");
	g_assert_cmpint(sond_tvfm_item_index_status(kind(arr, "y.txt"), i->ctx), ==,
			SOND_INDEX_STATUS_FULL);
	//das Archiv selbst: nur ein Teil indiziert
	g_assert_cmpint(sond_tvfm_item_index_status(z, i->ctx), !=, SOND_INDEX_STATUS_FULL);

	g_ptr_array_unref(arr);
	g_object_unref(z);
	idx_free(i);
}

static void test_indexstatus_mail_header(void) {
	Idx *i = idx_new();
	SondTVFMItem *mail = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;
	SondTVFMItem *nachricht = NULL;

	schreibe_eml(i->fix, "mail.eml", "Betreff", "Text der Mail", "anhang.txt", "Inhalt");
	mail = datei(i->fix, "mail.eml");
	g_assert_cmpint(sond_tvfm_item_load_children(mail, &arr, NULL, &error), ==, 0);
	g_assert_no_error(error);
	nachricht = kind(arr, "Message");
	g_assert_nonnull(nachricht);

	//die Nachricht ist der Header: er zählt, wenn er gezielt abgedeckt ist ...
	g_assert_cmpint(sond_tvfm_item_index_status(nachricht, i->ctx), ==, SOND_INDEX_STATUS_NONE);
	markiere(i, "mail.eml//header");
	g_assert_cmpint(sond_tvfm_item_index_status(nachricht, i->ctx), ==, SOND_INDEX_STATUS_FULL);

	g_ptr_array_unref(arr);
	g_object_unref(mail);
	idx_free(i);
}

static void test_indexstatus_mail_ganz_abgedeckt(void) {
	Idx *i = idx_new();
	SondTVFMItem *mail = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;

	schreibe_eml(i->fix, "mail.eml", "Betreff", "Text der Mail", "anhang.txt", "Inhalt");
	mail = datei(i->fix, "mail.eml");
	g_assert_cmpint(sond_tvfm_item_load_children(mail, &arr, NULL, &error), ==, 0);
	g_assert_no_error(error);

	//... oder wenn die ganze Mail abgedeckt ist
	markiere(i, "mail.eml");
	g_assert_cmpint(sond_tvfm_item_index_status(kind(arr, "Message"), i->ctx), ==,
			SOND_INDEX_STATUS_FULL);

	g_ptr_array_unref(arr);
	g_object_unref(mail);
	idx_free(i);
}

/* ---------------------------------------------------------------------------------------- */
/* SeaDrive-Hilfen (load)                                                                   */

static void test_load_ohne_seadrive_wurzel(void) {
	Fix *f = fix_new();
	SondTVFMItem *txt = NULL;
	GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);

	schreibe(f, "a.txt", TESTTEXT);
	txt = datei(f, "a.txt");
	f->owner->seadrive = FALSE;

	g_assert_null(sond_tvfm_load_root_file_path((SondTVFMOwner*) f->owner,
			sond_tvfm_item_get_priv(txt)->sond_file_part));
	g_assert_false(sond_tvfm_load_not_loaded((SondTVFMOwner*) f->owner,
			sond_tvfm_item_get_priv(txt)->sond_file_part));
	sond_tvfm_load_collect(arr, (SondTVFMOwner*) f->owner, txt);
	g_assert_cmpuint(arr->len, ==, 0);

	g_ptr_array_unref(arr);
	g_object_unref(txt);
	fix_free(f);
}

static void test_load_geladene_dateien(void) {
	Fix *f = fix_new();
	SondTVFMItem *txt = NULL;
	SondTVFMItem *dir = NULL;
	SondTVFMItem *wurzel_item = NULL;
	GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
	gchar *pfad = NULL;

	schreibe(f, "sub/a.txt", TESTTEXT);
	schreibe(f, "sub/tief/b.txt", TESTTEXT);
	txt = datei(f, "sub/a.txt");
	dir = verzeichnis(f, "sub");
	wurzel_item = wurzel(f);
	f->owner->seadrive = TRUE;

	pfad = sond_tvfm_load_root_file_path((SondTVFMOwner*) f->owner,
			sond_tvfm_item_get_priv(txt)->sond_file_part);
#ifdef G_OS_WIN32
	{
		gchar *erwartet = g_strconcat(f->dir, "/", "sub/a.txt", NULL);

		g_assert_cmpstr(pfad, ==, erwartet);
		g_free(erwartet);
	}
#else
	g_assert_null(pfad);
#endif
	g_free(pfad);

	//lokal vorhandene Dateien müssen nicht geladen werden: weder die Datei noch Verzeichnisse
	g_assert_false(sond_tvfm_load_not_loaded((SondTVFMOwner*) f->owner,
			sond_tvfm_item_get_priv(txt)->sond_file_part));
	sond_tvfm_load_collect(arr, (SondTVFMOwner*) f->owner, txt);
	sond_tvfm_load_collect(arr, (SondTVFMOwner*) f->owner, dir);
	sond_tvfm_load_collect(arr, (SondTVFMOwner*) f->owner, wurzel_item);
	g_assert_cmpuint(arr->len, ==, 0);

	g_ptr_array_unref(arr);
	g_object_unref(txt);
	g_object_unref(dir);
	g_object_unref(wurzel_item);
	fix_free(f);
}

static void test_load_container_zeigt_auf_oberste_datei(void) {
	Fix *f = fix_new();
	SondTVFMItem *z = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;
	gchar *pfad = NULL;

	schreibe_zip(f, "sub/arch.zip", "y.txt", TESTTEXT, NULL);
	z = datei(f, "sub/arch.zip");
	f->owner->seadrive = TRUE;
	g_assert_cmpint(sond_tvfm_item_load_children(z, &arr, NULL, &error), ==, 0);
	g_assert_no_error(error);

	//ein Teil im Archiv gehört zur Datei des Archivs
	pfad = sond_tvfm_load_root_file_path((SondTVFMOwner*) f->owner,
			sond_tvfm_item_get_priv(kind(arr, "y.txt"))->sond_file_part);
#ifdef G_OS_WIN32
	{
		gchar *erwartet = g_strconcat(f->dir, "/", "sub/arch.zip", NULL);

		g_assert_cmpstr(pfad, ==, erwartet);
		g_free(erwartet);
	}
#else
	g_assert_null(pfad);
#endif
	g_free(pfad);

	//ohne Fileteil kein Pfad
	g_assert_null(sond_tvfm_load_root_file_path((SondTVFMOwner*) f->owner, NULL));

	g_ptr_array_unref(arr);
	g_object_unref(z);
	fix_free(f);
}

void test_tvfm_status_register(void) {
	g_test_add_func("/tvfm_status/texte_seadrive", test_texte_seadrive);
	g_test_add_func("/tvfm_status/texte_ordner", test_texte_ordner);
	g_test_add_func("/tvfm_status/live_nicht_vorhandene_datei", test_live_nicht_vorhandene_datei);
	g_test_add_func("/tvfm_status/live_normale_datei", test_live_normale_datei);
#ifdef G_OS_WIN32
	g_test_add_func("/tvfm_status/live_gepinnt", test_live_gepinnt);
	g_test_add_func("/tvfm_status/badges_seadrive_ordner_gepinnt",
			test_badges_seadrive_ordner_gepinnt);
#endif
	g_test_add_func("/tvfm_status/indizierbar", test_indizierbar);
	g_test_add_func("/tvfm_status/badges_ohne_quellen", test_badges_ohne_quellen);
	g_test_add_func("/tvfm_status/badges_index", test_badges_index);
	g_test_add_func("/tvfm_status/badges_anhang_einer_mail", test_badges_anhang_einer_mail);
	g_test_add_func("/tvfm_status/badges_schloss", test_badges_schloss);
	g_test_add_func("/tvfm_status/badges_kein_seadrive_pfad", test_badges_kein_seadrive_pfad);
	g_test_add_func("/tvfm_status/badges_seadrive_datei", test_badges_seadrive_datei);
	g_test_add_func("/tvfm_status/badges_seadrive_container_erbt_von_der_datei",
			test_badges_seadrive_container_erbt_von_der_datei);
	g_test_add_func("/tvfm_status/badges_seadrive_ordner", test_badges_seadrive_ordner);
	g_test_add_func("/tvfm_status/indexstatus_datei", test_indexstatus_datei);
	g_test_add_func("/tvfm_status/indexstatus_nicht_indizierbar", test_indexstatus_nicht_indizierbar);
	g_test_add_func("/tvfm_status/indexstatus_ordner", test_indexstatus_ordner);
	g_test_add_func("/tvfm_status/indexstatus_eintrag_im_zip", test_indexstatus_eintrag_im_zip);
	g_test_add_func("/tvfm_status/indexstatus_mail_header", test_indexstatus_mail_header);
	g_test_add_func("/tvfm_status/indexstatus_mail_ganz_abgedeckt",
			test_indexstatus_mail_ganz_abgedeckt);
	g_test_add_func("/tvfm_status/load_ohne_seadrive_wurzel", test_load_ohne_seadrive_wurzel);
	g_test_add_func("/tvfm_status/load_geladene_dateien", test_load_geladene_dateien);
	g_test_add_func("/tvfm_status/load_container_zeigt_auf_oberste_datei",
			test_load_container_zeigt_auf_oberste_datei);
}
