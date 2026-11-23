/*
 zond (anbindung_zaehlung.c) - Akten, Beweisstücke, Unterlagen
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

/* Umrechnung von Seitenzahlen einer Anbindung zwischen der Zählung der Datei auf der Platte, der
 * Live-Zählung des geöffneten Dokuments (mit gelöschten und noch nicht gespeicherten eingefügten
 * Seiten) und der Zählung, die der Viewer anzeigt. */

#include "anbindung_zaehlung.h"

#include "zond_pdf_document.h"

#include "../znd/anbindung.h"

void anbindung_get_orig(ZondPdfDocument* zpdfd, Anbindung* anbindung) {
	Anbindung anbindung_akt = { 0 };

	if (!anbindung)
		return;

	anbindung_akt = *anbindung;

	for (guint i = 0; i < zond_pdf_document_get_number_of_pages(zpdfd); i++) {
		PdfDocumentPage* pdfp = NULL;

		pdfp = zond_pdf_document_get_pdf_document_page(zpdfd, i);

		/* Seiten, die in der frisch von der Platte geöffneten Kopie
		 * (doc) NICHT (mehr) vorhanden sind, verschieben die Live-
		 * Numerierung gegenüber der Platten-Numerierung: entweder weil
		 * sie live neu eingefügt, aber noch nicht gespeichert sind
		 * (pdfp->inserted), oder weil sie bei einem früheren Speichern
		 * bereits aus der Datei ausgeschlossen wurden (pdfp->deleted &&
		 * pdfp->on_disk_deleted) - live aber (als "Karteileiche") noch
		 * mitgezählt werden. Noch nicht gespeicherte Löschungen
		 * (pdfp->deleted && !pdfp->on_disk_deleted) existieren in doc
		 * dagegen noch (werden gerade erst in diesem Speichervorgang
		 * entfernt) und zählen daher wie eine normale, unveränderte
		 * Seite mit. */
		if (pdfp && (pdfp->inserted ||
				(pdfp->deleted && pdfp->on_disk_deleted))) {
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

		/* Ergebnis ist die Numerierung der Datei auf der Platte nach dem Speichern. Die
		 * Live-Numerierung bleibt dabei unverändert (gelöschte Seiten bleiben als
		 * Karteileichen stehen, s. PdfDocumentPage->on_disk_deleted), die Datei verliert sie
		 * aber: jede Seite, die danach nicht mehr in der Datei steht, zählt hier nicht mit.
		 * Innerhalb des zu speichernden Teils sind das alle gelöschten Seiten (auch neu
		 * eingefügte und wieder gelöschte), außerhalb nur die schon früher gespeicherten
		 * und die eingefügten (sie kommen erst mit ihrem eigenen Speichern in die Datei und
		 * wurden in anbindung_aktualisieren() zuvor hinzugerechnet). */
		gboolean weg = FALSE;

		if (i >= anbindung_zpdfd_part.von.seite && i <= anbindung_zpdfd_part.bis.seite)
			weg = pdfp->deleted;
		else
			weg = pdfp->inserted || (pdfp->deleted && pdfp->on_disk_deleted);

		if (weg) {
			if (i <= anbindung->von.seite) {
				anbindung->von.seite--;
				anbindung->bis.seite--;
			}
			else if (i <= anbindung->bis.seite)
				anbindung->bis.seite--;
		}
	}

	return;
}

/* Von der Live-Numerierung in die Zählung, die der Viewer anzeigt: gelöschte Seiten sind nicht
 * sichtbar und zählen nicht mit, eingefügte schon. Das ist die Numerierung der Datei nach dem
 * Speichern. */
void anbindung_get_sichtbar(ZondPdfDocument* zpdfd, Anbindung* anbindung) {
	Anbindung anbindung_akt = { 0 };

	if (!anbindung)
		return;

	anbindung_akt = *anbindung;

	for (guint i = 0; i < zond_pdf_document_get_number_of_pages(zpdfd); i++) {
		PdfDocumentPage* pdfp = zond_pdf_document_get_pdf_document_page(zpdfd, i);

		if (pdfp && pdfp->deleted) {
			if (i <= anbindung_akt.von.seite) {
				anbindung->von.seite--;
				anbindung->bis.seite--;
			}
			else if (i <= anbindung_akt.bis.seite)
				anbindung->bis.seite--;
		}
	}
}

/* Seite, die schon bei einem früheren Speichern aus der Datei ausgeschlossen wurde (steht live
 * noch als Karteileiche) */
static gboolean seite_auf_platte_weg(PdfDocumentPage* pdfp) {
	return pdfp && pdfp->deleted && pdfp->on_disk_deleted;
}

/* Seite, die vor dem nächsten Speichern auf der Platte steht: weder eingefügt noch schon früher
 * ausgeschlossen. Noch nicht initialisierte Seiten (NULL) sind unveränderte Plattenseiten. */
static gboolean seite_auf_platte(PdfDocumentPage* pdfp) {
	return !pdfp || (!pdfp->inserted && !seite_auf_platte_weg(pdfp));
}

/* Fasst die Anbindung anbindung (Zählung der Platte vor dem Speichern) den Teil part, in den
 * Seiten eingefügt wurden, ganz ein oder ist sie ihm gleich? Dann gehören eingefügte Seiten an
 * ihrer Grenze zu ihr. Der Teil steht live mit seinen eingefügten Randseiten im Array: seine
 * Grenzen auf der Platte sind die erste und letzte Plattenseite darin. */
static gboolean anbindung_umfasst_teil(ZondPdfDocument* zpdfd, Anbindung anbindung,
		ZPDFDPart* part) {
	GPtrArray* arr_pages = zond_pdf_document_get_arr_pages(zpdfd);
	Anbindung part_live = { 0 };
	Anbindung part_platte = { 0 };
	gint rang = 0;
	gint erste = -1;
	gint letzte = -1;

	zpdfd_part_get_anbindung(part, &part_live);

	for (gint i = 0; i < (gint) arr_pages->len && i <= part_live.bis.seite; i++) {
		if (!seite_auf_platte(g_ptr_array_index(arr_pages, i)))
			continue;

		if (i >= part_live.von.seite) {
			if (erste < 0)
				erste = rang;
			letzte = rang;
		}
		rang++;
	}

	if (erste < 0)
		return FALSE;

	part_platte.von.seite = erste;
	part_platte.von.index = part->first_index;
	part_platte.bis.seite = letzte;
	part_platte.bis.index = part->last_index;

	return anbindung_1_gleich_2(anbindung, part_platte)
			|| anbindung_1_eltern_von_2(anbindung, part_platte);
}

/* Von der Zählung der Datei auf der Platte (wie in der Datenbank) in die Live-Zählung. Live
 * stehen zusätzlich die schon früher gespeicherten Löschungen (Karteileichen) und die noch nicht
 * gespeicherten eingefügten Seiten im Array. Eingefügte Seiten an der Grenze der Anbindung
 * gehören zu ihr, wenn sie in einen Teil eingefügt wurden, den die Anbindung umfasst oder dem sie
 * gleicht (im Viewer eines Abschnitts eingefügte Seiten sind in dem Abschnitt). Gegenstück zu
 * anbindung_get_orig(). */
void anbindung_aktualisieren(ZondPdfDocument* zpdfd, Anbindung* anbindung) {
	GPtrArray* arr_pages = NULL;
	g_autofree gint* live_der_platte = NULL; //Live-Position der k-ten Plattenseite
	gint n_platte = 0;
	gint von = 0;
	gint bis = 0;
	Anbindung anbindung_orig = { 0 };

	if (!anbindung || anbindung_is_empty(anbindung))
		return;

	anbindung_orig = *anbindung;

	arr_pages = zond_pdf_document_get_arr_pages(zpdfd);
	live_der_platte = g_new(gint, arr_pages->len + 1);

	for (gint i = 0; i < (gint) arr_pages->len; i++)
		if (seite_auf_platte(g_ptr_array_index(arr_pages, i)))
			live_der_platte[n_platte++] = i;

	//Seiten hinter der letzten Plattenseite zählen einfach weiter
	von = (anbindung->von.seite < n_platte) ? live_der_platte[anbindung->von.seite] :
			anbindung->von.seite + (gint) arr_pages->len - n_platte;
	bis = (anbindung->bis.seite < n_platte) ? live_der_platte[anbindung->bis.seite] :
			anbindung->bis.seite + (gint) arr_pages->len - n_platte;

	//unmittelbar davor eingefügte Seiten
	for (gint j = von - 1; j >= 0; j--) {
		PdfDocumentPage* pdfp = g_ptr_array_index(arr_pages, j);

		if (seite_auf_platte_weg(pdfp))
			continue;

		if (pdfp && pdfp->inserted && pdfp->inserted_in &&
				anbindung_umfasst_teil(zpdfd, anbindung_orig, pdfp->inserted_in))
			von = j;
		else
			break;
	}

	//unmittelbar dahinter eingefügte Seiten
	for (gint j = bis + 1; j < (gint) arr_pages->len; j++) {
		PdfDocumentPage* pdfp = g_ptr_array_index(arr_pages, j);

		if (seite_auf_platte_weg(pdfp))
			continue;

		if (pdfp && pdfp->inserted && pdfp->inserted_in &&
				anbindung_umfasst_teil(zpdfd, anbindung_orig, pdfp->inserted_in))
			bis = j;
		else
			break;
	}

	anbindung->von.seite = von;
	anbindung->bis.seite = bis;

	return;
}
