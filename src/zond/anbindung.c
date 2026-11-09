#include "anbindung.h"

#include <stdlib.h>
#include <string.h>

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

gboolean anbindung_ist_unterseitig(Anbindung anbindung) {
	/* Punkt: markiert eine einzelne Position, nie eine ganze Seite -
	 * zählt hier immer als unterseitig. */
	if (anbindung_is_pdf_punkt(anbindung))
		return TRUE;

	/* von.index != 0: beginnt nicht am Seitenanfang.
	 * bis.index != EOP: endet nicht am Seitenende (EOP = "End Of Page",
	 * s. anbindung.h - von anbindung_build_file_section()/den Anbindungs-
	 * Editierstellen im Viewer explizit so gesetzt, wenn "bis zum Ende
	 * der Seite" gemeint ist). */
	return anbindung.von.index != 0 || anbindung.bis.index != EOP;
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

gint anbindung_bis_seite(Anbindung anbindung) {
	return (anbindung.bis.seite == 0 && anbindung.bis.index == 0) ?
			anbindung.von.seite : anbindung.bis.seite;
}

void anbindung_ges_nur_anbindung(Anbindung anbindung,
		gchar const *section_parent, Anbindung *ges) {
	if (!anbindung_is_pdf_punkt(anbindung)) {
		*ges = anbindung;

		return;
	}

	if (section_parent)
		anbindung_parse_file_section(section_parent, ges);

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
	GString *text = g_string_new(NULL);

	g_string_append_printf(text, "S. %i", anbindung->von.seite + 1);
	if (anbindung->von.index)
		g_string_append_printf(text, ", Index %d", anbindung->von.index);
	if (anbindung->bis.seite || anbindung->bis.index)
		g_string_append_printf(text, " - S. %d", anbindung->bis.seite + 1);
	if (anbindung->bis.index != EOP)
		g_string_append_printf(text, ", Index %d", anbindung->bis.index);

	return g_string_free(text, FALSE);
}

gboolean anbindung_is_empty(Anbindung* anbindung) {
	if (anbindung->von.seite || anbindung->von.index ||
			anbindung->bis.seite || anbindung->bis.index)
		return FALSE;

	return TRUE;
}
