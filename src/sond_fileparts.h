/*
 sond (sond_fileparts.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SRC_SOND_FILEPARTS_H_
#define SRC_SOND_FILEPARTS_H_

#include <glib-object.h>
#include <gtk/gtk.h>
#include <mupdf/pdf.h>
#include <gmime/gmime.h>

G_BEGIN_DECLS

#define SOND_TYPE_FILE_PART sond_file_part_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePart, sond_file_part, SOND,
		FILE_PART, GObject)

struct _SondFilePartClass {
	GObjectClass parent_class;

	gchar* path_root;
	GPtrArray* arr_opened_files;
};

SondFilePart* sond_file_part_create_from_mime_type(gchar const* path,
		SondFilePart*, gchar const*);

SondFilePart* sond_file_part_create_from_stream(fz_context*,
		fz_stream*, gchar const*, SondFilePart*, GError**);

SondFilePart* sond_file_part_get_parent(SondFilePart *);

void sond_file_part_set_parent(SondFilePart*, SondFilePart*);

gchar const* sond_file_part_get_path(SondFilePart *);

void sond_file_part_set_path(SondFilePart*, const gchar*);

gboolean sond_file_part_get_has_children(SondFilePart*);

void sond_file_part_set_has_children(SondFilePart*, gboolean);

GPtrArray* sond_file_part_get_arr_opened_files(SondFilePart*);

gchar* sond_file_part_write_to_tmp_file(fz_context*, SondFilePart*, GError**);

gint sond_file_part_open(SondFilePart*, gboolean, GError**);

gchar* sond_file_part_get_filepart(SondFilePart*);

SondFilePart* sond_file_part_from_filepart(fz_context*,
		gchar const*, GError**);

gint sond_file_part_delete(SondFilePart*, GError**);

gint sond_file_part_rename(SondFilePart*, gchar const*, GError**);

gint sond_file_part_copy(SondFilePart*, SondFilePart*, gchar const*, GError**);

//SondFilePartZip definieren
#define SOND_TYPE_FILE_PART_ZIP sond_file_part_zip_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartZip, sond_file_part_zip, SOND,
		FILE_PART_ZIP, SondFilePart)

struct _SondFilePartZipClass {
	SondFilePartClass parent_class;
};

//Sond_File_Part_PDF definieren
#define SOND_TYPE_FILE_PART_PDF sond_file_part_pdf_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartPDF, sond_file_part_pdf, SOND,
		FILE_PART_PDF, SondFilePart)

struct _SondFilePartPDFClass {
	SondFilePartClass parent_class;
};

pdf_document* sond_file_part_pdf_open_document(fz_context*,
		SondFilePartPDF*, gboolean, GError **);

gint sond_file_part_pdf_save(fz_context*, pdf_document*, SondFilePartPDF*, GError**);

gint sond_file_part_pdf_load_embedded_files(SondFilePartPDF*, GPtrArray**, GError**);

//Sond_File_Part_GMessage definieren
#define SOND_TYPE_FILE_PART_GMESSAGE sond_file_part_gmessage_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartGMessage, sond_file_part_gmessage, SOND,
		FILE_PART_GMESSAGE, SondFilePart)

struct _SondFilePartGMessageClass {
	SondFilePartClass parent_class;
};

gint sond_file_part_gmessage_load_path(SondFilePartGMessage*,
		gchar const*, GPtrArray**, GError**);

//Sond_File_Part_Leaf definieren
#define SOND_TYPE_FILE_PART_LEAF sond_file_part_leaf_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartLeaf, sond_file_part_leaf, SOND,
		FILE_PART_LEAF, SondFilePart)

struct _SondFilePartLeafClass {
	SondFilePartClass parent_class;
};

gchar const* sond_file_part_leaf_get_mime_type(SondFilePartLeaf*);

void sond_file_part_leaf_set_mime_type(SondFilePartLeaf*, gchar const*);

G_END_DECLS

#endif /* SRC_SOND_FILEPARTS_H_ */
