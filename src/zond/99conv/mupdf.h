#ifndef MUPDF_H_INCLUDED
#define MUPDF_H_INCLUDED

#define ERROR_MUPDF_CTX(x,y) { if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( \
                        "Bei Aufruf " x ":\n", fz_caught_message( y ), NULL ) ); \
                         return -1; }

typedef struct _Projekt Projekt;
typedef struct fz_context fz_context;
typedef struct fz_document fz_document;
typedef struct pdf_document pdf_document;
typedef struct _ZondPdfDocument ZondPdfDocument;

typedef char gchar;
typedef int gint;


fz_context* mupdf_init( gchar** );

void mupdf_close_context( fz_context* );

fz_document* mupdf_dokument_oeffnen( fz_context*, const gchar*, gchar** );

gint mupdf_save_doc( fz_context*, fz_document*, const gchar*, gchar** );


#endif // MUPDF_H_INCLUDED
