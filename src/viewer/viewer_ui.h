/*
 zond (viewer_ui.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef VIEWER_UI_H_INCLUDED
#define VIEWER_UI_H_INCLUDED

typedef struct _Pdf_Viewer PdfViewer;

/* Viewer in einem eigenen Fenster (Hülle um das Widget, Kopfzeile als Titelleiste). */
PdfViewer* viewer_init(PdfViewerSession*);

/* Viewer als Widget ohne Fenster: pv->widget setzt der Aufrufer in seine Oberfläche ein (es ist
 * schon angezeigt). Die Kopfzeile pv->headerbar (Seite, Textsuche, Titel) gehört nicht zum Widget;
 * der Aufrufer setzt sie in seine Titelleiste oder in eine Leiste. Geschlossen wird mit
 * viewer_save_and_close() oder viewer_schliessen(), das Widget wird dabei zerstört. */
PdfViewer* viewer_init_widget(PdfViewerSession*);
void viewer_ui_recreate_tree_thumb(PdfViewer*);

#endif // VIEWER_UI_H_INCLUDED
