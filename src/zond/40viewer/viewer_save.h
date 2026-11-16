/*
 zond (viewer_save.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef VIEWER_SAVE_H_INCLUDED
#define VIEWER_SAVE_H_INCLUDED

typedef struct _Pdf_Viewer PdfViewer;
typedef struct _GError GError;
typedef int gint;

/* Alle "schmutzigen" (dirty) DisplayedDocuments des Viewers speichern -
 * Journal (Annots/Rotation/OCR/Seiten) je Dokument in eine Arbeitskopie
 * einpflegen, physisch speichern, Anbindungen/FTS-Index nachziehen. */
gint viewer_save_dirty_dds(PdfViewer*, GError**);

/* Vor dem Schließen ggf. fragen, ob gespeichert werden soll (falls
 * button_speichern aktiv), dann viewer_schliessen(). */
void viewer_save_and_close(PdfViewer*);

#endif // VIEWER_SAVE_H_INCLUDED
