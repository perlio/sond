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

	GPtrArray* (*load_children)(SondFilePart*, GError**);
	gboolean (*has_children)(SondFilePart*);
	GPtrArray* (*get_arr_opened_files)(SondFilePart*);
};

SondFilePart* sond_file_part_get_parent(SondFilePart *);

gchar const* sond_file_part_get_path(SondFilePart *);

GPtrArray* sond_file_part_load_children(SondFilePart*, GError**);

gboolean sond_file_part_has_children(SondFilePart*);

//SondFilePart Error
#define SOND_TYPE_FILE_PART_ERROR sond_file_part_error_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartError, sond_file_part_error, SOND,
		FILE_PART_ERROR, SondFilePart)

struct _SondFilePartErrorClass {
	SondFilePartClass parent_class;
};

SondFilePartError* sond_file_part_error_create(gchar const*,
		SondFilePart*, GError*);

//SondFilePartRoot definieren
#define SOND_TYPE_FILE_PART_ROOT sond_file_part_root_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartRoot, sond_file_part_root, SOND,
		FILE_PART_ROOT, SondFilePart)

struct _SondFilePartRootClass {
	SondFilePartClass parent_class;
};

SondFilePartRoot* sond_file_part_root_create(gchar const*);

//SondFilePartZip definieren
#define SOND_TYPE_FILE_PART_ZIP sond_file_part_zip_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartZip, sond_file_part_zip, SOND,
		FILE_PART_ZIP, SondFilePart)

struct _SondFilePartZipClass {
	SondFilePartClass parent_class;
};

SondFilePartZip* sond_file_part_zip_create(gchar const*, SondFilePart*);

//Sond_File_Part_Dir definieren
#define SOND_TYPE_FILE_PART_DIR sond_file_part_dir_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartDir, sond_file_part_dir, SOND,
		FILE_PART_DIR, SondFilePart)

struct _SondFilePartDirClass {
	SondFilePartClass parent_class;
};

SondFilePartDir* sond_file_part_dir_create(gchar const*, SondFilePart*, GError**);

GPtrArray* sond_file_part_dir_load_children(SondFilePart*, GError**);

gboolean sond_file_part_dir_has_children(SondFilePart*);

//Sond_File_Part_PDF definieren
#define SOND_TYPE_FILE_PART_PDF sond_file_part_pdf_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartPDF, sond_file_part_pdf, SOND,
		FILE_PART_PDF, SondFilePart)

struct _SondFilePartPDFClass {
	SondFilePartClass parent_class;
};

void sond_file_part_pdf_lock_document(SondFilePartPDF*);

void sond_file_part_pdf_unlock_document(SondFilePartPDF*);

gint sond_file_part_pdf_open_document(SondFilePartPDF*, GError**);

void sond_file_part_pdf_close_document(SondFilePartPDF*);

SondFilePartPDF* sond_file_part_pdf_create(gchar const*, SondFilePart*, GError**);

//Sond_File_Part_PDF_Page_Tree definieren
#define SOND_TYPE_FILE_PART_PDF_PAGE_TREE sond_file_part_pdf_page_tree_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartPDFPageTree, sond_file_part_pdf_page_tree, SOND,
		FILE_PART_PDF_PAGE_TREE, SondFilePart)

struct _SondFilePartPDFPageTreeClass {
	SondFilePartClass parent_class;
};

SondFilePartPDFPageTree* sond_file_part_pdf_page_tree_create(gchar const*,
		SondFilePart*, GError **);

gchar const* sond_file_part_pdf_page_tree_get_section(
		SondFilePartPDFPageTree*);

//Sond_File_Part_Leaf definieren
#define SOND_TYPE_FILE_PART_LEAF sond_file_part_leaf_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartLeaf, sond_file_part_leaf, SOND,
		FILE_PART_LEAF, SondFilePart)

struct _SondFilePartLeafClass {
	SondFilePartClass parent_class;
};

SondFilePartLeaf* sond_file_part_leaf_create(gchar const*, SondFilePart*, gchar const*);

gchar const* sond_file_part_leaf_get_content_type(SondFilePartLeaf*);

void sond_file_part_leaf_set_content_type(SondFilePartLeaf*, gchar const*);

G_END_DECLS

#endif /* SRC_SOND_FILEPARTS_H_ */
