#ifndef TEST_H_INCLUDED
#define TEST_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct fz_context fz_context;
typedef struct pdf_obj pdf_obj;

typedef int gint;
typedef char gchar;


gint test( Projekt*, gchar** );

gint pdf_print_content_stream( fz_context*, pdf_obj*, gchar** );


#endif // TEST_H_INCLUDED

