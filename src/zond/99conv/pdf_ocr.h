#ifndef OCR_H_INCLUDED
#define OCR_H_INCLUDED

typedef struct _GPtrArray GPtrArray;
typedef int gint;
typedef char gchar;

typedef struct _Projekt Projekt;
typedef struct _Info_Window InfoWindow;


gint pdf_ocr_pages( Projekt*, InfoWindow*, GPtrArray*, gchar** );

#endif // OCR_H_INCLUDED
