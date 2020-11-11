#ifndef PDF_DATEIEN_H_INCLUDED
#define PDF_DATEIEN_H_INCLUDED

#include <mupdf/fitz.h>

typedef struct fz_context fz_context;
typedef struct fz_document fz_document;
typedef struct _Projekt Projekt;
typedef struct _Document_Page DocumentPage;

typedef int gint;
typedef char gchar;
typedef void* gpointer;
typedef int gboolean;
typedef struct pdf_obj pdf_obj;
typedef struct pdf_document pdf_document;
typedef struct fz_page fz_page;


gint pdf_document_get_dest( fz_context*, pdf_document*, gint, gpointer*,
        gboolean, gchar** );

gint pdf_get_page_num_from_dest_doc( fz_context*, fz_document*, const gchar*, gchar** );

gint pdf_get_page_num_from_dest( fz_context*, const gchar*, const gchar*, gchar** );

float pdf_get_rotate( fz_context*, pdf_obj* );

gint pdf_update_content_stream( fz_context*, pdf_obj*, fz_buffer*, gchar** );

fz_buffer* pdf_get_content_stream_as_buffer( fz_context*, pdf_obj*, gchar** );

gint pdf_copy_page( fz_context*, pdf_document*, gint, gint, pdf_document*,
        gint, gchar** );

gchar* find_next_BT( gchar*, size_t, gchar** );

gint pdf_filter_stream( fz_context*, pdf_obj*, gint, gchar** );

gint pdf_show_hidden_text( fz_context*, pdf_obj*, gchar** );

gint pdf_render_stext_page_direct( DocumentPage*, gchar** );

gchar* pdf_get_text_from_stext_page( fz_context*, fz_stext_page*, gchar** );

#endif // PDF_DATEIEN_H_INCLUDED
