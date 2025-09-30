#include <stdlib.h>
#include <gtk/gtk.h>
#include <ctype.h>

#include "general.h"
#include "../zond_pdf_document.h"
#include "../40viewer/viewer.h"
//#include "../../misc.h"

gchar*
get_rel_path_from_file_part(gchar const *file_part) {
	if (!file_part)
		return NULL;
	if (strlen(file_part) < 4)
		return NULL;

	if (strstr(file_part, "//"))
		return g_strndup(file_part + 1,
				strlen(file_part + 1) - strlen(strstr(file_part, "//")));
	else
		return NULL;
}

gboolean anbindung_1_gleich_2(const Anbindung anbindung1,
		const Anbindung anbindung2) {
	if ((anbindung1.von.seite == anbindung2.von.seite)
			&& (anbindung1.bis.seite == anbindung2.bis.seite)
			&& (anbindung1.von.index == anbindung2.von.index)
			&& (anbindung1.bis.index == anbindung2.bis.index))
		return TRUE;
	else
		return FALSE;
}

static gint anbindung_vergleiche_pdf_pos(PdfPos pdf_pos1, PdfPos pdf_pos2) {
	if (pdf_pos1.seite < pdf_pos2.seite)
		return -1;
	else if (pdf_pos1.seite > pdf_pos2.seite)
		return 1;
	//wenn gleiche Seite:
	else if (pdf_pos1.index < pdf_pos2.index)
		return -1;
	else if (pdf_pos1.index > pdf_pos2.index)
		return 1;

	return 0;
}

gboolean anbindung_is_pdf_punkt(Anbindung anbindung) {
	if ((anbindung.von.seite || anbindung.von.index) && !anbindung.bis.seite
			&& !anbindung.bis.index)
		return TRUE;

	return FALSE;
}

gboolean anbindung_1_vor_2(Anbindung anbindung1, Anbindung anbindung2) {
	if (anbindung_1_gleich_2(anbindung1, anbindung2))
		return FALSE;

	if (anbindung_is_pdf_punkt(anbindung1)) {
		if (anbindung_vergleiche_pdf_pos(anbindung1.von, anbindung2.von) == -1)
			return TRUE;
		else
			return FALSE;
	} else if (anbindung_is_pdf_punkt(anbindung2)) {
		if (anbindung_vergleiche_pdf_pos(anbindung1.bis, anbindung2.von) == -1)
			return TRUE;
		else
			return FALSE;

	} else //beides komplette Anbindung
	if (anbindung_vergleiche_pdf_pos(anbindung1.bis, anbindung2.von) == -1)
		return TRUE;

	return FALSE;
}

gboolean anbindung_1_eltern_von_2(Anbindung anbindung1, Anbindung anbindung2) {
	gint pos_anfang = 0;
	gint pos_ende = 0;

	//PdfPunkt kann niemals Eltern sein.
	if (anbindung_is_pdf_punkt(anbindung1))
		return FALSE;

	//Gleiche können nicht Eltern/Kinder sein
	if (anbindung_1_gleich_2(anbindung1, anbindung2))
		return FALSE;

	//wenn Anbindung1 Datei ist, dann ist sie Eltern
	if (!anbindung1.von.seite && !anbindung1.von.index && !anbindung1.bis.seite
			&& !anbindung1.bis.index)
		return TRUE;

	//wenn Anbindung2 Datei ist, dann kann sie kein Kind sein
	if (!anbindung2.von.seite && !anbindung2.von.index && !anbindung2.bis.seite
			&& !anbindung2.bis.index)
		return FALSE;

	pos_anfang = anbindung_vergleiche_pdf_pos(anbindung1.von, anbindung2.von);
	pos_ende = anbindung_vergleiche_pdf_pos(anbindung1.bis, anbindung2.bis);

	if (pos_anfang > 0)
		return FALSE; //fängt schon später an...
	else //fängt entweder davor oder gleich an
	{
		if (pos_anfang == 0 && pos_ende <= 0)
			return FALSE; //Fängt gleich an, hört nicht später auf...
		else if (pos_anfang < 0 && pos_ende < 0)
			return FALSE; //Fängt vorher an, hört nicht mindestens gleich auf
	}

	return TRUE;
}

static void anbindung_parse_pdf_pos(gchar const *section, PdfPos *pdf_pos) {
	pdf_pos->seite = atoi(section + 1);
	pdf_pos->index = atoi(strstr(section, ",") + 1);

	return;
}

void anbindung_parse_file_section(gchar const *file_section,
		Anbindung *anbindung) {
	if (!file_section)
		return;

	if (g_str_has_prefix(file_section, "{{")) {
		anbindung_parse_pdf_pos(file_section + 1, &anbindung->von);

		anbindung_parse_pdf_pos(strstr(file_section, "}") + 1, &anbindung->bis);
	} else
		anbindung_parse_pdf_pos(file_section + 1, &anbindung->von);

	return;
}

void anbindung_build_file_section(Anbindung anbindung, gchar **section) {
	if (anbindung.bis.seite == 0 && anbindung.bis.index == 0)
		*section = g_strdup_printf("{%d,%d}", anbindung.von.seite,
				anbindung.von.index);
	else
		*section = g_strdup_printf("{{%d,%d}{%d,%d}}", anbindung.von.seite,
				anbindung.von.index, anbindung.bis.seite, anbindung.bis.index);

	return;
}

typedef struct _Inserts {
	gint page_doc;
	JournalEntry entry;
} Inserts;

void anbindung_aktualisieren_insert_pages(ZondPdfDocument const* zond_pdf_document, Anbindung* anbindung) {
	GArray* arr_journal = NULL;
	GArray* arr_insertions = NULL;

	if (!anbindung) return;

	arr_journal = zond_pdf_document_get_arr_journal(zond_pdf_document);
	arr_insertions = g_array_new(FALSE, FALSE, sizeof(Inserts));

	//erst alle entries mit Type == PAGES_INSERTED aussondern
	for (gint i = 0; i < arr_journal->len; i++) {
		JournalEntry entry = { 0 };

		entry = g_array_index(arr_journal, JournalEntry, i);

		if (entry.type == JOURNAL_TYPE_PAGES_INSERTED) {
			Inserts insert = { 0 };
			gint u = 0;

			insert.page_doc = pdf_document_page_get_index(entry.pdf_document_page);
			insert.entry = entry;

			//direkt an richtiger Stelle einsortieren
			for (u = 0; u <= arr_insertions->len; u++) {
				Inserts insert_loop = { 0 };

				if (u == arr_insertions->len) break;

				insert_loop = g_array_index(arr_insertions, Inserts, u);
				if (insert.page_doc  <= insert_loop.page_doc) break;
			}

			g_array_insert_val(arr_insertions, u, insert);
		}
	}

	//dann neu durchlaufen lassen und Anbindung ändern
	for (gint i = 0; i < arr_insertions->len; i++) {
		Inserts insert = { 0 };
		gboolean ende_versch = FALSE;

		insert = g_array_index(arr_insertions, Inserts, i);
		if (insert.page_doc < anbindung->von.seite) {
			anbindung->von.seite += insert.entry.pages_inserted.count;
			ende_versch = TRUE;
		} else if (insert.page_doc == anbindung->von.seite) { //Randlage Anfang
			if (!((insert.entry.pages_inserted.pos_dd == -1) && //linker Rand
					//dann prüfen ob dd, welches eingefügt wurde, genauso groß
					(insert.entry.pages_inserted.size_dd_pages == //_is_pdf_punkt is ejal
					anbindung->bis.seite - anbindung->von.seite) &&
					(insert.entry.pages_inserted.size_dd_index == anbindung->bis.index)))
				anbindung->von.seite += insert.entry.pages_inserted.count; //auch Anfang verschieben

			ende_versch = TRUE;
		}
		else if (insert.page_doc <= anbindung->bis.seite)
			ende_versch = TRUE;
		else if (insert.page_doc == anbindung->bis.seite + 1) { //Randlage Ende
			if (insert.entry.pages_inserted.pos_dd == 1 &&
					insert.entry.pages_inserted.size_dd_pages == //_is_pdf_punkt is ejal
					anbindung->bis.seite - anbindung->von.seite &&
					insert.entry.pages_inserted.size_dd_index == anbindung->von.index)
				ende_versch = TRUE;
		}

		if (ende_versch && !anbindung_is_pdf_punkt(*anbindung))
			anbindung->bis.seite += insert.entry.pages_inserted.count;
	}

	g_array_unref(arr_insertions);

	return;
}

static void anbindung_aktualisieren_delete_page(ZondPdfDocument* zond_pdf_document, Anbindung* anbindung) {
	GArray* arr_journal = NULL;

	if (!anbindung) return;

	arr_journal = zond_pdf_document_get_arr_journal(zond_pdf_document);

	for (gint i = 0; i < arr_journal->len; i ++) {
		JournalEntry entry = { 0 };

		entry = g_array_index(arr_journal, JournalEntry, i);

		if (entry.type == JOURNAL_TYPE_PAGE_DELETED) {
			gint page_doc = 0;

			page_doc = pdf_document_page_get_index(entry.pdf_document_page);

			if (page_doc < anbindung->von.seite) {
				anbindung->von.seite--;

				if (!anbindung_is_pdf_punkt(*anbindung))
					anbindung->bis.seite--;
			} else if (!anbindung_is_pdf_punkt(*anbindung) &&
					page_doc <= anbindung->bis.seite)
				anbindung->bis.seite--;
		}
	}

	return;
}

void anbindung_aktualisieren(ZondPdfDocument* zond_pdf_document,
		Anbindung* anbindung) {
	anbindung_aktualisieren_insert_pages(zond_pdf_document, anbindung);
	anbindung_aktualisieren_delete_page(zond_pdf_document, anbindung);

	return;
}
