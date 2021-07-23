#ifndef OCR_H_INCLUDED
#define OCR_H_INCLUDED

typedef struct _GPtrArray GPtrArray;
typedef int gint;
typedef char gchar;

typedef struct _Projekt Projekt;
typedef struct _Info_Window InfoWindow;

fz_buffer* pdf_ocr_get_content_stream_as_buffer( fz_context*, pdf_obj*,
        gchar** );

gint pdf_ocr_pages( Projekt*, InfoWindow*, GPtrArray*, gchar** );

#endif // OCR_H_INCLUDED
