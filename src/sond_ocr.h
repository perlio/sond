/*
 sond (sond_ocr.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2025  peloamerica

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

#ifndef SRC_SOND_OCR_H_
#define SRC_SOND_OCR_H_

typedef struct TessBaseAPI TessBaseAPI;
typedef struct _SondFilePartPDF SondFilePartPDF;

gint sond_ocr_page(fz_context*, pdf_page*, pdf_obj*,
		TessBaseAPI*, TessBaseAPI*, gint (*)(TessBaseAPI*, gpointer), gpointer,
		void (*)(void*, gchar const*, ...), gpointer, GError**);

gint sond_ocr_pdf_doc(fz_context*, pdf_document*,
		SondFilePartPDF*, TessBaseAPI*, TessBaseAPI*,
		gint (*)(TessBaseAPI*, gpointer), gpointer,
		void (*)(void*, gchar const*, ...), gpointer, GError**);

gint sond_ocr_init_tesseract(TessBaseAPI**, TessBaseAPI**, gchar const*, GError**);

gint sond_ocr_pdf(SondFilePartPDF*, gint (*)(TessBaseAPI*, gpointer), gpointer,
		void (*)(void*, gchar const*, ...), gpointer, GError**);

#endif /* SRC_SOND_OCR_H_ */
