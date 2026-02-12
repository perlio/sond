#include "general.h"

#include <stdlib.h>
#include <gtk/gtk.h>
#include <ctype.h>

#include "../zond_pdf_document.h"
#include "../40viewer/viewer.h"
#include "../../misc.h"

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

gchar* anbindung_to_human_readable(Anbindung* anbindung) {
	gchar* text = NULL;

	text = g_strdup_printf("S. %i", anbindung->von.seite + 1);
	if (anbindung->von.index)
		text = add_string(text,
				g_strdup_printf(", Index %d", anbindung->von.index));
	if (anbindung->bis.seite || anbindung->bis.index)
		text = add_string(text,
				g_strdup_printf(" - S. %d", anbindung->bis.seite + 1));
	if (anbindung->bis.index != EOP)
		text = add_string(text,
				g_strdup_printf(", Index %d", anbindung->bis.index));

	return text;
}

void anbindung_get_orig(ZondPdfDocument* zpdfd, Anbindung* anbindung) {
	Anbindung anbindung_akt = { 0 };

	if (!anbindung)
		return;

	anbindung_akt = *anbindung;

	for (guint i = 0; i < zond_pdf_document_get_number_of_pages(zpdfd); i++) {
		PdfDocumentPage* pdfp = NULL;

		pdfp = zond_pdf_document_get_pdf_document_page(zpdfd, i);

		if (pdfp && pdfp->inserted) {
			if (i <= anbindung_akt.von.seite) {
				anbindung->von.seite--;
				anbindung->bis.seite--;
			}
			else if (i <= anbindung_akt.bis.seite)
				anbindung->bis.seite--;
		}
	}

	return;
}

//aus aktueller Anbindung diejenige errechnen, die sich nach Speichern von zpdfd_part ergibt
void anbindung_korrigieren(ZPDFDPart* zpdfd_part, Anbindung* anbindung) {
	GPtrArray* arr_pages = NULL;
	Anbindung anbindung_zpdfd_part = { 0 };

	zpdfd_part_get_anbindung(zpdfd_part, &anbindung_zpdfd_part);
	arr_pages = zond_pdf_document_get_arr_pages(zpdfd_part->zond_pdf_document);

	for (gint i = anbindung->bis.seite; i >= 0; i--) {
		PdfDocumentPage* pdfp = NULL;

		pdfp = g_ptr_array_index(arr_pages, i);

		if (!pdfp)
			continue;

		//innerhalb des zu speichernde zpdfd_parts?
		if (i >= anbindung_zpdfd_part.von.seite && i <= anbindung_zpdfd_part.bis.seite) {
			//nur gelöschte Seiten herausrechnen
			if (pdfp->deleted) {
				if (i <= anbindung->von.seite) {
					anbindung->von.seite--;
					anbindung->bis.seite--;
				}
				else if (i <= anbindung->bis.seite)
					anbindung->bis.seite--;
			}
		}
		else {//außerhalb: eingefügte Seiten
			if (pdfp->inserted) { //weil die sind ja zuvor hinzugerechnet worden
				if (i <= anbindung->von.seite) {
					anbindung->von.seite--;
					anbindung->bis.seite--;
				}
				else if (i <= anbindung->bis.seite)
					anbindung->bis.seite--;
			}
		}
	}

	return;
}

void anbindung_aktualisieren(ZondPdfDocument* zpdfd, Anbindung* anbindung) {
	GPtrArray* arr_pages = NULL;
	Anbindung anbindung_orig = { 0 };

	arr_pages = zond_pdf_document_get_arr_pages(zpdfd);

	for (guint i = 0; i < arr_pages->len; i++) {
		PdfDocumentPage* pdfp = NULL;

		pdfp = g_ptr_array_index(arr_pages, i);

		if (pdfp && pdfp->inserted) {
			Anbindung anbindung_zpdfd_part = { 0 };
			Anbindung anbindung_zpdfd_part_orig = { 0 };

			zpdfd_part_get_anbindung(pdfp->inserted, &anbindung_zpdfd_part);
			anbindung_zpdfd_part_orig = anbindung_zpdfd_part;
			anbindung_get_orig(zpdfd, &anbindung_zpdfd_part_orig);

			if (i < anbindung_zpdfd_part.von.seite) {
				anbindung->von.seite++;
				anbindung->bis.seite++;
			}
			else if (i == anbindung_zpdfd_part.von.seite) {
				if (!anbindung_1_gleich_2(anbindung_orig, anbindung_zpdfd_part_orig) &&
						!anbindung_1_eltern_von_2(anbindung_orig, anbindung_zpdfd_part_orig))
					anbindung->von.seite++;
				anbindung->bis.seite++;
			}
			else if (i < anbindung_zpdfd_part.bis.seite)
				anbindung->bis.seite++;
			else if (i == anbindung_zpdfd_part.bis.seite) {
				if (!anbindung_1_gleich_2(anbindung_orig, anbindung_zpdfd_part_orig) &&
						!anbindung_1_eltern_von_2(anbindung_orig, anbindung_zpdfd_part_orig))
					anbindung->bis.seite++;
			}
		}
	}

	return;
}
