#ifndef TEST_H_INCLUDED
#define TEST_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct fz_context fz_context;
typedef struct pdf_obj pdf_obj;

typedef int gint;
typedef char gchar;
typedef struct _GError GError;

void pdf_print_buffer(fz_context*, fz_buffer*);

gint pdf_print_content_stream(fz_context*, pdf_obj*, GError**);

gint test(Projekt*, GError**);

#endif // TEST_H_INCLUDED

