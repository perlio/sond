#ifndef MUPDF_H_INCLUDED
#define MUPDF_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct fz_context fz_context;
typedef struct fz_document fz_document;
typedef struct pdf_document pdf_document;
typedef struct _Document Document;

typedef char gchar;
typedef int gint;


fz_context* mupdf_init( );

void mupdf_close_context( fz_context* ctx );

fz_document* mupdf_dokument_oeffnen( fz_context*, const gchar*, gchar** );

gint mupdf_open_document( Document*, gchar** );

gint mupdf_save_doc( fz_context*, pdf_document*, const gchar*, gchar** );

gint mupdf_save_document( Document* document, gchar** errmsg );

void mupdf_close_document( Document* );

#endif // MUPDF_H_INCLUDED
