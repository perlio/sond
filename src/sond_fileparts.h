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

G_BEGIN_DECLS

#define SOND_TYPE_FILE_PART sond_file_part_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePart, sond_file_part, SOND,
		FILE_PART, GObject)

struct _SondFilePartClass {
	GObjectClass parent_class;

	gchar* path_root;
	GPtrArray* arr_opened_files;

	gboolean (*has_children)(SondFilePart*);
	gboolean (*has_sections)(SondFilePart*);
	GPtrArray* (*get_arr_opened_files)(SondFilePart*);
};

SondFilePart* sond_file_part_create_from_content_type(gchar const* path,
		SondFilePart*, gchar const*);

SondFilePart* sond_file_part_get_parent(SondFilePart *);

void sond_file_part_set_parent(SondFilePart*, SondFilePart*);

gchar const* sond_file_part_get_path(SondFilePart *);

void sond_file_part_set_path(SondFilePart*, const gchar*);

gboolean sond_file_part_has_children(SondFilePart*);

GPtrArray* sond_file_part_get_arr_opened_files(SondFilePart*);

fz_stream* sond_file_part_get_istream(fz_context*, SondFilePart*, gboolean, GError**);

gchar* sond_file_part_write_to_tmp_file(SondFilePart*, GError**);

gint sond_file_part_open(SondFilePart*, gboolean, GError**);

gchar* sond_file_part_get_filepart(SondFilePart*);

SondFilePart* sond_file_part_from_filepart(fz_context*,
		gchar const*, GError**);

gint sond_file_part_delete_sfp(SondFilePart*, GError**);

gint sond_file_part_replace(SondFilePart*, fz_context*, fz_buffer*, GError**);

gint sond_file_part_rename(SondFilePart*, gchar const*, GError**);

gchar* sond_file_part_rename_dir(SondFilePart*, gchar const*, gchar const*, GError**);

//SondFilePart Error
#define SOND_TYPE_FILE_PART_ERROR sond_file_part_error_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartError, sond_file_part_error, SOND,
		FILE_PART_ERROR, SondFilePart)

struct _SondFilePartErrorClass {
	SondFilePartClass parent_class;
};

GError* sond_file_part_error_get_error(SondFilePartError*);

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

gint sond_file_part_pdf_load_embedded_files(SondFilePartPDF*, GPtrArray**, GError**);

//Sond_File_Part_Leaf definieren
#define SOND_TYPE_FILE_PART_LEAF sond_file_part_leaf_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartLeaf, sond_file_part_leaf, SOND,
		FILE_PART_LEAF, SondFilePart)

struct _SondFilePartLeafClass {
	SondFilePartClass parent_class;
};

gchar const* sond_file_part_leaf_get_content_type(SondFilePartLeaf*);

G_END_DECLS

#endif /* SRC_SOND_FILEPARTS_H_ */
