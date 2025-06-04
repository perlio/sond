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

G_BEGIN_DECLS

#define SOND_TYPE_FILE_PART sond_file_part_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePart, sond_file_part, SOND,
		FILE_PART, GObject)

struct _SondFilePartClass {
	GObjectClass parent_class;

	GPtrArray* (*get_children)(SondFilePart*, GError**);
	gboolean (*has_children)(SondFilePart*, GError**);
};

SondFilePart* sond_file_part_create(GType, const gchar*, SondFilePart*);

SondFilePart* sond_file_part_get_parent(SondFilePart *);

gchar const* sond_file_part_get_path(SondFilePart *);

GPtrArray* sond_file_part_get_children(SondFilePart*, GError**);

gboolean sond_file_part_has_children(SondFilePart*, GError**);

//Sond_File_Part_Dir definieren
#define SOND_TYPE_FILE_PART_DIR sond_file_part_dir_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartDir, sond_file_part_dir, SOND,
		FILE_PART_DIR, SondFilePart)

struct _SondFilePartDirClass {
	SondFilePartClass parent_class;
};

GPtrArray* sond_file_part_dir_get_children(SondFilePart*, GError**);

gboolean sond_file_part_dir_has_children(SondFilePart*, GError**);

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

//Sond_File_Part_Leaf definieren
#define SOND_TYPE_FILE_PART_LEAF sond_file_part_leaf_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartLeaf, sond_file_part_leaf, SOND,
		FILE_PART_LEAF, SondFilePart)

struct _SondFilePartLeafClass {
	SondFilePartClass parent_class;
};

gchar const* sond_file_part_leaf_get_content_type(SondFilePartLeaf*);

void sond_file_part_leaf_set_content_type(SondFilePartLeaf*, gchar const*);

G_END_DECLS

#endif /* SRC_SOND_FILEPARTS_H_ */
